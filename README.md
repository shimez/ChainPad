# ChainPad — Project Chimera / Phase 2

**1つの物理入力からOSC / MIDI / Keyboard HIDを組み合わせて実行する、Advanced / Unified Firmware。**

対象は **XIAO ESP32S3 / ESP32C3 / ESP32C6 / ESP32C5 + 現行ChainOSCPad PCB**。ChainOSCPadを置き換えるものではありません。

| ボード | MIDI Transport | Keyboard Transport | Flash / application上限 |
|---|---|---|---|
| XIAO ESP32S3 | Both（既定）/ USB / BLE | USB / BLE | 8 MB / 6 MB |
| XIAO ESP32C3 | BLE固定 | BLE固定 | 4 MB / 3 MB |
| XIAO ESP32C6 | BLE固定 | BLE固定 | 4 MB / 3 MB |
| XIAO ESP32C5 | BLE固定 | BLE固定 | 8 MB / 6 MB |

C3/C6/C5もWeb UIのTransport欄を表示し、**「BLE（固定）」＋「この機種はBLEのみ対応」**として選択不可にします。USB接続は書き込み/コンソール用で、USB MIDI/HIDにはなりません。上部のUSB状態も「非対応」と表示します。

## 実装範囲

| 機能 | Phase 2 |
|---|---|
| Input | 12キーのPress/Release、Encoder CW/CCW、Encoder Push Press/Release |
| Action Chain | Eventごとに最大8 Actions、登録順にdispatch |
| OSC | Wi-Fi / int32、float32、bool、string、送信先IPv4/Portは共通 |
| MIDI | USB / BLE / Both（USB + BLE）、Note On、Note Off、Control Change |
| Keyboard HID | USB / BLE、KeyDown、KeyUp、Release All、修飾キー、F13–F24 |
| USB | MIDI + Keyboard + CDC複合デバイス |
| BLE | 単一NimBLEサーバーでMIDI/HID共存、個別の購読状態管理 |
| Configurator | Light/Dark切り替え、Protocol別フォーム、追加/削除/並べ替え、Press/Release、設定保存/読み込み、接続状態、テスト、実行中止・HID解除 |
| 保存 | LittleFSのレコード形式。Network＋Event単位のJSONを逐次処理し、検証後に原子的に切り替え |
| Receiver | OSC / OS MIDI / Direct BLE-MIDI / フォーカス中HIDの時系列表示、CSV出力 |

Wait ActionとKey Preset / Full ConfigurationのJSON Import・Exportに対応します。Sequence、OTA、可変Hardware Metadataは未実装です。

## Phase 2の使い方

- Actionの種類で **Wait** を選び、0〜86400000 ms（24時間）の整数を設定します。Waitも1 Actionとして数えます。
- Waitはその実行だけを待機させます。Press / Release、別キー、同じキーの再入力は独立して動作し、Action順序を自動補正しません。PressのWait後にNote Onがあると、先にReleaseのNote Offが実行される場合があります。
- 同時に待機できる実行は32個です。上限時は新しい待機付きChain全体を拒否し、画面の「Chain受付失敗」に記録します。WaitなしのRelease等は引き続き実行できます。
- 保存・適用、実行中止・HID解除、Restartでは待機中のChainを中止します。Waitは最小待機時間であり、HTTP処理等による遅延が加わる場合があります。
- **Key Preset Export / Import** は選択キーのPress / Release両方を扱います。別キーへImport可能です。Encoder Pushにも対応し、Encoder回転は全体設定で扱います。
- **全体設定 Export / Import** は全28 Events、OSC送信先、Wi-Fi資格情報を含みます。ファイルにWi-Fiパスワードが含まれます。Exportには未保存の編集も含みます。
- Importは検証後に編集画面へ反映され、**保存・適用**で確定します。不正ファイルは現在の編集を変更しません。C3/C5/C6へのImportではUSB TransportをBLEへ適応し、その変更を表示します。
- 0.3.0のLittleFS保存設定はそのまま利用できます。旧NVS形式の読み込み・移行処理はありません。

## ビルド・書き込み

PlatformIO Core / VS Code PlatformIOを使用します。依存バージョンは `platformio.ini` に固定しています。

接続するボードの環境名を指定します。

```text
XIAO ESP32S3 → xiao_esp32s3
XIAO ESP32C3 → xiao_esp32c3
XIAO ESP32C6 → xiao_esp32c6
XIAO ESP32C5 → xiao_esp32c5
```

以下はS3の例です。C3/C6/C5では環境名を置き換えてください。`pio run` のみで実行すると4機種すべてをビルドします。

```powershell
pio run -e xiao_esp32s3
pio run -e xiao_esp32s3 -t upload --upload-port COM番号
pio device monitor --port COM番号 --baud 115200
```

この環境ではPlatformIOがPATHにない場合、次の形式で実行できます。

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run
```

- 初回書き込みやUSBポートが見えない場合は、XIAOのBOOTを押しながらRESETしてダウンロードモードへ入ります。起動後のCDC COM番号は書き込み時と変わる場合があります。
- WebUIはビルド時にFirmwareへ埋め込まれます。`uploadfs` は不要です。
- `partitions.csv` はS3/C5の8MB Flash用（LittleFS `settings`: `0x630000`, 1856 KiB）。`partitions_4mb.csv` はC3/C6の4MB用（`settings`: `0x330000`, 832 KiB）。applicationはいずれも `0x10000`。
- **0.3.0から保存方式はLittleFSのみです。旧設定は読み込み・移行されません。Wi-FiとAction Chainを再設定してください。** 更新時はpartition tableも含むWeb InstallerかPlatformIO uploadを使用してください。`firmware.bin` だけの書き込みでは新しい領域を利用できません。
- ビルド成果物は `.pio/build/<環境名>/firmware.bin` と `firmware.factory.bin`。通常はPlatformIOのuploadを使用します。

## 最初のAction Chain

ブラウザから書き込む場合は **[Web Installer](https://shimez.github.io/ChainPad/)** をPC版Chrome / Edgeで開いてください。S3/C3/C6/C5を自動判別します。

Web Installerは `site/index.html`、公開用manifest・結合Firmwareは `scripts/build_site.py` で生成します。GitHub Actionsの `.github/workflows/pages.yml` がmainのFirmware/UI変更時に4機種をビルド・検査し、GitHub Pagesへまとめて配信します。公開画面にソースのコミットID、ダウンロード欄にSHA256付きビルド情報を表示します。

1. 起動後、Wi-Fi **`ChainPad-Setup`** に接続します。Password: **`chimera-pad`**。
2. OSの「ネットワークにログイン」通知／自動表示されたWi-Fi専用画面でSSID / Passwordを入力し、**保存して再起動** を押します。自動表示されない場合は **http://192.168.4.1/** を開いてください（HTTPSではなくHTTP）。
3. 同じWi-Fiに接続し、**http://chainpad.local/** または本体のSTA IPを開きます。上部の常時表示される「OSC送信先」にReceiver PCのIPv4 / Port（既定9000）を入力します。Wi-Fi設定は画面下部の独立した折りたたみ欄にあります。APから通常の設定画面を開く場合は **http://192.168.4.1/configurator** を使用できます。
4. **KEY 01 → 3 Protocolの設定例を配置** を押します。
5. **保存・適用** → Wi-Fi設定を変更した場合は **Restart**。
6. PCでBLE **`ChainPad Chimera`** をペアリングします。S3の場合はUSBにもMIDI/HID/CDCが列挙されます。
7. [ChainPadReceiver](tools/ChainPadReceiver/README.md) を起動し、OS MIDI inputからChainPadのMIDI入力ポートをOpenします（S3はUSBまたはBLE、C3/C6/C5はBLE）。
8. KEY 01を押して離します。

```text
KEY 01 Press                    KEY 01 Release
  OSC /Costume int 2               OSC /Costume int 0
  USB+BLE MIDI Note On Ch1 48 127  USB+BLE MIDI Note Off Ch1 48 0
  BLE Keyboard F13 KeyDown        BLE Keyboard F13 KeyUp
```

実際の例のOSC Addressは `/avatar/parameters/Costume` です。使用先に合わせて編集してください。

上記はS3の設定例です。新規MIDI Actionと設定例のTransportはS3では **Both** が既定で、USB/BLEへ同じメッセージを送ります。C3/C6/C5の設定例は **BLE** です。S3でUSB Keyboardも試す場合は対応するKeyDown/KeyUpを追加できます。

画面上部の **表示テーマ / Theme** で **Light / Dark** を選択できます。テーマはブラウザのlocalStorageに保存され、同じアクセス先で次回表示時に復元されます。本体設定の保存や再起動は不要です。

STA接続後は表示されたIPまたは `http://chainpad.local/` でもアクセスできます。Setup APは設定復旧用に常時有効です。APへ接続したPCだけで試す場合は、OSC HostをそのPCの `192.168.4.x` に設定できます。

## Actionの扱い

- **Note On / Note Off、KeyDown / KeyUpは独立Action**です。自動的にRelease側を補完しません。設定例ボタンは両方をまとめて作成します。
- EncoderのCW/CCWにはRelease Eventがありません。KeyDownを割り当てる場合は、同じChainの後続にKeyUpを追加するか、別Inputから解放してください。Phase 1に自動Tap時間調整はありません。
- KeyboardはTransportごとに6キー同時押し。各KeyDownに付けた修飾キーは、そのKeyUpまで維持されます。Usageが異なるキーの修飾キーはOR合成されます。同一UsageのKeyDownは冪等で、複数Input間の参照カウントは行いません。
- 登録順は**dispatch順**です。異種Transport間のPC到着順・同時刻到着を保証するものではありません。
- MIDIの **Both** はUSB/BLEそれぞれの送信キューへ独立に投入します。片方が未接続でも接続中の側へ送信し、少なくとも片方が受け付ければActionはAcceptedです。両方未接続ならUnavailableです。既存のUSB/BLE設定は読み込み時に維持されます。切り替える場合はNote On/Offの両方を変更してください。
- C3/C6/C5では、保存済み設定の読み込み・API保存時にMIDIのUSB/BothとKeyboardのUSBをBLEへ正規化します。古い設定やUIを経由しないAPIでもUSB出力が選択されません。S3の既存Transportは維持します。
- 未接続の出力はskipし、後続Actionを実行します。未接続中のイベントを再接続後に再生しません。
- MIDI/HIDはTransportごとに64件のFIFOを持ち、接続中の一時的な送信失敗を再試行します。再接続時は過去のキューを破棄し、HIDは空レポートを送ります。MIDIの自動一括CC送信はありません。
- FIFO overflow時は該当Transportのキューを破棄します。HIDは押下状態も解除します。入力キューoverflow、保存・適用、「実行中止・HID解除」では待機中のChainと入力・出力キューを中止し、HID解除を要求します。MIDIメッセージは生成しません。
- **AcceptedはUDP送信受付またはMIDI/HIDキュー受付**であり、相手アプリでの受信確認ではありません。Receiverと実機で確認します。
- HTTP処理・設定保存中はdispatchが遅れることがあります。入力スキャンは別タスクですが、Phase 1は厳密なリアルタイム遅延を保証しません。
- 設定APIはローカル開発ネットワーク向けです。Wi-Fi資格情報を含むため、設定JSONを公開しないでください。

## 構成

```text
src/
  hardware.h             現行PCBのピン仕様のみ
  capabilities.h         SoCごとのUSB MIDI/HID可否・機種名
  inputs.*               1msスキャン、debounce、encoder → logical InputEvent
  model.*                Action / Chain / Config、レコード単位の検証・JSON変換
  config_store.cpp       LittleFSの逐次保存・検証・atomic renameによる確定
  engine.*               InputEvent → 登録順dispatch（GPIO依存なし）
  osc_backend.cpp        OSCワイヤーフォーマット / UDP
  midi_backend.cpp       MIDI送信FIFO、再接続・overflow回復
  keyboard_backend.cpp   HID押下状態 / FIFO / 修飾キー合成
  transports.*           USB複合デバイス、共有BLEサーバー
  main.cpp               Network、Web API、ライフサイクル
web/index.html           Action Chain Configurator
tools/ChainPadReceiver/  PC側受信ツール
tests/                   Firmware core / Receiver / UI fixture
docs/                    設計、実機確認手順、検証記録
```

Input Engineは行列やGPIOを知りません。将来の小型PCBは入力層・UIへのハードウェア情報提供を変更して対応できる構造です。現在はMetadataなしで12キー＋Encoder＋Push＋LEDの最大構成として動作します。

## 検証

- [設計・API・設定モデル](docs/architecture.md)
- [実機テスト手順](docs/physical-test.md)
- [検証結果と残タスク](docs/verification.md)
- [参考実装と依存ライブラリ](THIRD_PARTY_NOTICES.md)

PoC2のC6での実機確認結果は参考実装の成立性です。**今回の各ボード向けChainPad Firmware自体の実機受入確認とは別です。**
