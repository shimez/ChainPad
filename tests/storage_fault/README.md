# Storage fault injection：H1–H4（現行挙動の再現試験）

## A. 総括・作業基準

- 開始HEAD：`7abb5959cba64b102bfcc3168d4be8fb560619b5`、branch `main`。
- 開始時dirty：既存のFactory Reset、UI、site、docs、host runner変更等を保持。
  `test/storage-fault-injection` branchは作成せず、stash／破棄／commit／pushなし。
- 今回の追加は`tests/run_storage_fault.ps1`と`tests/storage_fault/`のみ。
- H1–H4はS3/C5/C6のStandard容量を使ったhost試験で再現。
- **PASSは既知の不具合挙動を期待どおり観測した意味。修正済み・安全性合格ではない。**
- 実機書込み、電源断試験、本番コード修正は実施していない。

## 実行方法

リポジトリルートで実行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_storage_fault.ps1
```

前提：Visual Studio C++ Build Tools、`.venv`のPython、既存PlatformIO ArduinoJson依存。
生成物は`.pio/storage-fault-tests/`のみ。`sources.json`へHEAD、実handler行番号、
全src/webファイルのSHA256を記録し、`handlers.inc`を毎回再生成する。

## B. 結果一覧

以下は3つのhost targetすべてで同じ結果。

| ID | 故障注入 | 実行結果・観測 | 判定 |
|---|---|---|---|
| H3 active | exists内部のactive openだけ1回失敗 | 元ファイル不変、load成功、Missing、defaults、出力許可。後続保存でactive上書き | 再現成功 |
| H3対照 | exists成功後の明示active open失敗 | IoError、出力禁止 | 問題なし（この分岐のみ） |
| H3 reset | NVS intentなし、有効pending journalのexists内部openだけ失敗 | journalは残るがResetBoot::Normal、blocked=false | 再現成功 |
| H4 Wi-Fi Action | chain encode内3回目のallocateだけ失敗 | overflow=true、構文有効、HTTP200、16→15 Actions、load後も15 | 再現成功 |
| H4 GET Action | 同上 | HTTP200、全体JSON構文・schemaとも有効、16→15 Actions | 再現成功 |
| H4 Wi-Fi Rotation | rotation encode内4回目のallocateだけ失敗 | overflow=true、構文有効、HTTP200、16→15 Outputs、load後も15 | 再現成功 |
| H4 GET Rotation | 同上 | HTTP200、全体JSON構文・schemaとも有効、16→15 Outputs | 再現成功 |
| H4対照 | 別のallocate失敗点 | JSON構文は有効だがschema不正、Wi-Fi保存HTTP400、active不変 | 問題なし（この分岐のみ） |
| H1復旧 | rename直後の最初のactive openだけ失敗 | HTTP400、diskはstagedと完全一致する新設定、RAM defaults、出力禁止。loadConfigで新設定復旧 | 再現成功 |
| H1後続上書き | 上記→実GET handler→取得defaultsの再保存 | HTTP200で正常な新設定をdefaultsへ上書き。再loadでもdefaults | 再現成功 |
| H2通常commit対照 | HID保持＋両backend pending＋Engine Wait、rename後open失敗 | HTTP400、出力禁止、jobs=0、pending破棄、HID解除、入力discard=1 | 問題なし（この分岐のみ） |
| H2 Wi-Fi | 同条件で実Wi-Fi PUT handler | HTTP400、出力禁止なのにjobs=1、両pending残存。tick再開でMIDI 1件送信、HID保持、入力discard=0 | 再現成功 |

## C. 各問題の詳細

### H3：存在判定の誤分類

構成：本番`loadConfig()`と`factoryResetBoot()`、テスト専用LittleFS。
SDK `libraries/FS/src/vfs_api.cpp:86–98`と同じくexistsがread openを行い、
失敗理由を区別せずfalseを返すようモデル化する。mapからファイルを削除しない。

`failExistsPath`と`existsFailures`で対象パス・回数を限定する。
`injected==1`とディスク内容の不変を確認してから、Missingと後続上書きを確認。
対照としてexistsは成功、次の通常openだけ失敗させるとIoErrorになる。

原因：`src/config_store.cpp:103–107`はexists=falseをMissingとする。
`src/factory_reset.cpp:38–45`もjournal不存在として扱う。
後段のopenエラー処理は保護になるが、exists内の失敗はそこまで届かない。

修正後の期待：確実な不存在のみMissing／journalなし。open不能はI/Oエラー・停止。
限界：SDKのFSクラス自体をリンクした試験ではなく、その失敗意味を再現したfake。
NVSは「intentなし」のみをモデル化し、NVS故障やSDK初期化は試験しない。

### H4：配列要素追加時のallocator失敗

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

修正後の期待：不完全encodeは保存／成功GETとして完了させない。元の配列を保持。
限界：単発allocator故障（次のallocateは成功）を注入する試験。
実機でのOOM頻度、持続OOM、heap断片化の発生確率は評価しない。
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

### H2：Wi-Fi失敗時panic漏れ

構成：本番Engine／MIDI backend／Keyboard backendをリンク。
BLE経路でHID Usage4を実際にfake transportへ送信し押下状態を作る。
transportを一時書込不能にしてUsage5とMIDIをキューに残し、Engineに10秒Waitを開始。
通常commit／Wi-Fi PUTそれぞれにrename後active open失敗を注入。

`main.cpp`のHTTP handler、panic、resetGuard、result、jsonResponseを
毎回機械抽出し、そのコードをそのままコンパイルする。handlerの条件分岐を
テスト側で手書きし直していない。抽出形式が変われば生成を失敗させる。

Wi-Fiでは出力禁止になってもjob=1、MIDI/HID pendingが残る。
transportを復旧して本番backendsTickを呼ぶとMIDI 1件と押下HID reportが送信される。
通常commitではjob=0、pendingが破棄され、tickで空HID reportが送られる。
入力discard関数はcounterで代用し、呼ばれるかどうかを比較する。

原因：`src/main.cpp:99`は保存失敗ですぐreturn、通常commitの`:155`には
出力不可の場合のpanicがある。`:302`のbackend tickはConfig出力許可と独立。
Engineの新規実行・Rotation送信のguardだけではbackendキューを停止できない。

修正後の期待：適用失敗で出力不可に移行した場合、両handlerで同じ取消・HID解除要求。
限界：WebServer/TCP/Wi-Fi stack、入力FreeRTOS queue、実Bluetooth通信はfake。
loop全体は実行せず、本番loopが行うbackend tickを明示呼出しする。
HID解除をホストへ送信するロジックまでは確認するが、実接続先への到達保証ではない。

## D. テスト基盤

- `tests/run_storage_fault.ps1`：独立runner、S3/C5/C6を順番にcompile/run。
- `tests/storage_fault/generate.py`：実handler/helper抽出、ArduinoJson shim、source hash。
- `tests/storage_fault/tests.cpp`：H3→H4→H1→H2、対照ケースを含むcharacterization。
- `tests/storage_fault/LittleFS.h`：パス限定exists-open故障、rename直後のactive-open故障。
- `tests/storage_fault/json_fault.h`：公開Allocator、対象encode内の単発N回目故障、観測値。
- `tests/storage_fault/model_adapter.cpp`：実encode呼出しのscope wrapper。
- `tests/storage_fault/http_adapter.h`：route登録、request引数、response status/bodyの収集。
- `tests/storage_fault/reset_sdk.h`と4つのSDK header：journal試験用NVS absent adapter。
  想定外のerase／format／NVS writeには例外を出し、誤った経路を通らないようにする。
- 本README：結果・再現条件・引継ぎ。

既存host fake／runnerは今回変更しない。productのsrc・WebUI・API仕様・formatも変更なし。
test includeパス・wrapper・allocatorは専用runnerだけに設定。
本番PlatformIO環境へ故障注入用define／include／sourceを追加していない。

## E. 回帰・ビルド

- 既存`tests/run_host.ps1`：成功。Factory Reset、通常storage／Engine／backend／Rotation、
  Capabilitiesを含む。既存runnerに含まれるC3の試験も実行されたが、C3対応作業ではない。
- 新規runner：S3/C5/C6すべてcharacterization成功。
- Standard firmware build：S3/C5/C6すべて成功（合計2分52秒）。
- `tests/check_image.py --env xiao_esp32s3 xiao_esp32c5 xiao_esp32c6`：すべて成功。
  partition、image配置、Flashサイズ、機種、USB descriptor gatingを確認。
- 最終fault-test生成時の全src/web SHA256と作業終了時ファイルが一致。
- 実機試験：未検証。

ログ：`%LOCALAPPDATA%\Temp\opencode\storage-fault-existing-host.log`、
`storage-fault-new.log`、`storage-fault-standard-build.log`。

## F. 次の修正へ

推奨順序：
1. H2：Wi-Fi適用失敗時のpanicを通常commitと同じ条件で実行。
2. H3：確実な不存在とI/O失敗を区別。
3. H4：各encode直後のoverflow確認、GETの不完全成功防止。
4. H1：commit済み適用失敗状態と復旧導線、復旧前の上書き可否を設計。

H2/H3/H4は局所的な修正候補。H1は「ディスクに新設定はあるが使えない」状態を
どう表示・回復するかの判断が必要。二重ConfigやHTTP非同期化は前提としない。

修正後の安全性テストは現行不具合をassertする本suiteとは別に扱うこと。
移行先の期待値：H3はIoError／停止、H4は欠落設定を成功させない、
H1は誤上書き不可または明示確認、H2は取消と解除要求あり。
このREADMEの期待値は設計候補であり、修正後テストが合格したという報告ではない。

追加調査：実ESP32 allocator特性、SDKの同期エラー伝達、Flash電断時の永続性。
今回はH1–H4の論理経路の再現を完了し、修正・実機検証は未着手。
