# C6 Phase 3A — 進行中

基点 `7308e474be4af031adb660bee9644c9ba0f76caa`。候補は通常の `xiao_esp32c6` に、`src/diagnostics.cpp` の既存DMA表示ガードへ `|| defined(CONFIG_IDF_TARGET_ESP32C6)` を追加したもの。取得値は `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT)`。候補アプリ SHA256 `c5d22b8b39f6c5796f6ee9fe9efa925059ff2c52875e7986dbbf4d33c1afa321`。区間境界値は区間内の下限を保証しない。正式版の内部8-bit値をDMA値として比較しない。

`run_host`・`run_obs`・`run_storage_fault`、4機種の通常ビルド、ELFとイメージ検査はPASS。C6のInvocationは296 B、内部Engineは9,548 B。

実機の元Flash 4 MiBを退避し全域照合、パーティション表からアプリとLittleFSを分離保存。公開設定・capabilitiesは再起動後の再取得とも一致。GitHub正式版 `ChainPad-ESP32-C6.bin` のSHA256は `1f369a7f446ff2dc011945c8fb2d7183d715b7b34a86470822567461e091a24c`。配布イメージの0x10000から末尾までが退避したアプリの対応バイトと一致したので、そのまま基準版として使用。元設定と生のFlashはGit外。Callなし物理入力fixture（入力番号をOSC intで送信）を保存・GET照合し、PC受信をAPI入力1件で予備確認。物理試験のPASSとは別。
# 物理操作の途中結果（C6総合判定は保留）

交互押しの正式版初回は操作数未確定で68/72。原因を特定できないため初回記録を保持し、交互押しだけを再測定した（10組＋終了マーカーの42/42）。候補版は全入力72/72で実機events差分も72。RotationValueのStopでは候補版のevents差分12（10ノッチ＋終了キーpress/release）に対して受信は値8件＋終了マーカー1件で、正式版と同じ列。候補版Stop区間の割当失敗・入出力overflow増分は0。DMA largestの境界値は69,620→67,572 B。
# 32枠物理試験の初回は測定不成立

満杯準備時点では uptimeMs=89219、runningChains=32。しかし別プロセスのUDP受信開始時に32枠の事前条件を満たさず中止。その後のstatusはuptimeMs=27161、events=0、runningChains=0。少なくとも間に再起動があり、物理キーが拒否／取消された証拠ではない。直前の計測はUSBシリアルも開閉しており、リセット原因は未確定。再試験ではシリアルを操作せず、同一プロセス内でUDPをbindしてから32枠を埋め、statusで成立を確認する。
# 候補版・混在／停止／保存の確認

物理満杯再試験の `retry-full-result.json` は同一bootで拒否+1・取消+32、PC受信は91→92のみ、押下残り0。別試行 `candidate-panic.json` はF13をPCで押下確認後に32枠を埋め、Panic後のF13解放・稼働0を確認。実OBS Ready＋BLE接続時の `candidate-obs-overlap.json` は208/208連番順序一致、GET一致、最大到着間隔約1.64秒、計測区間の割当失敗増分0。この間隔を性能同等性の証明には使わない。

224 Actionの再起動後GET一致。実機配信のConfiguratorをEdgeで開き、Full/KeyのExport BlobとファイルImportを往復し、別キーへの貼付でも絶対targetInput=27を維持、自己循環Importで編集中設定が不変であることを確認。Fullを戻してWebUI保存後、再GETが元の224 Action設定と一致した。
