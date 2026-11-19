// Quadruped master board — C++20, one-to-one with the "Master board — how the
// dog walks" diagram.
//
// Threads (std::jthread, clean shutdown via stop_token):
//   control  100 Hz: FSM -> per-leg phase -> foot trajectory -> IK -> safety
//            gate -> pack(seq+CRC) -> UDP sendto x4; heartbeat every 10th tick
//            in EVERY state (the 10 Hz heartbeat the Teensys disarm without).
//            Prints a status line whenever the state or a leg changes.
//   rx       blocks on UDP :5006, validates magic/CRC/seq, feeds the latest-
//            wins telemetry slots and command ACKs.
//   main     operator input (console stand-in for the gamepad).
#include <array>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <variant>

#include "quad/comms.hpp"
#include "quad/config.hpp"
#include "quad/estimator.hpp"
#include "quad/gait.hpp"
#include "quad/kinematics.hpp"
#include "quad/latest.hpp"
#include "quad/protocol.hpp"
#include "quad/safety.hpp"
#include "quad/state_machine.hpp"
#include "quad/timing.hpp"
#include "quad/types.hpp"
#include "quad/udp.hpp"

namespace {

using namespace quad;
using namespace std::chrono;

constexpr std::size_t idx(Leg l) { return static_cast<std::size_t>(l); }
constexpr std::size_t idx(int i) { return static_cast<std::size_t>(i); }

using LegLinks = std::array<LegLink, kNumLegs>;
using LegPoses = std::array<JointAngles, kNumLegs>;

// ---- shared state (latest-value-wins, same philosophy as the Teensy) -------
struct StatusSnapshot {
  SysState state{SysState::Boot};
  const char* fault_reason{""};
  std::array<LegHealth, kNumLegs> legs{};
  std::uint64_t lost{}, bad_packets{}, leg_restarts{};
  std::uint32_t commands_failed{};
};

Latest<BodyCommand> g_command;                        // operator -> control
std::array<Latest<LegTelemetry>, kNumLegs> g_telem;   // rx -> control
Latest<StatusSnapshot> g_status;                      // control -> console
std::mutex g_console;                                 // one line at a time

std::array<LegTelemetry, kNumLegs> snapshot_telemetry() {
  std::array<LegTelemetry, kNumLegs> t;
  for (int i = 0; i < kNumLegs; ++i) t[idx(i)] = g_telem[idx(i)].load();
  return t;
}

std::uint32_t uptime_ms(Clock::time_point t0) {
  return static_cast<std::uint32_t>(duration_cast<milliseconds>(Clock::now() - t0).count());
}

const char* leg_word(const LegHealth& h) {
  if (!h.fresh) return "stale";
  if (h.fault) return "FAULT";
  if (h.link_degraded) return "loss!";
  return h.armed ? "armed" : "ok";
}

std::string format_status(const StatusSnapshot& s) {
  std::string out = "[" + std::string(to_string(s.state)) + "]";
  if (s.state == SysState::Fault && *s.fault_reason) out += std::string(" (") + s.fault_reason + ")";
  out += " legs";
  for (int i = 0; i < kNumLegs; ++i)
    out += std::string(" ") + leg_name(static_cast<Leg>(i)) + ":" + leg_word(s.legs[idx(i)]);
  out += " | lost=" + std::to_string(s.lost) + " bad=" + std::to_string(s.bad_packets) +
         " leg_restarts=" + std::to_string(s.leg_restarts);
  if (s.commands_failed) out += " cmd_failed=" + std::to_string(s.commands_failed);
  return out;
}

void print_line(const std::string& line) {
  const std::lock_guard lock(g_console);
  std::cout << line << '\n' << std::flush;
}

JointAngles lerp(const JointAngles& a, const JointAngles& b, double s) {
  return {a.tilt + (b.tilt - a.tilt) * s, a.hip + (b.hip - a.hip) * s,
          a.knee + (b.knee - a.knee) * s};
}

double smoothstep(double s) { return s * s * (3.0 - 2.0 * s); }

// ---- RX thread: diagram "UDP recvfrom telemetry ~500 Hz x 4 legs" ----------
void rx_thread(std::stop_token stop, net::UdpSocket& sock, PacketParser& parser,
               LegLinks& links) {
  net::Endpoint from;
  while (!stop.stop_requested()) {
    const auto raw = sock.recv(from);     // 50 ms timeout keeps us responsive
    if (!raw) continue;
    const RxPacket pkt = parser.parse(*raw, Clock::now());
    if (const auto* t = std::get_if<RxTelemetry>(&pkt)) {
      g_telem[idx(t->leg)].store(t->telemetry);
    } else if (const auto* a = std::get_if<RxAck>(&pkt)) {
      links[idx(a->leg)].on_ack(a->acked_seq);
    }
    // monostate: bad magic/CRC/type or a stale duplicate -> already counted
  }
}

// ---- turns FSM state changes (and operator acknowledgement) into commands --
class CommandSequencer {
 public:
  void update(SysState state, const BodyCommand& cmd, LegLinks& links, std::uint32_t t_ms,
              Clock::time_point now) {
    if (state != last_) {
      if (state == SysState::Standup) broadcast(links, proto::Cmd::Enable, t_ms, now);
      if (state == SysState::Idle || state == SysState::Fault)
        broadcast(links, proto::Cmd::Disable, t_ms, now);
      last_ = state;
    }
    // In FAULT with enable released (the operator's acknowledgement), ask
    // every leg to clear its latched faults. Sent once per acknowledgement:
    // a fault that re-latches keeps the master in FAULT instead of cycling,
    // and pressing e then d again sends it again.
    if (state == SysState::Fault && !cmd.enable && !cmd.estop && !cleared_) {
      broadcast(links, proto::Cmd::ClearFaults, t_ms, now);
      cleared_ = true;
    }
    if (cmd.enable) cleared_ = false;
  }

 private:
  static void broadcast(LegLinks& links, proto::Cmd c, std::uint32_t t_ms,
                        Clock::time_point now) {
    for (auto& l : links) l.send_command(c, 0.0f, t_ms, now);
  }
  SysState last_ = SysState::Boot;
  bool cleared_ = false;
};

// ---- control thread: the whole centre column of the diagram at 100 Hz ------
void control_thread(std::stop_token stop, LegLinks& links, const PacketParser& parser,
                    const LegPoses& stand_pose, Clock::time_point t0) {
  constexpr double dt = duration<double>(cfg::kControlPeriod).count();
  SystemFsm fsm;
  GaitScheduler gait;
  SafetyGate gate;
  StateEstimator estimator;
  CommandSequencer sequencer;
  PeriodicSleeper sleeper;

  LegPoses start_pose{};
  SysState prev_state = SysState::Boot;
  std::string last_printed;
  std::uint32_t tick = 0;
  auto next = Clock::now();

  while (!stop.stop_requested()) {
    next += cfg::kControlPeriod;
    const auto now = Clock::now();
    const auto cmd = g_command.load();
    const auto telem = snapshot_telemetry();

    // Per-leg watchdog + safety gate (right column feeding the gate).
    gate.update(telem, now);
    const BodyState body =
        estimator.update(telem, ImuSample{});  // dedicated IMU plugs in here (v1)
    const bool tilt_ok = std::abs(body.roll) < cfg::kMaxTiltRad &&
                         std::abs(body.pitch) < cfg::kMaxTiltRad;

    // System + gait FSM ("body state" + "leg faults" arrows both land here).
    const SysState state =
        fsm.step({.cmd = cmd, .all_legs_healthy = gate.all_healthy(),
                  .all_legs_armed = gate.all_armed(), .any_leg_fault = gate.any_fault(),
                  .body_tilt_ok = tilt_ok}, dt);
    const std::uint32_t t_ms = uptime_ms(t0);
    ++tick;

    // Commands first, so a new one leaves ahead of this tick's setpoints.
    sequencer.update(state, cmd, links, t_ms, now);
    for (auto& link : links) link.poll_command(now);

    // Heartbeat: 10 Hz, sent in EVERY state so legs can tell idle from dead.
    if (tick % cfg::kHeartbeatEvery == 0)
      for (auto& link : links) link.send_heartbeat(state, t_ms);

    if (state == SysState::Standup) {
      // Ramp in joint space from the pose each leg REPORTED when stand-up
      // began to the standing pose. Progress holds at 0 until every leg has
      // armed, so the first setpoints match where the legs already are.
      if (prev_state != SysState::Standup)
        for (int i = 0; i < kNumLegs; ++i) start_pose[idx(i)] = telem[idx(i)].position;
      const double s = smoothstep(fsm.standup_progress());
      for (int i = 0; i < kNumLegs; ++i)
        links[idx(i)].send_setpoint(lerp(start_pose[idx(i)], stand_pose[idx(i)], s), t_ms);
    } else if (state == SysState::Stand || state == SysState::Trot) {
      if (state == SysState::Trot) gait.advance(dt); else gait.reset();
      const BodyCommand active = (state == SysState::Trot) ? cmd : BodyCommand{};
      for (int i = 0; i < kNumLegs; ++i) {
        const Vec3 foot = gait.foot_target(static_cast<Leg>(i), active, cfg::kBodyHeight);
        if (const auto joints = inverse_kinematics(foot))
          links[idx(i)].send_setpoint(*joints, t_ms);
        // Unreachable target: no setpoint; the leg holds its last target, and
        // its own 50 ms timeout covers a persistent gap.
      }
    }
    prev_state = state;

    // Publish status; print a line whenever something visible changed.
    StatusSnapshot snap{.state = state,
                        .fault_reason = fsm.fault_reason(),
                        .legs = gate.legs(),
                        .lost = parser.lost(),
                        .bad_packets = parser.bad_packets(),
                        .leg_restarts = parser.leg_restarts(),
                        .commands_failed = 0};
    for (const auto& l : links) snap.commands_failed += l.commands_failed();
    g_status.store(snap);
    std::string key = std::string(to_string(state)) + snap.fault_reason;
    for (const auto& h : snap.legs) key += leg_word(h);
    if (key != last_printed) {
      print_line(format_status(snap));
      last_printed = std::move(key);
    }

    sleeper.sleep_until(next);
  }
}

// ---- operator input: console stand-in for the gamepad ----------------------
void print_help() {
  print_line("commands: e=enable d=disable/acknowledge fault w/s=forward/back "
             "a/f=turn space=stop x=ESTOP q=quit  (press Enter after each)");
}

}  // namespace

int main() {
  using namespace quad;
  print_line("quadruped master — " + std::to_string(kNumLegs) + " legs, control " +
             std::to_string(duration_cast<milliseconds>(cfg::kControlPeriod).count()) +
             " ms tick");

  // The standing pose must be reachable with the configured leg geometry.
  LegPoses stand_pose{};
  for (int i = 0; i < kNumLegs; ++i) {
    const auto leg = static_cast<Leg>(i);
    const auto joints = inverse_kinematics(neutral_foot(leg, cfg::kBodyHeight));
    if (!joints) {
      std::cerr << "fatal: standing pose unreachable for leg " << i
                << " (check kBodyHeight and leg lengths in config.hpp)\n";
      return 1;
    }
    stand_pose[idx(i)] = *joints;
  }
  print_help();

  try {
    net::UdpSocket sock(cfg::kPortToMaster);
    const auto t0 = Clock::now();

    // Element i must be the link for leg number i, because links[idx(leg)] is
    // how every other part of the code finds a leg's link.
    LegLinks links{LegLink(static_cast<Leg>(0), sock), LegLink(static_cast<Leg>(1), sock),
                   LegLink(static_cast<Leg>(2), sock), LegLink(static_cast<Leg>(3), sock)};
    PacketParser parser;

    std::jthread rx([&](std::stop_token st) { rx_thread(st, sock, parser, links); });
    std::jthread control([&](std::stop_token st) {
      control_thread(st, links, parser, stand_pose, t0);
    });

    BodyCommand cmd{};
    for (std::string line; std::getline(std::cin, line);) {
      bool quit = false;
      for (const char c : line) {
        switch (c) {
          case 'e': cmd.enable = true;  cmd.estop = false; break;
          case 'd': cmd.enable = false; break;
          case 'w': cmd.vx = std::min(cmd.vx + 0.05, cfg::kMaxVx); break;
          case 's': cmd.vx = std::max(cmd.vx - 0.05, -cfg::kMaxVx); break;
          case 'a': cmd.yaw_rate = std::min(cmd.yaw_rate + 0.2, cfg::kMaxYawRate); break;
          case 'f': cmd.yaw_rate = std::max(cmd.yaw_rate - 0.2, -cfg::kMaxYawRate); break;
          case ' ': cmd.vx = cmd.vy = cmd.yaw_rate = 0.0; break;
          case 'x': cmd.estop = true; cmd.enable = false; break;
          case 'q': quit = true; break;
          default: break;
        }
      }
      if (quit) break;
      g_command.store(cmd);
      print_line(std::format("vx={:+.2f} yaw={:+.2f}  ", cmd.vx, cmd.yaw_rate) +
                 format_status(g_status.load()));
    }
    print_line("shutting down (jthreads stop via stop_token)");
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
