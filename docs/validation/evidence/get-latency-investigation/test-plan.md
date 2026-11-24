# 試験方法とデータ辞書

XIAO ESP32C6を3台使用し、[A/B/C](implementation-history.md)をそれぞれ1台に固定した。Wi-Fi・同一PCのHTTPクライアントを使い、各機の試験を順次実施。測定対象への並列HTTP負荷、物理キー操作はなし。当初案の同一個体の載せ替え、全密度各10回・別起動での反復、外部入力治具、Firmware内部の計時は実施していない。

## GET単体

schema v2。各機の設定から通信設定を維持し、RotationをAction Chainモード（値出力なし）に揃えた。入力番号 `i` と入力内番号 `j`（ともに0始まり）の各ActionはWi-Fi OSC int、delay 0、address `/lat/i%02d/a%02d`、値 `1000+8*i+j`。Sは入力0・1に各1件、M/Lは28入力に各4/8件で2/112/224枠。Actionは起動しない。

1. 試験前設定を保持し、各機でbegin→28入力のstage→rotation→commit。設定GETで期待する公開設定と全体比較。旧版とCのnetwork公開項目の違いは各版自身の形式で扱う。
2. 各密度でstatus/configを各3回ウォームアップ。本測定は1要求1 TCP接続・HTTP/1.1・非圧縮・順次読了、要求間1秒。試行ごとにstatus→configと逆順を交互にした。
3. 初回S/M/Lは各endpoint **3回**。A/BはBLE未接続・OBS非搭載、CはBLE購読あり・OBS Ready。再測定は全機BLE未接続、CはOBS Disabled、**Lだけ各10回**。試験中の再起動を挟まず、稼働0とイベント数を確認。診断は要求タイミングの計測外。各機の終了時に試験前設定を再保存しGETで照合。

PCの単調時計で、要求送信完了→最初の応答バイトを受けた時点が `ttfbMs`、最初→最後の本文ペイロードを含むrecvが `bodyMs`、要求送信完了→HTTPメッセージ終端（chunkedなら終端チャンク受信）が `totalMs`。TCP接続とJSON解析を除く。`bodyBytes` はHTTPヘッダ・チャンク枠を除く本文長。同一recvで本文が揃えば `bodyMs=0`。各指標の中央値は別々に算出し、加算して総時間を求めない。

## OSC併用

全機BLE未接続・OBS無効または非搭載。入力0に `/lat/seq` のOSC int 0～7を配置し、間に200 ms Waitを7件挿入（15枠）。入力1に未起動OSC 1件、残る入力2～27に各8件。合計224枠。固定OSCの形式はGET単体と同じ。全量GETで保存確認し、3機のAction列ハッシュの一致を確認した。このfixtureは単体Lと同じ**枠数**だが同じ本文ではない。

- UDP受信を別スレッドで開始し、試行につきHTTP triggerは入力0に1回のみ。OSC 2受信の50 ms後を予定点とし、`none` は時刻記録のみ、`status`／`config` はそこでTCP接続を開始してGET。実際の要求送信完了は接続処理分だけ遅れる。OSC 7の後も250 ms受信。キー入力・連続trigger・Loop・Callは不使用。
- 各機各条件1回のウォームアップ後、5ブロックの各々に3条件を1回ずつ入れ順序を循環。UDP形式・送信元・連番・件数・GETの応答を検査し、前後で同一起動、入力イベント増分1、稼働0を確認。trialの異常、予定点に間に合わないtrigger、狙った位相を外した試行を成功扱いしない。割当失敗はブロック前後の差分で、個々のパケットからは推定しない。

## 再集計

`data/` は1行1 JSON。測定済みの数値・照合結果だけを収録し、実設定やHTTP本文は含まない。

- `simple-get.jsonl`、`matched-l.jsonl`：`label,condition,endpoint,trial` で特定。集計対象は `phase=measurement` のみ。`warmup` は除外。`stage` 行は設定保存とAction列ハッシュ、カウンタ差分・接続状態、復元照合。`matched` はconfigでは期待設定との意味的一致、statusでは稼働・接続等の試験条件成立。`null` のカウンタは未測定。
- `osc.jsonl`：`label,block,condition` で特定。block 0は `warmup`、1～5は `measurement`。`osc` の各対は `[連番,試行開始からのPC受信ms]`。PCの絶対的な時計原点は除外。`http.startMs/endMs` は同じ試行起点の要求送信完了／HTTP完了時刻。`targetMs` は予定点、`markMs` は予定点待ちから復帰した時刻で、GET開始時刻と混同しない。
- 各試行で隣接差7個の中央値・最大値と `maxPair` を得る。`gap23Ms=t3−t2`、`overlapMs=max(0,min(t3,end)−max(t2,start))`、`postEndMs=t3−end`。`none` ではGET関係の値は定義しない。`errors` が空か、連番が正しいかを確認したうえで、条件別に**試行単位の指標の中央値**を取る。各機の `restore` はその試験前設定との比較結果であり、Firmwareの差し戻しではない。
