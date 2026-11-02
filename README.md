# Jetson Orin + STM32H7 closed-loop visual tracker

> Status: in progress (started October 2026).

A camera mounted to a motor for visually tracking and centering an AprilTag (pan-only).

Vision in ROS 2 runs on a **Jetson Orin Nano**, while an **STM32H723** controls a geared DC motor in a 1 kHz closed loop. The real-time MCU communicates with the Jetson via **CAN**, with bulk telemetry handled over **Ethernet**. The motor control loop runs independently of the Linux and ROS 2 timing on the Jetson, and disables the motor if the jetson stops responding.

Safety:
- hardware e-stop
- firmware state machine
- heartbeat timeout
- current limiting
- watchdog timer

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
  end
  PWM --> DRV[VNH5019] --> MOT[goBILDA 5203 gearmotor]
  MOT -- encoder --> CTL
  DRV -- current sense --> PWM
  ES[e-stop] -- "cuts 12 V" --> DRV
  ES -- sense --> SAFE
```

## Repo layout (planned)
```
firmware/   STM32 firmware: lib/ (portable, host-tested), bsp/ (CubeMX), app/
ros2_ws/    ROS 2 packages (camera, apriltag, tracker, can_bridge)
hardware/   BOM, wiring
docs/       plan, setup guides, design, CAN spec, results
tools/      analysis scripts (jitter, step response, latency)
```
