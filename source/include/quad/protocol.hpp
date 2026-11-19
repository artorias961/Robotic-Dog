#pragma once
// Wire protocol per Conventions.md: little-endian packed structs,
// common 14-byte header, CRC16-CCITT over header+payload (crc field last).
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace quad::proto {

inline constexpr std::uint16_t kMagic   = 0xC0DE;  // fixed start marker: anything else is rejected instantly
inline constexpr std::uint8_t  kVersion = 1;

enum class Type : std::uint8_t {
  Setpoint  = 1,  // master -> leg, 100 Hz
  Command   = 2,  // master -> leg, acked
  Heartbeat = 3,  // master -> leg, 10 Hz, every FSM state
  Telemetry = 4,  // leg -> master, ~500 Hz
  Ack       = 5,  // leg -> master, echoes command seq
};

enum class Cmd : std::uint8_t {
  Enable      = 1,  // arm (legs refuse while faults are latched)
  Disable     = 2,  // disarm
  ClearFaults = 3,  // operator acknowledged a fault; legs honor it only while disarmed
};

#pragma pack(push, 1)
struct Header {
  std::uint16_t magic;
  std::uint8_t  version;
  std::uint8_t  type;
  std::uint8_t  leg_id;   // 0..3, 0xFF = n/a
  std::uint8_t  flags;    // reserved
  std::uint32_t seq;      // per-sender, monotonic
  std::uint32_t t_ms;     // sender uptime, ms
};
static_assert(sizeof(Header) == 14);

struct SetpointPacket {
  Header header;
  float  target_rad[3];   // tilt, hip, knee
  std::uint16_t crc;
};
struct CommandPacket {
  Header header;
  std::uint8_t cmd;       // Cmd
  float        param;
  std::uint16_t crc;
};
struct HeartbeatPacket {
  Header header;
  std::uint8_t fsm_state; // SysState, informational
  std::uint16_t crc;
};
struct TelemetryPacket {
  Header header;
  float pos_rad[3];
  float current_a[3];
  std::uint16_t fault_flags;
  std::uint16_t lost_packets;
  std::uint16_t crc_errors;
  std::uint8_t  armed;
  std::uint8_t  state;
  std::uint16_t crc;
};
struct AckPacket {
  Header header;
  std::uint32_t acked_seq;
  std::uint16_t crc;
};
#pragma pack(pop)

// CRC16-CCITT (poly 0x1021, init 0xFFFF), computed over everything before crc.
constexpr std::uint16_t crc16(std::span<const std::uint8_t> data) {
  std::uint16_t crc = 0xFFFF;
  for (std::uint8_t b : data) {
    crc ^= static_cast<std::uint16_t>(b) << 8;
    for (int i = 0; i < 8; ++i)
      crc = (crc & 0x8000) ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021)
                           : static_cast<std::uint16_t>(crc << 1);
  }
  return crc;
}

// Stamp the trailing crc field of any packet struct.
template <typename P>
void seal(P& pkt) {
  auto* bytes = reinterpret_cast<std::uint8_t*>(&pkt);
  pkt.crc = crc16({bytes, sizeof(P) - sizeof(std::uint16_t)});
}

// Validate size, magic, version and crc; returns a copy on success.
template <typename P>
std::optional<P> parse(std::span<const std::uint8_t> raw, Type expected) {
  if (raw.size() != sizeof(P)) return std::nullopt;
  P pkt;
  std::memcpy(&pkt, raw.data(), sizeof(P));
  if (pkt.header.magic != kMagic) return std::nullopt;
  if (pkt.header.version != kVersion) return std::nullopt;
  if (pkt.header.type != static_cast<std::uint8_t>(expected)) return std::nullopt;
  if (crc16(raw.first(sizeof(P) - sizeof(std::uint16_t))) != pkt.crc) return std::nullopt;
  return pkt;
}

}  // namespace quad::proto
