# Worklog

A rolling log for the project's work.

## 2026-11-17
Motor bring-up, parts 3 to 6:
- `motor.c`/`motor.h`: open-loop driver, `motor` CLI (percent duty, 1 s default and 5 s max run deadline), EN/DIAG fault interrupt on EXTI13 that latches until `motor clear`, supervisor FAULT state with a bit per fault source.
- Encoder sign: positive duty turned the shaft counterclockwise but counted down. Fixed with TIM4 CH1 polarity Falling in CubeMX. Positive = counterclockwise from the shaft end, which matches ROS REP 103 with the motor shaft-up.
- One output revolution by hand: 751 counts (751.8 expected).
- Deadband from rest: about +7% and -8% to -10%, varies with gear and commutator position. Plan for a friction feed-forward in the control loop.
- Full duty, Saleae on ENC A: up to speed in about 0.1 s (time constant roughly 25 ms), then flat at 2,760 counts/s = 220 RPM (rated 223). Supply held 12 V, about 480 mA peak on its display, never current limited.
- Stop coasted instead of braking: 0.4 s and about 500 counts (240° of output shaft) after the deadline. VNH5019 datasheet section 2.4: a 0 on PWM turns off both low sides, so INA = INB = 0 with PWM low leaves the motor floating. Brake to GND needs PWM held high. Now `bridge_brake()` (INA = INB = 0, CCR1 = ARR + 1) for stops and reversals, `bridge_coast()` (everything off) for faults so a faulted driver doesn't carry braking current.
- After the fix: stops in about 37 ms and about 30 counts (14°).
- Captures: `docs/results/2026-11-17-motor-encA-100-3000.csv` (coasting), `docs/results/2026-11-17-motor-encA-100-3000-brake.csv` (braking).

Saved for next time:
- Part 7: current sense during a run, fault interrupt test with the EN row jumpered to GND, stop latency on the Saleae
- Correct the motor lab: stop and reversal brake need PWM high
- Results file for the session in `docs/results/`
- `motor_tick()` in the control tick (see 2026-11-16)

## 2026-11-16
Motor bring-up, part 1 (CubeMX):
- TIM1 CH1 PWM on PE9: PSC 0, ARR 12999, so 20 kHz from the 260 MHz timer clock with 13,000 duty steps. CH1N off, the VNH5019 does its own bridge timing.
- TIM4 encoder on PD12/PD13 (AF2): TI1 and TI2 (x4), period 65535, pull-ups, input filter 6 on both channels. CubeMX sets the filter per channel; CH2 was still 0 after the first pass.
- INA/INB on PE11/PE14, push-pull, start low. EN/DIAG on PE13, open drain, starts released (high), no pull. `HAL_GPIO_ReadPin` reads IDR (the actual pin) even in output mode, so a driver fault is ODR 1 with IDR 0. Driving it low to disable also reads 0, so that isn't a fault.
- ADC3 IN9 on PF4: PLL2P 96 MHz / 8 = 12 MHz, 92.5-cycle sampling (~7.7 µs). ADC3 has its own sample time list (2.5 to 640.5 cycles, no 64.5). The 100 nF at the pin supplies the sampling charge, so 2.5 cycles would also work; 92.5 is margin. PLL2 is only used by the ADC, FDCAN stays on PLL1Q at 130 MHz.

Wiring (jumper hub):
- Out of 3.3 V pins: CN8-7 is the only 3V3 socket on the Zio headers. CN8-3 (IOREF) is also 3.3 V (tied to the MCU supply by SB10/SB11/SB20), and the morpho headers (CN11/CN12) aren't populated on this board.
- Built a jumper hub on a 24×18 proto board instead of loose jumpers. One column of pins jumpered to the Nucleo (12 jumpers, in CN8/CN9/CN10 order), one header per device on the other side: J1 CAN transceiver, J2 Pololu, J3 encoder. One signal per row, top-side 3V3 and GND buses, Saleae test pins on every signal.
- On the hub: ENA/ENB tied together (straight-through Pololu ribbon) and the CS filter, 1 kΩ in series + 100 nF to GND at the Nucleo side (~1.6 kHz corner, smooths the 20 kHz PWM ripple for the ADC).
- Two GND jumpers (CN9-23, CN10-5). One is enough electrically; the second covers a jumper working loose. Motor current never goes through the hub or the Nucleo, only supply to Pololu.
- CAN re-verified through the hub.
- Lab reference: VNH5019 datasheet (truth table, fault latch), [Pololu VNH5019 carrier](https://www.pololu.com/product/1451), ST UM2407 Zio tables 19-21.

Saved for next time:
- Motor bring-up part 2: wire the Pololu and encoder through the hub, meter checks before power
- Part 3: `motor.c`/`motor.h`, auto-stop, `motor` CLI commands, `motor_fault()` as ODR 1 and IDR 0
- Photos and a wiring table for the hub board in `hardware/`
- Bus-off recovery in the supervisor, rolling counter in protocol frames, FDCAN TX queue in priority order
- Direct GND wire between the Nucleo side and J17
- Move `motor_poll()` from the 10 ms supervisor loop into the 1 kHz control tick as `motor_tick()`: encoder extension, fault-line backup and the run deadline (counted in control ticks, since TIM6 at priority 4 can't make FreeRTOS calls). The tick becomes the only writer of the bridge and `position`; tasks post requests (`cmd_pending` set last with `__DMB()`, zero/stop flags) and never touch CCR1/INA/INB. Fault EXTI sets the latch first, then safes the bridge. `motor_current_ma()` stays in task context (ADC polling waits). Update the lab and the stubs to match.

## 2026-11-15
- CAN over USB-C: SN65HVD230 board and a USB-C breakout in a printed PETG housing ("CAN" embossed on the lid), and a USB-C cable carrying CAN-H, CAN-L and GND. Files in `hardware/cad/can-sn65hvd230-*`, photos `hardware/photos/can-usb-*`. Print profile on [MakerWorld](https://makerworld.com/en/models/3621895-sn65hvd230-can-enclosure-with-usb-c-panel-port#profileId-4152002). Continuity checked with the plug both ways up (same mapping).
- Load test over it: `can test 10000` with `can_echo.py` on the Jetson, 10,000/10,000 round trips, 0 gaps, 0 bad data. Nucleo TEC/REC 0; Jetson ERROR-ACTIVE, 0 bus errors, RX 10,000 frames. Details in `hardware/can-wiring.md`.

## 2026-11-14
- Bambu A1 printer, learning Fusion. First part: an enclosure for the Pololu VNH5019 carrier, ahead of motor bring-up.
- Shell with slotted vents on two sides, four holes for VIN/GND/OUTA/OUTB, a slot for the 10-pin logic ribbon, and screw posts with heat-set threaded inserts. Base plate with standoffs placed from Pololu's DXF of the board, plus windows to read the pin silkscreen from underneath.
- Printed in PETG: it holds up near the driver's heat, where PLA softens around 55-60 °C.
- Files in `hardware/cad/` (STEP for each part, Bambu 3MF), photos and CAD renders in `hardware/photos/`, public print profile on [MakerWorld](https://makerworld.com/en/models/3612437-vnh5019-enclosure#profileId-4140255).

## 2026-11-13
CAN fault injection (CANable's driver isn't loaded, so the Nucleo and Jetson are the only active nodes):

| Test | Nucleo | Jetson |
|---|---|---|
| Jetson `can0` down, Nucleo sends | TEC 128, error passive, stays there | (off the bus) |
| Nucleo TX (PD1) unplugged, Jetson sending | BUS OFF after one send; still receives | ERROR-PASSIVE, TEC 128, retransmits nonstop |
| TX plugged back in | stays BUS OFF until reset | recovers 1 per acknowledged frame |

- A node with nobody to ACK it climbs to error passive and stops at 128: an error-passive transmitter doesn't count ACK errors.
- TEC only comes down on successful transmissions. After test A the Nucleo sat at 127 while receiving 2,000 frames.
- Auto retransmission resends an unacknowledged frame back to back: ~435,620 bus errors in about a minute on the Jetson, and its TX queue filled behind the stuck frame (`cangen`: "No buffer space available").
- The Nucleo received those retransmissions as valid frames (rx 781 -> 2193 during the fault): once the sender is error passive, its error flag is recessive and doesn't cancel the frame for receivers. CAN can deliver duplicates, so the protocol frames get a rolling counter.
- The Nucleo stays bus off until FDCAN is restarted (on the M_CAN, bus off sets CCCR.INIT). Today that's a reset; the supervisor will handle it. The Jetson recovers by itself (`restart-ms 100`). Bringing `can0` down and up doesn't reset mttcan's error counters.
- A broken TX wire only shows from the other side: the Nucleo looked healthy (receiving, no TX errors) until it tried to send.
- `can` now prints the controller state with the warning level: `ok (error active)`, `ok, error count >= 96 (warning)`, `degraded (error passive)`, `BUS OFF (stopped, needs restart)`. The spec's "error active" means allowed to signal errors, not that an error is happening, so the label says ok.
- J17: pin 1 is TX, matching the carrier silkscreen and the carrier board spec. Wiring written up in `hardware/can-wiring.md`. CANable's `gs_usb` driver isn't in this JetPack kernel; it stays on the bus as the shared ground path for now.
- Raw output in `docs/results/2026-11-13-can-fault-injection.txt`. Ref: Bosch CAN Specification 2.0, fault confinement section.

Saved for next time:
- Bus-off recovery in the supervisor: detect `bus_off`, log, restart FDCAN with a backoff, FAULT if it repeats
- Rolling counter in the protocol frames (duplicates are real)
- FDCAN TX queue in priority order (Tx Queue mode) instead of FIFO, so a low-priority frame can't hold up the heartbeat
- Direct GND wire between the Nucleo side and J17 so the CANable isn't the only shared ground
- Photos of the CAN wiring in `hardware/photos/`
- Clock check, supervisor liveness check, `clear_fault` as a message, MAXN SUPER

## 2026-11-06
CAN bus between the Jetson and the Nucleo:
- Bus: Nucleo and Jetson at the two ends, each with an SN65HVD230 (3.3 V, 120 Ω on board). CANable in the middle with its termination off. 59.6 Ω across CAN-H/CAN-L with power off. The two sides share ground through the CANable's USB ground to the Jetson (Nucleo is grounded through the Mac's USB, the Jetson through its own supply).
- Jetson J17: soldered a 1×4 header, broken out of the case on jumpers. Bring-up is `tools/jetson/can_up.sh`: pinmux (two registers, values from NVIDIA's CAN guide, lost on every reboot, so the script reads them back), `can`/`can_raw`/`mttcan` modules, 1 Mbps, 80% sample point, `restart-ms 100`, bus error reporting.
- J17 pin order: the carrier board spec and NVIDIA's CAN guide disagree on which of pins 1/2 is TX. Checked on the bench instead: with the bus idle, the transceiver drives RX high; a J17 RX pin sitting low means the controller sees a busy bus and never starts sending (no errors either, the frame just sits in the queue).
- Nucleo pins on the CN9 Zio connector: PD1 (TX) pin 27, PD0 (RX) pin 25, GND pin 23, 3V3 on CN8 pin 7. Checked PD1 first in external loopback with the Saleae before wiring the transceiver: frames decode (id 0x7F0, seq + inverted seq), 5-bit run 4.99 µs, 6-bit run 5.99 µs, so 1 Mbps. Frame to frame ~127 µs (~7,900 frames/s), and the RX ISR turns around the next TX in ~1-4 µs.
- FDCAN now in Normal mode, SJW 5 (= seg2). Jetson side picks 50 tq at its 50 MHz clock, SJW 5, 80% sample point vs the Nucleo's 80.8%.

| Direction | Frames | Errors |
|---|---|---|
| Jetson -> Nucleo | 1,001 (1 + `cangen` burst of 1000) | Nucleo TEC/REC 0 |
| Nucleo -> Jetson | 1 (`can test 1`, id 0x7F0) | Jetson 0 errors, 0 dropped |

- Nucleo REC read 4 right after the Jetson rebooted (J17 glitches while the pins get reconfigured). It counts back down by 1 per good frame and was 0 after the burst.
- Saleae on the real bus: Jetson frames decode with ACK true. On the Nucleo's TX pin the only activity is one dominant bit in each frame's ACK slot, the Nucleo acknowledging. Screenshot `docs/results/2026-11-06-can-bus-decode-ack.png`.
- `can send <id> <hex>` CLI command (hex string like `cansend`, 0-8 bytes): Nucleo -> Jetson frames on demand.
- Round trip over the real bus: `tools/jetson/can_echo.py` echoes id 0x7F0 back, Nucleo runs `can test 10000`: tx 10000, rx 10000, 0 gaps, 0 bad data, TEC/REC 0.
- Timing from a Saleae capture of the whole run (20,000 frames), `tools/can_rtt.py`. Turnaround = gap after a frame minus the 11 bits the bus must stay idle:

| Turnaround | p50 | p99 | p99.9 | max |
|---|---|---|---|---|
| Nucleo (RX ISR -> next frame) | 1.9 µs | 2.0 µs | 2.9 µs | 3.0 µs |
| Jetson (driver -> kernel -> Python -> send) | 105 µs | 215 µs | 346 µs | 1794 µs |

- Round trip p50 350 µs, mean 355 µs (2,817/s). In external loopback (no Jetson) it was ~127 µs.
- The Nucleo's turnaround is flat to within ~1 µs. The Jetson's is ~55x slower at p50 and has a 17x spread from p50 to max: Linux scheduling, the network stack, and Python. That spread is why the 1 kHz loop and the heartbeat timeout live on the MCU; the Jetson sends setpoints and doesn't close the loop.
- Raw output in `docs/results/2026-11-06-can-link.txt`. Capture `2026-11-06-can-echo-10k.csv.gz` (gzipped, 13.6 MB raw) and its report.
- Also: SSH key login to the Jetson (`ssh jetson`).

Saved for next time:
- Photos: README hero shot, J17 breakout and bus wiring in `hardware/photos/`, Saleae decode in `docs/results/` (JPEG ~1600 px, GPS stripped)
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
