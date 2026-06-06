# OmniRemote

English | [中文](README.zh-CN.md)

> One StickS3, every AC in the house.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Flash from browser](https://img.shields.io/badge/flash-browser-147e63?logo=googlechrome&logoColor=white)](https://openbrt.github.io/omniremote/install-en.html)

![OmniRemote overview](docs/assets/readme-hero.svg)

## What It Is

OmniRemote is open-source firmware that turns a **M5Stack StickS3** into a universal infrared remote for home air conditioners. Pair each AC once, then walk room to room and control the AC in front of you with one physical device. No phone app, cloud account, or WiFi is required after setup.

| Topic | Details |
| --- | --- |
| Hardware | M5Stack StickS3 with built-in IR transmitter, screen, two buttons, and microphone |
| Coverage | 83 built-in AC protocol variants, including Gree, Midea, Haier, Daikin, Mitsubishi, Hitachi, TCL, Hisense, Panasonic, and more |
| Pairing | Hold A to enter SCAN; the stick tests protocols one by one and locks when the AC beeps |
| Daily use | Short-press A to broadcast POWER toggle to all saved AC profiles; only the AC in line of sight reacts |
| Connectivity | Browser flashing and configuration over USB; the device itself stays offline |

## Quick Start

| Step | Action | Result |
| --- | --- | --- |
| 1. Flash | Open the [English installer](https://openbrt.github.io/omniremote/install-en.html) or [Chinese installer](https://openbrt.github.io/omniremote/) in desktop Chrome / Edge, connect the StickS3, then flash the firmware | OmniRemote is installed on the StickS3 |
| 2. Pair | Point the stick at an AC, hold the front A button, then release when you hear a beep or see the AC panel react | The matching protocol is saved as an AC profile |
| 3. Use | Short-press A to toggle power; short-press B to scroll the menu; long-press B to run the selected temperature, mode, or fan command | One offline remote controls every paired AC |

![Pairing flow](docs/assets/pairing-flow.svg)

## Button Map

![StickS3 button map](docs/assets/buttons.svg)

| Action | Function |
| --- | --- |
| A short press | Broadcast POWER toggle to every saved AC profile |
| A long press | Enter SCAN and pair a new AC |
| B short press | Scroll the HOME menu: temperature, mode, fan, saved ACs |
| B long press | Run the selected menu item and broadcast it |
| B short press during SCAN | Manually lock the current protocol for ACs that do not beep |
| A + B held for 3 seconds | Factory reset and clear all profiles |

## Speed Up SCAN

By default, SCAN walks through all 83 built-in protocols, which can take about 80 to 90 seconds end to end. If you only own one or two AC brands, flash the firmware first, then open the [configure page](https://openbrt.github.io/omniremote/configure-en.html) and select the brands you actually have. Web Serial writes the brand bitmap to NVS, and future SCAN runs only test those protocols.

Configuration is fully offline: the computer talks to the StickS3 over USB serial, and the StickS3 never joins a network.

## Why AC Only

Air-conditioner IR commands are not simple button codes. Each command packs `mode / temp / fan / power` into a brand-specific state frame. OmniRemote builds on [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266), which provides AC state-machine encoders that generate the full frames each brand expects.

TV, fan, and projector protocols are much harder to discover reliably without the original remote for A/B testing. Bit ordering, protocol variants, and model differences can be ambiguous across IR libraries. ACs are a better fit for automatic discovery because many units emit a beep after accepting a valid command, and the StickS3 microphone can close the loop: send a frame, hear the beep, save the protocol.

## Hardware

- M5Stack StickS3
- ESP32-S3-PICO-1, 8 MB Flash + 8 MB OPI PSRAM
- Built-in IR TX on GPIO 46
- Built-in IR RX on GPIO 42, currently unused
- 1.14" LCD, A/B buttons, ES8311 I2S microphone
- 250 mAh internal battery, USB-C charging and flashing

No external IR LED, wires, or modules are required.

## Developer Build

Most users should use the [web installer](https://openbrt.github.io/omniremote/install-en.html). For development:

```bash
cd firmware-idf-pure
pio run -t upload
pio device monitor
```

When re-flashing from the browser, leave `Erase device` unchecked if you want to keep paired AC profiles and the brand mask stored in NVS.

## Repository Layout

```text
docs/                 GitHub Pages web installer, configure pages, firmware binaries, and documentation images
firmware-idf-pure/    Current ESP-IDF / PlatformIO firmware project
firmware/             Older Arduino / PlatformIO firmware project
msc-disk-files/       Files exposed by the StickS3 USB mass-storage drive
tools/                Helper scripts, including USB MSC FAT image generation
```

## Built With

- [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266) - AC protocol encoders.
- [M5Unified](https://github.com/m5stack/M5Unified) - M5Stack board abstraction.
- [esp-web-tools](https://github.com/esphome/esp-web-tools) - browser flashing.

## License

[MIT](LICENSE). Use it, modify it, sell it. Pull requests are welcome, especially for new AC brand variants.
