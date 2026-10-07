# ChainPad

**1つのキーで、OSC / MIDI / HIDをまとめて操作。**

ChainPadは、複数の操作を組み合わせて実行できる、12キー＋ロータリーエンコーダーのキーパッドです。VRChatの演出・アバター操作から、音楽ソフトやOBSの操作まで、よく使う機能を手元のキーにまとめられます。

このリポジトリでは、XIAO ESP32シリーズ向けのFirmwareとブラウザ設定画面を開発・配布しています。現行ChainOSCPad PCBのハードウェア構成に対応しています。

**[Getting Started](https://shimez.github.io/ChainPad/getting-started/)** — 接続・設定・使い方

[ChainPadポータル](https://shimez.github.io/ChainPad/) — 紹介動画・製品概要

## ChainPadでできること

| 機能 | 用途の例 |
|---|---|
| **OSC** | Wi-FiでOSCメッセージを送信。VRChatのアバターパラメーターなど、OSCに対応する機能を操作 |
| **MIDI** | Note On／Off・Control Changeで、音楽ソフトやMIDI対応ワールドを操作 |
| **Keyboard HID** | キーボード入力・ショートカットで、OBSのシーン切り替えやマイクのミュートなどを操作 |

利用するアプリ・ワールド側でも、受信設定や機能の対応が必要です。

## 主な特徴

- **操作を組み合わせる**：OSC・MIDI・HIDを1つのAction Chainに登録できます。
- **押す／離すを個別に設定**：キーとEncoder PushのPress／Releaseに、それぞれ動作を設定できます。
- **操作の間にWaitを挟む**：Chainの途中に待機時間を設定できます。
- **ブラウザで編集・本体に保存**：専用設定アプリのインストールは不要です。
- **設定を再利用**：キー単位・全体設定のImport／Exportに対応しています。

## Action Chainとは

1つの入力に対して、複数のActionを登録順に実行する仕組みです。OSC・MIDI・HID・Waitを自由に組み合わせられます。

例えば、1つのキーに次のような動作を設定できます。

```text
キーを押す（Press）
  → OSCでパラメーターを変更
  → MIDI Note Onを送信
  → キーボードのキーを押す（KeyDown）

キーを離す（Release）
  → MIDI Note Offを送信
  → キーボードのキーを離す（KeyUp）
```

Press／Releaseは独立した設定です。Note OffやKeyUpは自動追加されないため、必要な解除操作も登録します。

キーとEncoder Pushは、Press／Release合計で最大16 Actions。Encoder回転は、時計回り・反時計回りに各8 Actionsを設定できます。

## 対応ハードウェア

現行ChainOSCPad PCBの**12キー・ロータリーエンコーダー・Push・LED**構成に対応しています。

| ボード | OSC | MIDI／Keyboard HID |
|---|---|---|
| XIAO ESP32S3 | Wi-Fi | USB／Bluetooth LE |
| XIAO ESP32C3 | Wi-Fi | Bluetooth LE |
| XIAO ESP32C6 | Wi-Fi | Bluetooth LE |
| XIAO ESP32C5 | Wi-Fi | Bluetooth LE |

C3／C6／C5のUSBは書き込み・コンソール用で、USB MIDI／HIDには対応しません。

### 自作する方へ

配線ガイドは**準備中**です。自作する場合は、Firmwareが想定する配線に合わせる必要があります。現在のピン定義は[`src/hardware.h`](src/hardware.h)にありますが、部品選定や回路図を含む組み立てガイドではありません。

## はじめ方

- **[Getting Started](https://shimez.github.io/ChainPad/getting-started/)**：接続からブラウザでの設定、利用先アプリの設定まで。
- [Web Installer](https://shimez.github.io/ChainPad/installer/)：Firmwareの書き込み・更新。PC版Chrome／Edgeとデータ通信対応USBケーブルを使用します。

## 詳細情報・開発

| 資料 | 内容 |
|---|---|
| [ChainPadReceiver](tools/ChainPadReceiver/README.md) | OSC・MIDI・キーボード入力を確認するPC側検証ツール。通常利用に必須ではありません |
| [リソース診断](docs/resource-diagnostics.md) | シリアルからメモリ・保存領域などを確認する方法 |
| [開発・検証資料](docs/) | 設計資料と各段階の検証記録。過去の仕様を含むため、すべてが現行仕様を示すものではありません |
| [ビルド設定](platformio.ini) | PlatformIOの対応環境・依存ライブラリ。ソースからビルドする方向け |
| [不具合報告・相談](https://github.com/shimez/ChainPad/issues) | GitHub Issues |
| [Third-Party Notices](THIRD_PARTY_NOTICES.md) | 参考実装・依存ライブラリの情報 |

Encoderの回転量に応じて値を扱う**Rotation Valueは開発中**です。現在はPosition更新まで対応しており、OSC／MIDIへの値の送信と専用編集UIは未実装です。
