# Worklog

A rolling log for the project's work.

## 2026-11-03
- Mac toolchain: native Homebrew (`/opt/homebrew`), cmake, ninja, gcc-arm-embedded (Arm GNU Toolchain 15.3.Rel2).
- NUCLEO-H723ZG arrived. Not brought up yet. Going with C for firmware and classic STM32CubeMX (CubeMX2 only covers newer series like the C5).
- PID controller in `firmware/lib/pid.c/.h`: struct API (config + state), fixed sample period, derivative on measurement, integrator and output clamps, `pid_tune()` keeps the integral so gain changes don't bump the output.
- `pid_tune()` rejects NaN/inf in the config with `isfinite()`. NaN fails every comparison, so the min/max checks alone don't catch it.
- Host tests: CMake + Ninja + CTest, Unity v2.6.0 pulled with FetchContent. Unity has double asserts off by default, needed `UNITY_INCLUDE_DOUBLE`. Run with `./firmware/scripts/test.sh`.
- 7 tests pass: P only on first call, convergence on a first-order plant, NaN config rejected, reset, output limits, anti-windup, no derivative kick. ctest reports 1 test since it counts the test executable, not the Unity tests (`./build/test_pid` shows each one).

PID references:
- Brett Beauregard, [Improving the Beginner's PID](http://brettbeauregard.com/blog/2011/04/improving-the-beginners-pid-introduction/) (derivative kick, tuning changes, reset windup)
- Tim Wescott, [PID Without a PhD](https://www.wescottdesign.com/articles/pid/pidWithoutAPhd.pdf)
- Åström & Murray, [Feedback Systems](https://fbsbook.org), ch. 11 PID Control (free online)

Saved for next time:
- Nucleo bring-up: CubeMX project, blink, UART, 1 kHz timer ISR, measure jitter
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
