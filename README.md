# Jetson Orin + STM32H7 closed-loop visual tracker

> Status: in progress (started October 2026).

A camera mounted to a motor for visually tracking and centering an AprilTag (pan-only).

Vision in ROS 2 runs on a **Jetson Orin Nano**, while an **STM32H723** controls a geared DC motor in a 1 kHz closed loop. The real-time MCU communicates with the Jetson via **CAN**, with bulk telemetry handled over **Ethernet**. The motor control loop runs independently of the Linux and ROS 2 timing on the Jetson, and disables the motor if the Jetson stops responding.

Safety:
- hardware e-stop
- firmware state machine
- heartbeat timeout
- current limiting
- watchdog timer

## Progress
- [x] Jetson: JetPack 7.2.1 + ROS 2 Jazzy, IMX219 at 720p60 (59.77 fps, 0 dropped over ~68 s)
- [x] PID controller with host unit tests (anti-windup, no derivative kick, NaN-safe config)
- [x] STM32 bring-up: 520 MHz, FreeRTOS, task table, queued leveled logging, UART CLI
- [ ] 1 kHz control tick + jitter measurement
- [ ] CAN link to the Jetson
- [ ] Motor, encoder, current sense
- [ ] Safety: e-stop, heartbeat, watchdog
- [ ] ROS 2 tracking pipeline
- [ ] Measured results

## Demo
Coming soon

## Results
| Metric | Target (Day 0 estimate) | Measured |
|---|---|---|
| Photon-to-motion latency, p50 / p90 | < 60 / 80 ms | TBD |
| Control-loop period jitter, p99 | < 2 µs | TBD |
| Step response (30°): rise time / overshoot | < 100 ms / < 10 % | TBD |
| Heartbeat-loss → motor disabled | ≤ 50 ms | TBD |

## Architecture (planned)
```mermaid
flowchart LR
  subgraph J[Jetson Orin Nano · JetPack 7.2.1 · ROS 2 Jazzy]
    CAM[camera node<br/>IMX219 720p60] --> TAG[apriltag node]
    TAG --> TRK[tracker node<br/>outer loop 60 Hz]
    TRK --> BR[can_bridge<br/>SocketCAN]
    LOG[telemetry logger]
  end
  BR -- "CAN 1 Mbps: setpoint, heartbeat, command" --> M
  M -- "CAN: status 100 Hz" --> BR
  M -- "UDP: 1 kHz telemetry" --> LOG
  subgraph M[NUCLEO-H723ZG · FreeRTOS]
    SAFE[safety state machine] --> CTL[pos→vel PID 1 kHz]
    CTL --> PWM[PWM 20 kHz + current limit]
    DBG[logs + CLI<br/>USART3]
  end
  PWM --> DRV[VNH5019] --> MOT[goBILDA 5203 gearmotor]
  MOT -- encoder --> CTL
  DRV -- current sense --> PWM
  ES[e-stop] -- "cuts 12 V" --> DRV
  ES -- sense --> SAFE
```

## Repo layout
```
firmware/
  lib/        portable C, host-tested (PID, CLI parser)
  tests/      Unity host tests
  nucleo/     NUCLEO-H723ZG target: CubeMX BSP (Core/, Drivers/, .ioc) + app/
  scripts/    build.sh, test.sh (host), build_nucleo.sh, flash.sh (target)
ros2_ws/      ROS 2 packages (camera, apriltag, tracker, can_bridge)
hardware/     BOM, wiring
docs/         plan, worklog, firmware architecture, results/
tools/        analysis scripts (jitter, step response, latency)
```

## Build and test
Needs cmake, ninja, Arm GNU Toolchain (`arm-none-eabi-gcc`), and STM32CubeProgrammer (`STM32_Programmer_CLI` on PATH).

```
./firmware/scripts/test.sh            # host unit tests (PID, CLI)
./firmware/scripts/flash.sh           # build + flash the Nucleo over ST-LINK
screen /dev/cu.usbmodem* 115200       # logs + CLI, type ? for commands
```

## Docs
- [Plan](docs/plan.md): milestones, results targets, scope
- [Worklog](docs/worklog.md): day-by-day progress
- [Firmware architecture](docs/firmware-architecture.md): tasks, priorities, logging, memory
- [Results](docs/results/): raw measurements
