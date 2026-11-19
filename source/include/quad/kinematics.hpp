#pragma once
// Diagram box: "inverse kinematics ×4 legs — foot (x,y,z) -> tilt, hip, knee".
// 3-DOF leg, hip frame per Conventions.md (X fwd, Y left, Z up, foot below -> z < 0).
// Zero positions: tilt 0 = leg plane vertical, hip 0 = upper leg straight down,
// knee 0 = lower leg in line with upper. Signs are placeholders until the
// frame is measured — fix them in one place here.
#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

#include "quad/config.hpp"
#include "quad/types.hpp"

namespace quad {

inline std::optional<JointAngles> inverse_kinematics(const Vec3& foot) {
  constexpr double L1 = cfg::kUpperLen, L2 = cfg::kLowerLen;

  if (foot.z >= 0.0) return std::nullopt;  // foot must be below the hip

  // Tilt rotates the leg plane around the X axis to reach lateral offsets.
  const double tilt = std::atan2(foot.y, -foot.z);
  // Distance from hip to foot projected into the (now vertical) leg plane.
  const double r = std::sqrt(foot.y * foot.y + foot.z * foot.z);

  const double d2 = foot.x * foot.x + r * r;
  const double d  = std::sqrt(d2);
  const double reach_min = std::abs(L1 - L2) + 1e-4;
  if (d > (L1 + L2) * 0.999 || d < reach_min) return std::nullopt;

  // Two-link planar IK. knee = 0 when straight, bends positive.
  const double cos_knee = (L1 * L1 + L2 * L2 - d2) / (2.0 * L1 * L2);
  const double knee = std::numbers::pi - std::acos(std::clamp(cos_knee, -1.0, 1.0));

  const double cos_a = (L1 * L1 + d2 - L2 * L2) / (2.0 * L1 * d);
  const double alpha = std::acos(std::clamp(cos_a, -1.0, 1.0));
  const double hip   = std::atan2(foot.x, r) + alpha;  // 0 = straight down

  return JointAngles{.tilt = tilt, .hip = hip, .knee = knee};
}

}  // namespace quad
