# Action Chain Loop 実機検証記録・試験案

Phase 3の実機確認用。前半は当初の試験案、後半は実測記録。
2026-11-18の利用者による手動試験1〜8は、確認範囲内でPASS。
詳細・CSV・Panic計測結果は
[手動試験記録](evidence/action-chain-loop-20261118/README.md)を参照。
利用者判断により基本試験で一区切りとし、追加試験は必要時に実施する。

## 準備と記録

- 対象機種、接続ポート、Firmwareコミット、USB/BLE接続先、Wi-Fi RSSI、
  OSC受信先・OBS接続状態を記録する。
- 現在のFull Presetを保存し、試験用設定に入れ替える。試験後は元の設定へ戻す。
- 同じ機種・接続条件で「Loopなし」を先に測り、Loop負荷中との差を比較する。
- C3はビルド確認と実機動作確認を区別する。使用可能な実機から実施する。
- 計測中の診断出力は通常OFFとする。ログ出力自体の負荷を混ぜない。

## 負荷パターン

| ケース | Chain | 主な確認点 |
|---|---|---|
| 基準 | Loopなしの単発OSC／MIDI／HID | 通常時の遅延・欠落 |
| 空Loop | 999回×8段の空Loop | 制御命令だけでも入力・通信に戻る |
| Waitなし | 999回×2段、内部にOSC 1個 | 継続送信時の応答性 |
| 同時実行 | WaitなしLoopを32 Invocation起動 | 公平性・33個目の拒否・Panic |
| Stop連続 | Loop内部にStopとOSC | 自分の継続・他Chainの停止・応答性 |
| 重いAction | All Notes／ペア送信をLoop内に配置 | Backend処理と待機を含む負荷 |
| 複合負荷 | USB/BLE接続、OBS有効、HTTPポーリング併用 | 通信競合時の応答性 |

枠数制限を守る（Press/Release合計16、CW/CCW各8）。
32枠満杯のとき新しい待機Chainが拒否されることは仕様であり、
入力欠落と混同しない。キー入力応答の比較にはLoopなし・待機なしの単発Actionを使う。
Stopケースでは、他Chainが意図どおり中断された結果を遅延・欠落と数えない。

## 測定方法

### キー入力

- 別キーに単発OSC（専用アドレス）またはMIDI Noteを割り当て、
  Loop中に押下・解放を各100回以上行う。受信側で件数と順序を記録する。
- 遅延を正確に測る場合は、ロジックアナライザー等で物理入力エッジと
  対応出力を同じ時間軸で測る。USB/BLE/OSC受信PCの時刻と装置時刻は
  同期なしに差し引かない。
- 計測器がない場合は件数・順序・体感停止の有無を記録し、
  入力から出力までの絶対遅延は「未測定」とする。

### HTTP

- 同一PCから`GET /api/status`を直列に200回送信し、
  単調時計で要求開始から応答本文取得完了までを測る。
- リクエスト間隔は100 ms以上、タイムアウトは2秒。並列発行しない。
- 基準／各負荷で中央値・p95・最大・タイムアウト数を記録する。
- `/api/trigger`で負荷を開始する場合、32枠満杯の409応答は別集計とする。

### Panic

- Loop出力の受信記録を開始し、`POST /api/panic`を送る。
- 要求開始→HTTP応答完了、および受信側で最後のLoop出力を観測する時刻を記録する。
- PC側に既に届いたUDPやOS側キューの排出を、Firmwareが再開した送信と区別する。
- Panic後に少なくとも2秒観測し、Loopが再開しないこと、runningChainsが0になること、
  保持HIDキーが解除されることを確認する。再入力では先頭から実行する。
- 通信が正常な状態で10回以上繰り返す。

## 判定案

- 必須：watchdog再起動・操作不能・Panic後のLoop再開がないこと。
- 必須：受付された単発キー入力の出力件数・順序に異常がないこと。
- 初期目標：HTTPタイムアウト0、p95の増加100 ms以内、Panic応答500 ms以内。
  これらは製品保証値ではなく、基準測定と比較するための暫定目標。
- 超過時は命令数予算、単一Backend呼出し時間、Wi-Fi/USB/BLEの遅延を切り分ける。
  予算変更を行う場合は既存Chain・満杯時・Stopの自動回帰を再実行する。

## C6自動試験・初期物理入力試験の経緯

以下の「未解決」「未実施」「実行中」は各測定時点の状態を示す。
最終的な自動測定結果は「OSC頻度制御版」、後続の手動確認は上記リンクを参照。

- 対象：XIAO ESP32C6、COM18、`192.168.0.15`。
- 書込み前の公開設定を
  `C:\Users\ctake\AppData\Local\Temp\opencode\chainpad-loop-c6\config-20261118-051825.json`
  に退避。Wi-Fiパスワードは取得せず、設定適用時も省略による保持を使用する。
- 初回書込みはPlatformIOのcp932進捗表示でUnicodeEncodeErrorとなり中断。
  プロセス終了後、`PYTHONUTF8=1`／`PYTHONIOENCODING=utf-8`で再実行し成功。
  bootloader／partition／applicationのハッシュ検証、リセット後のHTTP応答、
  `maxLoopCount:999`を確認した。
- 試験Firmware SHA256：
  `6fc62e6e0226e4371f692e724e97f404564d0345ff6d340ca84ab856e5040923`。
- `tests/loop_hardware_probe.py`の5ケース自動試験を実行中。
  本試験のKEY2出力はHTTP triggerで起動するため、物理キー入力の実測とは区別する。

### 初回測定：32同時起動で中断

各200回。HTTPは`GET /api/status`のPC側RTT。

| ケース | HTTP中央値 / p95 / 最大（ms） | Panic RTT（ms） | 単発probe OSC |
|---|---|---:|---:|
| 基準 | 27.30 / 92.36 / 107.93 | 39.74 | 200/200 |
| 空Loop 8段 | 33.71 / 93.33 / 107.49 | 78.80 | 200/200 |
| WaitなしOSC・1 Invocation | 66.61 / 96.19 / 157.62 | 16.82 | 200/200 |

- `osc-32`のInvocation起動中にHTTPが2秒でタイムアウトしたため中断。
  `stop-no-wait`はこの実行では未測定。
- finallyでPanicと元設定の復元が成功し、GET完全一致を確認。
- 32同時実行の応答性は**未解決**。起動ごとの同時実行数・RTT・
  メインループ間隔最大値を記録する追加切り分けを開始した。

### 切り分けと予算調整

- 追加測定では14個目のtrigger RTTが1969.44 ms、18個目で2秒タイムアウト。
  元設定は再度復元・GET一致を確認した。
- メインループ間隔の累積最大値は628995 µsで変化していない。
  したがってHTTP遅延を単一のEngine処理時間だけに帰属させることはできない。
  連続OSCによる通信負荷を含めて比較する。
- Phase 3で全体予算を256→64命令／cycleへ調整して再検証する。
  Invocationあたり16命令、LoopなしChain、Stopの継続規則は維持する。
  この段階では改善は未確認。

### 64命令版：32起動成功、通信負荷の課題は継続

- 変更後の通常/HID trace/機種別ホストテストは全成功。
  C6ビルド・書込み・ハッシュ検証成功。
- 32 Invocationの起動を確認。200回のstatus RTT：
  中央値83.46 ms、p95 173.55 ms、最大422.49 ms。
- **triggerは198応答／200試行で、2回タイムアウト**。
  `/loop-test/key`のOSC受信は195件。GET 200成功だけをもって合格とはしない。
- `events`は32→232で全試行が本体に到達している。
  `skipped`は26717→152546となり、連続OSCの送信失敗が多数発生。
  単発probeの受信不足について、UDP損失とBackend失敗の内訳はこの統計だけでは確定できない。
- Panic RTT 106.86 ms、最後の負荷OSCはPanic要求開始から103.37 ms。
  2秒観測後runningChains=0、cancelledChains=32。元設定復元・GET一致成功。
- 通信なし空Loop32個で比較する切り分けを実行中。

### 空Loop32個との比較

- 同じ64命令版で空Loop32個を起動し、statusと単発probeを各200回実行。
- HTTP中央値27.36 ms、p95 94.90 ms、最大108.33 ms。
  status／triggerは各200応答、タイムアウト0、probe OSC受信200/200。
- Panic RTT 12.70 ms、runningChainsは32→0。skipped増加なし。
  元設定の復元とGET完全一致も成功。
- この比較では、空Loop32個は基準に近い応答性を維持し、
  OSCを連続送信するLoop32個で悪化する。通信負荷が有力だが、
  Wi-Fiドライバ・AP・受信PCのどこで飽和するかは未確定。
- 次の対策案はLoop由来OSCの全Invocation共通の送信頻度制御。
  予算不足時はActionを消費せず再開待ちとし、公平な順番で再開する。
  LoopなしChainの送信と既存Backendエラー時のスキップ規則は維持する。
  Loopの実行速度へ影響するため、実装前に利用者へ方針を確認する。

### OSC頻度制御版：5ケースの自動測定成功

利用者承認後、Loop入りInvocation共通でOSC受付を4 ms間隔とし、
待機は到達順・送信前に行う実装へ変更。ホスト回帰全成功後にC6へ書込み。

| ケース | HTTP中央値 / p95 / 最大（ms） | Panic RTT（ms） | probe OSC |
|---|---|---:|---:|
| 基準 | 29.74 / 92.13 / 106.12 | 23.11 | 200/200 |
| 空Loop 8段 | 70.73 / 95.12 / 103.33 | 20.76 | 200/200 |
| WaitなしOSC・1 Invocation | 73.39 / 100.55 / 106.78 | 34.23 | 200/200 |
| WaitなしOSC・32 Invocation | 68.89 / 93.78 / 107.10 | 17.36 | 200/200 |
| Loop内Stop＋OSC | 72.27 / 101.72 / 115.44 | 81.16 | 200/200 |

- 全ケースでstatus／trigger各200応答、タイムアウト0、skipped増加0、
  inputOverflows増加0、Panic後runningChains=0。
- 最後の負荷OSCはPanic要求開始から、単一Loop 28.07 ms、
  32 Loop 8.58 ms、Stop Loop 74.83 ms。各HTTP応答完了より前で、
  その後の2秒観測にLoop出力の再開なし。
- 元設定復元とGET完全一致成功。
- 生データ：`chainpad-loop-c6/results-20261118-054923.json`
  （上記Temp/opencode配下）。本結果はHTTPによる起動試験であり、
  物理キー入力・BLE負荷・HID解除の実測結果は含まない。

### 物理KEY2：20回押下

- WaitなしOSC Loopを1 Invocation実行し、利用者がKEY2を1秒に1回、
  約0.2秒押して完全に離す操作を20回実施。
- probe OSCは20/20受信、Press／Releaseは40イベント。
  skipped増加0、inputOverflows増加0。
- status HTTPは1200/1200成功。中央値47.14 ms、p95 101.36 ms、最大167.18 ms。
- Panic RTT 70.56 ms、停止後runningChains=0。最後の負荷OSC到着は
  Panic要求開始から58.49 msで応答完了より前。その後の観測で再開なし。
- 元設定復元とGET完全一致成功。
- 生データ：`chainpad-loop-c6/results-20261118-055639.json`（上記Temp/opencode配下）。
- 本試験は物理キーの取りこぼし確認であり、入力エッジからOSC出力までの
  絶対遅延は測定していない。BLE／HID／OBS混合負荷、All Notes重負荷、
  Panic 10回反復は未実施。
