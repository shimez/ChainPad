# Phase 1 実機受入テスト

対象: XIAO ESP32S3 / ESP32C3 / ESP32C6 / ESP32C5、現行ChainOSCPad PCB、Windows PC。PoC2のC6での確認結果とは別に記録します。以下のUSB検証はS3のみ対象です。

## APキャプティブポータル確認（全機種）

1. `ChainPad-Setup` に接続し、Android/iOS/Windowsのログイン通知からWi-Fi専用画面が開くことを確認。SSID・パスワード・保存して再起動ボタンのみが表示されること。
2. 自動表示されない場合は `http://192.168.4.1/` を開く。SSID/Passwordを入力して「保存して再起動」を実行。再接続してWi-Fi接続状態と設定保持を確認。OSC送信先とAction Chainsも保持されること。通常の設定画面はSTA IPのルート、またはAP IPの `/configurator` から開く。
3. AP接続中に `nslookup example.com 192.168.4.1` でAP IPが返ること、`/generate_204`, `/hotspot-detect.html`, `/connecttest.txt` がHTTP 302で `/?setup=wifi` へ誘導されることを確認。
4. APの `/api/unknown` とSTA IPの未知パスが404を返すこと、STA接続後もAPから設定できることを確認。

## C3/C6/C5での追加確認

1. 対象の `xiao_esp32c3` / `xiao_esp32c6` / `xiao_esp32c5` を指定してbuild/upload。
2. 起動し、Setup APからWeb UIを開く。表示機種名が対象ボードと一致すること。
3. MIDIとKeyboardのTransport欄が消えずに **BLE（固定）** となり、選択不可であること。上部USB MIDI/HID表示が **非対応** であること。
4. 設定例のPress/ReleaseがいずれもBLEになること。OSC + BLE Note On/Off + BLE KeyDown/Upを同じキーで確認。
5. Receiverでは **OS MIDI input** のBLE MIDIポートを開く。USB MIDIポートを探す必要はありません。
6. APIへMIDI USB/Both、Keyboard USBを含む有効な設定を保存し、`/api/config` がBLEを返すこと。再起動後もBLEのままで動作すること。
7. 12キー、Encoder CW/CCW、Push、LEDを確認。単一コアのC3/C6/C5で起動停止や入力タスク生成失敗がないこと。
8. 実用設定の保存・再読み込み・電源再投入を確認。Action数を増やすテストでは `freeHeap` と保存APIの結果も記録すること。

上記を3機種で個別に記録し、S3ではUSB/BLE/Bothが引き続き選択・動作することも確認します。

## 1. 起動と設定

1. `pio run`、S3へupload。USB MIDI、Keyboard、CDCが同時に列挙されることを確認。
2. `ChainPad-Setup` / `chimera-pad` → `http://192.168.4.1/`。
3. 全Chainが空で起動し、`inputsReady` がtrueであること。
4. Wi-Fi / OSC Host / Portを設定して保存。Restart後も設定が残り、STAが接続されること。
5. KEY 01の設定例を配置し保存。Release側を表示し、OSC int 0 / Note Off / KeyUpが独立して編集できること。

## 2. 最小コンセプト成立

現在の設定例のMIDIはBothが既定です。この節でUSBだけを確認する場合は、Press/Release両方のMIDI TransportをUSBに変更してください。

Press:

1. OSC `/avatar/parameters/Costume` Int 2
2. USB MIDI Note On / Ch1 / 48 / 127
3. BLE Keyboard KeyDown / F13

Release:

1. OSC同Address Int 0
2. USB MIDI Note Off / Ch1 / 48 / 0
3. BLE Keyboard KeyUp / F13

ReceiverでOSCとUSB MIDIを受信し、WindowsでBLEをペアリング。OBSにF13ホットキーを登録します。

- KEY 01を押すとOSC int 2、Note On 48、OBSの指定操作が実行される。
- 離すとOSC int 0、Note Off 48、KeyUpが実行され、入力が残らない。
- 短押し・長押し・50回繰り返し。Note数、Release数、OBS動作を記録。
- 長押し中のKeyboard repeatはOSの動作です。OBS側のホットキー設定と区別します。
- ConfiguratorのSkipped / Overflowが増えていないこと。

## 3. 全5 Transport出力

KEY 01のPress/ReleaseへBLE MIDI Note On/Off（USBとは別Note）とUSB Keyboard KeyDown/KeyUp（BLEとは別Usage）を追加します。

ReceiverのDirect BLE-MIDIで接続し、ConfiguratorでBLE MIDIとBLE HIDが両方接続表示になること。OSC・USB MIDI・BLE MIDI・USB HID・BLE HIDが同じ物理操作から動作することを確認します。

**受信順は固定合否条件にしません。** 各Transportの1対1のイベント対応、内容、Release、取りこぼしを確認します。CSVを保存し、PC受信時刻/Deltaを記録します。

## 4. 入力と複数操作

- Bothを使う検証ではNote On/Off/CCそれぞれがUSB/BLEへ届くこと、片方を切断しても他方の受信が続くことを確認。

- KEY 01–12のrow-major対応を1つずつ確認。
- Encoder CW/CCWへ異なるOSCまたはCCを設定し、1 detent = 1 Event、方向を確認。必要に応じ `hardware.h` の設定/入力層を調整。
- Push Press/Releaseを確認。
- 複数キーを押し、別UsageのHIDキーが他キーのReleaseで消えないこと。共通Ctrl付きの2キーで、片方のRelease後もCtrlが残ること。
- HIDは6キー同時押し。7キー目がBusyになり、既存キーの状態が壊れないこと。
- Matrixの同時押し可能数はPCBのダイオード構成にも依存します。実機でghostingの有無を記録します。

## 5. 切断・保存・復旧

- BLE切断中もOSC/USBが動作する。未接続ActionだけSkippedになる。
- BLE再接続後、過去のNote On/KeyDownを再生しない。新しい押下が動作する。
- USB MIDIポートの開閉、USB再接続後に古いNote/Keyが残らない。
- キー押下中にPanicし、MIDI/HIDが解放される。
- キー押下中に設定を変更して保存し、旧設定のNote/Keyが残らない。
- Wi-Fi不達でもSetup APとUSB/BLEが利用できる。
- 設定を変更・保存して電源を入れ直し、Press/Release両Chainが残る。
- 多数のAction（合計JSON >4 KiB）を保存し再起動して読み戻せる。
- PCをスリープ/復帰し、再接続時の出力状態を確認する。

## 記録テンプレート

```text
Date:
Firmware SHA256:
Board / PCB revision:
OS / Bluetooth adapter:
USB enumeration:
BLE MIDI + HID simultaneous subscription:
OSC / USB MIDI / BLE MIDI / USB HID / BLE HID:
Press count / Release count:
Skipped / Input overflow / Output overflow:
Power-cycle persistence (>4 KiB):
Disconnect / reconnect / Panic:
OBS hotkey result:
Receiver CSV:
Pass / issues:
```
