#pragma once
// Single source of truth for tunables. Values mirror Conventions.md —
// change them there first, here second.
#include <chrono>
#include <cstdint>
#include <string_view>

namespace quad::cfg {

using namespace std::chrono_literals;

// ---- network (Conventions.md: static IPs, no DHCP) -------------------------
// Indexed by leg number (see Leg in types.hpp). The master's own address,
// 192.168.1.1, is set in the PC's network settings for the robot's switch.
inline constexpr std::string_view kLegIp[4] = {
    "192.168.1.11",  // 0 Front Right (firmware 1)
    "192.168.1.12",  // 1 Front Left  (firmware 2)
    "192.168.1.13",  // 2 Rear Right  (firmware 3)
    "192.168.1.14",  // 3 Rear Left   (firmware 4)
};
inline constexpr std::uint16_t kPortToLeg    = 5005;  // setpoints, commands, heartbeat
inline constexpr std::uint16_t kPortToMaster = 5006;  // telemetry, acks

// ---- rates and timeouts -----------------------------------------------------
inline constexpr auto kControlPeriod    = 10ms;   // 100 Hz gait tick
inline constexpr int  kHeartbeatEvery   = 10;     // every 10th tick -> 10 Hz, in every FSM state
inline constexpr auto kTelemetryTimeout = 100ms;  // no telemetry from a leg -> leg fault
inline constexpr auto kCommandRetry     = 20ms;   // COMMAND retransmit until ACK
inline constexpr int  kCommandMaxRetry  = 10;
inline constexpr std::uint32_t kRestartSlackMs = 1000;  // sender uptime going back more than this = restart
inline constexpr double kArmTimeoutS    = 0.5;    // STANDUP: every leg must report armed by then
inline constexpr auto kLossWindow       = 1000ms; // link-trend window per leg
inline constexpr int  kLossWindowLimit  = 20;     // lost in window -> leg degraded
inline constexpr double kMaxTiltRad     = 0.6;    // |roll|/|pitch| beyond this -> fault

// ---- gait defaults (tune on hardware) ---------------------------------------
inline constexpr double kCycleTime   = 0.6;    // s per full gait cycle at trot
inline constexpr double kBodyHeight  = 0.16;   // m, standing hip-to-ground
inline constexpr double kSwingLift   = 0.04;   // m, foot clearance in swing
inline constexpr double kMaxVx       = 0.25;   // m/s
inline constexpr double kMaxYawRate  = 1.0;    // rad/s
inline constexpr double kStandupTime = 2.0;    // s, BOOT->STAND ramp

// ---- leg geometry (metres; placeholder until the frame is measured) ---------
inline constexpr double kUpperLen = 0.11;  // hip -> knee
inline constexpr double kLowerLen = 0.12;  // knee -> foot

}  // namespace quad::cfg
