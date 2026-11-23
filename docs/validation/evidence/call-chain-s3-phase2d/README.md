# S3 Phase 2D：合格

S3は内部Engine 9,548 Bを維持。`normal-get.json` はC5と同じ224 Action設定をWi-Fi・BLE MIDI/HID接続で保存・再起動後に照合した記録。GET単独4回・保存後GET3回の全内容一致、全測定区間で割当失敗増分0。内部DMA最大連続空きの区間境界値27,636～31,732 Bは、送信中の瞬間下限ではない。

`normal.json` は27段Call・親子Loop・OSC FIFO・32枠Stop/Panicの実行結果。32枠時のloopTask最小余裕4,052 B、OBSワーカー5,984 B。`ble-events.json`・`usb-events.json` は各試行開始からのPC受信時刻（ms）で、MIDI Note OffとF13 Upが対応する親OSCより先着。別途、実OBSの8シーンとEdgeの選択欄が一致した。

事前に全8 MiBを退避・照合し、終了後COM12で元アプリ・LittleFSのdigest、COM13復帰後に元v0.5.3・設定・capabilities・ready・稼働0を確認。実設定とFlashバックアップは含まない。
