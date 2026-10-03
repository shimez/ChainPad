# ChainPadReceiver

Phase 1のPC側実機検証ツールです。OSC、複数のOS MIDI入力、直接接続したBLE-MIDI、フォーカス中のKeyboardイベントを同じ時系列で表示します。

## Windowsで起動

Python 3.11または3.12（python.org版、Tcl/Tkを含む）を使用してください。

```powershell
cd tools/ChainPadReceiver
py -3.11 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements.txt
.\.venv\Scripts\python.exe receiver.py
```

1. OSCは既定で `0.0.0.0:9000`。FirmwareのOSC HostをこのPCのIPv4アドレスに設定します。Windows FirewallでこのPythonのプライベートネットワークUDP受信を許可します。
2. **OS MIDI input → Refresh → ポート選択 → Open / Reconnect**。USBだけでなく、Windows側で公開されているBLE-MIDI入力もこの欄で受信します。表示ラベルを `USB MIDI` / `BLE MIDI` に設定しますが、ラベルは受信経路を切り替えるものではありません。複数ポートを同時に開けます。
3. OS MIDI入力ポートを利用できない場合の補助経路として **Direct BLE-MIDI → Scan → ChainPad Chimera → Connect** を用意しています。これはBleakによる広告の検索・通知購読で、Windowsの接続済み機器一覧ではありません。HIDなどで既に接続され広告が停止している機器はScanに表示されない場合があります。
4. BLE KeyboardはWindows側で `ChainPad Chimera` をペアリングします。Direct BLE-MIDIとHIDは同じPCで利用してください。接続・購読状態はConfiguratorにも表示されます。
5. HID test areaにフォーカスするとKeyDown/KeyUpが表示されます。OBSのホットキー動作はOBS側でも確認してください。

## 表示の意味

- Time: PCで受信コールバックを処理した時刻。
- Delta: 直前の**データイベント**からの単調時計ベースの時間差。INFO/ERRORは時間差に含めません。
- OSCはWi-Fi経由を想定したラベル。受信UDPパケットだけで物理ネットワークを判別するものではありません。
- OS MIDIポートのUSB/BLEラベルは手動指定です。ポート名からTransportを断定しません。
- HIDイベントはOS処理後のフォーカス中の入力で、USB/BLEの識別やグローバル監視は行いません。ファンクションキーの名前はOS/Tk依存です。
- FirmwareのAction dispatch順とPC受信順は、各Transportのバッファリングで異なる場合があります。DeltaはFirmware内部の実行時間や厳密な端末間レイテンシではありません。
- 最後の5000行を保持し、CSV Exportできます。受信キューは10000件までで、取りこぼしはQueue dropsに表示します。
- Direct BLEデコーダはPhase 1で使うChannel VoiceとRealtime用です。SysEx/System Commonは対象外としてERROR表示します。
- Direct BLEとOS側BLE MIDIを同時に購読すると重複表示される場合があります。検証ではどちらか一方を選択してください。

OSCを別アプリでも受信する場合はポート競合を避けます。VRChatとReceiverを同時に比較したい場合、Phase 1では送信先が全OSC Actionで共通なので、まずReceiver向けに検証し、次にVRChat向けへ切り替えてください。

## PoC用chainpad_monitor.pyとの対応・再接続

ユーザー提供の `chainpad_monitor.py` は `mido.get_input_names()` / `mido.open_input()` を使用しており、本Receiverの **OS MIDI input** と同じAPI経路です。PoCで使ったBLE入力ポートはまずこの欄で選びます。Direct BLEのScanやペアリング削除は、この経路の通常操作に必要ありません。

本体RestartやUSB再接続後に受信が止まったら、Refreshで現在のポートを選び、**Open / Reconnect** を押してください。選択したポートの古いハンドルを閉じて開き直します。自動再接続ではありません。他の開いているポートは維持します。

以前の実装では開いたポート名が記録されているとOpenを無視していました。この動作をPoC用モニターと同様の明示的再接続へ修正しています。ただし、この差が実機で報告された受信停止の原因だったかは未確認です。

PoC用モニターと比較するときは同時起動を避けてください（OSCポート競合やMIDIポートの占有を避けるため）。ポート一覧が異なる場合は、実行Python環境・Mido backend・選択ポート名も確認します。
