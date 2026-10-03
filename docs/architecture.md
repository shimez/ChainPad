# Phase 1 Architecture

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

Phase 1では設定は28個のChainを持ち、各Chainに最大8 Actions。ハードウェアのないキーを省くMetadataはまだ導入していません。将来もEngineへROW/COLを持ち込む必要はありません。

## 実行・スレッド

- GPIO interrupt: encoder quadrature遷移を蓄積。送信しません。
- `esp_timer` 1ms callback: input scannerを通知。
- input scanner task: キー10ms debounce、Push、encoderを論理イベントへ変換し、128件のFreeRTOSキューへ投入。
- Arduino loop task: Web API、入力イベント消費、Engine、各Backend、出力FIFOを処理。
- NimBLE/USB callbacks: 接続・購読generationをatomic変数へ反映。ActionやConfigを直接操作しません。

Configの適用、Action実行、Backend状態更新はloop task内で直列化されています。GPIOスキャナはConfigを参照しません。したがって保存中に半分更新されたChainを実行しません。

HTTP処理中もスキャンは継続しますが、dispatchはHTTP処理後になります。入力がキュー容量を超えたらカウンタを増やして全出力Panicを行い、欠落Releaseによる押しっぱなしを回復します。

## Backend

- OSC: 最大97-byte Address領域（終端含む）、65-byte String領域。4-byte alignmentとbig-endian。型タグ `i/f/T/F/s`。各Actionは1個の引数。
- MIDI: USB/BLEごとにFIFO。Note On/Off/CCの3-byte Channel Voice。ユーザー表示はChannel 1–16、wireは0–15。送信失敗時はFIFOの先頭を保持。
- HID: USB/BLEごとの押下Usage集合と修飾キー集合。最大6キー。KeyUpは該当Usageのみ解放、Release AllはTransport内の全キー解放。状態遷移ごとの8-byte reportをFIFOに記録するため、短いPress/Releaseも最終状態だけに潰しません。
- Shared transport: `transports.cpp` だけがUSB/NimBLEを初期化。BLEは1つのserver、MIDI serviceとHID serviceの2種類。Phase 1は同一PC/centralとの利用を想定します。

S3ではUSB CDCの自動起動を無効化し、明示的にCDCを作成してからMIDI/HIDと一緒にUSBを起動します。これによりdescriptorと製品名を起動前に設定できます。TinyUSBのnonblocking HID enqueue APIを使い、転送完了待ちtimeoutを送信失敗と誤認しません。C3/C6/C5はboard variantのUSB Serial/JTAG consoleを使用します。

BLE advertisingは31-byte制限内で両Service UUID・Appearance・Flagsを格納し、名前はscan responseへ分離します。MIDI/HIDそれぞれのonSubscribeとdisconnectでgenerationを更新し、loopが切断状態を観測できないほど短い再購読でもローカル状態をリセットします。

Panicまたは再接続時、MIDIは全チャンネルのSustain Off/All Sound Off/All Notes Off、HIDは空report。未送信の過去イベントは破棄します。これは状態初期化であり、送信結果の端末間ackではありません。

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

Delayは予約フィールド `delayMs: 0`。Phase 1では非ゼロを拒否します。将来のDelay Action追加時はProtocol enum/dispatchとConfigurator、必要ならschema migrationを拡張します。

### 検証と永続化

リクエスト上限48000 bytes。型、範囲、Protocol/Transportの組み合わせ、IPv4、重複Input、Action数を検証します。未知schemaVersionは拒否します。String長はUTF-8のbyte数です。全OSC Action共通の送信先はDNS待ちを避けるため数値IPv4です。

検証後に正規化JSONを作り、専用128 KiB `config_nvs` の1つのNVS blobへ書き込みます。NVS stringの約4 KiB制限を避けています。保存成功後だけruntime Configへ適用します。保存失敗時は以前のruntime設定を維持します。起動時に読み込み/検証が失敗すると、空Chainの既定設定で起動しbootMessageに理由を表示します。

S3/C5は8MB Flash内のapplication 6MB＋config_nvs（0x610000）、C3/C6は4MB Flash内のapplication 3MB＋config_nvs（0x310000）です。いずれもapplication offsetは0x10000。

### API

| Method | Path | 内容 |
|---|---|---|
| GET | `/` | 埋め込みConfigurator |
| GET | `/api/capabilities` | ボード名、USB MIDI/HID可否、Transport選択肢・既定値 |
| GET | `/api/config` | 完全な設定（資格情報を含む） |
| PUT | `/api/config` | 検証→保存→適用→Panic。SSID/Password変更は再起動で反映 |
| GET | `/api/status` | 接続、静的ハードウェア情報、実行/失敗/overflow/retry、最終Action |
| POST | `/api/trigger` | `{"input":0}`：保存済みChainを実行 |
| POST | `/api/panic` | MIDI/HID状態リセット、入力キュー破棄 |
| POST | `/api/restart` | Panic後に再起動 |

Action結果 `accepted/unavailable/busy/failed` は直近1件と累積カウンタです。USB/BLEの接続表示はmounted/subscribed状態であり、相手アプリが受信していることまでは示しません。
