# Storage tests：H2安全性回帰＋H1/H3/H4不具合再現

## A. 総括・作業基準

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
| H2通常commit対照 | HID保持＋両backend pending＋Engine Wait、rename後open失敗 | HTTP400、出力禁止、jobs=0、pending破棄、不要MIDI=0、HID解除、入力discard=1 | 安全性回帰PASS |
| H2 Wi-Fi | 同条件で実Wi-Fi PUT handler | 通常commitと同じ停止結果。HTTP応答前の取消も確認 | 安全性回帰PASS |
| H2入力検証対照 | 不正SSID型 | HTTP400、renameなし、active/RAM保持、panicなし、再起動予約なし | 対照PASS |
| H2書込失敗対照 | pending writeを1回だけ失敗 | HTTP400、renameなし、active/RAM保持、panicなし、再起動予約なし | 対照PASS |
| H2成功対照 | 故障なし | HTTP200、panic、SSID更新、750ms後の再起動予約、既存応答文を維持 | 対照PASS |

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
- `tests/storage_fault/tests.cpp`：H3→H4→H1のcharacterization、H2安全性回帰と対照。
- `tests/storage_fault/LittleFS.h`：パス限定exists-open故障、rename直後のactive-open故障、pending限定write失敗。
- `tests/storage_fault/json_fault.h`：公開Allocator、対象encode内の単発N回目故障、観測値。
- `tests/storage_fault/model_adapter.cpp`：実encode呼出しのscope wrapper。
- `tests/storage_fault/http_adapter.h`：route登録、request引数、response status/body収集、応答前の状態検査hook。
- `tests/storage_fault/reset_sdk.h`と4つのSDK header：journal試験用NVS absent adapter。
  想定外のerase／format／NVS writeには例外を出し、誤った経路を通らないようにする。
- 本README：結果・再現条件・引継ぎ。

既存host fake／runnerは今回変更しない。本番変更はWi-Fi失敗分岐の1行だけ。
WebUI・API仕様・format・Config/Action容量は変更なし。新しい本番状態・キューは追加なし。
test includeパス・wrapper・allocatorは専用runnerだけに設定。
本番PlatformIO環境へ故障注入用define／include／sourceを追加していない。

## E. 回帰・ビルド

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

推奨順序：
1. H3：確実な不存在とI/O失敗を区別。SDKのエラー取得方法とjournalへの適用範囲を先に確認。
2. H4：各encode直後のoverflow確認、GETの不完全成功防止。
3. H1：commit済み適用失敗状態と復旧導線、復旧前の上書き可否を設計。

H2は今回修正済み。H3/H4は局所的な修正候補。H1は「ディスクに新設定はあるが使えない」状態を
どう表示・回復するかの判断が必要。二重ConfigやHTTP非同期化は前提としない。

H2安全性回帰とH1/H3/H4の現行不具合をassertするテストを区別すること。
移行先の期待値：H3はIoError／停止、H4は欠落設定を成功させない、
H1は誤上書き不可または明示確認。これら未修正項目の期待値は設計候補。

追加調査：実ESP32 allocator特性、SDKの同期エラー伝達、Flash電断時の永続性。
H2修正・host安全性検証を完了。H1/H3/H4修正・実機検証は未着手。
