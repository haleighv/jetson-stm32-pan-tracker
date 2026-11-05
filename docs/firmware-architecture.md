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
| Status LEDs | GPIO | PB0, PE1, PB14 | LD1 green, LD2 amber, LD3 red. Only the LED task drives them. Red is for the supervisor. |
| Scope/debug pin | GPIO | PG14 (Arduino D2) | timing measurements |
| Watchdog | IWDG | - | |

Read: UM2407 (Nucleo pinout, solder bridges), DS13313 (pin alternate functions), RM0468 (TIM, FDCAN, ETH, ADC, IWDG chapters).

## Execution contexts
Ordered most urgent first. Safety beats comms beats observability beats debug: if the CPU gets busy, CLI and logs suffer first, heartbeat and faults last, and the control ISR never waits on a task.

| Context | Type | Rate | Priority | Does | Talks to |
|---|---|---|---|---|---|
| control | TIM6 ISR | 1 kHz | TBD (moves above the syscall limit later) | encoder -> PID -> PWM, current check, push telemetry sample | supervisor, telemetry (lock-free) |
| estop | EXTI ISR | event | TBD | PWM off, latch fault | supervisor |
| msg | task (dispatcher) | event | SUMMIT | copy each posted message to its subscribers | everyone (bus) |
| supervisor | task | ~100 Hz | HIGH | startup order, state machine, heartbeat timeout, control-loop liveness, IWDG | all (bus), led |
| host_comms | task | event (CAN RX) + periodic status TX | NORMAL | setpoints + heartbeat from Jetson, status back | supervisor, control |
| telemetry | task | batches of 1 kHz samples | NORMAL | pack samples, send UDP | control |
| Tmr Svc | FreeRTOS timer task | event | 2 (same as NORMAL) | runs software timer callbacks (LED blink/fade) | led |
| led | task | event | LOW | only owner of LD1-LD3: on/off/blink/fade | bus |
| cli | task | event (UART RX) | LOW | debug commands | log, bus |
| log | task | event | LOW | only owner of log sinks (UART TX today) | everyone |
| init | task, runs once | boot | default | create tasks, wait at the sync point, release them, delete itself | - |

Status: init, log, cli, msg, led, supervisor running. control is configured (TIM6), not started. Others are placeholders.

### Task creation
- One table in `app/task_init.c` (function, name, stack bytes, param, priority, handle, sync). Priorities come from `task_priority_t` in `task_init.h` (LOW/NORMAL/HIGH/SUMMIT).
- CubeMX's default task is repurposed as `init`. It sets up logging, the message bus, and the sync point, creates the table, waits for every task to sync, releases them, then `vTaskDelete(NULL)`. Init runs after the scheduler starts, so it's allowed to block.
- Every task follows the same shape: a local `initialize()` (create timers/buffers, subscribe to its events), then `task_sync()`, then its loop.
- Startup sync (`app/task_utilities.c`): each task gives a counting "ready" semaphore and waits on a gate. Init opens the gate once all have checked in; it passes from task to task, highest priority first. No loop runs and nothing is posted before every task has subscribed. Init logs a warning every second if a task never checks in.
- The log task doesn't sync (`sync = false`), so it can print while the others initialize.
- Dynamic allocation (heap_4, 48 KB), but only during init. Nothing allocates after boot, so heap use is fixed and checkable with `xPortGetFreeHeapSize()`.
- Task names stay under 16 chars (`configMAX_TASK_NAME_LEN`).

Open questions:
- TBD: how control shares data with tasks without FreeRTOS calls (once it runs above the syscall priority)
- TBD: stack sizes, measured with the CLI `tasks` command (stack high-water marks)
- TBD: does the supervisor create host_comms/telemetry after hardware init, instead of init creating everything

Read: PM0253 (exception model, NVIC priorities), FreeRTOS Cortex-M interrupt priority page.

## Logging
- API in `app/log.h`: `LOG_E/W/I/D(...)` with a per-file `LOG_TAG`. Output: `[   12.345] I cli: message`.
- `log_write()` formats into a fixed 128-byte message and queues it by value. It never blocks: a full queue drops the message and bumps a counter, and the log task prints `dropped N` once.
- Task context only (asserts `!xPortIsInsideInterrupt()`). ISRs set flags or counters that a task logs.
- Two filters: a global level checked before formatting (cheap), then a per-sink `min_level`.
- `LOG_COMPILE_LEVEL` strips levels at compile time (e.g. no DEBUG strings in Release).

### Sinks
Only the log task calls sinks, one message at a time, so sinks need no locking and a slow sink delays logs, never the caller.

| Sink | Status | min_level | Notes |
|---|---|---|---|
| uart | done | DEBUG | USART3 blocking transmit, 50 ms timeout |
| flash | planned | WARN | RAM buffer, programmed in 32-byte flash words. Records get seq/timestamp/len/CRC for power-loss recovery. |
| udp | planned | TBD | `init()` fails until the Ethernet link is up, batches into datagrams for the Jetson |

Rules:
- One owner of UART TX: the log task. `printf` (`retarget.c`) only before the scheduler starts or in fault handlers.
- No flash erase while ARMED. The H723 has a single flash bank, so an erase stalls instruction fetch, which would break control timing and the 50 ms heartbeat guarantee. Erase only in IDLE, or run control and flash code from ITCM.
- Flash sink stays WARN and above (~10k erase cycles per sector).
- First step before flash: a `.noinit` RAM ring buffer that survives warm resets, dumped with a CLI `log dump` after a fault.

## Messaging
- Pub/sub (`app/msg.c/.h`). Anyone posts to one global queue (32 deep): `msg_post()` from tasks, `msg_post_from_isr()` from ISRs. Never blocks unless the caller asks.
- The `msg` dispatcher task copies each message to every task subscribed to its event id. Each subscriber has its own queue (`msg_subscriber_create()` + `msg_subscribe()` in its `initialize()`), since a queue can only have one reader.
- Messages are 12 bytes: event id (`app/events.h`) plus a small payload union. Copied by value.
- Full queues drop and count: bus full, unrouted (no subscriber), and per subscriber. CLI `msgs` shows them.
- Rule: the 1 kHz control path stays off the bus (extra hop and copy). Control shares data through variables.

## Interrupt priorities
| Priority | Who | Can call FreeRTOS? |
|---|---|---|
| TBD | control ISR, e-stop | no |
| TBD | configMAX_SYSCALL_INTERRUPT_PRIORITY | - |
| TBD | FDCAN, ETH, USART3 RX | yes (FromISR) |

Current:
- TIM6 at 5, the highest priority that can still call FreeRTOS FromISR APIs (`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` = 5).
- USART3 at 6, below TIM6, so typing can't delay the control tick.

## State machine
BOOT -> IDLE -> ARMED -> FAULT

| From | To | Trigger |
|---|---|---|
| BOOT | IDLE | init done |
| IDLE | ARMED | TBD (arm command over CAN) |
| ARMED | IDLE | TBD |
| any | FAULT | `EVENT_ERROR` on the bus (done); e-stop, heartbeat timeout (<= 50 ms), current limit, control loop stalled, assert/HardFault (planned) |
| FAULT | IDLE | `clear_fault` from the CLI. Latched: never clears on its own. TODO: send it as a message so only the supervisor changes state. |

In FAULT: PWM off, red LED (LD3) via the LED task, fault cause logged and reported over CAN.

## Memory
- FreeRTOS heap_4, 48 KB. Allocate only during init, never after boot.
- Log queue: 32 x ~130 bytes ≈ 4 KB.
- TBD: control ISR code in ITCM, its data in DTCM.
- TBD: Ethernet DMA buffers in a non-cacheable MPU region.

Read: AN4891 (bus matrix, TCM, where code/data should go), AN4839 (L1 cache), AN4838 (MPU), ES0491 (errata).

## Clocks and timebase
- HSE 8 MHz from the ST-LINK (bypass) -> PLL1 -> SYSCLK 520 MHz, VOS0. Same 520 MHz as ST's FreeRTOS example.
- AHB 260 MHz, APB 130 MHz, APB1 timer clock 260 MHz.
- FreeRTOS owns SysTick (1 kHz tick). HAL timebase on TIM17.
- TIM6: PSC 259, ARR 999 -> 1 MHz counter (1 µs per count), 1 kHz update.

## Code layout
```
firmware/
  lib/       portable code, host-tested (pid)
  tests/     Unity host tests
  nucleo/    CubeMX-generated BSP (Core/, Drivers/, Middlewares/, .ioc) + app/
    app/     my board code (HAL + FreeRTOS), flat: .h next to .c
  scripts/   build.sh/test.sh (host), build_nucleo.sh/flash.sh (target)
```
- Pure logic with no HAL or FreeRTOS (PID, CLI parser) goes in `lib/` with host tests.
- Generated code is only edited inside USER CODE blocks. The init task's body calls `task_init_run()`.
- File name = function prefix (`task_cli.c` -> `task_cli()`).

## Error handling
- TBD: which faults latch vs reset
- TBD: what gets logged on HardFault / stack overflow / assert
- Reset cause read from RCC_RSR at boot and logged.
