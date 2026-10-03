# References and third-party components

## 参考プロジェクト

- ChainOSCPad: 基板ピン配置、キー行列、エンコーダ、Wi-Fi/WebUI/設定保存の設計。
- ChainOSCPad-MIDI: USBMIDI/TinyUSB、NimBLE 2.5.1、BLE-MIDI UUIDとtimestamp packet、open-drain matrix、encoder interrupt。
- ChainOSCPad-Keyboard: USB HID Usage、NimBLE HID report、F13–F24。
- ChainOSC_PoC2: C6におけるWi-Fi OSC + BLE MIDI + BLE HIDの同時動作（ユーザー実機確認済み）、共有BLEサーバーと個別購読管理。

PoC2内の接続資格情報は本プロジェクトへ取り込んでいません。PoC2のC6実績は、本FirmwareのS3での実機確認を代替しません。

参考元MIDI/KeyboardのMIT notice（ピン走査・quadrature・BLE packet/reportの派生部分を含む）:

```text
MIT License

Copyright (c) 2026 shimez and contributors
Copyright (c) 2026 Shimez

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## 依存ライブラリ

Firmware: pioarduino platform-espressif32、Espressif Arduino Core / ESP-IDF、TinyUSB、NimBLE-Arduino 2.5.1、ArduinoJson 7.4.2。

Receiver: Python / Tcl-Tk、python-osc 1.9.3、Mido 1.3.3、python-rtmidi 1.5.8、Bleak 0.22.3とWindows依存ライブラリ。

依存ライブラリのソース・ライセンスは各配布物を参照してください。Firmwareの依存はPlatformIOにより取得されます。
