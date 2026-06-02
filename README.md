# ESP32 Capacitive Soil Moisture — Sensor Characterization

Characterization sketch and raw dataset for a capacitive soil moisture sensor on
an ESP32, captured on real mint substrate. This is the data-collection stage of
a larger plant-watering monitor: the goal here is to measure how the sensor
actually behaves before writing any calibration curve or control logic.

> **Status:** early / characterization phase. The sketch captures raw ADC data.
> The calibration curve, watering states and UI are not implemented yet — see
> the roadmap below.

## What's in here right now

- `src/main.cpp` — a characterization sketch (explicitly *not* the final
  firmware). It reads the sensor on the ESP32 ADC (GPIO34, 12-bit, 11 dB
  attenuation), applies a 10-sample moving average, and prints one CSV line per
  sample over serial at 2 Hz.
- `docs/calibration_log.md` — raw readings captured on 2026-04-24 following a
  fixed protocol: dry air → glass of water → freshly watered mint.

## Measured reference points

From the 2026-04-24 run, the raw ADC value (0–4095) settles roughly at:

| Condition            | Raw ADC (approx.) |
|----------------------|-------------------|
| Dry air              | ~2520             |
| Submerged in water   | ~1050             |
| Freshly watered mint | ~1700             |

The ~1470-count span between dry air and water leaves plenty of resolution to
build a 0–100 % moisture scale in the next phase.

## Build & flash

Built with [PlatformIO](https://platformio.org/) on the Arduino framework.

```bash
pio run                 # compile
pio run -t upload       # flash to the board
pio device monitor      # CSV output @ 115200 baud
```

## Capture protocol

The sketch header documents the procedure used to gather the data: hold the
sensor in air, then submerged in a glass of water, then in the pot right after
watering — each for at least 60 s — logging the serial CSV for every phase.

## Roadmap

1. Capture the 48 h drying curve (`docs/drying_log.csv`).
2. Derive calibration constants and move them into `include/config/calibration.hpp`.
3. Map raw readings to a 0–100 % scale and add watering states.
4. OLED + RGB + buzzer UI on FreeRTOS.
5. Telegram alerts over WiFi.

## License

MIT — see [LICENSE](LICENSE).
