#pragma once
// Diagram boxes: "per-leg watchdog — telemetry fresh? fault flags? loss
// counters rising?" and "safety gate: legs healthy + armed?".
#include <array>

#include "quad/config.hpp"
#include "quad/types.hpp"

namespace quad {

struct LegHealth {
  bool fresh{false};     // telemetry within kTelemetryTimeout
  bool fault{false};     // leg-reported fault flags
  bool armed{false};
  bool link_degraded{false};  // loss counter rising inside kLossWindow
};

class SafetyGate {
 public:
  // Evaluate all legs against the latest telemetry snapshot.
  void update(const std::array<LegTelemetry, kNumLegs>& telem, Clock::time_point now) {
    all_healthy_ = true;
    all_armed_ = true;
    any_fault_ = false;
    for (int i = 0; i < kNumLegs; ++i) {
      auto& h = health_[static_cast<std::size_t>(i)];
      const auto& t = telem[static_cast<std::size_t>(i)];
      h.fresh = t.valid && (now - t.received_at) < cfg::kTelemetryTimeout;
      h.fault = t.valid && t.fault_flags != 0;
      h.armed = t.valid && t.armed;

      // "loss counters rising?": watch each leg's lost_packets over a 1 s
      // window. On a dedicated switch it should stay flat; a rising count is
      // a cable/bug to investigate and the leg is treated as unhealthy.
      auto& w = window_[static_cast<std::size_t>(i)];
      if (w.started == Clock::time_point{}) {
        w.started = now;
        w.lost_at_start = t.lost_packets;
      } else if (now - w.started >= cfg::kLossWindow) {
        const int delta =
            static_cast<int>(t.lost_packets) - static_cast<int>(w.lost_at_start);
        h.link_degraded = (delta > cfg::kLossWindowLimit) || (delta < 0);
        w.started = now;
        w.lost_at_start = t.lost_packets;
      }

      if (!h.fresh || h.fault || h.link_degraded) all_healthy_ = false;
      if (!h.fresh || !h.armed) all_armed_ = false;  // stale "armed" means nothing
      if (h.fresh && h.fault) any_fault_ = true;
    }
  }

  bool all_healthy() const { return all_healthy_; }
  bool all_armed() const { return all_armed_; }
  bool any_fault() const { return any_fault_; }  // a live leg reports fault flags
  const std::array<LegHealth, kNumLegs>& legs() const { return health_; }

 private:
  struct LossWindow {
    Clock::time_point started{};
    std::uint16_t lost_at_start{0};
  };
  std::array<LegHealth, kNumLegs> health_{};
  std::array<LossWindow, kNumLegs> window_{};
  bool all_healthy_{false};
  bool all_armed_{false};
  bool any_fault_{false};
};

}  // namespace quad
