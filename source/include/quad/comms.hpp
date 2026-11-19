#pragma once
// Diagram boxes: "pack setpoints + seq + CRC", "UDP sendto x4 — setpoints
// 100 Hz, heartbeat 10 Hz always", "UDP recvfrom telemetry", plus the acked
// command channel from Conventions.md (retransmit every 20 ms, max 10).
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <deque>
#include <span>
#include <variant>

#include "quad/config.hpp"
#include "quad/protocol.hpp"
#include "quad/types.hpp"
#include "quad/udp.hpp"

namespace quad {

template <typename P>
std::span<const std::uint8_t> as_bytes_of(const P& pkt) {
  return {reinterpret_cast<const std::uint8_t*>(&pkt), sizeof(P)};
}

// One master-side link to one Teensy leg controller.
//
// Threading: the control thread owns everything except last_acked_seq_,
// which the RX thread writes through on_ack(). Non-copyable because of the
// atomic, so construct links in place.
class LegLink {
 public:
  LegLink(Leg leg, const net::UdpSocket& sock)
      : leg_(leg),
        sock_(&sock),
        to_(net::Endpoint::make(cfg::kLegIp[static_cast<int>(leg)], cfg::kPortToLeg)) {}
  LegLink(const LegLink&) = delete;
  LegLink& operator=(const LegLink&) = delete;

  void send_setpoint(const JointAngles& j, std::uint32_t t_ms) {
    proto::SetpointPacket p{
        .header = make_header(proto::Type::Setpoint, t_ms),
        .target_rad = {static_cast<float>(j.tilt), static_cast<float>(j.hip),
                       static_cast<float>(j.knee)},
        .crc = 0};
    proto::seal(p);
    sock_->send_to(to_, as_bytes_of(p));
  }

  void send_heartbeat(SysState state, std::uint32_t t_ms) {
    proto::HeartbeatPacket p{.header = make_header(proto::Type::Heartbeat, t_ms),
                             .fsm_state = static_cast<std::uint8_t>(state),
                             .crc = 0};
    proto::seal(p);
    sock_->send_to(to_, as_bytes_of(p));
  }

  // Commands are delivered one at a time, in order. A new command goes on
  // the wire immediately when nothing is in flight, so it leaves ahead of
  // the setpoints sent later in the same tick.
  void send_command(proto::Cmd cmd, float param, std::uint32_t t_ms, Clock::time_point now) {
    proto::CommandPacket p{.header = make_header(proto::Type::Command, t_ms),
                           .cmd = static_cast<std::uint8_t>(cmd),
                           .param = param,
                           .crc = 0};
    proto::seal(p);
    queue_.push_back(p);
    if (queue_.size() == 1) transmit_front(now);
  }

  // Call every control tick: completes acked commands, retransmits on schedule.
  void poll_command(Clock::time_point now) {
    if (queue_.empty()) return;
    if (last_acked_seq_.load(std::memory_order_acquire) >= queue_.front().header.seq) {
      advance(now);
      return;
    }
    if (now < next_retry_) return;
    if (attempts_ >= cfg::kCommandMaxRetry) {
      ++commands_failed_;  // the FSM's "all legs armed?" check catches the result
      advance(now);
      return;
    }
    transmit_front(now);
  }

  // RX thread: record the ACK; the control thread consumes it on its next poll.
  void on_ack(std::uint32_t acked_seq) {
    auto cur = last_acked_seq_.load(std::memory_order_relaxed);
    while (acked_seq > cur &&
           !last_acked_seq_.compare_exchange_weak(cur, acked_seq, std::memory_order_release,
                                                  std::memory_order_relaxed)) {
    }
  }

  std::uint32_t commands_failed() const { return commands_failed_; }
  Leg leg() const { return leg_; }

 private:
  proto::Header make_header(proto::Type type, std::uint32_t t_ms) {
    return proto::Header{.magic = proto::kMagic,
                         .version = proto::kVersion,
                         .type = static_cast<std::uint8_t>(type),
                         .leg_id = static_cast<std::uint8_t>(leg_),
                         .flags = 0,
                         .seq = ++tx_seq_,
                         .t_ms = t_ms};
  }

  void transmit_front(Clock::time_point now) {
    sock_->send_to(to_, as_bytes_of(queue_.front()));
    ++attempts_;
    next_retry_ = now + cfg::kCommandRetry;
  }

  void advance(Clock::time_point now) {
    queue_.pop_front();
    attempts_ = 0;
    if (!queue_.empty()) transmit_front(now);
  }

  Leg leg_;
  const net::UdpSocket* sock_;
  net::Endpoint to_;
  std::uint32_t tx_seq_ = 0;

  std::deque<proto::CommandPacket> queue_;
  int attempts_ = 0;
  Clock::time_point next_retry_{};
  std::uint32_t commands_failed_ = 0;
  std::atomic<std::uint32_t> last_acked_seq_{0};
};

// ---- RX side ----------------------------------------------------------------
struct RxTelemetry {
  Leg leg;
  LegTelemetry telemetry;
};
struct RxAck {
  Leg leg;
  std::uint32_t acked_seq;
};
using RxPacket = std::variant<std::monostate, RxTelemetry, RxAck>;

// Validates and unpacks one datagram (diagram: magic + CRC ok? seq newer? ->
// count gaps as lost). Written by the RX thread only; counters are atomic so
// other threads can read them.
class PacketParser {
 public:
  RxPacket parse(std::span<const std::uint8_t> raw, Clock::time_point now) {
    if (raw.size() < sizeof(proto::Header)) { bad_packets_.fetch_add(1); return {}; }
    proto::Header h;
    std::memcpy(&h, raw.data(), sizeof(h));
    switch (static_cast<proto::Type>(h.type)) {
      case proto::Type::Telemetry: return parse_telemetry(raw, now);
      case proto::Type::Ack:       return parse_ack(raw);
      default: bad_packets_.fetch_add(1); return {};
    }
  }

  std::uint64_t lost() const { return lost_.load(std::memory_order_relaxed); }
  std::uint64_t bad_packets() const { return bad_packets_.load(std::memory_order_relaxed); }
  std::uint64_t leg_restarts() const { return restarts_.load(std::memory_order_relaxed); }

 private:
  struct LegSeq {
    std::uint32_t last_seq = 0;
    std::uint32_t last_t_ms = 0;
    bool have_t = false;
  };

  RxPacket parse_telemetry(std::span<const std::uint8_t> raw, Clock::time_point now) {
    const auto pkt = proto::parse<proto::TelemetryPacket>(raw, proto::Type::Telemetry);
    if (!pkt || pkt->header.leg_id >= kNumLegs) { bad_packets_.fetch_add(1); return {}; }

    auto& s = legs_[pkt->header.leg_id];
    // Leg uptime going backwards = the Teensy rebooted (e.g. its watchdog):
    // start over instead of ignoring it for as long as it was previously up.
    if (s.have_t && pkt->header.t_ms + cfg::kRestartSlackMs < s.last_t_ms) {
      s.last_seq = 0;
      s.last_t_ms = pkt->header.t_ms;
      restarts_.fetch_add(1);
    } else if (!s.have_t || pkt->header.t_ms > s.last_t_ms) {
      s.last_t_ms = pkt->header.t_ms;
    }
    s.have_t = true;

    if (s.last_seq != 0 && pkt->header.seq <= s.last_seq) return {};  // dup/old
    if (s.last_seq != 0 && pkt->header.seq > s.last_seq + 1)
      lost_.fetch_add(pkt->header.seq - s.last_seq - 1);
    s.last_seq = pkt->header.seq;

    LegTelemetry t{
        .position = {pkt->pos_rad[0], pkt->pos_rad[1], pkt->pos_rad[2]},
        .current_a = {pkt->current_a[0], pkt->current_a[1], pkt->current_a[2]},
        .fault_flags = pkt->fault_flags,
        .lost_packets = pkt->lost_packets,
        .crc_errors = pkt->crc_errors,
        .armed = pkt->armed != 0,
        .received_at = now,
        .valid = true};
    return RxTelemetry{static_cast<Leg>(pkt->header.leg_id), t};
  }

  RxPacket parse_ack(std::span<const std::uint8_t> raw) {
    const auto pkt = proto::parse<proto::AckPacket>(raw, proto::Type::Ack);
    if (!pkt || pkt->header.leg_id >= kNumLegs) { bad_packets_.fetch_add(1); return {}; }
    return RxAck{static_cast<Leg>(pkt->header.leg_id), pkt->acked_seq};
  }

  std::array<LegSeq, kNumLegs> legs_{};
  std::atomic<std::uint64_t> lost_{0}, bad_packets_{0}, restarts_{0};
};

}  // namespace quad
