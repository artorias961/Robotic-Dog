#pragma once
// Diagram boxes: "system + gait FSM (phase clock)", "per-leg phase",
// "foot trajectory generator — stance: push back, swing: arc forward".
#include <array>
#include <cmath>
#include <numbers>

#include "quad/config.hpp"
#include "quad/types.hpp"

namespace quad {

// Trot: diagonal pairs move together. FL+RR at phase offset 0.0, FR+RL at 0.5.
// Decided by leg name, so it stays correct whatever the leg numbering.
constexpr double trot_offset(Leg leg) {
  return (leg == Leg::FL || leg == Leg::RR) ? 0.0 : 0.5;
}
inline constexpr double kDutyFactor = 0.5;  // fraction of the cycle a foot is in stance

// Neutral foot position in each leg's hip frame (X fwd, Y left, Z up).
inline Vec3 neutral_foot(Leg leg, double body_height) {
  return Vec3{.x = 0.0, .y = is_left(leg) ? 0.03 : -0.03, .z = -body_height};
}

class GaitScheduler {
 public:
  // Advance the global phase clock by dt seconds; phi wraps in [0,1).
  void advance(double dt) {
    phi_ += dt / cfg::kCycleTime;
    phi_ -= std::floor(phi_);
  }
  void reset() { phi_ = 0.0; }
  double phase() const { return phi_; }

  // Foot target for one leg, given the current body command.
  // Walking straight: stance slides the foot backward (pushes the body
  // forward); swing lifts and arcs it forward to the next foothold.
  Vec3 foot_target(Leg leg, const BodyCommand& cmd, double body_height) const {
    double p = phi_ + trot_offset(leg);
    p -= std::floor(p);

    // Per-leg stride: forward from vx, lateral from vy, differential from yaw
    // (left legs shorten, right legs lengthen when turning CCW).
    const bool  left   = is_left(leg);
    const double yaw_scale = 1.0 + (left ? -1.0 : 1.0) * 0.4 * (cmd.yaw_rate / cfg::kMaxYawRate);
    const double stride_x  = cmd.vx * cfg::kCycleTime * yaw_scale;
    const double stride_y  = cmd.vy * cfg::kCycleTime;

    const Vec3 neutral = neutral_foot(leg, body_height);
    Vec3 foot = neutral;

    if (p < kDutyFactor) {
      // Stance: linear slide from +stride/2 to -stride/2, foot on ground.
      const double s = p / kDutyFactor;                 // 0 -> 1
      foot.x += stride_x * (0.5 - s);
      foot.y += stride_y * (0.5 - s);
    } else {
      // Swing: return -stride/2 -> +stride/2 with a sine lift.
      const double s = (p - kDutyFactor) / (1.0 - kDutyFactor);
      foot.x += stride_x * (s - 0.5);
      foot.y += stride_y * (s - 0.5);
      foot.z += cfg::kSwingLift * std::sin(std::numbers::pi * s);
    }
    return foot;
  }

 private:
  double phi_ = 0.0;
};

}  // namespace quad
