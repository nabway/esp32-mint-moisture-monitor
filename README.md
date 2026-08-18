# Menta Monitor — ESP32 Soil Moisture Alerts

A pot of mint that tells you when it needs water.

An ESP32 reads a capacitive soil moisture sensor, turns the raw ADC value into a
0–100 % scale, and messages you on Telegram when the plant changes state. It also
publishes readings over MQTT so you can chart them elsewhere.

**Status:** v2.1.0 — tested and running.

## How it works

```
sensor → ADC + moving average → 0–100 % → state → Telegram / MQTT
```

1. **Read.** Sample GPIO34 every 15 s, average the last 10 samples to kill noise.
2. **Scale.** Map raw ADC to a percentage using two calibrated points
   (`include/config/calibration.hpp`). Higher raw = drier soil.
3. **Classify.** Percentage becomes one of five states, with 3 % hysteresis so a
   reading hovering on a boundary doesn't flap.
4. **Notify.** State change → Telegram message. Every 30 s → MQTT publish.

| State         | Moisture | Meaning                |
|---------------|----------|------------------------|
| `CRITICAL`    | < 20 %   | 🚨 plant is stressed   |
| `WATER_NOW`   | 20–40 %  | ⚠️ water today          |
| `WATER_SOON`  | 40–60 %  | 🌤 water in a day or two |
| `OK`          | 60–80 %  | ✅ ideal                |
| `OVERWATERED` | ≥ 80 %   | 💧 let it drain        |

The on-board LED (GPIO2) also lights up below 50 % as a local, no-network signal.

## Telegram

Alerts arrive automatically on every state change. You can also ask the bot:

- `/status` — current moisture, raw ADC and uptime
- `/help` — command list

## MQTT

Publishes a retained JSON payload to `menta/moisture` over TLS (HiveMQ Cloud, port 8883):

```json
{"percent": 74, "raw": 1323, "state": "OK"}
```

## Hardware

| Part | Detail |
|------|--------|
| Board | ESP32 DevKit v1 / WROOM-32 |
| Sensor | Capacitive soil moisture v1.2, 3.3 V |
| Sensor signal | GPIO34 (ADC1_CH6, 12-bit, 11 dB attenuation) |
| LED | GPIO2, on-board |

Pins live in one place: `include/config/pins.hpp`.

## Setup

Built with [PlatformIO](https://platformio.org/) on the Arduino framework.

```bash
cp include/config/secrets.example.hpp include/config/secrets.hpp
# fill in WiFi, MQTT and Telegram credentials — secrets.hpp is gitignored

pio run              # compile
pio run -t upload    # flash
pio device monitor   # logs @ 115200 baud
```

Up to three WiFi networks can be listed; the device tries each in turn on boot
and reconnects on its own if the link drops.

## Calibration

The defaults come from a real mint pot, measured on 2026-04-24:

| Condition            | Raw ADC | → % |
|----------------------|---------|-----|
| Dry baseline         | 2600    | 0   |
| Freshly watered      | 1000    | 100 |

Your soil and sensor will differ. To recalibrate: read the raw value with the
sensor in dry substrate and again right after watering, then put those two
numbers in `RAW_DRY_FLOOR` and `RAW_WET_FULL` in
`include/config/calibration.hpp`. Everything downstream follows from them.

Raw data from the original run is in `docs/calibration_log.md` and
`docs/drying_log.csv`.

## Layout

```
include/config/   pins, calibration constants, secrets
src/sensor/       ADC sampling, averaging, state machine
src/net/          WiFi (multi-SSID) and MQTT over TLS
src/cloud/        Telegram bot — alerts and commands
src/main.cpp      wiring it all together
```

## Roadmap

- OLED + RGB + buzzer local UI
- Move the loop onto FreeRTOS tasks
- Proper CA certificate validation instead of `setInsecure()`

## License

MIT — see [LICENSE](LICENSE).
