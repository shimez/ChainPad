# C6：保存内部のメモリ（2026-11-23）

`feature/call-chain`、ベース `19c6fad`。診断専用 `pio run -e xiao_esp32c6_save_mem`。固定表8行に `heap_caps_get_info(INTERNAL | 8BIT)` のfree・largestを別々に最小値集計。計測点では出力・確保なし。115200 baudの `v` で表をリセットし、1保存ごとに `V` で停止・表示。`id` は論理入力、28は回転設定、255は保存ヘッダー。`file-written` はpendingへのシリアライズと改行出力後、flush/close前。失敗経路・アロケータ内部の連続トレースではない。

条件：既存85枠設定を退避。OBS Readyで各保存を開始、周期診断OFF。通常設定→前回と同じ224枠（長いOSCアドレス・文字列）→同じ224枠の再保存。各回GET一致。保存後に `m/h` を採取。生の採取値は `serial.log.txt`（IP表記のみ置換）。

| 保存 | 採取点最小free (B) | 採取点最小largest (B) |
|---|---:|---:|
| 通常 | 76,980（file-written） | 56,308（stage-decoded/encoded/file-written） |
| 224枠・初回 | 60,864（stage-decoded） | 40,948（file-written） |
| 224枠・2回目 | 61,400（file-written） | 37,876（stage-decoded/encoded/file-written） |

前回の保存外側の観測最小free 79,788 B・largest 44,020 Bから、それぞれ18,924 B・6,144 B低い値を観測。別ビルド・別起動でのサンプル差。診断表・通信状態・断片化も異なるので因果的な固定差ではない。2回目のGET後はlargest 60,404 Bに戻った。commit検証・適用時のChain読込のlargestは両試行とも53,236 B以上。保存後の累積minFreeは56,168 Bであり、個々のチェックポイントのfreeとは区別する。

元アプリ領域 `0x10000` から `0x300000` バイトを退避し、書き込み前後に実機と照合（SHA256 `a64b6d8f53f6bf4d75555f2edad3b0daab4cc6a544346fd1dce10ea60078044e`）。設定・アプリを復元し、再起動後に設定GETが退避内容と一致、0.13.0・ready・稼働0を確認。設定・バイナリはリポジトリ外。
