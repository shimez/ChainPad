# C5 Phase 2D：機能合格、内部DMAヒープは要調査

COM11。元のv0.8.0、29 Actions。候補通常ビルドのSHAは `results.json`。古いWinMMポートではなくC5側の購読が成立する `ChainPad IN 1` を使用。HTTP入力で子に250 msホールドのペア送信、親の次ActionにOSC。PC実受信時刻（ms）は `pc-ble.json`。Note Off→親OSCは16.68 ms、F13 Up→親OSCは25.26 ms。PC側観測であってFirmware内部時間ではない。

長いOSC文字列を含む224枠を2回保存・GET照合、再起動後にもGET一致。待機中の子を持つ32 InvocationをHTTP statusで確認、StopとPanicを別試行しそれぞれ32→0、取消増分32。Stop後のCall先OSCをPC受信。

`memory.txt` は周期出力OFFの保存後／満枠前後のシリアルm/h（IP表記は置換）。224枠2回目保存後の内部8-bitヒープの累積minFree 9,532 B、loopTaskの最低未使用スタック5,000 B。割当失敗カウンタは開始時0→1回目24→2回目37。先頭4失敗は `caps=0x80c`（INTERNAL|DMA|8BIT）、要求1,678～1,686 Bに対し、その条件でのlargestは1,332～1,588 B。入力・transport overflowは0。割当要求元はこのログでは特定不可。この結果だけでメモリ余裕を合格とはしない。

初回の書き込み試行は消去前のstub起動で停止。手動RESET後、元版・設定を確認。再ペアリング後に8 MiB全flashと設定を改めて退避・照合し、ROM操作の間は実機起動を確認して再試験。終了後は元アプリ領域とLittleFSをflash照合、v0.8.0・capabilities・設定GET一致を確認。NVSの書き戻しなし。バックアップはGit外。
