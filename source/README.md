# Quadruped master board

C++20 implementation of the "Master board — how the dog walks" diagram.
Protocol, IPs, rates and timeouts follow `Conventions.md` in the team wiki.

## Diagram box → code map

| Diagram box                               | File                              |
|-------------------------------------------|-----------------------------------|
| system + gait FSM (boot→…→trot · fault)   | `include/quad/state_machine.hpp`  |
| per-leg phase, foot trajectory generator  | `include/quad/gait.hpp`           |
| inverse kinematics ×4 legs                | `include/quad/kinematics.hpp`     |
| safety gate + per-leg watchdog            | `include/quad/safety.hpp`         |
| pack setpoints + seq + CRC, UDP sendto ×4 | `include/quad/comms.hpp`          |
| heartbeat 10 Hz always                    | `control_thread` in `main.cpp`    |
| UDP recvfrom telemetry                    | `rx_thread` in `main.cpp`         |
| state estimator (+ dedicated IMU hook)    | `include/quad/estimator.hpp`      |
| packet format, magic 0xC0DE, CRC16        | `include/quad/protocol.hpp`       |
| latest-value-wins shared slots            | `include/quad/latest.hpp`         |
| precise 100 Hz tick (Windows HR timer)    | `include/quad/timing.hpp`         |
| all tunables (IPs, rates, geometry)       | `include/quad/config.hpp`         |

## Threads

- **control** (`std::jthread`, 100 Hz): FSM → phase → trajectory → IK →
  safety gate → pack + send. Sends the 10 Hz heartbeat in *every* state, which
  lets a Teensy tell "master idle" from "master dead" (legs disarm after
  500 ms without it). Prints a status line whenever the state or a leg changes.
- **rx**: blocks on UDP :5006, validates magic/CRC/seq, counts losses,
  detects leg reboots, routes telemetry to `Latest<T>` slots and ACKs to the
  command channel.
- **main**: console stand-in for the gamepad. Replace with a real gamepad
  reader later; it only has to fill `BodyCommand`.

## Operating it (console)

| Key | Effect |
|-----|--------|
| `e` | enable: IDLE → STANDUP (ramps from the pose the legs report) → STAND |
| `w` / `s`, `a` / `f` | forward/back, turn (STAND → TROT while moving) |
| space | stop moving |
| `d` | disable; **in FAULT, acknowledges the fault** (sends CLEAR_FAULTS) |
| `x` | software E-stop (the hardware E-stop is separate) |
| `q` | quit |

Press Enter after each. To recover from a fault: `d`, then `e`. If the master
stays in FAULT after `d`, the leg's fault cause is still present; the status
line shows which leg.

## Startup and recovery rules

- Commands (ENABLE/DISABLE/CLEAR_FAULTS) go out immediately, in order, and
  retry every 20 ms until ACKed (10 attempts).
- STANDUP holds at the measured pose until every leg reports armed; a leg that
  doesn't arm within 0.5 s → FAULT ("leg did not arm"). A leg that disarms
  while standing or trotting → FAULT ("leg disarmed").
- A leg reboot is detected from its uptime going backwards (`leg_restarts`
  in the status line) and its telemetry is accepted immediately.
- Faults latched before the master started (e.g. the previous master died)
  put it in FAULT at boot; with enable off, it clears them automatically.

## C++20 used

`std::jthread`/`stop_token` (clean shutdown), `std::span` (packet parsing),
`std::variant` (received packet types), `std::format`, `std::atomic`
(lock-free ACK hand-off), `std::numbers::pi`, designated initializers,
`concepts` (`Latest<T>` requires trivially copyable), `chrono` literals.

## Build

The repository's top-level `CMakeLists.txt` builds this code with the bundled
toolchain. From the repository root:

```
.\tools\mingw64\bin\cmake.exe --preset windows
.\tools\mingw64\bin\cmake.exe --build --preset windows
```

To run it from a terminal, put the bundled toolchain on `PATH` first; the
program needs its runtime DLLs (`libstdc++-6.dll` and friends). In PowerShell:

```
$env:PATH = "$PWD\tools\mingw64\bin;$env:PATH"
.\build\windows\RoboticDog.exe
```

In VS Code, Run/Debug does this for you (`.vscode/launch.json`), and the CMake
Tools extension uses the same presets. On Linux, run
`bash tools/linux-x64/setup.sh` once, then use the bundled CMake with
`--preset linux`. No external dependencies.

## Honest limitations (v1 skeleton)

- IK signs and leg geometry are placeholders until the frame is measured —
  fix them in `config.hpp`/`kinematics.hpp` only.
- `Latest<T>` is a pragmatic seqlock; on PC-class hardware this is fine.
- Console input is line-buffered (press Enter); a gamepad thread replaces it.
- No logging-to-file yet.
- The Teensy applies each setpoint as it arrives (no interpolation). Fine for
  50 Hz hobby servos; add interpolation before moving to motors + PID.
