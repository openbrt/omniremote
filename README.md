# OmniRemote

> One stick, every appliance — an open-source universal IR remote on M5StickS3.
>
> 一根棒控全家电器 — 基于 M5StickS3 的开源万能红外遥控固件。

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Flash from browser](https://img.shields.io/badge/flash-browser-2dd4bf?logo=googlechrome&logoColor=white)](https://openbrt.github.io/omniremote/)

**🌐 Flash from your browser → <https://openbrt.github.io/omniremote/>**

---

## What it is · 这是什么

OmniRemote turns a **single M5StickS3** into a universal IR remote that
controls every IR appliance in your home — ACs, TVs, fans, projectors,
set-top boxes — through one physical button.

Walk into any room, press once, the right device responds. No phone app,
no cloud account, no WiFi after setup.

把一根 **M5StickS3** 变成一个能控全家所有红外电器的万能遥控器,空调、电视、
风扇、投影、机顶盒 — 一个按钮搞定。走进哪间屋,按一下,对应的设备响应。
没有手机 App,不需要账号,配完之后不联网。

---

## How pairing works · 怎么配对

**One gesture for every device type**: long-press button A. The stick cycles
through known IR codes for that appliance type, one per ~800 ms. Watch / listen
to your device — the **moment it reacts, release**. The last-sent code is saved
as a profile.

对每种设备**只有一个动作**:长按 A 键。设备每 800ms 试一个红外码。一边看着
/ 听着你的电器,**它一有反应立刻松手**,刚才发出去的那条码就被保存。

| Device type | What "reaction" looks like                | Auto-detect |
|-------------|-------------------------------------------|-------------|
| **AC**      | "嘀" beep + airflow                       | ✅ mic detects beep, auto-freezes |
| TV          | Screen lights up                          | ❌ user releases manually |
| Fan         | Blades spin / power LED on                | ❌ user releases manually |
| Projector   | Lamp ignites                              | ❌ user releases manually |

For AC, the device's built-in microphone listens for the AC's beep and
**auto-freezes the scan on the matching brand** — usually under 10 seconds.
For everything else you release the button by hand when you see the device
respond.

空调有 mic 自动识别 "嘀" 声 (通常 10 秒内就找到);其他设备用眼睛看到反应
后手动松手。

---

## Daily use · 日常使用

- **A short press**: TOGGLE — broadcasts power on/off to every saved device.
  The one in front of you reacts; the others (out of line-of-sight) ignore.
- **A long-hold**: pair a new appliance (SCAN mode).
- **B short**: scroll menu.
- **B long**: execute selected menu item (temp ±, mode, fan speed, add device).
- **A + B held 3 s**: factory reset, wipe all profiles.

---

## Status · 现状

- ✅ V0.3 (current) — AC support with 75+ protocol variants (Gree all 3 model variants,
  Daikin / Mitsubishi / Hitachi / Haier / Midea / TCL / Hisense / Panasonic etc.)
  + mic auto-detect + manual B-short lock fallback
- 🚧 V1 (next) — **browser-based personalization**: pick the brands you actually
  have at home on a static GitHub Pages site, flash the firmware **straight from
  your browser** via [esp-web-tools](https://esphome.github.io/esp-web-tools/).
  No CLI, no compile.
- 🚧 V2 — TV / Fan / projector / set-top box code libraries, configured the same way.

---

## Hardware · 硬件

- **M5Stack StickS3** — about ¥150 / $20
  - ESP32-S3-PICO-1, 8 MB Flash + 8 MB OPI PSRAM
  - Built-in IR TX (GPIO 46) + IR RX (GPIO 42)
  - 1.14" LCD + 2 buttons + I²S microphone (ES8311)
  - 250 mAh internal LiPo, USB-C charging

No external components needed.

---

## Build & flash (CLI) · 命令行烧录

For V0.3, until the browser flasher ships:

```bash
cd firmware-ac-remote
pio run -e m5sticks3-ac -t upload
pio device monitor -e m5sticks3-ac
```

Restore the original CueKit firmware if you flashed over it:

```bash
esptool --port /dev/cu.usbmodem<XXX> write_flash 0x0 \
  backup_20260505_184432.bin
```

---

## License

[MIT](LICENSE). Use it, modify it, sell it. Pull requests welcome — especially
for new AC brand variants we don't cover yet.

---

## Built with

- [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266) — the AC
  protocol library that makes the whole thing possible.
- [M5Unified](https://github.com/m5stack/M5Unified) — board abstraction.
- (Coming) [esp-web-tools](https://github.com/esphome/esp-web-tools) — browser flashing.
