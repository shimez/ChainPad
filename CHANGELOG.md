# Changelog

## 0.6.0 — Local integration (not published)

- Add OBS WebSocket Actions on XIAO ESP32S3, ESP32C5 and ESP32C6: scene switching,
  stream/record start and stop, input mute and toggle.
- Add independent OBS connection settings, status, and manual bounded scene discovery.
- Preserve Action Chain limits and Config / Full / Key Preset compatibility.
- Add bounded asynchronous execution, scene-memory diagnostics and buffered S3 serial output.
- Validate OBS operation and memory smoke on S3/C5/C6; C6 mixed OSC/OBS/BLE-HID,
  Delay ordering, persistence and post-restart execution passed hardware regression.
- Known caveat: one reported C5 PSRAM barrier allocation error at startup; cause unknown,
  not reproduced in follow-up, no unexpected restart observed in follow-up tests.
  Long-duration and high-load evaluation remains ongoing.

Firmware version changes from `0.5.3` to `0.6.0`, retaining the existing suffix-free
format and `/api/status` version source. WebUI displays that value. Configuration
schema and Preset format versions are independent and unchanged.
See [integration notes](docs/obs-integration.md). No GitHub release or installer
publication is included in this local version update.
