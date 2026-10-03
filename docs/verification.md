# Phase 1 検証記録

## 0.3.0 LittleFS保存（2026-10-04）

- 保存先をLittleFSの唯一の内部形式へ変更。旧形式の読み込み・移行・fallbackコードなし。NetworkとEvent単位で処理し、Config全体の複製・全体JSONのデバイスRAM展開を廃止。
- Firmwareコアのホストテスト: 通常保存/読み込み、Wi-Fi専用保存、224個の長いOSC Actions（保存全体48KB超）、途中書き込み失敗、rename失敗、未完了/失効token、再起動、中途ファイル・active破損を検証しPASS。既存OSC/MIDI/HID/EngineとC3/C6/C5の保存・BLE正規化テストも全てPASS。
- ブラウザC6 fixture: 保存がNetwork＋28 Chains＋commitの30リクエストになること、通信失敗時のUI復帰・未確定設定保持・再保存・再読み込みを確認しPASS。設定例の最大リクエスト本文は340 bytes。
- S3/C3/C6/C5のローカルビルドと生成image検査は全機種PASS。LittleFS partitionのoffset/size、Flash範囲、application配置、機種識別、USB条件付き組み込みを確認。
- 実機LittleFSの電源断耐性・C6保存再確認は未実施。手順はphysical-test.md参照。

以下は旧版の検証履歴です（2026-10-03）。

## 0.2.1: C6のWi-Fi保存時メモリ不足

- `/api/wifi` で設定全体のJSON生成→再解析→Config全体の複製を行っていたため、Config用の連続メモリ確保が失敗すると `Insufficient memory` を返していた。
- Wi-Fi保存専用関数でSSID/Passwordのみ検証し、現行設定を1回JSON化して保存。Configの複製を廃止し、NVS書き込み前にJSONツリーも解放する。
- ホストテストでConfigサイズ以上のnothrow確保を失敗させ、従来の全体保存が失敗する条件でWi-Fi専用保存が成功することを確認。OSC/Chains保持、NVS失敗時の設定保持、不正入力拒否、再読み込みもPASS。既存ホストテストとC3/C6/C5能力テストもPASS。
- C6実機での再確認は未実施。

## MIDI入力欄の1行表示

- Configuratorの最大幅を1280px、左右paddingを16pxへ調整。画面幅1100px以上ではMIDIを6列とし、Channel/Note/Velocityの数値欄をコンパクトに配置。
- ブラウザのC3 fixtureで1100px/1280px時に6入力欄が同じ上端に並ぶこと、390px時は折り返して横スクロールしないことを確認: PASS。
- S3/C3/C6/C5の埋め込みUI再ビルド: 全機種PASS。

## OSC送信先とWi-Fi設定の分離

- OSC送信先を上部の常時表示パネルへ、Wi-Fi設定を最下部の独立した折りたたみ欄へ移動。
- ブラウザでOSC入力欄の初期表示、Wi-Fi欄の最下部配置・初期折りたたみ、両設定の保存/再読み込みをC3 fixtureで確認: PASS。
- 埋め込みUI更新後のS3/C3/C6/C5ビルド: 全機種PASS。後続のSHA256は以前のビルドの履歴です。

## BLE固定欄の整列・Wi-Fi専用ポータル

- BLE固定の補足文があっても入力欄を上揃え・高さ42pxに統一。C3 fixtureのMIDI/Keyboardについてブラウザの要素座標で上端と高さの一致を確認: PASS。
- Wi-Fi専用画面の入力2個・ボタン1個、保存成功表示、fixture上のWi-Fi更新とOSC/Chains保持を確認: PASS。実機の保存・再起動は未確認。
- S3/C3/C6/C5ビルド・生成image検査: 全機種PASS。

最新 `firmware.bin` SHA256:

```text
S3 8a77ee39e82945cf717b6f75fdc496da7a7e78f0f4e502312d21a147b6d594ea
C3 3e5acc63df66bc822f93f2c5273b9e78a5160ab038d6ca98b5953a2f87978da7
C6 c66a8a668dadf621ead6ae5015381a43a06db55132f1af60293d1f8a702b19f7
C5 21a95e8a2afc7df3ae0edde1db0c7a1deb78beda95f1faf16ca65c172ceb9f30
```

以下は過去の検証履歴です。

## APキャプティブポータル追加

- Setup AP用ワイルドカードDNS、AP側HTTP GETの設定画面への302誘導、Network欄の自動展開と保存/再起動ボタンを追加。
- C3 UI fixtureで `/?setup=wifi` のNetwork展開・案内表示・SSID/Passwordの保存・再起動API呼び出しをブラウザで確認: PASS。
- Android/iOS/Windowsによるログイン通知、実機DNS応答・HTTPリダイレクト・Wi-Fi再接続は未検証。手順は `docs/physical-test.md` を参照。
- Firmware 4機種ビルド・生成image検査: 全機種PASS（partition範囲、image配置、Flashサイズ、機種名、USB descriptor gating）。後続の0.2.0節のSHA256はポータル追加前の履歴です。

キャプティブポータル追加後の `firmware.bin` SHA256:

```text
S3 53a6df2f8543aafb4309260affb3aaa1640b00444273102c3622951ac3ae333e
C3 a182992d1a567c545bb3d8e36cf5aeef98555335c15d67c01aec00e1f5fc2634
C6 7cc0acdadce855592ddf43f399bfd7ebd1009ecfa64cd6dbcba9d54ef618ba70
C5 a75e5f8733c3ed9d14c90801ac71c9a293271a39b314794910bba14388cd61d6
```

## 0.2.0: C3 / C6 / C5対応

- SoC別capabilities、BLE固定UI、USB MIDI/HID条件付きコンパイル、単一コア用input task、4MB/8MB partitionを追加。
- ホストテスト: S3の既存6グループに加え、C3/C6/C5各マクロでcapabilities・USB/BothからBLEへの正規化・保存/読み込み・Press/Release実行を検証しPASS。
- ブラウザ: C3 fixtureをBLE-only機種の代表として、MIDI/HID欄が表示されたままdisabledの「BLE（固定）」になること、保存したPress/ReleaseがBLEになること、USB状態が「非対応」であることを確認しPASS。S3 fixtureではMIDI Both/USB/BLEとKeyboard USB/BLEの選択が有効であることを確認しPASS。
- 再現用UI fixture: `python tests/serve_configurator.py --board c3`（`s3/c3/c6/c5`を指定可能）。
- 4機種向けFirmware build・生成image検査: 全機種PASS。partition範囲・重なり・factory image内のapplication配置・Flashサイズ・機種名・USB MIDI descriptorのS3限定組み込みを確認。
- C3/C6/C5での実機操作・BLE接続・設定電源再投入テストは未実施。[physical-test.md](physical-test.md) に追加手順を記載。

| Environment | RAM bytes | Application bytes | 結果 |
|---|---:|---:|---|
| xiao_esp32s3 | 134604 | 1265266 | PASS |
| xiao_esp32c3 | 92268 | 1318793 | PASS |
| xiao_esp32c6 | 97484 | 1480466 | PASS |
| xiao_esp32c5 | 104296 | 1534073 | PASS |

0.2.0 `firmware.bin` SHA256:

```text
S3 81cb85d1058eb0e1ea28e39e6e994b1e065b331250a0862bcbabace5618d6725
C3 09bb46177139a6a366f4242fcbf4025388c5bcb2b534f694cc572a9799d5b0ed
C6 f27cb58a478408f8458f561d0189a02d65af48204d4d9e82f9b1eb1df74e24e1
C5 a7a7822d6818dd7ed397893f65a8508f1bdb2cf75ef3553dd108b24d6ce80e81
```

生成image検査:

```powershell
py -3.12 tests/check_image.py --env xiao_esp32s3 xiao_esp32c3 xiao_esp32c6 xiao_esp32c5
```

## 追加更新: Light / DarkとMIDI Both

- Web UIのLight/Dark切り替え、localStorageへの保存、再読み込みでの復元をブラウザで確認: PASS。
- 新規MIDIとPress/Release設定例のBoth既定値、保存JSON、KeyboardにBothを表示しないことを確認: PASS。
- Firmwareホストテスト6グループ: PASS。BothでNote On/Offを両出力へ送ること、USB/BLE片側未接続時の継続、両側未接続、片側FIFO overflowの分離、設定round-tripを追加確認。
- S3向けビルドと生成image配置チェック: PASS。RAM 134604 bytes、Flash 1263666 bytes。
- 最新 `firmware.bin` SHA256: `67349d83c04a967cfa005011ec167275a93ca3f4ff3c769ac046bf1b41b5742b`。
- 本更新のBoth同時送信は実機では未確認。ユーザーからは更新前のOSC/Keyboard/MIDI動作成功の報告あり（MIDI不達の主因はActionのTransport設定）。

以下は初期実装時の検証記録です。

## 結果

| 項目 | 結果 |
|---|---|
| XIAO ESP32S3 Firmware build | PASS |
| USB MIDI/HID/CDC + NimBLE MIDI/HIDのリンク | PASS |
| 生成partitionの範囲・重なり・factory image配置 | PASS |
| Firmware coreホストテスト | PASS（下記5グループ） |
| Receiverテスト | PASS（6 tests） |
| Configuratorのブラウザ操作 | PASS（ローカルAPI fixture） |
| S3基板への書き込み・実入力・無線/USB相互接続 | **未実施** |
| このFirmwareからOBSへの実ホットキー操作 | **未実施** |
| 実NVSの電源断・再起動・大量設定保存 | **未実施**（ホストではNVS adapter） |

ユーザー提供PoC2のC6でのOSC/BLE MIDI/BLE HID同時動作実績を設計の参考にしました。上表の未実施項目をPoCの結果で置き換えていません。

## Build

PlatformIO `xiao_esp32s3` / pioarduino 55.03.311 / Arduino-ESP32 3.3.11 / NimBLE-Arduino 2.5.1 / ArduinoJson 7.4.2。

```text
RAM:   134604 / 327680 bytes (41.1%)
Flash: 1262074 / 6291456 application bytes (20.1%)
Result: SUCCESS
```

物理Flashは8 MB。PlatformIOの6 MB表示はこのプロジェクトで設定したapplication上限です。生成image headerのFlashサイズは8 MBであることを検査しました。

`firmware.bin` SHA256:

```text
cb17e68dc9725aa3b6eb5236b28c50a8aca198de6bfeb5c50e60a74efd7a6196
```

生成物: `.pio/build/xiao_esp32s3/firmware.bin`、`firmware.factory.bin`。

## Firmware coreホストテスト

実際の `model.cpp / engine.cpp / *_backend.cpp` とArduinoJsonをMSVCでコンパイル。GPIO/無線/USB/NVSをホストadapterへ差し替えています。

1. 設定検証・重複Input/範囲外Channel/非ゼロDelay/不正IPv4拒否、保存失敗時のruntime保持、>4 KiBの設定reload、容量制限。
2. OSC int/float/bool/stringのwire bytesとpadding。
3. MIDI On/Offの順序・失敗時のOff再送・再接続時の全Channel reset・FIFO overflow時Panic。
4. HID同時キーと共通modifierの維持・Up再送・6KRO制限・短時間の再購読generationによるreset。
5. OSC + USB MIDI + BLE KeyboardのPress/Release、1つのProtocolが未接続でも後続が実行されること。

```powershell
pio run
powershell -ExecutionPolicy Bypass -File tests/run_host.ps1
py -3.12 tests/check_image.py
```

ホストテストにはVisual Studio C++ Build Toolsが必要です。partition/imageテストにはFirmwareビルド成果物が必要です。

## Receiver

Python 3.12 / Tcl-Tk / `requirements.txt` の固定依存を使用。

- BLE-MIDI timestamp wrap、Note On/Off。
- 複数message / running status / CC。
- malformed packetの拒否。
- velocity 0のNote OnをNote Offとして表示。
- 複数threadの時刻・queue順、queue overflowカウンタ。
- **実UDP loopback → OSC parser → Tk timeline** とmock OS MIDI callbackが1画面に表示されること、Time/Delta/内容。

```powershell
py -3.12 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r tools/ChainPadReceiver/requirements.txt
.\.venv\Scripts\python.exe -m unittest discover -s tests -p test_receiver.py -v
```

このテストではBLE adapterや実MIDI機器へ接続していません。

## Configurator

`tests/serve_configurator.py` でlocal fixtureを起動し、ブラウザで以下を確認:

- 28 Event / 14 Inputグループの表示。
- KEY 01の3 Protocol例でPress/Releaseの独立Chainが生成される。
- 保存JSONのProtocol、Transport、Note On/Off、KeyDown/Up、F13 Usageが正しい。
- 保存済み設定の読み込み。
- Encoder CW/CCWのID 26/27対応。
- Protocol変更時の条件付きフォーム表示、Action削除。

```powershell
.\.venv\Scripts\python.exe tests/serve_configurator.py
# http://127.0.0.1:8765/
```

fixtureは表示確認用で、Firmware側のschema検証やNVSを代替しません。

## 次の実機確認

[physical-test.md](physical-test.md) に従い、最初に **1キー → OSC + USB MIDI + BLE Keyboard** を確認し、その後BLE MIDIとUSB Keyboardを追加します。再接続、Panic、電源再投入による設定保持まで確認できた時点で実機受入完了とします。
