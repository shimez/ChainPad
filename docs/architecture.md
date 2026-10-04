# Phase 2 Architecture

## Input / Action境界

`inputs.cpp` だけが `hardware.h` を参照します。キーはrow-majorでKEY 01=ROW0/COL0、KEY 12=ROW3/COL2。非選択行はopen-drain HIGHで開放します。

`InputEvent { input, timeMs }` は論理IDとスキャン時刻です。Engineは物理構造を参照せず、設定されたChainを登録順にdispatchします。

## APキャプティブポータル

Setup APの起動成功後、Arduino DNSServerのワイルドカード応答（TTL 0）でドメイン名をAP IPへ解決します。このバージョンのDNSServerはAsyncUDPで処理するためloopでのDNSポーリングは不要です。

AP側へのHTTP GETではルートと未知のパス（Android/Apple/Windowsの接続確認URLを含む）から `http://<AP IP>/?setup=wifi` に302リダイレクトします。接続先local IPでAPアクセスを判定し、STA側の未知パスと未知の `/api/` は404を維持します。ポータルは `web/wifi.html` のWi-Fi専用画面で、SSID・パスワード・保存して再起動ボタンのみを表示します。`GET /api/wifi` はSSID/Passwordを返し、`PUT /api/wifi` は現行設定のWi-Fi情報だけを更新・検証・保存し、成功時に再起動します。通常のConfiguratorはSTA側のルートまたは `/configurator` から開けます。DNS開始結果はstatusの `captivePortal` に返します。OSによる自動表示は端末設定に依存するため、HTTPのAP IP直接入力でも利用できます。

## ボード能力とTransport

`capabilities.h` がコンパイル対象SoCからUSB MIDI/HID能力と機種名を決定します。S3のみUSB OTGのMIDI/HIDを組み込み、C3/C6/C5ではTinyUSB MIDI/HIDのコードをコンパイル対象から外します。これらのUSB Serial/JTAGはコンソールとして使います。

`GET /api/capabilities` は `hardware`, `usbMidi`, `usbKeyboard`, `defaultMidiTransport`, `midiTransports`, `keyboardTransports` を返します。Configuratorは設定読み込みと同時にこの情報を取得し、Transportの選択肢と既定値を決定します。C3/C6/C5はdisabled selectに「BLE（固定）」を表示し、画面の構成は共通です。

Firmware側もConfig検証時に、C3/C6/C5のMIDI USB/BothおよびKeyboard USBをBLEへ正規化します。これは有効な既存設定の機種適応であり、未知のTransport文字列やKeyboard Bothは引き続き拒否します。保存/起動後の `/api/config` は正規化された設定を返します。S3では既存Transportを保持します。

input scannerは `ARDUINO_RUNNING_CORE` を利用し、C3/C6/C5の単一コアでも有効なCPU上に配置します。各機種のD0–D10はArduino board variantの定義に従います。

| Event ID | 意味 |
|---|---|
| 0, 1 | KEY 01 Press, Release |
| 2, 3 … 22, 23 | KEY 02 … KEY 12 Press, Release |
| 24, 25 | Encoder Push Press, Release |
| 26, 27 | Encoder CW, CCW |

設定は28個のChainを持ち、キーとEncoder PushのPress / Releaseは合計16 Actionsを共有します。各側は0〜16件、Encoder CW/CCWは各8件です。全体最大224件は変わりません。ハードウェアのないキーを省くMetadataはまだ導入していません。将来もEngineへROW/COLを持ち込む必要はありません。

## 実行・スレッド

- GPIO interrupt: encoder quadrature遷移を蓄積。送信しません。
- `esp_timer` 1ms callback: input scannerを通知。
- input scanner task: キー10ms debounce、Push、encoderを論理イベントへ変換し、128件のFreeRTOSキューへ投入。
- Arduino loop task: Web API、入力イベント消費、Engine、各Backend、出力FIFOを処理。
- NimBLE/USB callbacks: 接続・購読generationをatomic変数へ反映。ActionやConfigを直接操作しません。

Configの適用、Action実行、Backend状態更新はloop task内で直列化されています。GPIOスキャナはConfigを参照しません。したがって保存中に半分更新されたChainを実行しません。

HTTP処理中もスキャンは継続しますが、dispatchはHTTP処理後になります。入力がキュー容量を超えたらカウンタを増やし、実行中止・入力出力キュー破棄・HID解除を行います。MIDIメッセージは生成しません。

## Backend

- OSC: 最大97-byte Address領域（終端含む）、65-byte String領域。4-byte alignmentとbig-endian。型タグ `i/f/T/F/s`。各Actionは1個の引数。
- MIDI: USB/BLEごとにFIFO。Note On/Off/CCの3-byte Channel Voice。ユーザー表示はChannel 1–16、wireは0–15。送信失敗時はFIFOの先頭を保持。
- HID: USB/BLEごとの押下Usage集合と修飾キー集合。最大6キー。KeyUpは該当Usageのみ解放、Release AllはTransport内の全キー解放。状態遷移ごとの8-byte reportをFIFOに記録するため、短いPress/Releaseも最終状態だけに潰しません。
- Shared transport: `transports.cpp` だけがUSB/NimBLEを初期化。BLEは1つのserver、MIDI serviceとHID serviceの2種類。Phase 1は同一PC/centralとの利用を想定します。

S3ではUSB CDCの自動起動を無効化し、明示的にCDCを作成してからMIDI/HIDと一緒にUSBを起動します。これによりdescriptorと製品名を起動前に設定できます。TinyUSBのnonblocking HID enqueue APIを使い、転送完了待ちtimeoutを送信失敗と誤認しません。C3/C6/C5はboard variantのUSB Serial/JTAG consoleを使用します。

BLE advertisingは31-byte制限内で両Service UUID・Appearance・Flagsを格納し、名前はscan responseへ分離します。MIDI/HIDそれぞれのonSubscribeとdisconnectでgenerationを更新し、loopが切断状態を観測できないほど短い再購読でもローカル状態をリセットします。

実行中止または再接続時、未送信の過去イベントを破棄し、HIDは空reportを送ります。MIDIは保存・実行中止・接続・キューoverflowのいずれでも自動メッセージを生成しません。明示的に設定されたCC Actionは通常どおり送信します。HID送信は端末間ackではありません。

## Config schemaVersion 1

```json
{
  "schemaVersion": 1,
  "network": {
    "ssid": "",
    "password": "",
    "oscHost": "192.168.1.100",
    "oscPort": 9000
  },
  "chains": [
    {
      "input": 0,
      "actions": [
        {"protocol":"osc","transport":"wifi","delayMs":0,"address":"/Costume","type":"int","value":2},
        {"protocol":"midi","transport":"usb","delayMs":0,"message":"noteOn","channel":1,"number":48,"value":127},
        {"protocol":"keyboard","transport":"ble","delayMs":0,"message":"keyDown","usage":104,"modifiers":0}
      ]
    }
  ]
}
```

上記は説明用の抜粋です。APIへ送る完全な設定にはinput 0–27を重複なく1回ずつ含めます。空Chainは `actions: []`。

MIDI `message`: `noteOn`, `noteOff`, `cc`。`value` はVelocityまたはCC Value。NoteOnは1–127、他は0–127。

MIDI `transport`: `usb`, `ble`, `both`。S3の新規Actionと設定例の既定値は `both`、C3/C6/C5は `ble`。APIでMIDIのtransportを省略した場合も同じ既定値になります。S3では明示された既存usb/bleを維持し、C3/C6/C5ではBLEに正規化します。BothはUSB/BLEにそれぞれ1回dispatchし、各FIFOで独立に再送します。少なくとも片方がAcceptedならAction全体をAcceptedと数え、両方未接続ならUnavailable、受付成功がなく送信側の失敗があればFailedです。Keyboard/OSCにはBothを許可しません。

Keyboard `message`: `keyDown`, `keyUp`, `releaseAll`。`usage` はKeyboard Usage Pageの4–115（F13=104）。`modifiers` はbit 0–7 = Left Ctrl, Shift, Alt, GUI, Right Ctrl, Shift, Alt, GUI。KeyUpでは保存済みの該当Usageの修飾キーが解放されます。Release Allでもスキーマ上usage/modifiersを保持しますが実行時には無視します。

Waitは `{"protocol":"wait","delayMs":100}`。0〜86400000 msの整数で、Transportを持ちません。他のActionのdelayMsは0です。

Engineは最大32個の独立した実行状態（入力ID・次のAction位置・再開時刻）を固定配列で保持します。Config/Chainの複製はありません。同じ入力の再発火も新しい実行となります。Wait期限をmillisの差分で判定し、期限到達後は登録順に進みます。連続Waitはそれぞれ実際に到達した時点から計時します。0 msはその場で次へ進みます。PressとReleaseの交錯を並べ替えません。同時再開は実行登録順です。

待機枠が満杯の場合、新規の非ゼロWaitを含むChainは先頭Actionの出力前に全体を拒否します。即時Chainは実行できます。statusのrunningChains/rejectedChains/cancelledChainsで確認できます。設定保存・Panic・Restart・入力overflow時は全実行を中止します。HTTP処理は同じloop上にあり厳密な時間精度は保証しません。

### 設定ファイル

共通Action/Chain構造を `web/presets.js` で検証します。Key Presetはformat=`chainpad-key-preset`, version=1, chains=[{input:0,actions:[…]},{input:1,actions:[…]}]。0/1は対象キーのPress/Releaseへ対応します。

全体ファイルはformat=`chainpad-configuration`, version=1とschemaVersion/network/chainsを持ちます。全28 EventsとOSC送信先を含み、networkにはoscHost/oscPortのみ出力します。SSID・パスワードはExportに含めず、Import時は現在の編集画面のWi-Fi設定を保持します。以前のファイルにWi-Fi設定が含まれていても無視します。Importはブラウザ上で全件検証してから編集へ一括反映します。1 MiBのファイル上限があります。Firmwareへは既存のEvent単位保存APIで送信し、全体JSONをデバイスRAMへ持ち込みません。Action順は保持し、Event IDのみ所定の位置へ対応づけます。

### 検証と永続化

内部保存は **LittleFSのみ**。`settings` partitionの `/config.records` に、storageVersion 1とNetworkを含むヘッダー1行＋入力ID順の28個のChain JSON行を保存します。外部GETのschemaVersion 1 JSONとは独立した内部形式です。旧形式の読み込み・移行・fallbackはありません。

ブラウザ側で設定をsnapshotし、Network（最大2048 bytes）をbegin、各EventのChain（最大16384 bytes）を順にstage、最後にcommitします。FirmwareはNetworkまたは1 Chain分だけを検証・正規化して `/pending.records` へ追記します。Config全体の複製も全体JSONのRAM展開もありません。String長はUTF-8のbyte数で、Protocol/Transport、IPv4、入力ID順序、Action数、型・範囲を検証します。新しいbeginは以前のtokenを無効化し、途中失敗した保存は次のbeginからやり直します。

commitは全レコードを逐次再検証した後、LittleFSのrename-over-existingで一括確定します。通信中断・書き込み失敗・再起動の途中ファイルはactiveとして読まれません。検証・rename失敗までは以前のディスク設定とruntimeを維持します。確定後も1 Chainずつ適用します。確定後の読み出し自体に失敗した場合は空Chainへ戻してエラーを返します。保存・適用中にEngineを並行実行しません。

起動時も検証パス→適用パスで逐次読み込みます。activeがない場合は既定値、破損の場合は空Chainで起動しbootMessageに理由を表示します。初期フォーマットはpartition全体が消去状態の場合だけ行い、mountできない非消去領域を自動消去しません。

`GET /api/config` はNetworkと各ChainをHTTP chunkで順次返します。Wi-Fi専用保存も同じレコード形式・トランザクションを使い、変更対象以外を1 Chainずつ転記します。48KBの設定全体サイズ上限はなくなり、一時RAMは全体サイズではなく1 Chainのサイズで制限されます。今後Event数を増やしても保存時の一時RAMは増えません。Action数を増やす場合は1 Chainの上限と実行用Configの静的RAMも評価が必要です。

S3/C5はapplication 6MiB＋LittleFS 1856KiB（0x630000）、C3/C6はapplication 3MiB＋LittleFS 832KiB（0x330000）。application offsetは0x10000です。新しいpartition tableを必ず書き込みます。

### API

| Method | Path | 内容 |
|---|---|---|
| GET | `/` | 埋め込みConfigurator |
| GET | `/api/capabilities` | ボード名、USB MIDI/HID可否、Transport選択肢・既定値 |
| GET | `/api/config` | 完全な設定（資格情報を含む） |
| POST | `/api/config/begin` | Network JSONを検証し保存tokenを返す |
| PUT | `/api/config/chain?token=…&input=…` | Event ID順に1 Chainを検証して仮保存 |
| POST | `/api/config/commit?token=…` | 全件再検証→LittleFS rename→適用→Panic。Wi-Fi変更は再起動で反映 |
| GET | `/api/status` | 接続、静的ハードウェア情報、実行/失敗/overflow/retry、最終Action |
| POST | `/api/trigger` | `{"input":0}`：保存済みChainを実行 |
| POST | `/api/panic` | 実行中止・入力出力キュー破棄・HID解除（MIDI送信なし） |
| POST | `/api/restart` | Panic後に再起動 |

Action結果 `accepted/unavailable/busy/failed` は直近1件と累積カウンタです。USB/BLEの接続表示はmounted/subscribed状態であり、相手アプリが受信していることまでは示しません。
