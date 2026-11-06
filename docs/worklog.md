# Worklog

A rolling log for the project's work.

## 2026-11-06
CAN bus between the Jetson and the Nucleo:
- Bus: Nucleo and Jetson at the two ends, each with an SN65HVD230 (3.3 V, 120 Ω on board). CANable in the middle with its termination off. 59.6 Ω across CAN-H/CAN-L with power off. GND wire between the Nucleo and Jetson sides (Nucleo is grounded through the Mac's USB, the Jetson through its own supply).
- Jetson J17: soldered a 1×4 header, broken out of the case on jumpers. Bring-up is `tools/jetson/can_up.sh`: pinmux (two registers, values from NVIDIA's CAN guide, lost on every reboot, so the script reads them back), `can`/`can_raw`/`mttcan` modules, 1 Mbps, 80% sample point, `restart-ms 100`, bus error reporting.
- J17 pin order: the carrier board spec and NVIDIA's CAN guide disagree on which of pins 1/2 is TX. Checked on the bench instead: with the bus idle, the transceiver drives RX high; a J17 RX pin sitting low means the controller sees a busy bus and never starts sending (no errors either, the frame just sits in the queue).
- Nucleo pins on the CN9 Zio connector: PD1 (TX) pin 27, PD0 (RX) pin 25, GND pin 23, 3V3 on CN8 pin 7. Checked PD1 first in external loopback with the Saleae before wiring the transceiver: frames decode (id 0x7F0, seq + inverted seq), 5-bit run 4.99 µs, 6-bit run 5.99 µs, so 1 Mbps. Frame to frame ~127 µs (~7,900 frames/s), and the RX ISR turns around the next TX in ~1-4 µs.
- FDCAN now in Normal mode, SJW 5 (= seg2). Jetson side picks 50 tq at its 50 MHz clock, SJW 5, 80% sample point vs the Nucleo's 80.8%.

| Direction | Frames | Errors |
|---|---|---|
| Jetson -> Nucleo | 1,001 (1 + `cangen` burst of 1000) | Nucleo TEC/REC 0 |
| Nucleo -> Jetson | 1 (`can test 1`, id 0x7F0) | Jetson 0 errors, 0 dropped |

- Nucleo REC read 4 right after the Jetson rebooted (J17 glitches while the pins get reconfigured). It counts back down by 1 per good frame and was 0 after the burst.
- Raw output in `docs/results/2026-11-06-can-link.txt`.
- Also: SSH key login to the Jetson (`ssh jetson`).

Saved for next time:
- Saleae on the real bus: ACK slot should read true now (it can't in loopback)
- `can send <id> <hex>` CLI command
- `tools/can_echo.py` on the Jetson, then `can test 10000` over the real bus
- Fault injection: pull CAN-H, watch error passive / bus off on both sides. Nucleo needs software to recover from bus off (`restart-ms` covers the Jetson)
- `hardware/can-wiring.md`
- Clock check, supervisor liveness check, `clear_fault` as a message, MAXN SUPER

## 2026-11-05
- Control tick running: TIM6 at 1 kHz in `app/control.c`, callback registered with `HAL_TIM_RegisterCallback()` (CubeMX: TIM register callbacks on) so `main.c` stays untouched. Register before `HAL_TIM_Base_Start_IT()`: HAL only takes callbacks while the timer is READY.
- Supervisor starts it after `task_sync()`. Scope pin PG14 toggles every tick.
- Period measured in the ISR with the DWT cycle counter (520,000 cycles = 1 ms). ISR is the only writer; `jitter clear` just sets a flag the ISR acts on.
- CLI `jitter [clear]`: min/max period in cycles, deviation from 1 ms in ns, and when the min/max happened (ticks since start, in s). The "when" is what made the spikes traceable.
- Boot always delayed one tick by the same amount (±1.15 µs, identical every boot): tasks finishing init plus the log burst. Supervisor now restarts the stats 1 s after the tick starts so `jitter` shows steady state.
- Sync log order fixed: tasks log "waiting" before checking in, otherwise the higher-priority init task prints "all initialized" first.

Jitter, DWT min/max over ~30-60 s windows after warm-up:

| Config | Idle | 3 LED fades + debug logging |
|---|---|---|
| I-cache off, TIM6 prio 5 | ±96 ns (spikes to ±6 µs while typing) | ±7.8 µs |
| I-cache on, TIM6 prio 5 | ±153 ns | ±946 ns |
| I-cache on, TIM6 prio 4 | ±153 ns | ±153 ns |

- I-cache on: worst case under load ±7.8 µs -> ±0.95 µs. Without it the code runs straight from flash with wait states.
- With the cache on, the remaining ±946 ns spike lined up with an `led` command ("max at 21.869 s" next to `[21.871] D led: led 1 fade`). Handling it enters kernel critical sections (timer commands, queue copies), and FreeRTOS masks priorities 5-15 with BASEPRI during those.
- TIM6 at priority 4 (unticked "Uses FreeRTOS functions" in CubeMX NVIC): above the mask, so load no longer matters. Cost: no FreeRTOS calls of any kind in that ISR. Comment at the ISR says so.
- ±153 ns (~80 cycles) left is interrupt entry variation. Keeping I-cache on, TIM6 at 4. D-cache still off.
- Caveat: min/max over short windows is noisy. Still need a long run and Saleae p99.

Saleae (Logic 8, 100 MS/s, D2/PG14 + GND, jumper wire, no soldering), 90 s each, I-cache on, TIM6 prio 4:

| Capture | Periods | Anomalies | Rate | std | p99 \|dev\| | p99.9 | max |
|---|---|---|---|---|---|---|---|
| idle | 90,224 | 0 | +2398 ppm | 444 ns | 1112 ns | 1408 ns | 1822 ns |
| 3 LED fades + debug logging | 90,337 | 0 | +2895 ppm | 443 ns | 1115 ns | 1425 ns | 1825 ns |

- p99 1.1 µs, under the 2 µs target, idle and loaded. No missed ticks.
- `tools/jitter.py`: jitter is measured against the mean period, not 1 ms, so a rate offset doesn't count as jitter. `-d docs/results` writes `<name>.report.txt` next to each CSV plus a summary table. Raw CSVs and reports in `docs/results/`.
- Doesn't line up with the DWT yet: Saleae shows a flat ±1.1 µs spread no matter the load (DWT says ±153 ns), and the rate moves between captures (+783 ppm in a quick look earlier, then +2398, +2895). TIM6 is hardware-driven, so its rate can't change with load.
- Best guess: the board clock. HSE is the ST-LINK's MCO output (bypass), not a crystal. The DWT counts that same clock, so it can't see its own source wandering; the Saleae can. Not confirmed. Parked for later.

FDCAN loopback:
- FDCAN1 in CubeMX: classic CAN, internal loopback (TX wired to RX inside the chip, no pins or transceiver needed). Kernel clock 130 MHz from PLL1Q. 1 Mbps = prescaler 5, seg1 20, seg2 5 (26 time quanta, 80.8% sample point). Classic over FD: 8 bytes fits a setpoint plus a counter, and every transceiver and USB-CAN adapter speaks it.
- Pins moved to PD0/PD1 (the Nucleo's CAN pins on the Zio connector). CubeMX put them on PA11/PA12 first, which is the user USB port.
- Message RAM: 1 standard filter, RX FIFO0 16 deep, TX FIFO 16 deep, 8-byte elements. Filter is a mask of 0 (accept every standard id into FIFO0); the global filter rejects everything else and remote frames.
- `app/can.c`: start, send, stats, loopback self-test. RX callback registered before `HAL_FDCAN_Start()`, same rule as TIM6. FDCAN1 interrupt at priority 6, so it can use FromISR calls; TIM6 stays at 4 above it.
- RX ISR drains FIFO0 in a loop: the interrupt fires per new frame, not per frame waiting, so reading one per interrupt would strand the rest. Bounded by the FIFO depth. For real traffic in milestone 2 this moves to a task (ISR notifies, task drains and publishes).
- Self-test is ping-pong: one frame in flight, each RX queues the next TX. Payload is the sequence number plus its bitwise NOT, so the ISR counts lost frames (seq jumps) and corrupted ones (halves don't match). The CAN CRC already covers the wire; this catches my own RX-path bugs.
- `can_get_stats()` copies the counters inside a critical section so the CLI never sees a half-updated set; TEC/REC and error passive/bus off come straight from the controller.
- CLI `can [test <n>|clear]`.

| Run | TX | RX | Lost | Corrupted | TX FIFO full | TEC / REC |
|---|---|---|---|---|---|---|
| 10k frames | 10,000 | 10,000 | 0 | 0 | 0 | 0 / 0 |
| 1M frames | 1,000,000 | 1,000,000 | 0 | 0 | 0 | 0 / 0 |

- Milestone 1 CAN item done (target was 10k frames, 0 errors). Raw output in `docs/results/2026-11-05-fdcan-loopback.txt`.
- Read: ST [AN5348](https://www.st.com/resource/en/application_note/dm00625700-fdcan-peripheral-on-stm32-devices-stmicroelectronics.pdf) (FDCAN on STM32), RM0468 FDCAN chapter, TI [SLOA101](https://www.ti.com/lit/SLOA101) (CAN basics).

CI:
- GitHub Actions (`.github/workflows/ci.yml`), two jobs on ubuntu-24.04: host tests (`test.sh`), and the firmware build (`build_nucleo.sh Debug`) with Arm GNU Toolchain 15.3.Rel2, same as the Mac.
- Checkout needs `fetch-depth: 0`: the version string comes from `git describe`, which needs history and tags. The default shallow clone only gets a hash.
- Each run uploads `nucleo.elf` + `nucleo.map` and logs the size, so any commit has a flashable binary and flash/RAM use can be compared over time.
- Both jobs green. That closes milestone 1.

Saved for next time:
- Clock check: `uptime` vs Mac clock over 30-60 min (+2400 ppm = ~4.3 s per 30 min). Then LSE (32.768 kHz crystal) out on MCO1/PA8, capture with the Saleae: if that reads clean, it's the HSE, not the Saleae.
- Liveness check in the supervisor (tick count must advance ~10 per 10 ms, else FAULT)
- `clear_fault` as a message
- Check for MAXN SUPER

## 2026-11-04
- Tagged v0.1.0. `version` shows `0.1.0 (v0.1.0) Debug`.
- Baseline before adding tasks: heap 38 KB free of 48 KB (35.6 KB min ever). Stack free: cli 580 words, log 416, IDLE 90.
- VS Code: `Cmd+Shift+B` build, `F6` flash, `Cmd+Shift+T` host tests, `F5` debug. Debugging with Cortex-Debug + ST's ST-LINK GDB server, stops at `main()`, peripheral registers from the H723 SVD. Went with Cortex-Debug over the STM32Cube debugger since the STM32Cube one wouldn't detect a project in a repo subfolder.
- `tasks.json` and `launch.json` are tracked now. Paths use `${env:HOME}`, tool versions are in the README.
- Scope pin on PG14 (Arduino D2) for timing measurements.
- Supervisor task: BOOT/IDLE/ARMED/FAULT, 10 ms loop with `vTaskDelayUntil()`. FAULT latches. `state` and `clear_fault` in the CLI. Subscribes to `EVENT_ERROR`, which puts it in FAULT.
- Messaging: pub/sub. Tasks post to one global queue, a dispatcher task (highest priority) copies each message to every task subscribed to that event. Each subscriber has its own queue. Drops are counted, `msgs` shows them. Event ids in `app/events.h`.
- LED task owns LD1 green, LD2 amber, LD3 red. Modes: on/off/blink/fade, driven by FreeRTOS software timers. Fade is software PWM from a 1 ms timer (10 steps, 100 Hz), only runs while something is fading. LD2 (PE1) has no timer output, so no hardware PWM on all three. Red is for the supervisor.
- Fade curve in `firmware/lib/led_fade.c` (squared so it looks even) with 6 host tests. 30 host tests total.
- Startup sync: each task runs its own `initialize()` (timers, subscriptions), then waits in `task_sync()`. Init task releases everyone once all have checked in. Nothing posts or runs its loop before every task has subscribed. Log task doesn't sync so it can print during init.
- CLI: `loglevel` renamed to `log_level`, `led <1-3> <on|off|blink|fade> [ms]` goes through the LED task, added `msgs`, `state`, `clear_fault`.

Saved for next time:
- Check bus, LED task, and sync on the board (boot log order, `msgs` shows 0 unrouted, fade)
- `clear_fault` from the CLI should post a message to the supervisor instead of changing its state directly
- Lab 5: start TIM6 control tick, liveness check, `jitter` command, Saleae captures idle + loaded
- Architecture doc: message bus, task sync, LED ownership
- SSH key login
- Check for MAXN SUPER

## 2026-11-03
- Mac toolchain: native Homebrew (`/opt/homebrew`), cmake, ninja, gcc-arm-embedded (Arm GNU Toolchain 15.3.Rel2).
- NUCLEO-H723ZG arrived. Going with C for firmware and classic STM32CubeMX (CubeMX2 only covers newer series like the C5).
- PID controller in `firmware/lib/pid.c/.h`: struct API (config + state), fixed sample period, derivative on measurement, integrator and output clamps, `pid_tune()` keeps the integral so gain changes don't bump the output.
- `pid_tune()` rejects NaN/inf in the config with `isfinite()`. NaN fails every comparison, so the min/max checks alone don't catch it.
- Host tests: CMake + Ninja + CTest, Unity v2.6.0 pulled with FetchContent. Unity has double asserts off by default, needed `UNITY_INCLUDE_DOUBLE`. Run with `./firmware/scripts/test.sh`.
- 7 tests pass: P only on first call, convergence on a first-order plant, NaN config rejected, reset, output limits, anti-windup, no derivative kick. ctest reports 1 test since it counts the test executable, not the Unity tests (`./build/test_pid` shows each one).

Nucleo bring-up:
- ST's GPIO blink demo from [STM32CubeH7](https://github.com/STMicroelectronics/STM32CubeH7) (`Projects/NUCLEO-H723ZG`) built and flashed from STM32CubeIDE. Board, ST-LINK, and toolchain confirmed.
- Generated `firmware/nucleo/` with CubeMX: CMake toolchain, FreeRTOS (CMSIS_V2), LD1/LD2/LD3 on PB0/PE1/PB14.
- Clocks: 8 MHz HSE bypass from the ST-LINK -> PLL1 -> 520 MHz SYSCLK, VOS0. Same 520 MHz as ST's FreeRTOS example. AHB 260 MHz, APB 130 MHz, APB1 timers 260 MHz.
- HAL timebase on TIM17 since FreeRTOS owns SysTick.
- TIM6 for the 1 kHz control tick: PSC 259, ARR 999 (1 MHz counter, 1 µs per count), ARR preload on, IRQ priority 5 (highest priority that can still call FreeRTOS FromISR APIs).
- Builds and flashes from the command line: `./firmware/scripts/build_nucleo.sh` and `./firmware/scripts/flash.sh` (STM32_Programmer_CLI over SWD).
- LD1 blinking at 1 Hz from a FreeRTOS task. Scheduler, clocks, and GPIO working.

Firmware foundation (logging, tasks, CLI):
- Reordered: logging and a CLI before the control tick and jitter capture, so testing and iterating is faster
- Task table in `app/task_init.c`. CubeMX's default task is repurposed as `init`: it sets up logging, creates every task in the table, then deletes itself. Dynamic allocation (heap_4, 48 KB), but only at boot.
- FreeRTOS: newlib reentrant on, stack overflow check 2 (hook turns LD3 on and halts)
- Logging (`app/log.h`): `LOG_E/W/I/D` with a per-file tag, `[   12.345] I cli: msg`. Formats into a fixed 128-byte message and queues it without blocking; full queue drops and counts. The log task is the only owner of the sinks: UART now, flash and UDP later. Global level filter before formatting, per-sink level after.
- printf retargeted to USART3, only for boot and fault messages.
- CLI: parser and line editing in `firmware/lib/cli.c` (no HAL/RTOS, host-tested), board commands in `app/cli_cmds.c`. USART3 RX interrupt (priority 6) -> stream buffer -> CLI task. Output goes through the log task so only one thing writes the UART.
- CLI commands: `?` (alphabetical), `heap`, `led`, `loglevel`, `reset`, `tasks`, `uptime`, `version`. Up/down arrows recall the last 2 lines.
- 17 host tests for the CLI (line editing, CRLF, escape sequences, arg limits, help order, history). PID tests still 7/7.
- Versioning: semver in `project(... VERSION 0.1.0)`, plus `git describe --tags --always --dirty`, written to a generated `version.h` in the build folder. Shown in the boot banner and `version`.

PID references:
- Brett Beauregard, [Improving the Beginner's PID](http://brettbeauregard.com/blog/2011/04/improving-the-beginners-pid-introduction/) (derivative kick, tuning changes, reset windup)
- Tim Wescott, [PID Without a PhD](https://www.wescottdesign.com/articles/pid/pidWithoutAPhd.pdf)
- Åström & Murray, [Feedback Systems](https://fbsbook.org), ch. 11 PID Control (free online)

Saved for next time:
- Tag v0.1.0
- Supervisor task (state machine skeleton, status LED)
- Start TIM6, scope pin on PG14 + jitter capture (idle and loaded)
- Fill in the TBDs in `docs/firmware-architecture.md`
- SSH key login
- Check for MAXN SUPER

## 2026-11-01
- Installed the [Waveshare aluminum case with camera holder](https://www.waveshare.com/product/ai/accessories/cases/jetson-orin-case-a.htm) following Waveshare's install instructions. Power LED and power button wired to the outside of the case.
- Issue: IMX219 not detected at first -> CSI port wasn't configured. Fixed with `sudo /opt/nvidia/jetson-io/jetson-io.py`, set to IMX219-A on CSI-0.
- First camera capture with `nvgstcapture-1.0 --orientation 2` ("j" to capture to Documents, "q" to exit). Orientation 2 flips the image 180° since the case mounts the camera upside down.
- 720p60 check with nvarguscamerasrc: 59.77 fps average over ~68 s, 0 dropped. Week 1 camera gate passed. Raw output in `docs/results/2026-11-01-imx219-720p60-fps.txt`
- Measured frame period is ~16.73 ms, not the nominal 16.67 ms. Use the measured value for the latency budget.
- Staying on JetPack 7.2.1 since the IMX219 works. That means ROS 2 Jazzy instead of Humble.
- Headless SSH over Wi-Fi works, including right after boot with no login.
- USB serial console works from the Mac (`screen /dev/cu.usbmodem* 115200`). SSH over USB-C doesn't: the Mac sees the Jetson's USB network interface but it stays inactive. Deferred, Wi-Fi SSH is enough for now.
- Docker 29.1.3 is already installed on JetPack 7.2.1. Not using it yet.

Saved for next time:
- SSH key login
- Check for MAXN SUPER

## 2026-10-30
- Installed: JetPack 7.2.1 (R39.2.1) via USB ISO to SSD. Firmware was 36.4.7 and got updated by the installer.
- Mistake: flashed the original Jetson Nano image (JetPack 4.6.1) first via micro SD. It doesn’t boot the Orin Nano. NVIDIA’s download page no longer lists an up-to-date Orin Nano SD image; check the latest instructions from the NVIDIA quick start guide for Orin Nano
- Issue: no display during boot -> the DP cable was bad. A mini-DP cable fixed it.
- Board confirmed alive via force recovery (it showed up as the NVIDIA APX device over USB-C); learned how to enter force recovery mode via jumpers (J14 pin 10 (FC REC) jumped to pin 7 (GND), then the Mac showed NVIDIA APX)
- Power mode: 25 W by default. MAXN SUPER not yet checked.
- Installed v4l-utils
- C270 USB webcam: detected, about 15 fps average at night. Likely low-light auto-exposure or the default video format; not yet investigated.
- Full upgrade on 348 packages

Saved for next time:
- Headless SSH (enable SSH, key login, test with the monitor unplugged)
- Check for MAXN SUPER
- Once CSI camera arrives: IMX219 camera test, which determines if 7.2.1 is viable for the project
