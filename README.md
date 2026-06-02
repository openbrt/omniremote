# OmniRemote

> One stick, every AC in the house — an open-source universal **air-conditioner** IR remote on M5StickS3.
>
> 一根棒控全家空调 — 基于 M5StickS3 的开源万能**空调**红外遥控固件。

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Flash from browser](https://img.shields.io/badge/flash-browser-147e63?logo=googlechrome&logoColor=white)](https://openbrt.github.io/omniremote/)

**🌐 Flash it from your browser:**
- 中文 → <https://openbrt.github.io/omniremote/>
- English → <https://openbrt.github.io/omniremote/install-en.html>

---

## What it is · 这是什么

OmniRemote turns a **single M5StickS3** into a universal AC IR remote that
controls every air conditioner in your home through one physical button.
83 AC protocol variants ship in firmware (Gree YAW1F / YBOFB / YX1FSF,
Midea + Coolix, Daikin, Mitsubishi, Hitachi, Haier, TCL, Hisense, Panasonic, …).

Walk into any room, press once, the AC there reacts. No phone app, no cloud
account, no WiFi.

把一根 **M5StickS3** 变成全家空调的万能遥控:固件里内置 83 个空调协议变体
(格力 YAW1F/YBOFB/YX1FSF、美的+Coolix、大金、三菱、日立、海尔、TCL、海信、
松下……)。走进哪间屋按一下,那间屋的空调响应。没有 App,不需要账号,
配完不联网。

> **Why AC-only?** We tried extending to TVs / fans / projectors via the
> Flipper-IRDB code library in v0.5 and found that without a working source
> remote to A/B against, hit rates on non-AC protocols are unacceptably low —
> bit-ordering conventions vary across IR libraries and protocol variants are
> hard to disambiguate. AC is the sweet spot because IRremoteESP8266 ships
> per-brand state-machine encoders that pack `(mode, temp, fan)` into the
> exact byte sequences each AC expects, **and** ACs emit an audible "beep"
> on accepting a command, which the StickS3's mic can hear — so brand
> discovery actually closes the loop. See [`feedback_irdb_send_is_hard`](https://github.com/openbrt/omniremote/issues) for the full post-mortem.

---

## How pairing works · 怎么配对

**One gesture**: long-press button A. The stick sweeps through enabled AC
protocols (1 try per ~800 ms), sending "Cool + 18 °C + Max fan + Turbo + Beep"
on each one to maximize how loudly the AC reacts. The device's built-in
microphone listens for the AC's beep — the moment a beep is heard, the scan
**auto-freezes** on the matching brand and that protocol becomes a saved
profile. Typical pairing time: a few seconds.

For ACs that don't beep, **B short = manual lock**: press it the instant you
see the panel light up or feel airflow.

**一个动作**: 长按 A。设备依次试启用的空调协议(每 ~800 ms 一条),广播
"制冷 + 18°C + Max 风 + Turbo + Beep" 让空调反应最响。机器内置麦克风
听到空调"嘀"声立刻**自动锁定**当前协议,存进 profile。通常几秒就配上。

不带 beep 的空调用 **B 短按手动锁**:看到面板亮 / 感到出风,立刻按 B。

---

## Daily use · 日常使用

- **A short press**: broadcast POWER toggle to every saved AC. The one
  in front of you reacts; the others (out of line-of-sight) ignore.
- **A long-hold**: pair a new AC (SCAN mode).
- **B short**: scroll menu (temp ± / mode / fan).
- **B long**: execute selected menu item and broadcast.
- **A + B held 3 s**: factory reset, wipe all profiles.

A short = 同时给所有已配空调发开关; A 长按 = SCAN 配新空调;
B 短按滚菜单, B 长按执行;A+B 同时按 3 秒 = 出厂复位。

---

## Trim the brand list in the browser · 浏览器筛品牌

If you only have, say, Gree + Midea at home, SCAN tries the other 70+
protocols for no reason. After flashing, open the [**configure page**](https://openbrt.github.io/omniremote/configure.html)
in desktop Chrome / Edge, click "Connect", check only the brands you actually
own. Web Serial pushes the bitmap straight into NVS — SCAN drops from ~80 s
to ~5 s. Fully offline.

只有 1-2 个空调品牌时,可以在[配置页](https://openbrt.github.io/omniremote/configure.html)
勾选,SCAN 只试这些,从 80 秒变 5 秒。Web Serial 直接推到 NVS,不联网。

---

## Hardware · 硬件

- **M5Stack StickS3** — about ¥150 / $20
  - ESP32-S3-PICO-1, 8 MB Flash + 8 MB OPI PSRAM
  - Built-in IR TX (GPIO 46) + IR RX (GPIO 42, unused in this build)
  - 1.14" LCD + 2 buttons + I²S microphone (ES8311) — mic is what enables
    auto-detect of AC beep
  - 250 mAh internal LiPo, USB-C charging

No external components needed.

---

## Build & flash (CLI) · 命令行烧录

For day-to-day use, [flash from the browser](https://openbrt.github.io/omniremote/).
The CLI path is for development:

```bash
cd firmware-idf-pure
pio run -t upload
pio device monitor
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
- [esp-web-tools](https://github.com/esphome/esp-web-tools) — browser flashing.
