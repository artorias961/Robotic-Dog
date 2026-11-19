#pragma once
// Diagram box: "system + gait FSM — boot -> idle -> standup -> stand -> trot, fault".
// The FSM decides *what* the dog is doing; the gait scheduler decides *where*
// the feet go. On FAULT the master stops setpoints and disarms the legs.
#include <algorithm>
#include <cmath>

#include "quad/config.hpp"
#include "quad/types.hpp"

namespace quad {

class SystemFsm {
 public:
  struct Inputs {
    BodyCommand cmd;
    bool all_legs_healthy{};  // telemetry fresh, no fault flags, link ok
    bool all_legs_armed{};    // every leg's telemetry says armed
    bool any_leg_fault{};     // a live leg reports latched fault flags
    bool body_tilt_ok{true};  // "body state" arrow: estimator -> FSM
  };

  // Called once per 100 Hz tick. Returns the state to act on this tick.
  SysState step(const Inputs& in, double dt) {
    if (in.cmd.estop) go_fault("E-stop");
    // Fall detection from the state estimator (inert until the dedicated
    // IMU driver fills roll/pitch; joints alone can't see body tilt).
    if (!in.body_tilt_ok && moving_or_standing()) go_fault("body tilt");

    switch (state_) {
      case SysState::Boot:
        state_ = SysState::Idle;
        break;

      case SysState::Idle:
        // Legs disarmed, only heartbeat flows. Faults latched before this
        // master started (e.g. the previous master died) still need clearing,
        // which happens in FAULT. ENABLE goes out on entering STANDUP.
        if (in.any_leg_fault) { go_fault("leg fault latched"); break; }
        if (in.cmd.enable && in.all_legs_healthy) {
          standup_t_ = 0.0;
          arm_wait_ = 0.0;
          state_ = SysState::Standup;
        }
        break;

      case SysState::Standup:
        if (!in.all_legs_healthy) { go_fault("leg unhealthy"); break; }
        if (!in.all_legs_armed) {
          // Hold the ramp at its start pose (the measured pose) until every
          // leg confirms it armed; a leg that never does is a fault.
          arm_wait_ += dt;
          if (arm_wait_ > cfg::kArmTimeoutS) go_fault("leg did not arm");
          break;
        }
        standup_t_ += dt;
        if (standup_t_ >= cfg::kStandupTime) state_ = SysState::Stand;
        break;

      case SysState::Stand:
        if (!in.all_legs_healthy) { go_fault("leg unhealthy"); break; }
        if (!in.all_legs_armed) { go_fault("leg disarmed"); break; }
        if (!in.cmd.enable) { state_ = SysState::Idle; break; }
        if (wants_motion(in.cmd)) state_ = SysState::Trot;
        break;

      case SysState::Trot:
        if (!in.all_legs_healthy) { go_fault("leg unhealthy"); break; }
        if (!in.all_legs_armed) { go_fault("leg disarmed"); break; }
        if (!in.cmd.enable) { state_ = SysState::Idle; break; }
        if (!wants_motion(in.cmd)) state_ = SysState::Stand;
        break;

      case SysState::Fault:
        // Leaving FAULT needs the operator: releasing enable acknowledges it
        // (the master then sends CLEAR_FAULTS), and every leg must come back
        // clean before IDLE.
        if (!in.cmd.estop && !in.cmd.enable && in.all_legs_healthy) state_ = SysState::Idle;
        break;
    }
    return state_;
  }

  SysState state() const { return state_; }
  const char* fault_reason() const { return fault_reason_; }

  // 0 -> 1 progress of the stand-up ramp (only advances once all legs armed).
  double standup_progress() const {
    return std::clamp(standup_t_ / cfg::kStandupTime, 0.0, 1.0);
  }

 private:
  void go_fault(const char* why) {
    if (state_ != SysState::Fault) fault_reason_ = why;
    state_ = SysState::Fault;
  }
  bool moving_or_standing() const {
    return state_ == SysState::Standup || state_ == SysState::Stand ||
           state_ == SysState::Trot;
  }
  static bool wants_motion(const BodyCommand& c) {
    return std::abs(c.vx) > 0.01 || std::abs(c.vy) > 0.01 || std::abs(c.yaw_rate) > 0.05;
  }

  SysState state_ = SysState::Boot;
  double standup_t_ = 0.0;
  double arm_wait_ = 0.0;
  const char* fault_reason_ = "";
};

}  // namespace quad
