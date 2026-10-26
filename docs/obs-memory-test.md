# OBS S3実機メモリ計測

通常S3ファームウェアを使用する。OBS TEST用ファームウェアではなく、
`.pio/build/xiao_esp32s3/firmware.bin` を従来の手順で書き込み、再起動する。
USBシリアルを115200 baudで開く。設定の消去は不要。

## コマンド

| 入力 | 動作 |
|---|---|
| `m` | 既存の現在値診断。内部RAMとPSRAMのfree/minFree/largestに、OBS接続状態・世代・Worker Stack HWMを追加 |
| `h` | 既存の起動・確保失敗・タスクスタック診断。`chainpad-obs` も表示 |
| `o`（小文字） | 次の手動シーン取得1回だけのメモリ記録を予約。取得や接続は開始しない |
| `O`（大文字） | 記録済みの取得前／解析中／処理後を表示。取得は開始しない |
| `?` | ヘルプ |

1文字を送信する。改行が付いてもよい。常時ログは既定OFFのままでよい。
`d` はON/OFFトグルなので、計測のために押す必要はない。

## 操作手順

1. **未接続**：OBS接続を無効にするか、OBSサーバーを停止して未接続状態にする。
   状態が落ち着いてから `m`。Disabled/Disconnected/Errorのどれかも記録する。
2. **Ready**：保存済み接続設定でOBSへ接続し、WebUIでConnectedを確認。`m`。
3. **一覧取得**：`o` を送って `OBS MEM armed` を確認。
   WebUIのシーン切り替えActionで「シーン一覧から選択」を1回押す。
   候補表示後、約1秒待って `O`、続いて `m`。
4. **通常Action実行後**：シーン切り替え、録画開始・停止、ミュート操作などを
   実行し、処理が落ち着いてから `m`。実行内容・回数も記録する。
5. **再接続後**：WebUIのReconnectで再接続し、Readyになってから `m`。
   再取得も比較する場合は再び `o` → 一覧ボタン → `O` → `m`。
6. 最後に `h` を記録すると、Worker/loopスタックと確保失敗記録も確認できる。

シーン数、合計名の長さの目安、OBS版、Wi-Fi/BLEの使用状態をログと一緒に残す。
条件を比較する間は、別の設定保存・Preset操作などを避ける。
この操作では認証情報やシーン名をシリアルへ出力しない。

## ログの意味

`m` の例（数値は実測ログを参照）：

```text
RAM internal: free=... minFree=... largest=... allocated=... loopStackMin=...
RAM PSRAM: free=... minFree=... largest=... allocated=...
OBS: state=Ready generation=... workerStackMin=... lastError=...
```

- 単位はすべて **bytes**。
- internalは`MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`、PSRAMは
  `MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT`。両者を分けて記録する。
- `free`：その時点の空き。`largest`：その時点で確保可能な最大の連続ブロック。
- `minFree`：起動以来のアロケータ低水位情報。`o`でリセットされない。
  複数ヒープの低水位集計のため、厳密な同時刻の全体最小値とは限らない。
- `workerStackMin` / `STACK chainpad-obs lowWaterBytes`：Workerの過去最小の
  **未使用スタック量**。小さいほど余裕が少ない。確保済みスタックは8192 B。
  `loopStackMin`とは別物。mのWorker値はWorkerが直近に公開した値。
- `lastError`は過去の診断であり、Readyで残っていても現在の接続失敗とは限らない。

`O` の出力：

```text
OBS MEM armed=0 active=0 request=... generation=... mask=7 (bytes)
OBS MEM before t=... workerStackMin=...
 internal free=... minFree=... largest=...
 PSRAM free=... minFree=... largest=...
OBS MEM json-alive t=... workerStackMin=...
 ...
OBS MEM after t=... workerStackMin=...
 ...
```

- **before**：Workerが対象の一覧要求を送る直前。
- **json-alive**：一覧要求待ちの受信JSONを解析し、JSONドキュメントと受信
  ペイロードがまだ存在する時点。失敗応答・JSON解析失敗でも通れば記録する。
  複数の受信があった場合は最後の解析サンプル。
- **after**：要求完了／中断後、socket pumpと解析関数から戻り、一時ドキュメントと
  ライブラリ受信領域を解放した後。次の通常Actionが進行している場合もある。
- `mask=7`：3点が記録済み。`mask=5`：前後のみ（応答タイムアウトなど）。
  `armed=1`：まだ対象要求を待っている。`active=1`：計測中。
  busyで予約できなかった場合は現在の計測終了を待つ。

同じメモリ種別の `before.free - json-alive.free` で解析時の一時消費を概算できる。
`after.free` と取得後の `m` で回復を確認する。再取得前に毎回 `o` を送る。
`O`は読み出すだけで記録を消さず、次の予約または再起動まで保持する。

## 計測の範囲と影響

取得中の3点サンプリングであり、厳密な最大ピーク追跡ではない。JSONアロケータの
realloc途中、HTTP応答JSON生成、TCP/Wi-Fi/BLEなどの別タスクの一時確保は
サンプル間に発生し得る。minFreeも併用するが、取得単独の最低値とは断定しない。
必要なら再起動から同じ順序で比較する。シリアル診断自体も出力やファイル参照で
一時確保を行うので、最初のmを含め操作順を揃える。

記録は固定長で、Worker内からシリアル出力しない。通常時の追加ヒープ走査はなく、
`o`で予約した取得1回に限り、既存ヒープ統計とStack HWMを3点で読む。
統計取得にも実行時間とスタックを使うため、計測中のHWMには計測処理自身も含む。
Workerとloopの記録受け渡しは既存の短い排他で行い、ヒープ走査は排他の外で行う。

2バッファ化と16 KiB分割受信領域の固定確保は維持する。シーン取得は手動のみ。
今回の準備では実機への書き込みや接続設定の変更は行っていない。

## 準備時の検証・サイズ

`tests/run_obs.ps1` と `tests/run_obs_control.ps1` PASS。Hostのヒープ統計アダプタで、
未予約時にヒープ走査しないこと、通常Actionでは予約を消費しないこと、3点の記録、
1回限定、計測中の再予約拒否、タイムアウト後の終了を確認した。

通常S3ビルドPASS：静的RAM137820 B、Flash1612546 B。
2バッファ化のみのビルドに対し、診断追加分は静的RAM **+112 B**、Flash **+1252 B**。
記録本体は108 B。シーン用プール8344 B、Assembly16400 B、Worker stack8192 Bは維持。
これらはリンク測定であり、実機の空きヒープ／スタック余裕の測定結果ではない。

互換ビルドもPASS：通常C3（RAM136364 B / Flash1462209 B）、
S3 OBS TEST（RAM138652 B / Flash1603234 B）。実機計測には上記の通常S3を使用する。

## S3実機結果・承認（2026-10-26）

ユーザー提出の差し替え版報告書およびシリアルログに基づく。
**通常操作範囲の初回実機メモリ検証：PASS、ユーザー承認済み。**
XIAO ESP32S3 / ChainOSCPad PCB、通常S3、Wi-Fi STA、認証成功、
Action34/224、Config82648 B（PSRAM配置）で測定。

| 指標 | 実測 |
|---|---:|
| Disabled 内部RAM free | 56040 B |
| Ready 内部RAM free | 54148 B（約52.9 KiB） |
| 接続前後のfree差分 | -1892 B |
| 最終 internal minFree | 17492 B |
| internal largest | 31732 B |
| 通常 PSRAM free / largest | 8297028 / 8257524 B |
| Worker最小未使用スタック | 3176 B（確保8192 B） |
| loopTask最小未使用スタック | 4684 B |
| メモリ確保失敗 / transportOverflows / inputOverflows | 0 / 0 / 0 |

手動一覧取得は2回ともmask=7。内部RAM freeのbefore/json-alive/afterは
1回目45712/50872/54012 B、2回目45620/50228/53568 B。
通常Action後54152 B、再接続後54148 B。2回目取得後のmで46588 Bへ一時低下したが、
追加測定では54148 Bへ回復。今回の範囲で継続的な減少・スタック不足の兆候なし。
最終状態Ready、lastError=0。SERIAL outputRejected=8は計測中に増加せず、
シリアル出力側の拒否件数であってOBS Actionの失敗件数ではない。

3点の差分はJSON単独の使用量ではなく、厳密なピークも示さない。
長時間耐久・高負荷は対象外。固定2バッファおよびAssembly16400 Bを維持する。
C5/C6の試験実装の基準データとするが、両機種の実機動作を保証しない。
C5は現行コードでConfigをPSRAMに配置しており、C6とは別に評価する。
