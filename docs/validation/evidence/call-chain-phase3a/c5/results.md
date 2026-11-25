# C5 Phase 3A 統合回帰：総合HOLD、S3への移行は条件付きPASS

レビュー確定。C5固有のSTA再接続は未検証として持ち越すが、S3への移行を妨げない。これはS3の試験結果ではない。C5の実機試験と試験前状態への復元は終了した。

候補 `574b939`（C5通常版）。Callなし比較は正式版 `v0.13.0`、復元先は試験前の `v0.8.0`。C6判定とは独立。ケース経過は[進捗記録](progress.md)、期待値は[計画](plan.md)に記載。

| ケース | C5での結果 |
|---|---|
| 配置・224枠設定 | PASS。Invocation 32枠とOBS assemblyの全域がPSRAM。OBS無効・BLE両購読の2起動で設定GET各4回と保存→GET各3回が一致。[29診断点](gate-obs-disabled.jsonl)で割当失敗0。JSON allocatorの詳細計測はPhase 2Dの引用で、今回の内部直接計時ではない。 |
| Call・Wait・Loop・停止 | PASS。27段Callが1枠で逆順復帰。親子Loop、共有子、同時実行、FIFO、[32枠満杯時の拒否・即時出力・Stop](physical-full.json)、32枠＋F13保持からのPanicを確認。 |
| OSC・入力・Rotation | PASS。物理72/72件、Stop／Wrap、OSC float・CC23・OBS音量の各段が正式版と一致。224枠GETとOSC併用で208/208件、設定一致。 |
| MIDI・HID・Consumer | 出力・解放PASS。新基準の内部復帰順はソース確認のみ。旧PC順序基準のFAILは維持。[Callあり／なし各20試行](pair-order-review.md)でCallなしでも逆転。欠落・重複・解放漏れなし。 |
| OBS | PASS。状態読戻し、シーン・グループ、media操作、専用プロファイルで録画・ローカル配信・VirtualCam・ReplayBuffer。WebUIの実シーン候補8件をOBSの名前集合と照合（最初の汎用select検査は対象不明のため採用せず、表示中のシーン選択欄に限定）。 |
| 設定管理 | PASS。Full/Key往復、絶対target貼付、Default/Empty、無効Import後の編集保護、範囲外stage／循環commit拒否後のactive保護。保存・再起動後の全体照合とCall先出力。 |
| BLE／OBS再接続 | 同一bootで個別切断・再接続を確認。BLE復帰後に新規MIDI/HID/OSC、OBS復帰後にOBS8操作とOSCを実受信。BLE復帰試験で一度複合status検査不成立、原因未特定。設定を変えずに出力前後の条件一致を再確認した。 |
| Wi-Fi STA再接続 | **HOLD（環境制約）**。個別切断困難との回答を受け、共有APを止めなかった。起動時STA接続成功は同一boot再接続の証拠としない。 |

## 復元

対象USB個体が退避時と一致することを照合。退避検証済み8 MiBコピーからの元factory（6 MiB）とsettings（LittleFS）を復元し、両領域を実Flashと個別digest照合。v0.8.0通常起動、29 Actions設定全体・capabilities・OBS設定の一致、入力ready、Wi-Fi、空Chainの受付、稼働0を確認。試験用OBSプロセスは終了し、OBSは元と同じDisconnected。復元直後はWindowsのBLE接続だけが試験前と異なっていた。NVS・ペアリング情報の上書きはしていない。
ユーザーによるBLE切断後、元版のHTTP GETのみで最終照合した。BLE MIDI/HIDはいずれも未接続、Wi-Fi接続、OBS Disconnected、ストレージ・入力ready、稼働0。機種・版・設定全体・capabilities・OBS設定を含め、照合対象はすべて試験前と一致した。復元完了。総合HOLDの理由はSTA個別再接続の未検証であり、復元の不一致ではない。
