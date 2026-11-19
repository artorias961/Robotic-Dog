# Quadruped conventions (v1)

Single source of truth for units, frames, network, packets, and calibration.
Change this page only by team agreement — code follows the page, never the other way around.

## Units

SI everywhere, no exceptions: **radians**, **meters**, **seconds**, **amperes**.
All values in packets are `float` (32-bit) or fixed-size integers. Never degrees, never millimeters on the wire.

## Coordinate frames

- **Body frame**: origin at the geometric center of the body. **X forward, Y left, Z up** (right-handed).
- **Leg numbering**: `FR = 0`, `FL = 1`, `RR = 2`, `RL = 3` (firmware labels 1–4 in the same order). The number is the leg's index in code, in the packet `leg_id`, and in the `LEG_ID` build flag.
- **Hip frame (per leg)**: same orientation as the body frame, origin at that leg's tilt-joint axis. Foot targets are expressed in the leg's hip frame.
- **Joint zero positions**: tilt = 0 when the leg plane is vertical; hip = 0 when the upper leg points straight down; knee = 0 when the lower leg is in line with the upper leg.
- **Joint sign**: positive rotation by the right-hand rule about each joint's axis. Document each axis direction with a photo in the wiki once the frame is built.

## Network (decision: static IPs, no DHCP)

| Position         | IP             | Firmware | `LEG_ID` |
|------------------|----------------|----------|----------|
| Master           | 192.168.1.1    | —        | —        |
| Front Right (FR) | 192.168.1.11   | 1        | 0        |
| Front Left (FL)  | 192.168.1.12   | 2        | 1        |
| Rear Right (RR)  | 192.168.1.13   | 3        | 2        |
| Rear Left (RL)   | 192.168.1.14   | 4        | 3        |

- Subnet `192.168.1.0/24`, dedicated switch, nothing else plugged in.
  Most home routers also use 192.168.1.x, so keep the master PC off Wi-Fi
  (or off any other 192.168.1.x network) while it drives the robot.
- Teensys accept packets **only** from 192.168.1.1.
- Each Teensy's MAC address is automatic and unique (derived from the chip's ID); nothing to configure.
- Ports: master → Teensy on **UDP 5005** (setpoints, commands, heartbeat); Teensy → master on **UDP 5006** (telemetry, acks).

## Packet format

Little-endian, packed structs (`#pragma pack(push,1)` / `__attribute__((packed))`).
Common header on every packet, CRC16-CCITT over header + payload (CRC field itself zeroed during computation).

```cpp
struct Header {            // 14 bytes
    uint16_t magic;        // 0xC0DE — "magic number": fixed start marker every valid
                           // packet begins with; anything else is rejected instantly
    uint8_t  version;      // protocol version, starts at 1
    uint8_t  type;         // PacketType below
    uint8_t  leg_id;       // 0-3, or 0xFF = broadcast/not applicable
    uint8_t  flags;        // reserved, 0
    uint32_t seq;          // per-sender, monotonically increasing
    uint32_t t_ms;         // sender uptime in ms
};

enum PacketType : uint8_t {
    SETPOINT  = 1,  // master -> leg: 3 joint targets (rad), 100 Hz
    COMMAND   = 2,  // master -> leg: see CommandId (acked)
    HEARTBEAT = 3,  // master -> leg: FSM state, 10 Hz, sent in every state
    TELEMETRY = 4,  // leg -> master: positions, currents, faults, stats, ~500 Hz
    ACK       = 5,  // leg -> master: echoes seq of the COMMAND it confirms
};

enum CommandId : uint8_t {
    ENABLE       = 1,  // arm; refused while the leg has latched faults
    DISABLE      = 2,  // disarm
    CLEAR_FAULTS = 3,  // operator acknowledged a fault; honored only while disarmed
};                     // (SET_PARAM for calibration push: reserved, not implemented yet)
// Every packet ends with: uint16_t crc;  (CRC16-CCITT)
```

### Sequence rules

- **Streams** (setpoints, heartbeats, telemetry): newest wins. A packet whose `seq` is not newer than the last one seen from that sender is stale and dropped; gaps count as lost packets.
- **Commands are never dropped as stale.** A retransmitted command keeps its original `seq`, which newer setpoints may already have passed. Legs de-duplicate commands by command `seq` instead, and always re-ACK a duplicate. A command is ACKed only once it is queued.
- **Restarts:** every header carries the sender's uptime (`t_ms`). If it jumps backwards by more than 1 s, the sender restarted and the receiver resets its sequence tracking for that sender. Without this, a restarted master (or rebooted leg) would be ignored for as long as the previous session ran.
- **Faults clear only by operator acknowledgement.** In FAULT, releasing enable (`d` on the console) sends CLEAR_FAULTS. A cause that is still present re-latches, and the master stays in FAULT.

### Rates and timeouts

| Stream / rule                              | Value                         |
|--------------------------------------------|-------------------------------|
| Setpoints (master → leg)                   | 100 Hz                        |
| Telemetry (leg → master)                   | ~500 Hz                       |
| Heartbeat (master → leg, every FSM state)  | 10 Hz                         |
| Leg failsafe: no setpoint while armed      | 50 ms → hold / torque off     |
| Leg disarm: no heartbeat                   | 500 ms → disarm               |
| Master: no telemetry from a leg            | 100 ms → leg fault, safety gate |
| COMMAND retransmit until ACK               | every 20 ms, give up after 10 |
| Master STANDUP: all legs must report armed | 0.5 s → FAULT ("leg did not arm") |
| Restart detection (sender uptime back by)  | > 1 s → reset sequence tracking |
| Leg hardware watchdog (i.MX WDOG2)         | 1 s (driver minimum 0.5 s); fed every 20–100 ms |
| Leg boot wait for Ethernet link            | 0 s (static IP is assigned regardless) |

## Calibration (decision: master-side config, pushed at ENABLE)

Joint calibration lives in **one file on the master** (`robot_config.yaml`), versioned in git.
The master sends each leg its parameters during the ENABLE handshake; Teensys keep them in RAM only and never need reflashing for a calibration tweak.

```yaml
# robot_config.yaml (excerpt)
legs:
  FL:
    tilt: { raw_at_zero: 2048, rad_per_count: 0.001534, sign: +1, min_rad: -0.6, max_rad: 0.6, current_limit_a: 3.0 }
    hip:  { raw_at_zero: 1990, rad_per_count: 0.001534, sign: -1, min_rad: -1.2, max_rad: 1.2, current_limit_a: 4.0 }
    knee: { raw_at_zero: 2105, rad_per_count: 0.001534, sign: +1, min_rad:  0.0, max_rad: 2.4, current_limit_a: 4.0 }
```

Rationale: one place to edit, diffable in git, nothing baked into firmware; a Teensy swap needs no per-board setup beyond its static IP.

## Build targets

- Master: C++20 (jthread, atomics, chrono, span, variant, format). Windows uses a high-resolution waitable timer for the 100 Hz loop; `sleep_until` alone jitters by ±15 ms there.
- Teensy: Zephyr RTOS; C, or restricted C++ (no exceptions, no RTTI, no heap in the control path).
- Actuators v1: hobby servos (Teensy writes position PWM directly). v2: DC motors + drivers (Teensy runs the joint PID). Same packet format either way.
