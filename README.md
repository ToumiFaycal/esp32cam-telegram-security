# ESP32-CAM Telegram Security Camera ("Smart Sentry")

A motion-triggered security camera built on an **ESP32-CAM** and an **HC-SR501 PIR sensor**. When motion is detected, the device takes a photo and sends it to your Telegram chat. You can also control it remotely with bot commands.

<!-- TODO: add a photo of the finished device and a short demo GIF or video link here -->

## Features

- **Motion detection** with a PIR sensor read through a GPIO interrupt
- **Photo alerts over Wi-Fi** sent to Telegram through the Bot API (TLS, using Telegram's root certificate)
- **Remote commands** from Telegram: take a photo on demand, mute alerts (indefinitely or for X minutes), resume
- **5-minute cooldown** between alerts so one event doesn't flood your chat
- **Owner-only control:** messages from any chat other than the configured chat ID are ignored
- **Low-memory camera setup** (VGA, single frame buffer, camera initialized before Wi-Fi/TLS) to fit within the ESP32-CAM's limited RAM

## Hardware

| Part | Notes |
|---|---|
| AI Thinker ESP32-CAM | Camera pin mapping in the sketch is for this board |
| HC-SR501 PIR sensor | Signal output connected to **GPIO 13** |
| USB-serial adapter (FTDI) or ESP32-CAM programmer base | The ESP32-CAM has no USB port <!-- TODO: state what you used --> |
| Stable 5 V supply | Weak USB ports can cause brown-outs and resets |

**Wiring**

| PIR pin | ESP32-CAM pin |
|---|---|
| VCC | 5V |
| GND | GND |
| OUT | GPIO 13 |

## Software requirements

- Arduino IDE with the ESP32 board package, board set to **AI Thinker ESP32-CAM**
- Libraries: **UniversalTelegramBot** and **ArduinoJson**
- Tested with: <!-- TODO: fill in your Arduino IDE version, ESP32 core version, library versions -->

## Setup

1. **Create a Telegram bot** with [@BotFather](https://t.me/BotFather) and copy the bot token.
2. **Find your chat ID** (for example by messaging a bot such as @userinfobot).
3. **Add your credentials:** copy `secrets.h.example` to `secrets.h` and fill in your Wi-Fi name (2.4 GHz), Wi-Fi password, bot token and chat ID. `secrets.h` is in `.gitignore`, so it stays off GitHub.
4. **Open** `esp32cam-telegram-security.ino` in the Arduino IDE, select the board, and upload (connect IO0 to GND while flashing, then reset the board).
5. Open the Serial Monitor at **115200 baud**. When the device joins Wi-Fi, you'll receive "Smart Sentry Armed!" in Telegram.

## Bot commands

| Command | What it does |
|---|---|
| `/photo` | Take and send a photo immediately |
| `/mute` | Disable motion alerts until resumed |
| `/mute X` | Disable motion alerts for X minutes (e.g. `/mute 15`) |
| `/resume` | Re-enable alerts and reset the cooldown, so the next motion triggers instantly |
| `/help` or `/start` | Show the command list |

## How it works

- The PIR output triggers an interrupt service routine that only sets a `volatile` flag. The photo capture and network calls happen in the main loop, not in the interrupt.
- The main loop polls Telegram for new messages every second, handles commands, checks whether a timed mute has expired, and processes motion events.
- After a motion alert, further alerts are suppressed for 5 minutes (`cooldownTime`). `/resume` and the end of a timed mute reset that timer.
- The camera is initialized **before** Wi-Fi and TLS so it can reserve the memory it needs; the TLS connection uses a large share of RAM.
- The JPEG frame buffer is streamed to Telegram with `sendPhotoByBinary()` through small callbacks, then returned to the driver.

## Known limitations and ideas

- No Wi-Fi reconnection logic: if the network drops, the device won't recover on its own.
- If camera initialization fails, the sketch reports it on serial but keeps running.
- The brown-out detector is disabled in `setup()`. This avoids resets on marginal power, but it can hide power problems, so use a solid 5 V supply.
- The cooldown is a fixed constant in the code. It could be made adjustable through a bot command.

## Security

- Never commit `secrets.h`. If a token is ever exposed, revoke it in @BotFather (`/revoke`) and generate a new one.

## Author

Faycal Toumi, Automation & Industrial Computer Science Engineering student at INSAT (Tunis).
