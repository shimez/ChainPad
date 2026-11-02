# Changelog

## 0.9.0

- Add a Configurator firmware update notification using the public Web Installer's
  build metadata, checked once after device status is available.
- Compare numeric versions and distributed chip families; link to the Installer
  without automatically updating firmware. Offline, timeout and metadata failures
  do not interrupt configuration or device status polling.
- Add automated coverage and CI checks for version comparison, hardware mapping,
  status sequencing and failure isolation; verify public metadata access in a browser.
- Clarify v0.7.0/v0.8.0 feature availability and the Linked Outputs / Output to
  Action Link / Action terminology change in the manuals.

## 0.8.0

- Add OBS volume Actions with absolute and relative dB adjustment, clamped to
  -100 through +26 dB. Replace the unpublished linear-volume Action schema with `volumeDb`.
- Add OBS volume to Rotation Value Action Link with source discovery, dB Start/End
  interpolation and latest-state output alongside OSC and MIDI. Control is one-way to OBS.
- Preserve existing OSC/MIDI settings and configuration/Preset format versions.
- Rename Rotation output controls to Action Link, add a Preview Position slider,
  and improve OBS volume field layouts.
- Extend the standalone OBS tester with linear-volume controls.
- Host, OBS and UI regression tests passed. Hardware OBS volume validation was
  completed by the user.

## 0.7.0

- Add OBS media control: restart, stop, pause, resume, next and previous, with
  manual media/VLC source discovery and Full/Key Preset persistence.
- Add scene/group item visibility control and manual item discovery.
- Group OBS operations in the WebUI and unify mute controls under one editor choice.
- Add the standalone OBS media tester and update setup/action documentation.
- Preserve configuration and Preset format versions and existing discovery memory limits.
- Media-control UI, protocol, Worker, HTTP and storage tests passed, along with
  S3/C3/C5/C6 builds and image checks. Hardware media-control validation is pending.

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
