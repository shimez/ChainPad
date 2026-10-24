# OBS通信基盤 — Phase 1

基点: main `6faa8641dee13c005a76562e2c6d5d87d55ef596`。
参照PoC: `b812a2dbe68520d0fdac8738c0c78ade0e7127de`。

## 構成・契約

- `obs_backend`: S3専用Worker、固定FIFO、設定コピー、公開受付API。
- `obs_protocol` / `obs_auth`: PoC由来の上限制御・OBS 5.xプロトコル・認証。
- `obs_lifecycle`: 排他区間内で扱う設定/Wi-Fi/実行/セッション世代。
- `obs_test_console`: `CHAINPAD_OBS_TEST`明示ビルドだけの実機試験経路。
- 非S3はinlineスタブ。USB capabilityとは独立した`CHAINPAD_HAS_OBS`を使用。
- 通常S3もWorkerは起動するが初期Disabled。Phase 1の通常版には有効化/送信する外部APIなし。
- Action/Config/Preset/LittleFS/Web UIは変更なし。公開capability追加はPhase 2へ延期。

公開APIはArduino loopから呼ぶ。`obsStatusSnapshot()`もloop上で取得する。
WorkerはConfigを参照せず、単一スロット設定メールボックスから値コピーを受け取る。
`obsApplySettings()`は同一値でも設定世代を更新し、終端エラーを明示再試行する。
`obsDispatch(scene,id)`のAcceptedは受付だけで、OBS成功は`obsNextResult()`で別途取得する。
SceneはUTF-8で1..192 bytes、passwordは0..128 bytes。制御文字は拒否。
Request IDは`chainpad-N`、起動内で再利用せず、上限到達時は拒否。
世代カウンタの上限到達時もfail-closed（Restart必要）。

### 停止と競合

Panicは短い排他区間で実行世代を更新し、直ちに新規受付と古い送信開始を禁止する。
Workerがセッションを閉じ、保留要求を破棄して再接続するため、新規操作が恒久拒否されることはない。
Stop Other Chainsはバックエンドへ通知しないため、受付済み要求を維持する。

**送信開始の境界は、JSON生成後、世代を排他区間で再検査してclaimした瞬間。**
ここより前の要求は未送信として取り消せる。ここからSocket呼出し/書込み完了まではin-flight。
Panicがその間に来てもOBS側実行は取り消せない。ネットワークI/Oをロック内に置いたり、
PanicがI/O完了を待ったりしない。結果コード-2は世代によるretireであり、外部取消成功を意味しない。
送信失敗(-3)・応答期限切れ(-4)も外部結果不明。再送はしない。

Wi-Fi切断イベントで世代を即時更新する。短時間で復帰してWorkerが切断状態を直接観測しなくても
旧セッションは無効。STA接続時だけ接続試行する。AP-onlyでは試行停止。
本体の30秒切断→AP-only固定→Restartで復帰という仕様は変更しない。
認証失敗/Kick等の終端エラーは、PanicやWi-Fi復帰では解除せず設定再適用で解除する。

### 上限・所有権

Worker: Arduino実行コア、優先度1、8192 B stack、10msごとにyield。
要求FIFO4、結果FIFO8、同時送信待ち1。結果FIFO満杯はdrop counterへ記録。
Socket操作/JSON生成/認証はWorkerだけ。通常loopは状態更新と待ち時間0の受付のみ。
HTTP header 1行512/合計2048 B、pump最大128 B。Frame/組立2048 B、JSON割当予算8192 B。
接続/認証/断片期限5秒、応答3秒、TCP設定1秒、heartbeat 10秒/3秒/1、backoff 1/2/4/8/16/30秒。
I/Oから戻るまで期限確認できないため、Workerの厳密な実時間上限ではない。
新しい接続試行ごとにprotocol stateをConnectingへ戻し、以前Readyだった場合もheader期限を検査する。

サーバー由来コメントは保存/表示しない。公開診断は数値/状態のみ。
設定・認証payloadはログ出力しない。テスト入力もエコーしない。
パスワードはRAMコピーだけに保持し、GET/Preset/保存へ流す処理は存在しない。

## ビルド・PC試験

```powershell
pio run -e xiao_esp32s3 -e xiao_esp32c3 -e xiao_esp32c5 -e xiao_esp32c6 -j 2
pio run -c platformio.obs-test.ini -e xiao_esp32s3_obs_test -j 2
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_obs.ps1
.venv/Scripts/python.exe tests/obs/check_patch.py
.venv/Scripts/python.exe tests/obs/check_isolation.py
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_host.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tests/run_storage_fault.ps1
Get-ChildItem tests/test_*.cjs | ForEach-Object { node $_.FullName; if ($LASTEXITCODE) { throw $_.Name } }
.venv/Scripts/python.exe tests/check_image.py --env xiao_esp32s3 xiao_esp32c3 xiao_esp32c5 xiao_esp32c6
```

WebSockets 2.7.2のみ、S3の環境別libdepsにパッチを適用。
`scripts/patches/WebSockets-2.7.2.patch`はレビュー用の正確な差分。
スクリプトは元ファイルのSHA256を固定し、全対象を検証してから変更する。
原本/適用差分をlibdepsへ保存。未知ソース/未知編集では失敗、再適用は冪等。
Nativeテストは本番protocol/auth、パッチ済み上流WebSockets.cpp、本番Worker/APIを実行する。
Worker試験のRTOS/Socket/Wi-FiはPCアダプタ。ESP32でのスケジュール/実TCP/OBS Studioの証明ではない。

## 実機試験手順

テスト用イメージ: `.pio/build-obs-test/xiao_esp32s3_obs_test/firmware.bin`。
通常版とビルド出力を分離。115200 baud。既存Wi-Fi設定でSTA接続させる。
以下は例示値であり、接続先・シーン・passwordを端末で置換する（ソースに記入しない）。

```text
!{"op":"settings","enabled":true,"ip":"192.0.2.1","port":4455,"password":"EXAMPLE_ONLY"}
!{"op":"status"}
!{"op":"scene","name":"ゲーム画面"}
!{"op":"status"}
!{"op":"scene","name":"ゲーム画面","count":16}
!{"op":"panic"}
!{"op":"status"}
```

- 1行LFで実行。CR無視、ESCで破棄、最大1024 B、超過行は拒否。端末のlocal echo/履歴は端末設定による。
- state: 0 Disabled / 1 Disconnected / 2 Connecting / 3 Authenticating / 4 Ready / 5 Error。
- admission: 0 Accepted / 1 Unavailable / 2 Busy / 3 Failed。成功はresultのsuccess=1/code=100を確認。
- statusは最大8件の結果を回収し、内部Heap free/min/largest、PSRAM free/used、stack余裕、loop最大間隔、接続/認証/応答msを表示。
- scene countは最大16件の連続受付。4件以上のburstでBusyを観測（Workerの並行処理により受付数は変わる）。
- 無効化はsettingsをenabled:falseで再入力。再試行は現在のsettingsを再入力。設定はRestartで消える。
- `panic`コマンドはOBSだけの試験。全体Panicは既存Configuratorのボタン/APIで別途確認する。

実機で確認する項目:
1. 認証あり/なし、誤パスワードでError→正しい設定再適用、存在/不存在シーン。
2. OBS停止/再起動、切断中の拒否、受付済みの保留要求も再接続後に実行されないこと。
3. Wi-Fi切断30秒未満で自動復帰、30秒以上でAP-onlyとなりattempts増加停止、Restartで復帰。
4. Panic前の未送信要求の破棄、Panic後に再接続して新規要求成功。in-flight結果は不明となり得る。
5. OBS有効/無効/無応答/切断反復と最大設定GET/保存中にHID/MIDI/OSC/Wait/Stop/Web UIが動くこと。
6. 同じ実機・設定でmainと比較し、入力→出力遅延/HTTP時間/overflow/Heap最小/最大ブロック/stackを記録。

OBS側の応答欠落・過大フレーム・分断headerはPC試験で注入する。実TCP版では制御可能なサーバーが必要。
測定値のloop最大間隔は入力遅延そのものではない。

## 検証記録

通常S3/C3/C5/C6ビルド・imageチェックPASS。
実機試験専用S3ビルドPASS（依存を新規取得してパッチ適用）。
ELF/オブジェクト/イメージ/依存ディレクトリで、非S3へのOBS混入なし、通常版へのテストconsole混入なしを確認。
`tests/obs/check_isolation.py`のPhase境界検査もPASS。
host（通常/trace/各capability）、全`tests/test_*.cjs`、保存障害（S3/C5/C6各932 H4ケースを含む）PASS。
OBS Native（protocol/auth、実WebSockets wire parser、実Worker+RTOS/Socket adapter、test console）PASS。
HTTP header上限/期限、誤認証、1/2/4/8/16/30秒backoff、Panic競合、旧要求非再送もPC試験で確認。
依存パッチのクリーン原本からの再適用・差分一致・冪等性・未知変更拒否PASS。
最初のS3リンク失敗はcapabilitiesのsdkconfig include順序で修正。
host/storageの最初の実行は240秒のツール期限を超えたため、期限なしbackgroundで再実行して完走した。

ビルド静的使用量（B、動的Heapピークは含まない）。比較元mainは基点コミットの既存測定記録。

| 環境 | main RAM | Phase 1 RAM | 差分 | main Flash | Phase 1 Flash | 差分 |
|---|---:|---:|---:|---:|---:|---:|
| S3通常 | 96,044 | 110,556 | +14,512 | 1,373,942 | 1,566,694 | +192,752 |
| S3試験用 | 96,044 | 111,604 | +15,560 | 1,373,942 | 1,572,362 | +198,420 |
| C3 | 136,332 | 136,332 | 0 | 1,436,149 | 1,436,175 | +26 |
| C5 | 65,736 | 65,736 | 0 | 1,651,951 | 1,651,977 | +26 |
| C6 | 141,548 | 141,548 | 0 | 1,598,346 | 1,598,372 | +26 |

S3 ELFでtaskStack=8192、requestStorage=864、resultStorage=96、settingStorage=152 Bを確認。
FIFO件数は4/8/1のまま。PoCとの差は主にシリアル編集領域・結果コメント領域の除外。
上表は通信基盤チェックポイント`e5c2c65`の測定値。後続のUSB CDC改善後の
最新ビルド使用量・試験結果は[serial-output.md](serial-output.md)を参照。

## Phase 1完了整理（2026-10-24）

ユーザーの完了判断に基づき、通信基盤とUSB CDC改善のStage 1/2/3-Aを完了として整理する。
通信基盤は`e5c2c65`でチェックポイント済み。後続CDC改善は未コミット。
ユーザー実施のS3 OBS test実機ログでは認証あり接続、Ready維持、16件の受付表示、
Accepted 4件の成功結果、通常診断との交互出力を確認した。Astraによる実装・ログ照合もPASS。
接続/認証/応答時間とHeap/stackの観測値・測定時点の限定は上記リンクに記録する。
以前の実機レビューではAP-onlyでの接続試行停止とUnavailable受付もPASSとされた。
全文ログや接続先・認証情報はユーザー側保管とする。

完了は全条件の実機保証を意味しない。短時間Wi-Fi断からの自動復帰、queued/in-flightを
区別したPanic取消、既存機能との共存、反復・長時間動作等の実機検証には残項目がある。
USB CDC Stage 3-B/Cは未実施・保留であり、Phase 1完了の必須条件から外す。
PC試験のPASSをこれら実機試験のPASSに置き換えない。

次段階はPhase 2「Web ConfiguratorでのOBS接続設定・保存・状態表示」の設計レビュー。
Action ChainへのOBS操作統合はPhase 3。今回の整理ではWorker動作・モデル・保存・UIの
コード変更、実機書込み、Commit/Push/Mergeを行わない。

今回の整理時にOBS Native、serial T01–T18、依存パッチ検査、OBS/serial成果物分離検査、
通常4ボードのimage検査を再実行しPASS。S3通常/OBS test、C3/C5/C6通常と両traceの
計7構成は、前回の最終成功ビルドを採用した。本番ソース・ヘッダー・確認対象ビルド設定の
更新時刻が各ELFより後でないことも確認した（ハッシュ付き再現ビルドの証明ではない）。
今回の変更は文書だけであり、host/trace/capability・保存障害の既存PASSも引き継ぐ。
