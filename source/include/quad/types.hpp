#pragma once
// Shared value types. Frames per Conventions.md: body X forward, Y left, Z up.
#include <array>
#include <chrono>
#include <cstdint>

namespace quad {

using Clock = std::chrono::steady_clock;

// Leg numbering per Conventions.md. The number is the leg's index everywhere
// (arrays, the packet leg_id, the firmware's LEG_ID build flag), so code must
// refer to legs by name, never by assuming an order.
//   index 0 = Front Right (firmware 1), 1 = Front Left (firmware 2),
//   index 2 = Rear Right  (firmware 3), 3 = Rear Left  (firmware 4)
enum class Leg : std::uint8_t { FR = 0, FL = 1, RR = 2, RL = 3 };
inline constexpr int kNumLegs = 4;

constexpr const char* leg_name(Leg leg) {
  switch (leg) {
    case Leg::FR: return "FR";
    case Leg::FL: return "FL";
    case Leg::RR: return "RR";
    case Leg::RL: return "RL";
  }
  return "?";
}

constexpr bool is_left(Leg leg) { return leg == Leg::FL || leg == Leg::RL; }

struct Vec3 {
  double x{}, y{}, z{};
};

// tilt / hip / knee, radians, zero positions per Conventions.md
struct JointAngles {
  double tilt{}, hip{}, knee{};
};

// The one struct every command source produces (gamepad today, autonomy later).
struct BodyCommand {
  double vx{};        // m/s forward
  double vy{};        // m/s left
  double yaw_rate{};  // rad/s, +CCW
  bool   enable{};    // operator wants the dog armed
  bool   estop{};     // software stop (hardware E-stop is separate!)
};

// What one leg reports back (unpacked from a TELEMETRY packet).
struct LegTelemetry {
  JointAngles   position{};
  std::array<float, 3> current_a{};  // per joint, amperes
  std::uint16_t fault_flags{};
  std::uint16_t lost_packets{};
  std::uint16_t crc_errors{};
  bool          armed{};
  Clock::time_point received_at{};
  bool          valid{false};
};

enum class SysState : std::uint8_t {
  Boot, Idle, Standup, Stand, Trot, Fault
};

constexpr const char* to_string(SysState s) {
  switch (s) {
    case SysState::Boot:    return "BOOT";
    case SysState::Idle:    return "IDLE";
    case SysState::Standup: return "STANDUP";
    case SysState::Stand:   return "STAND";
    case SysState::Trot:    return "TROT";
    case SysState::Fault:   return "FAULT";
  }
  return "?";
}

}  // namespace quad
