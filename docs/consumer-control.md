# HID Consumer Control

ConfiguratorのActionで「HID」、Messageで「Consumer Control」を選択します。
1回のActionで押下と解除を送ります。JIS／ASCIIの表示設定には依存しません。

| 操作 | Consumer Usage（page 0x0C） |
|---|---:|
| 音量UP | 0xE9 |
| 音量DOWN | 0xEA |
| スピーカーミュート | 0xE2 |
| 再生／一時停止 | 0xCD |
| 次の曲 | 0xB5 |
| 前の曲 | 0xB6 |
| 停止 | 0xB7 |

例：EncoderをRotation Directionモードにし、CWに音量UP、CCWに音量DOWN、
Encoder PushのPressにスピーカーミュートを設定できます。Release側は空で構いません。
押し続けによる自動リピート・Consumer専用KeyDown/KeyUp・holdMs・修飾キーには対応しません。
マイクミュートは含みません。実際の操作対象や動作は受信OS・アプリに依存します。

## 接続

- S3：USB／BLEを選択可能。
- C5／C6：BLE。USB指定のImportは既存HIDと同様にBLEへ正規化。
- BLEはKeyboard report 1を維持し、Consumer report 2を追加。
- USBはArduino標準Consumer Control descriptorを登録し、TinyUSBの非ブロッキングenqueueを使用。

**BLEのレポート構成が変わります。** 既にペアリング済みでConsumer操作が認識されない場合は、
PC側のBluetooth登録を解除して再ペアリングしてください。

## 保存と互換性

```json
{"protocol":"keyboard","transport":"ble","delayMs":0,"message":"consumer","usage":233,"modifiers":0}
```

schemaVersion/storageVersionは2のまま、既存Action/Config構造体のサイズ・件数は維持します。
既存設定は引き続き読み込めます。Consumerを含む設定は旧Firmwareでは未対応Messageとして拒否されるため、
旧Firmwareへ戻す場合は先にConsumer Actionを削除して保存してください。
Full/Key Preset、通常保存、Wi-Fi設定のみの保存も同じAction codecを使用します。

## 送信・安全停止

Keyboardとは別の小さなConsumer FIFOをUSB／BLEごとに持ちます。
押下・解除の2枠をまとめて確保し、満杯の場合はどちらも追加しません。
送信失敗では先頭を保持し再試行。ChainはConsumer送信待ちの間、後続Actionを待機します。
BLEの購読・世代はKeyboardと独立して管理します。
切断／再購読時は古いqueueを破棄し、新しい接続では最初に解除（usage 0）を送ります。
panicは両transportを、Release Allは指定transportのKeyboardとConsumerを解除します。
解除の送信失敗時もbackend tickで再試行します。

## 検証

- Host：7操作、USB／BLE、保存・再読込、無効usage/modifiers/holdMs拒否、C系列のBLE正規化。
- 押下→解除、連続操作、Keyboard保持との独立、独立購読、FIFO満杯、panic、切断／再接続、Release All、Chain順序。
- Web：Full/Key往復、BLE正規化、エラー拒否、Action要約表示。
- H1〜H4保存保護・既存Host/WebUI回帰、S3/C5/C6 Standardビルド・image検査。

実機でのUSB／BLE受信、ペアリングキャッシュ、OS／プレイヤーごとの動作は別途確認が必要です。
