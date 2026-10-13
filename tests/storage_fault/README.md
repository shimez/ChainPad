# Storage tests：H2/H3/H4安全性回帰＋H1不具合再現

## A. 総括・作業基準

### H4修正（現在）

- 開始HEAD：`2d689ad84c0b58f46c25109f07ce59280bc93a46`、`main`、開始時clean。
- ArduinoJson 7.4.2、SDK・形式・Config/Action容量・WebUIを維持。
- **H2/H3/H4は安全性回帰PASS。H1のPASSは未修正不具合の再現成功。**
- network/chain/rotation/full encodeはboolを返し、生成参照・overflow・要素数を検査。
  optional memberの確保失敗もoverflowで拒否。lossless floatはstack文字列からコピーする。
- checked serializerはoverflow、計測長、reserve、sticky書込失敗、実byte数、String長を検査。
- Wi-Fiは失敗をrename前に返し、transactionを無効化。active/RAM/出力状態を保持する。
- GETは30レコード事前検査＋送信時再検査。一件分のdoc/Stringだけ使用する。
  事前失敗は500、200後の失敗はclient.stop()。閉じ括弧・正常終端は送らない。
- `jsonResponse`もchecked serializerを使用。生成失敗は小さい固定エラー本文へフォールバックし、
  元がエラーstatusならそのstatusを保持する。
- commit／push／実機書込みなし。

### H3修正時の記録

- 開始HEAD：`f7041fc3567a9a3318f834b1e85f530f314fc5ed`、`main`、作業開始時clean。
- 変更前の通常host／fault suite成功を記録してから実装。
- mount確認付きPOSIX stat三値probe、起動Config読込失敗の保存ロック、reset markerエラー伝播を追加。
- **H2/H3のPASS SAFETYは安全性回帰合格。H1/H4のREPROは未修正不具合の再現成功。**
- H1のcommit後適用失敗には起動読込ロックを設定しない。H2実装・WebUI・HTTP API・保存形式・容量は変更しない。
- commit／push／実機書込みなし。

### H2修正時の記録

- H2修正開始HEAD：`cde0168670f77d620354e7d7fe3eb6aced828657`、branch `main`、開始時clean。
- 既存テストを変更前に実行し、S3/C5/C6すべてでH2を再現した。
- 本番変更は`src/main.cpp`のWi-Fi保存失敗分岐1行だけ。
  通常commitと同じ`if (!configOutputsAllowed()) panic();`を失敗応答前に追加した。
- **H2のPASS SAFETYは安全性回帰合格。H1/H3/H4のREPROは未修正不具合の再現成功。**
  suite全体のPASSは、すべての問題が修正されたことを意味しない。
- commit／push／実機書込み／電源断試験は実施していない。
- 初回故障注入作業の基準HEADは`7abb5959cba64b102bfcc3168d4be8fb560619b5`。
  その時点のdirty作業ツリーを保持してテストを追加した履歴は、今回の開始状態と区別する。

## 実行方法

リポジトリルートで実行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_storage_fault.ps1
```

前提：Visual Studio C++ Build Tools、`.venv`のPython、Node.js（fetch対応）、既存PlatformIO ArduinoJson依存。
生成物は`.pio/storage-fault-tests/`のみ。`sources.json`へHEAD、実handler行番号、
全src/webファイルのSHA256を記録し、`handlers.inc`を毎回再生成する。

## B. 結果一覧

以下は3つのhost targetすべてで同じ結果。

| ID | 故障注入 | 実行結果・観測 | 判定 |
|---|---|---|---|
| H3 probe | ENOENT／EIO／ENOMEM／EINVAL／EBADMSG／ENOTDIR／EMFILE／errno未設定・未知値 | ENOENTのみ正常mountでAbsent。他はError。成功時の古いerrnoは無視 | 安全性回帰PASS |
| H3 Config | stat/open/read/ヘッダー破損/本文破損/mount/VFS障害 | defaults・出力停止。障害だけ回復してGET→保存しても拒否し元byteを保持 | 安全性回帰PASS |
| H3復旧 | 失敗後のactive全件load | 正常load後のみ保存可能。loadなし・ファイル消失だけでは解除しない | 安全性回帰PASS |
| H3保存時対照 | 正常稼働中、beginのstatだけ失敗 | 保存拒否、正常RAMと出力許可を保持。起動読込ロックを作らない | 安全性回帰PASS |
| H3 reset | pending/doneのstat/open/read/VFS障害・内容破損 | Failed、marker byte保持、renameなし。pending回復後はformatせず再開 | 安全性回帰PASS |
| H3初回 | mount失敗＋全領域FF | Factory ResetはNormal。別プロセス試験で通常Configの初期mount/formatも確認 | 安全性回帰PASS |
| H3異常mount | 非FF／partition read失敗／partitionなし | Failed。tmpを消去許可に使わない | 安全性回帰PASS |
| H4 encode | network/chain/rotation、root/nested/pool/string、optional fields、full Configコピー | 単発・連続失敗を成否で通知 | 安全性回帰PASS |
| H4 Wi-Fi | 一段目・stage側encode、credentials/requestコピー、全30文字列化 | HTTP400、active全byte/RAM不変、rename/restart/panicなし、旧・失敗token commit不可 | 安全性回帰PASS |
| H4 GET | 全60文字列化、encode両巡の失敗 | 事前500／送信時stop、欠落した正常完了JSONなし、active/RAM不変 | 安全性回帰PASS |
| H4 writer | reserve/concat・途中・末尾・短いescape書込 | sticky失敗、部分Stringを返さない | 安全性回帰PASS |
| H4 WebUI | 実api/load＋500/切断/不正JSON | 既存編集状態を置換しない。正常16+16は採用 | 安全性回帰PASS |
| H1復旧 | rename直後の最初のactive openだけ失敗 | HTTP400、diskはstagedと完全一致する新設定、RAM defaults、出力禁止。loadConfigで新設定復旧 | 再現成功 |
| H1後続上書き | 上記→実GET handler→取得defaultsの再保存 | HTTP200で正常な新設定をdefaultsへ上書き。再loadでもdefaults | 再現成功 |
| H2通常commit対照 | HID保持＋両backend pending＋Engine Wait、rename後open失敗 | HTTP400、出力禁止、jobs=0、pending破棄、不要MIDI=0、HID解除、入力discard=1 | 安全性回帰PASS |
| H2 Wi-Fi | 同条件で実Wi-Fi PUT handler | 通常commitと同じ停止結果。HTTP応答前の取消も確認 | 安全性回帰PASS |
| H2入力検証対照 | 不正SSID型 | HTTP400、renameなし、active/RAM保持、panicなし、再起動予約なし | 対照PASS |
| H2書込失敗対照 | pending writeを1回だけ失敗 | HTTP400、renameなし、active/RAM保持、panicなし、再起動予約なし | 対照PASS |
| H2成功対照 | 故障なし | HTTP200、panic、SSID更新、750ms後の再起動予約、既存応答文を維持 | 対照PASS |

## C. 各問題の詳細

### H3：存在判定の誤分類

旧原因はArduino existsがopen失敗をfalseへ変換し、Config Missing／journalなしになったこと。
現在は実`src/storage_probe.cpp`をhostにもコンパイルし、sys/statとSDK呼出境界だけfake化する。

probeは`esp_littlefs_mounted("settings")`、Arduino mountpoint、root statを確認する。
固定root下の通常ファイルだけPresent。file stat失敗直後にerrnoを保存し、ENOENTのときも
rootを再確認する。その他・errno未設定・directory・パス不正はError。
全FF確認は共有関数でpartitionを256byteずつ読む。末尾も範囲内に限定。

Configはload時の判定/open/read/破損失敗により`bootReadLocked`を設定。
通常beginとWi-Fi保存の両方を拒否し、障害回復やヘッダーだけの成功では解除しない。
正常activeの検証pass＋適用pass成功後だけ解除。Missingは初回のみ許可し、
失敗を記録した後のMissingではロックを維持する。
未対応version単独では新たなロックを作らず、従来の明示置換を維持する。
保存中の失敗ではこのラッチを設定しないのでH1は未修正、H2は維持される。

pending/doneは同じ5byte `FR1:1`〜`FR1:3`の内容検査と三値probeを使う。
mount不能かつ非FF／読込不能はError。確認済み全FFはjournal不存在として初回起動可能。
doneのエラーもflowへ伝播する。優先順位は明示intent=0→valid pending→既存intentを維持。
pendingがvalidならdoneは参照しない。tmpは読まない。エラーでは自動再起動しない。
正常pendingからの再開・上限・明示再試行は既存／追加platformテストで検査。

H3 Config検査は実GET handlerのdefaultsからbeginを試し、Wi-Fi保存も拒否されることをassert。
元ファイルの全byte保持、障害解除のみではロック継続、全件load後の保存再開を確認。
初回の全FF→Factory Reset Normal→Config初期formatは`--blank-boot`の独立プロセスで検査。

限界：SDKの実FS／Flash／電源断はhostでは再現しない。errnoは公開API境界で注入する。
Arduino FileのEOF／read errorの完全分類は今回変更しない。NVS初期化のSDK動作も対象外。
直接VFS statはArduino FSの外側mutexを通らないため、probe中に別taskがmountを変更しない
既存の実行順序を前提とする。mount bookkeepingとrootを照合するが、外部taskによるVFS登録の
同時差し替えを原子的に防ぐ仕組みではない。
実機ではS3/C5/C6それぞれの初回起動、既存設定の正常load/save、読込障害後の再起動復旧、
pending書込〜NVS消去〜done rename各段階での電源断再開と試行上限を確認する。
Arduino initArduinoによるsetup前のNVS自動消去リスクは従来どおり未解決。

### H4：配列要素追加時のallocator失敗

#### 現在の安全性回帰

各S3/C5/C6 host variantで932ケース＋正常最大長/float端点/負のゼロ/制御文字検査。
network/chain/rotationの対象encode呼出し番号を指定し、GETの事前／送信巡と
Wi-Fiの初回／stage内encodeを別々に注入。全allocation境界で単発・持続故障を試す。
requestモードはnetworkからcredentials代入・requestコピーまで注入し、最初の
checked serializationのreserve境界で解除する。parseやH1のrename後適用には注入しない。
full encodeの中間doc・コピー・array追加もallocation全点で失敗を検査する。

String fakeは専用define時だけreserve/concat失敗を有効にする。GET全60レコード、
Wi-Fi全30レコードを対象とし、別途末尾失敗と6byte escapeの短い書込を検査。
HTTP adapterはstop後のsendを無視し、SDKと同じ自動終端呼出しもモデル化する。
`webui_get.cjs`は既存WebUIのapi/loadを抽出し、Node fetchで実chunked切断も検証する。
エラー時の編集状態未置換を確認するが、ESP32 SDKのTCP実装そのものをリンクした試験ではない。

#### 修正前の再現記録

構成：実ArduinoJson 7.4.2の公開Allocatorインターフェースを使用。
本番`model.cpp`をincludeしてencode関数名だけ変更し、wrapperで対象encodeの
最初の呼出し中だけ故障を有効にする。encode/decodeの本体は書き換えない。
JsonDocumentはテストinclude shimで、指定allocatorを渡す派生クラスに置換。
生成JSONの手編集、配列要素の手動削除は行っていない。

Action再現fixture：16 Actionsのうち先頭4個がWait、残り12個がOSC String。
chain encodeの3回目のallocateを1回だけ失敗させる。
Rotation再現fixture：16 Outputsのうち先頭6個がMIDI CC、残り10個がOSC Int。
rotation encodeの4回目のallocateを1回だけ失敗させる。
両方とも失敗したhost確保サイズは2048 bytes（128 variant slotsのpool）。

allocator失敗後にencode自身が終了した時点でoverflowを観測し、故障を解除して
生成JSONを記録する。通常のparseで構文有効性を確認。Wi-Fiは実handlerから
本番保存を通し、HTTP200と欠落数・load後の保持を確認する。
GETは実handlerの全responseを本番decodeConfigで検証し、schemaも通ることを確認。
異なる失敗点ではschema拒否・active不変となる対照も含める。
故障なしGETも各fixtureでschema正常・両配列16件を確認する。

原因：`src/config_store.cpp:197–209`、`src/main.cpp:113–123`のencode後に
overflow確認がない。配列要素が丸ごと欠けると残った配列はschema上有効。
文字列化後の別Documentでは元のoverflow情報を失う。
stageのwriteRecordにはoverflow保護があるが、この手前で起きた欠落を検出できない。

上記は修正前の再現条件。現在は同じfixtureを含む安全性回帰で欠落保存・正常完了を禁止。
実機でのOOM頻度、heap断片化の発生確率は評価しない。
hostは64-bit。ESP32の標準pool境界に合わせ`ARDUINOJSON_POOL_CAPACITY=128`を指定。
これはConfig／Action容量変更ではない。pointer・slotの実byteサイズはESP32と異なる。

### H1：commit済み／適用失敗／後続上書き

構成：旧Configを保存してからSSIDとOSC portを変更した新Configをstage。
rename成功時に、次のactive openだけを1回失敗させる。
実commit handlerからHTTP400を確認し、diskとrename前pendingの全byte一致を検査する。
RAMのSSID・Action数がdefaults、出力禁止であることを確認する。

独立した2経路：
1. loadConfigを呼び、新SSID／OSC port／Actionsが復元できることを確認。
2. 実GET handlerのdefaults応答をdecodeし、通常begin/stage/commitで再保存。
   HTTP200、旧新設定の消失、次回loadでもdefaultsを確認。

原因：`src/config_store.cpp:171–174`でrename後に失敗可能な再読込を行い、
失敗時にdefaultsへ戻す。GETはRAMを返し、beginはdiskヘッダー検査だけで許可する。
現在の保護は出力禁止と通常commit handlerのpanic、新activeが読めれば再起動復旧。

修正後の期待：commit済み適用失敗を明示し、復旧前のdefaults誤上書きを防止。
限界：再起動はloadConfigによるモデルであり、プロセス／SDK全体の再起動ではない。
ブラウザーDOMは実行せず、実GET JSONから再保存する操作を再現。

### H2：Wi-Fi失敗時panic漏れの修正・安全性回帰

構成：本番Engine／MIDI backend／Keyboard backendをリンク。
BLE経路でHID Usage4を実際にfake transportへ送信し押下状態を作る。
transportを一時書込不能にしてUsage5とMIDIをキューに残し、Engineに10秒Waitを開始。
通常commit／Wi-Fi PUTそれぞれにrename後active open失敗を注入。

`main.cpp`のHTTP handler、panic、resetGuard、result、jsonResponseを
毎回機械抽出し、そのコードをそのままコンパイルする。handlerの条件分岐を
テスト側で手書きし直していない。抽出形式が変われば生成を失敗させる。

修正前：Wi-Fiでは出力禁止になってもjob=1、MIDI/HID pendingが残り、
transport復旧後にMIDI 1件と押下HID reportが送信された。入力discard=0。
通常commitではjob=0、pending破棄、空HID report、入力discard=1だった。
この結果を`h2-before.log`に記録してから本番コードを修正した。

原因：従来の`src/main.cpp:99`は保存失敗ですぐreturn、通常commitの`:155`には
出力不可の場合のpanicがあった。`:302`のbackend tickはConfig出力許可と独立。
今回`:99`に同じ条件のpanicを追加し、既存のEngine取消・backend破棄・HID解除要求・
入力discardを再利用した。保存関数・Engine・backend実装は変更していない。

修正後：両handlerでjob=0、pendingなし、不要MIDI=0、HID全キー／modifier解除、
入力discard=1、HTTP400と既存エラーメッセージ、再起動予約なしをassert。
HTTP adapterのbeforeSend hookで、応答時点で取消・キュー破棄・discardが
済んでいることもassertする。HIDの実送信はtransport復旧後のtickで確認する。
入力discard関数はcounterで代用する。

追加対照：不正SSID型とpending write失敗は出力許可を維持し、jobとpendingを残す。
余計なpanicを行わず、旧active/RAMとHTTP400／メッセージを保持する。
成功時はHTTP200／`Saved. Restarting.`、既存panic、`millis()+750`予約を確認する。
限界：WebServer/TCP/Wi-Fi stack、入力FreeRTOS queue、実Bluetooth通信はfake。
loop全体は実行せず、本番loopが行うbackend tickを明示呼出しする。
HID解除をホストへ送信するロジックまでは確認するが、実接続先への到達保証ではない。

## D. テスト基盤

- `tests/run_storage_fault.ps1`：独立runner、S3/C5/C6を順番にcompile/run。
- `tests/storage_fault/generate.py`：実handler/helper抽出、ArduinoJson shim、source hash。
- `tests/storage_fault/tests.cpp`：H2/H3/H4安全性回帰、H1のcharacterizationと対照。
- `tests/storage_fault/string_fault.h`：String reserve/concatのrecord単位故障。
- `tests/storage_fault/webui_get.cjs`：既存WebUIの失敗応答受理防止。
- `tests/storage_fault/benchmark.py/.cpp`：H3 HEADとworktreeのGET比較（git show、checkout不要）。
- `src/storage_probe.h/.cpp`：共有のread-only probeと全FF確認。
- `tests/probe_platform/*`：stat、mount、partitionの故障注入。実probeをそのまま検査。
- `tests/storage_fault/LittleFS.h`：パス限定exists-open故障、rename直後のactive-open故障、pending限定write失敗。
- `tests/storage_fault/json_fault.h`：公開Allocator、対象encode内の単発N回目故障、観測値。
- `tests/storage_fault/model_adapter.cpp`：実encode呼出しのscope wrapper。
- `tests/storage_fault/http_adapter.h`：route登録、request引数、response status/body収集、応答前の状態検査hook。
- `tests/storage_fault/reset_sdk.h`と4つのSDK header：journal試験用NVS absent adapter。
  想定外のerase／format／NVS writeには例外を出し、誤った経路を通らないようにする。
- 本README：結果・再現条件・引継ぎ。

H3では既存host fake／runnerにもprobeのリンク・SDK境界fakeを追加。
WebUI・API仕様・format・Config/Action容量は変更なし。新規常駐状態は起動読込ロックのboolのみ。
test includeパス・wrapper・allocatorは専用runnerだけに設定。
本番PlatformIO環境へ故障注入用define／include／sourceを追加していない。

## E. 回帰・ビルド

### H4修正結果

- 基準fault runner：H1/H4再現、H2/H3回帰PASS。
- 修正後fault runner：S3/C5/C6各932ケース＋正常系、WebUI安全性PASS。
- H1再現、H2/H3回帰、通常host suite（Factory Reset含む）：PASS。
- S3/C5/C6 Standard build・image検査：すべてPASS。
- 3機種firmware.map：checked serializerリンク、故障注入symbol/object非混入を確認。

| Standard | RAM bytes（H3比） | Flash bytes（H3比） |
|---|---:|---:|
| S3 | 95,708（±0） | 1,347,478（-1,172） |
| C5 | 65,392（±0） | 1,625,489（-1,142） |
| C6 | 141,220（±0） | 1,571,882（-1,152） |

常駐Config容量・保存形式不変。GETは既存の一件分doc/Stringを再利用し、全Config複製・
Flash一時ファイルは使用しない。リンク後RAM総量と動的heap最大量は別指標。

GET比較：`.venv\Scripts\python.exe tests/storage_fault/benchmark.py`
Windows host / MSVC `/O2` / C6マクロ / 128 slots / 最大224長文Actions＋16 Outputs、
100回平均、通信なし。同じfixtureで応答97,191byte。

| 対象 | header確定まで | handler全体 |
|---|---:|---:|
| H3 `2d689ad` | 0.707 µs | 1,084.81 µs |
| H4 worktree | 1,388.72 µs | 2,767.77 µs |

これはhostでの一回の比較測定。ESP32/C6の実時間やWi-Fiレイテンシへ換算しない。
実機では最大設定時のTTFB/総時間、heap、USB/BLE併用、切断時のブラウザ動作を確認する。
200後の障害は500へ変更できない。SDKの送信内部OOM・実通信まですべて保証する修正ではない。

ログ：`%LOCALAPPDATA%\Temp\opencode\h4-before-fault.log`、`h4-after-fault.log`、
`h4-after-host.log`、`h4-build.log`、`h4-get-benchmark.log`。

### H3修正結果

- 基準host／fault runner：成功。
- 修正後host／fault runner：成功。H2/H3安全性回帰、H1/H4再現を区別。
- 最終追加の初回boot・pending復旧検査：PASS。
- S3/C5/C6 Standard build・image検査：すべてPASS。
- 3機種firmware.mapで本番probeのリンクと、storageFault/jsonFault/probeFake/fakeResetおよび
  故障注入objectの非混入を確認。
- `git diff --check`：PASS。

| Standard | RAM bytes（H2比） | Flash bytes（H2比） |
|---|---:|---:|
| S3 | 95,708（±0） | 1,348,650（+700） |
| C5 | 65,392（±0） | 1,626,631（+744） |
| C6 | 141,220（±0） | 1,573,034（+758） |

容量比較は保存済み`h2-build.log`と`h3-build.log`のPlatformIO表示値。
追加boolはあるがリンク後RAM総量は不変。probeのパス80byte、全FF確認256byteは一時stack領域。
Config/Action容量・保存形式は不変。実機検証は未実施。

- ログ：`%LOCALAPPDATA%\Temp\opencode\h3-before-host.log`、`h3-before-fault.log`、
  `h3-after-host.log`、`h3-after-fault.log`、`h3-build.log`。

### H2修正時の結果

- 修正前runner：S3/C5/C6すべてH2不具合再現成功。
- 修正後runner：S3/C5/C6すべてH2安全性回帰・3種のWi-Fi対照成功。
  H1/H3/H4も引き続き未修正不具合の再現成功。
- 既存`tests/run_host.ps1`：全suite成功。
- Standard firmware build：S3/C5/C6すべて成功（合計2分1.599秒）。
- `tests/check_image.py --env xiao_esp32s3 xiao_esp32c5 xiao_esp32c6`：すべて成功。
- 本番build設定にtest include/defineなし。3機種のfirmware.mapにも
  storage_fault／FaultDocument／jsonFault／rawEncodeChain／FaultFS／pendingWriteFailuresなし。
- `git diff --check`：成功（改行コードに関するGitの注意表示のみ）。
- 実機試験：未検証。

今回のログ：`%LOCALAPPDATA%\Temp\opencode\h2-before.log`、`h2-after.log`、
`h2-host.log`、`h2-build.log`。

## F. 次の修正へ

残るH1：commit済み適用失敗状態と復旧導線、復旧前の上書き可否を設計。

H2/H3/H4は修正済み。H1は「ディスクに新設定はあるが使えない」状態を
どう表示・回復するかの判断が必要。二重ConfigやHTTP非同期化は前提としない。

H2/H3/H4安全性回帰とH1の現行不具合をassertするテストを区別すること。
H1の移行先の期待値は誤上書き不可または明示確認（未修正、設計候補）。

追加調査：実ESP32 allocator特性、SDKの同期エラー伝達、Flash電断時の永続性。
H1修正・実機検証は未着手。H3実機では初回boot、ストレージ回復後Restart、reset中断再開を確認する。
