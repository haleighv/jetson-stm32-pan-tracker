# Project Plan

Top-down plan for the project. Daily progress is in [worklog.md](worklog.md).

## Goal
A camera mounted to a motor that tracks and centers an AprilTag (pan-only).

- Vision runs in ROS 2 on the Jetson Orin Nano.
- The STM32H723 runs the motor in a 1 kHz closed loop, independent of Jetson and ROS 2 timing.
- The two talk over CAN. Bulk telemetry goes over Ethernet.
- Safety: hardware e-stop, firmware state machine, heartbeat timeout, current limiting, watchdog timer.
- End result is measured: end-to-end latency, control loop jitter, and step response.

## Constraints
- About 60 hours of work total, roughly 10 hours/week. Started 2026-10-30. No fixed dates per milestone; calendar time will stretch around holidays and travel. Milestone 6 is buffer.
- Hardware is already bought (see BOM). No 3D printer.

## Platform
| Area | Choice | Notes |
|---|---|---|
| Compute | Jetson Orin Nano Super Dev Kit, JetPack 7.2.1 (L4T R39.2.1), Ubuntu 24.04 | Stayed on 7.2.1 once the IMX219 worked |
| ROS | ROS 2 Jazzy | Matches Ubuntu 24.04 |
| Camera | Arducam IMX219 (B0191) on CSI-0, 720p60 | 59.77 fps measured, 0 dropped |
| MCU | NUCLEO-H723ZG (Cortex-M7, 3x FDCAN, Ethernet) | NUCLEO-H563ZI was sold out everywhere |
| RTOS | FreeRTOS | Zephyr is a stretch goal |
| Motor | goBILDA 5203, 26.9:1, quadrature encoder | ~752 counts per output rev (~0.48°/count) |
| Motor driver | Pololu VNH5019 | Current sense, 20 kHz PWM |
| Bus | Classic CAN 2.0A, 1 Mbps | Commands, heartbeat, status |
| Telemetry | UDP over Ethernet, 1 kHz | Not safety-critical |

## Milestones
| # | Goal | Done when | Status |
|---|---|---|---|
| 1 | Bring-up | Camera at 720p60 ±1 fps for 60 s with no drops. Nucleo 1 kHz timer ISR with jitter p99 < 2 µs. FDCAN internal loopback, 10k frames, 0 errors. CI green. | Done |
| 2 | CAN link + closed loop | Jetson and Nucleo talk over CAN at 1 Mbps. Motor holds a commanded angle within ±1°. Heartbeat loss turns PWM off within 50 ms (scope). | CAN link up |
| 3 | Safety + control | E-stop cuts power and latches a fault. Current limit trips on stall. Watchdog reset recovers safely. Velocity and position loops tuned. UDP telemetry with no drops. Step response and jitter plots from scripts. | |
| 4 | ROS 2 pipeline | Camera -> AprilTag -> tracker -> CAN bridge. Tracks a tag moving on a monitor. Tracking error plot. | |
| 5 | Measurement + HIL | Latency p50/p90 with a per-stage breakdown. HIL tests for missed heartbeat, bad CRC/counter, out-of-range setpoint, bus-off. CAN spec v1.0. Design doc. | |
| 6 | Finish | Demo video (60-90 s). README results filled in. Blog post. Resume and LinkedIn lines. | |

## Milestone 1 checklist
- [x] Repo, README, commit template, .gitignore
- [x] Jetson flashed (JetPack 7.2.1 to SSD), headless SSH over Wi-Fi
- [x] IMX219 at 720p60 (59.77 fps avg, 0 dropped)
- [x] Mac toolchain (Command Line Tools, cmake, ninja, gcc-arm-embedded)
- [x] PID on the host with unit tests
- [x] CI: host tests + cross-compile (GitHub Actions)
- [x] Nucleo: clocks, UART, blink
- [x] Nucleo: 1 kHz timer ISR, DWT jitter stats (±153 ns under load)
- [x] Nucleo: Saleae jitter capture, p99 1.1 µs idle and loaded (`tools/jitter.py`)
- [x] FDCAN internal loopback: 1M frames, 0 lost, 0 corrupted, 0 errors

## Results targets
| Metric | Target | How it's measured |
|---|---|---|
| Photon-to-motion latency, p50 / p90 | < 60 / 80 ms | Photodiode on a screen flash vs first encoder edge, on one scope |
| Control loop period jitter, p99 | < 2 µs | GPIO toggle in the ISR on the Saleae, cross-checked with the DWT cycle counter |
| Step response (30°): rise time / overshoot | < 100 ms / < 10 % | Encoder telemetry over UDP |
| Heartbeat loss -> motor disabled | ≤ 50 ms | Scope on PWM vs CAN cable pull |

## Scope
Out of scope (decided by the hardware):
- Tilt axis (one motor)
- GMSL camera
- BLDC motor / FOC

Stretch goals, only if ahead:
- TensorRT detector
- Zephyr port, Rust
- PTP time sync

If behind, trim in this order:
1. Blog post -> short write-up in the README
2. Automated HIL tests -> scripted manual fault-test checklist
3. 1 kHz UDP telemetry over Ethernet -> 100 Hz telemetry over CAN only

Never cut: safety (e-stop, heartbeat timeout, current limit, watchdog) and the core measurements (latency, jitter, step response).

## Risks
| Risk | Plan |
|---|---|
| JetPack 7.2.1 issues on Orin Nano (power modes, jetson-io) | Camera already works. Still need to check MAXN SUPER. Can roll back to 6.2 with SDK Manager (recovery mode confirmed working). |
| Jetson CAN (soldering J17, pinmux, termination) | Internal loopback first. Use the CANable as a known-good node. Check ~60 Ω across CAN-H/CAN-L with power off. |
| H7 cache vs Ethernet DMA, cache-miss jitter in the control ISR | Non-cacheable MPU region for Ethernet buffers. Control ISR in ITCM, data in DTCM. |
| Coarse encoder (~0.48°/count) makes velocity noisy | Windowed velocity estimate. Tune the position loop first. |
| Tuning / backlash oscillation | Velocity loop first. Open-loop step to identify the plant. Soft limits. PSU current limit. |
| Schedule (~60 hours total) | One milestone at a time, in order. Trim list in Scope if behind. |

## Open questions
- Which power mode to lock for measurements (25W vs MAXN SUPER)
