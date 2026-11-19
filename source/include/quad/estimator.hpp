#pragma once
// Diagram box: "state estimator — joints + currents + IMU -> body pose
// (visual odometry later)". v1 keeps it minimal: joint-based body height
// plus a hook where the dedicated IMU plugs in without touching the gait.
#include <array>
#include <cmath>

#include "quad/config.hpp"
#include "quad/types.hpp"

namespace quad {

struct ImuSample {           // filled by the dedicated IMU driver (v1)
  double roll{}, pitch{};    // rad
  bool valid{false};
};

struct BodyState {
  double height{cfg::kBodyHeight};  // estimated hip-to-ground, m
  double roll{}, pitch{};           // rad, from IMU when present
  bool legs_consistent{true};
};

class StateEstimator {
 public:
  BodyState update(const std::array<LegTelemetry, kNumLegs>& telem,
                   const ImuSample& imu) {
    // Height from forward kinematics of the stance legs (cheap v1 version:
    // straight-leg approximation refined once joint signs are calibrated).
    double sum = 0.0;
    int n = 0;
    for (const auto& t : telem) {
      if (!t.valid) continue;
      const double ext = cfg::kUpperLen * std::cos(t.position.hip) +
                         cfg::kLowerLen * std::cos(t.position.hip + t.position.knee);
      sum += ext;
      ++n;
    }
    if (n > 0) state_.height = sum / n;
    state_.legs_consistent = (n == kNumLegs);

    if (imu.valid) {
      state_.roll = imu.roll;
      state_.pitch = imu.pitch;
    }
    return state_;
  }

  const BodyState& state() const { return state_; }

 private:
  BodyState state_{};
};

}  // namespace quad
