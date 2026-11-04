# Firmware Architecture

STM32H723 (NUCLEO-H723ZG) firmware for the pan axis. Skeleton from [plan.md](plan.md); TBDs get filled in as I read the docs listed under each section.

## Responsibilities
- Run the pan motor in a 1 kHz closed loop (encoder in, PWM out), independent of Jetson/ROS 2 timing.
- Take setpoints and heartbeat from the Jetson over CAN, report status back.
- Stream telemetry over UDP/Ethernet at 1 kHz (not safety-critical).
- Own safety: e-stop, heartbeat timeout, current limit, watchdog, fault state machine.

## Hardware interfaces
| Function | Peripheral | Pins | Notes |
|---|---|---|---|
| Control tick | TIM6 (basic timer), 1 kHz | - | TBD: PSC/ARR from APB1 timer clock |
| Motor PWM | TBD timer, 20 kHz | TBD | VNH5019 INA/INB/PWM |
| Encoder | TBD timer in encoder mode | TBD | goBILDA 5203, ~752 counts/output rev |
| Motor current | ADC | TBD | VNH5019 CS pin |
| CAN | FDCAN1, classic 2.0A, 1 Mbps | PD0 (RX), PD1 (TX) | external transceiver |
| Telemetry | ETH (RMII) | on-board PHY | |
| Console/log | USART3, 115200 8N1 | PD8 (TX), PD9 (RX) | ST-LINK virtual COM |
| E-stop input | GPIO/EXTI | TBD | |
| Status LEDs | GPIO | PB0, PE1, PB14 | LD1/LD2/LD3 |
| Scope/debug pin | GPIO | TBD | timing measurements |
| Watchdog | IWDG | - | |

Read: UM2407 (Nucleo pinout, solder bridges), DS13313 (pin alternate functions), RM0468 (TIM, FDCAN, ETH, ADC, IWDG chapters).

## Execution contexts
| Context | Type | Rate | Priority | Does | Talks to |
|---|---|---|---|---|---|
| control | TIM6 ISR | 1 kHz | TBD (above RTOS syscall limit) | encoder -> PID -> PWM, current check | TBD |
| supervisor | task | TBD | highest task | state machine, heartbeat timeout, control-loop liveness, kicks IWDG | all |
| can_comms | task | event | TBD | RX setpoint/heartbeat, TX status | supervisor, control |
| telemetry | task | 1 kHz batches | TBD | pack samples, send UDP | control |
| log | task | event | lowest | UART TX via DMA | everyone |
| estop | EXTI ISR | event | TBD | latch fault, PWM off | supervisor |

Open questions:
- TBD: how control shares data with tasks without FreeRTOS calls (it runs above the syscall priority)
- TBD: stack sizes

Read: PM0253 (exception model, NVIC priorities), FreeRTOS Cortex-M interrupt priority page.

## Interrupt priorities
| Priority | Who | Can call FreeRTOS? |
|---|---|---|
| TBD | control ISR, e-stop | no |
| TBD | configMAX_SYSCALL_INTERRUPT_PRIORITY | - |
| TBD | FDCAN, ETH, UART DMA | yes (FromISR) |

## State machine
BOOT -> IDLE -> ARMED -> FAULT

| From | To | Trigger |
|---|---|---|
| BOOT | IDLE | init done |
| IDLE | ARMED | TBD (arm command over CAN) |
| ARMED | IDLE | TBD |
| any | FAULT | e-stop, heartbeat timeout (<= 50 ms), current limit, control loop stalled, assert/HardFault |
| FAULT | IDLE | TBD (latched; how is it cleared?) |

In FAULT: PWM off, LD3 on, fault cause logged and reported over CAN.

## Memory
- Static allocation only (no heap).
- TBD: control ISR code in ITCM, its data in DTCM.
- TBD: Ethernet DMA buffers in a non-cacheable MPU region.

Read: AN4891 (bus matrix, TCM, where code/data should go), AN4839 (L1 cache), AN4838 (MPU), ES0491 (errata).

## Clocks and timebase
- HSE 8 MHz from the ST-LINK (bypass), SYSCLK 550 MHz. TBD: confirm in UM2407 and RM0468 RCC chapter.
- FreeRTOS owns SysTick; HAL timebase moves to a spare TIM.

## Code layout
```
firmware/
  lib/       portable code, host-tested (pid)
  tests/     Unity host tests
  nucleo/    CubeMX-generated BSP (Core/, Drivers/, .ioc) + app/
  scripts/
```
Generated code is only edited inside USER CODE blocks. main() calls app_main().

## Error handling
- TBD: which faults latch vs reset
- TBD: what gets logged on HardFault / stack overflow / assert
- Reset cause read from RCC_RSR at boot and logged.
