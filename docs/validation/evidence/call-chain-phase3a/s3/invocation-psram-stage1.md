# S3 第1段階：Invocationプールの外部配置（実施前の計画）

以下は実施前の条件・静的確認。実施後の判定は[第1段階の最終結果](invocation-psram-stage1-results.md)を参照。

比較元は `f78d912` 通常版、[対策前A/B](ab224-results.md) と[割当失敗の分析](obs-dma-investigation.md)。OBS Ready共存負荷の既存FAILを維持する。単回の非再現やビルド結果で判定を置き換えない。

## 実装と安全性

- `src/engine.h` の外部プール条件に通常S3を追加。C5と同じ `Engine::begin()`、同じ型・個数（32×296 B）、同じアロケータ・寿命。移すのは `Invocation` の配列で、Engine本体・FIFOの順序表・統計は内部のまま。C3/C6とHostは従来の内蔵配列。保存形式やCall/Loopの実行コードに差分なし。
- 初期化はグローバルコンストラクタではなく、PSRAM登録後のsetupで1回のみ。`SPIRAM | 8BIT` 指定の確保成功時にだけplacement newでゼロ初期化する。失敗時は`jobs == nullptr`、試行済みフラグにより再試行せず、内部RAMへのフォールバックもない。
- `main.cpp`は失敗をbootMessageへ記録し、入力開始と出力を抑止。`engineReady`／`configOutputsAllowed`はfalse、実行可能な設定でのAPI triggerは503。`triggerImpl`・`tick`もready判定がslot参照に先行する。`cancelAll`は個数と統計のみを更新する。診断はnullチェック後にのみslot端点を参照。これは失敗分岐の静的確認で、実機での割当失敗注入ではない。
- `src/diagnostics.cpp`：`Engine slots` の端点診断はS3でも有効。OBS assembly用外部配置診断だけをC5限定に保つ。S3のOBS assemblyは静的内部16,400 B、GETの `JsonDocument` は従来の既定アロケータ。総空き／DMA最大連続空きの改善量をELFの縮小量から決め打ちしない。

## PC上の検証結果

`tests/run_host.ps1` は通常・trace・機種別を含めPASS（Hostは内蔵配列のためPSRAM動作そのものの証拠ではない）。PlatformIOの通常4環境と `tests/check_image.py` の4機種検査はすべてPASS。`tests/measure_engine_layout.py` のELF/DWARF計測：

| 通常環境 | Invocation (B) | 内部Engine (B) | セクション |
|---|---:|---:|---|
| S3 | 296 | 80 | `.dram0.bss` |
| C5 | 296 | 80 | `.dram0.bss` |
| C3 / C6 | 各296 | 各9,548 | `.dram0.bss` |

S3 mapではEngineが0x50 B、`assemblyStorage` が0x4010 B。Engine単体は対策前比9,468 B減（ポインタ・初期化状態等を残す）。PlatformIO RAM集計は147,820→138,348 B（差9,472 B）。後者はリンク時の集計であり、実行時のDMA空き増分ではない。S3通常アプリSHA-256 `dc21cbdd1dd3e4e8a2f2a1a9967acbd3af57a0846c0c2a2416ca7047584fcc41`。未書き込み。

## 実機検証計画（書込みは別承認）

1. **個体・配置・共通条件。** 元の8 MiBバックアップとfactory/settings抽出物、元版 `v0.5.3` と比較用の正式版を区別する。承認後のみ書込用CDCで個体とアプリSHAを照合してアプリ領域だけ書き、通常通常CDCへ。各起動で224件の全内容・正規化ハッシュ、`engineReady`、`Engine slots: bytes=9472 count=32 externalRange=1`、有効なbaseと起動中の不変性を確認。Wi-Fi・USB MIDI/HID・BLE MIDI/HIDを実際に開き、OBS状態を明記する。同一PC・AP・個体・設定・保存履歴・OSC送信順で比較。
2. **先行ゲート：OBS Disabledで独立2起動。** [従来のゲート](candidate-gate.json)のfixtureと段取りで、起動ごとに静止/status、単独GET×4、保存→GET×3。第2起動は再保存前に永続設定一致を確認する。すべての区間境界で内部8-bit free/min/largest、内部DMA largest、PSRAM free/min/largest/allocated、stack low-water、失敗件数、overflow、uptimeと接続を記録。両起動とも割当失敗0、全設定一致、配置・接続維持、予期しない再起動・overflowなしが後続への条件。
3. **OBS Ready共存：独立3起動。** 対策前のA/Bランナーと同じCallなし224件（208 OSC＋16 Wait）を使い、起動ごとに引継ぎGET→同じ設定を1回保存→2秒静止→GET単独→OSC単独→GET＋OSC。受信器を先に開き、並行GETは先頭電文で開始。各列208/208件をバイト・順序・件数で照合、GET中の実受信が1件以上であること、全GET一致、各区間の割当失敗増分0を要求する。必要な境界診断を欠けば未判定。PC時刻・raw電文・GET開始終了・照合結果・診断原文はassert前に逐次保存する。
4. **比較方法。** [旧候補](ab224-candidate.json)と[正式版](ab224-release.json)に対して同じ条件・境界ごとの内部総空き、起動内累積min、DMA largestを起動別に並べる。Ready保存後静止の内部freeは旧候補43,864 B、正式版51,808 B。別起動のminを差分カウンタとして扱わず、正式版のDMA専用値は未測定のまま。境界largestと旧失敗瞬間948 Bは別指標。配置確認と実際の負荷時改善を別々に評価する。
5. **実行回帰。** 27段Callで1枠・逆順復帰、親子Loop・並列FIFO、250 ms Waitはtrigger前からUSB MIDIを受け、両CCの順・件数・間隔を記録。USB/BLE別に子末尾MIDI/HIDペアと親OSCを照合（欠落・重複・解放漏れNG、異経路のPC到着順は要求しない）。31→32枠から33件目拒否・先頭出力なし、即時実行とStopによる32取消を物理確認。Panicで32枠取消、USB/BLE別のキー解放、保留MIDIの再送なし・新規CCと明示Offを確認。終了時は稼働0・全解放・新規overflow/割当失敗0。
6. **停止・復元。** 新規割当失敗、配置不一致、設定不一致、異常再起動・出力異常があれば証拠を保存して後続を止める。全試験終了後の元アプリ・LittleFSの復元、両領域digestと起動後の全設定・接続状態の照合は[既存計画](plan.md)に従う。今回の実装レビューは実機操作の承認ではない。
