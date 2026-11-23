# C5：v0.13.0とPhase 2D候補の224 Action設定比較

同一C5・COM11。正式版v0.13.0と2D通常ビルド（アプリ `6f96daecfe93d52e67cecda098a2270ef5965bc35a7310dfaa93819423d5f4e7`）を順に測定。各版とも元の29 Actionから開始し、Callを含まない同一の224 Action設定を3回連続で保存。正規化した入力設定のSHA256は両版一致。各試行の開始・終了時にWi-Fi接続・BLE MIDIとHID購読あり、OBS未接続を確認。連続試行であり、独立した3回の再起動試験ではない。

| 版 | 開始→設定GET完了後の累積失敗数（各試行） | 増分 |
|---|---|---|
| v0.13.0 | 0→0、0→0、0→0 | 0、0、0 |
| 2D | 0→14、14→24、24→35 | 14、10、11 |

全6回で保存・GET成功、取得した設定全体が送信内容と一致。`h` の失敗件数を開始、begin、8 Chainごとのstage、rotation、commit、GET後で採取。両版ともcommit後の採取までは増分0。候補の全35件はcommit後の採取からGET後の採取までの区間に集中した。既存フックが保持する候補の先頭4件の要求サイズ／該当capabilityのlargest（B）は1678/1012、1680/1332、1686/820、1686/820。すべて `phase=loop`、`heap_caps_malloc`、`caps=0x80c`（INTERNAL | DMA | 8BIT）。以後のサンプルに再掲された先頭4件を新たな失敗として数えていない。正式版は失敗0のため、失敗時のサイズ・largestは該当なし。

今回の条件では2Dで再現するメモリ回帰。Callの実行自体は不要。GETに伴う送受信処理を含む区間まで特定したが、アロケータの呼び出し元・再試行の仕組み・増加したEngine領域単独の寄与は未確定。データ損失や保存／取得失敗は観測されなかったが、メモリ面の合格判定は保留。

追加診断コードなし。比較終了後、試験前のv0.8.0アプリとLittleFSを退避イメージから復元し、両領域をflash照合。再起動後ready・running=0、公開設定GETと元設定の一致を確認。NVSは書き戻していない。`baseline.json`、`candidate.json` は入力ハッシュ・接続条件・各段階のカウンタと失敗サンプルのみ。

## 保存から独立したGETと割当元の識別

同じ224枠を一度だけ保存（Callなし）、各GET前に1.5秒のアイドルとstatus GETを対照として測定。Wi-Fi接続、BLE MIDI/HID購読あり、OBS未接続。29枠GET・stage～commit・待機4回・status GET4回は両版とも失敗増分0。224枠GETは4回とも設定全体が一致した。

| ビルド | 224枠GETごとの失敗増分 | 初めの4失敗の要求サイズ／そのcapsでのlargest |
|---|---|---|
| 通常2D候補 | 12, 13, 13, 13 | 1,674～1,686 B／1,012～1,204 B |
| v0.13.0 Engine + 7,968 B内部BSS + 診断 | 16, 21, 10, 16 | 1,674～1,686 B／すべて1,524 B |

根拠：`candidate-get-only.json`、`reserved-get-only.json`。仮予約は `0x408223d0`、内部配置をmap・実機で確認。診断版は追加で最初の4件の戻りPC・タスク名を固定表に採取しており、失敗件数差を7,968 Bのみに定量帰属するものではない。

診断版の先頭4件とも `heap_caps_malloc`, `caps=0x80c`, 戻りPC `0x4000f506`。実機C5 revision v1.0に対応するEspressif [ROM ELF 20260528](https://github.com/espressif/esp-rom-elfs/releases/tag/20260528) の `esp32c5_rev100_rom.elf`（配布アーカイブSHA256照合済み）では、直前 `0x4000f504` は `esf_buf_alloc_dynamic+0xce` の `jalr a5`、戻り先で結果をNULL判定する。4件のタスクはloopTask 3・tiT 1。同アドレスのrev0用ELFは異なるため使用しない。直接の要求元はWi-Fiの動的フレームバッファ割当で、観測時は空き合計4,628～7,664 Bでも必要な連続DMA領域が足りない。通常候補版そのものではPC採取なし。JSON生成中の他の割当の間接寄与までは分離していない。

追加試験終了時も元アプリとLittleFSをflash照合し、v0.8.0・ready・running=0・設定GET・capabilities一致を確認。診断ソース、ELF、バックアップはGit外。
