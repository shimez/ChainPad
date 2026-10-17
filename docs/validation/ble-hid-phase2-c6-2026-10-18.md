# BLE HID Phase 2 — チャット共有テストログ

対象: ChainOSCPad / XIAO ESP32C6 / Windows BLE HID
対象コミット: `8eb308c`（Astraレビュー時点）

> このファイルは、このチャット内で確認できる数値と、ユーザーが貼り付けた生ダンプをまとめたものです。Test 1〜4の完全なCSV生ログは現在の会話情報に含まれていないため、復元・創作していません。Test 5の生ダンプは原文を収録しています。

## テスト一覧

| Test | 条件 | 計測モード | Action | notify要求 | FIFO high | Windows確認 |
|---|---|---:|---:|---:|---:|---|
| 1 | VolUp 3回、Mute 3回 | 2 | 6 | 12 | 2 | 6操作正常 |
| 2 | VolUp 10回連打 | 2 | 10 | 20 | 2 | 10操作正常 |
| 3 | VolUp/Mute交互 各5回 | 2 | 10 | 20 | 2 | 10操作正常 |
| 4 | エンコーダー10クリック | 1 | 10 | 20 | 2 | 未確認 |
| 5 | 高速ボタン連打（VolUp 10回、Mute 3回） | 1 | 13 | 26 | 3 | 未確認 |
| 合計 | | | **49** | **98** | 最大3 | |

## Test 1〜4 — このチャットで確認できる集計

### Test 1
- `input=12`, `action_accepted=6`, `push=pop=12`, `notify_true=12`, `status_ok=12`
- `usage=233`: 3、`usage=226`: 3、`usage=0`: 6
- PAIR: `mean_us=1949`, `max_us=1958`
- FIFO overflow / discard / lost: 0
- Windows側: 6操作すべて正常

### Test 2
- `input=20`, `action_accepted=10`, `push=pop=20`, `notify_true=20`, `status_ok=20`
- `usage=233`: 10、`usage=0`: 10
- PAIR: `mean_us=2065`, `max_us=2980`
- 押下間隔（詳細ログからの概算）: 111〜147 ms、平均約130 ms
- FIFO overflow / discard / lost: 0
- Windows側: 10操作すべて正常

### Test 3
- `input=20`, `action_accepted=10`, `push=pop=20`, `notify_true=20`, `status_ok=20`
- `usage=233`: 5、`usage=226`: 5、`usage=0`: 10
- PAIR: `mean_us=1962`, `max_us=1976`
- 押下間隔（概算）: 180〜220 ms
- FIFO overflow / discard / lost: 0
- Windows側: 10操作すべて正常

### Test 4
- `TRACE mode=1 start_us=155459213 stop_us=163693202 stored=0 lost=0 bytes=6144 status_ok=20 status_error=0 notify_true=20 notify_false=0 discarded_reports=0`
- `COUNT input=10`, `input_queued=10`, `input_dropped=0`, `input_consumed=10`, `action_accepted=10`, `action_rejected=0`, `chain_rejected=0`
- `push=20`, `pop=20`, `discard=0`, `fifo_overflow=0`, `notify_call=20`, `notify_return=20`, `status=20`, `dispatch=10`
- `INPUT 26=10`、その他の入力0
- `FIFO transport=0 initial=0 final=0 high=0`
- `FIFO transport=1 initial=0 final=0 high=2`
- `PAIR notify_request_pairs=10 mean_us=2009 max_us=2467`
- `USAGE usage=0 count=10`, `usage=233 count=10`、その他0
- Windows側: 厳密な反映回数未確認

## Test 5 — ユーザー貼付の生ダンプ（原文）

ユーザー説明: 「結構頑張ってVolUp10連打です」

補足: MuteはVolUpの隣のキーで、連打中に誤って押した可能性がある（推定）。

```text
TRACE mode=0 start_us=0 stop_us=0 stored=0 lost=0 bytes=6144 status_ok=0 status_error=0 notify_true=0 notify_false=0 discarded_reports=0
COUNT input 0
COUNT input_queued 0
COUNT input_dropped 0
COUNT input_consumed 0
COUNT action_accepted 0
COUNT action_rejected 0
COUNT chain_rejected 0
COUNT push 0
COUNT pop 0
COUNT discard 0
COUNT fifo_overflow 0
COUNT neutral 0
COUNT notify_call 0
COUNT notify_return 0
COUNT status 0
COUNT subscribe 0
COUNT disconnect 0
COUNT reset 0
COUNT dispatch 0
INPUT 0 0
INPUT 1 0
INPUT 2 0
INPUT 3 0
INPUT 4 0
INPUT 5 0
INPUT 6 0
INPUT 7 0
INPUT 8 0
INPUT 9 0
INPUT 10 0
INPUT 11 0
INPUT 12 0
INPUT 13 0
INPUT 14 0
INPUT 15 0
INPUT 16 0
INPUT 17 0
INPUT 18 0
INPUT 19 0
INPUT 20 0
INPUT 21 0
INPUT 22 0
INPUT 23 0
INPUT 24 0
INPUT 25 0
INPUT 26 0
INPUT 27 0
FIFO transport=0 initial=0 final=0 high=0
FIFO transport=1 initial=0 final=0 high=0
PAIR notify_request_pairs=0 mean_us=0 max_us=0 (not receiver hold time)
USAGE notify_requests usage=0 count=0
USAGE notify_requests usage=233 count=0
USAGE notify_requests usage=234 count=0
USAGE notify_requests usage=226 count=0
USAGE notify_requests usage=205 count=0
USAGE notify_requests usage=181 count=0
USAGE notify_requests usage=182 count=0
USAGE notify_requests usage=183 count=0
us,event,transport,usage,report_id,epoch,value
TRACE END
TRACE mode=1 start_us=79175212 stop_us=84174201 stored=0 lost=0 bytes=6144 status_ok=26 status_error=0 notify_true=26 notify_false=0 discarded_reports=0
COUNT input 26
COUNT input_queued 26
COUNT input_dropped 0
COUNT input_consumed 26
COUNT action_accepted 13
COUNT action_rejected 0
COUNT chain_rejected 0
COUNT push 26
COUNT pop 26
COUNT discard 0
COUNT fifo_overflow 0
COUNT neutral 0
COUNT notify_call 26
COUNT notify_return 26
COUNT status 26
COUNT subscribe 0
COUNT disconnect 0
COUNT reset 0
COUNT dispatch 13
INPUT 0 0
INPUT 1 0
INPUT 2 3
INPUT 3 3
INPUT 4 10
INPUT 5 10
INPUT 6 0
INPUT 7 0
INPUT 8 0
INPUT 9 0
INPUT 10 0
INPUT 11 0
INPUT 12 0
INPUT 13 0
INPUT 14 0
INPUT 15 0
INPUT 16 0
INPUT 17 0
INPUT 18 0
INPUT 19 0
INPUT 20 0
INPUT 21 0
INPUT 22 0
INPUT 23 0
INPUT 24 0
INPUT 25 0
INPUT 26 0
INPUT 27 0
FIFO transport=0 initial=0 final=0 high=0
FIFO transport=1 initial=0 final=0 high=3
PAIR notify_request_pairs=13 mean_us=1879 max_us=1988 (not receiver hold time)
USAGE notify_requests usage=0 count=13
USAGE notify_requests usage=233 count=10
USAGE notify_requests usage=234 count=0
USAGE notify_requests usage=226 count=3
USAGE notify_requests usage=205 count=0
USAGE notify_requests usage=181 count=0
USAGE notify_requests usage=182 count=0
USAGE notify_requests usage=183 count=0
us,event,transport,usage,report_id,epoch,value
TRACE END
```

## Astraレビュー結果（要点）

- Phase 2の基本計測はC6実機で正常動作と評価。追加の必須試験なし。
- 49 Actionに対して98件のUsage→0通知要求で件数整合。
- `status`件数一致は個別Usageの内容・順序やWindows到達を証明しない。
- FIFO high=3は通常のキュー滞留で説明可能で、同時3キー保持ではない。
- PAIRとCSVの数µs差は、`hid_trace.cpp`で別時刻を取得しているため。平均は整数除算。
- mode=1の`lost=0`は詳細リングに記録していないことによる。mode=2の`lost=0`とは意味が異なる。
- 「欠落なし」は認識された入力イベント以降の計測対象経路に限定する。
- 内部NimBLEフックは未適用のままとし、正常系基準データとして保存。
- レビュー時点で作業ツリーclean、コード変更・commit・pushなし。
