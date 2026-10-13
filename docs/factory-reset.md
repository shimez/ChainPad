# Factory Reset（安定版ベース）

基準：main `7abb5959cba64b102bfcc3168d4be8fb560619b5`。
Mini容量変更／HTTP試作は使用せず、同期HTTPと通常のConfig保存・読込形式を維持する。

## UIとAPI

- 通常WebUIへMaintenanceを新設。既存の「実行中止・HID解除」「Restart」は移動のみ。
  onclick、API、未保存確認文は変更していない。Wi-Fi欄の既存Restart導線も維持。
- Factory Resetは別の折りたたみ領域と確認ダイアログで対象を明示する。
- APのWi-Fi画面にもFactory Resetのみ追加。Configロードから独立したscriptで動作する。
- `GET /api/factory-reset`：idle／accepted／complete／failed。
- `POST /api/factory-reset`：JSON `{"confirm":"factory-reset"}`、最大128文字。
  確認欠如400、要求永続化失敗500、受付202。GETでは消去しない。
- 受付済みの同一boot内の二重要求はNVSを再書込せず、再起動期限も延長しない。
- 受付または復旧失敗中はConfig取得・全保存API・triggerを409で拒否し、通常のAction実行を停止する。
  Factory Reset自身はConfig状態やLittleFSのmount成功に依存しない。
  通常boot時の保存／読込の意味は変えない。

## 対象

- `settings` LittleFSを明示labelでformat：/config.records、/pending.records、その他のファイルを削除。
  Wi-Fi認証情報、OSC送信先、Action、Rotationを含む。
- `nvs`（data/nvs）の全範囲だけを消去し、全byteが0xffであることを確認。
  BLE bonding／CCCD、旧Wi-Fi保存情報、SDK管理情報を含む。必要なSDK情報は次回起動で再生成される。
- firmware／埋込みHTMLはfactory app内なので保持。partition table／otadataも触らない。
- PC/スマホ側のBluetooth登録、ブラウザーのテーマ、ダウンロード済みPresetは対象外。
  PC側の登録解除・再ペアリングを案内する。
- LittleFS formatは論理的初期化であり、消去済み秘密情報の物理的な復元不能性を保証する機能ではない。

## 二段階再起動と永続journal

1. HTTP受付時、NVS namespace `chainpad_reset`、key `attempt`へ0をcommit。
   成功後にpanicと再実行禁止を行い、202「受付」を送信。
2. handlerから戻って約750ms後、1回目の再起動。送信成功はブラウザ受信完了の保証ではない。
3. setupのConfig読込・backends／Wi-Fi／入力起動より前にreset flowを実行。
4. 試行番号1〜3をNVSへ保存してからsettingsをformat。
5. 空のsettingsに`/factory-reset.pending`を作る。内容は`FR1:1`等の試行番号のみ。
   tmpへwrite/flush/close→renameで置換し、再読込して確認。Configは複製しない。
6. NVSをdeinitして消去・検証。この段階ではWi-Fi/BLEを起動していない。
7. pendingを`/factory-reset.done`へrenameし、2回目の再起動。
8. 次回はdoneを検出しcompleteを表示。通常loadConfigはconfig.records不在を初回設定と扱う。
   Wi-Fi SSIDが空なので保存先STAへ接続せず、ChainPad-Setupで再設定する。

doneは「前回の初期化が完了した」記録として保持し、次のFactory Resetで消去する。
受付側で消去完了とは表示しない。完全初期化後に設定領域が物理的に空という意味ではなく、
初回状態＋資格情報を含まない小さな完了記録になる。

## 電源断・失敗・停止条件

- settings初期化前／途中：NVSに残った試行番号から、次bootで再試行。
- NVS消去前／途中／消去後rename前：settings側のpendingから再試行。
  NVSが部分消去で読めなくても、validなpendingがあればNVS消去を続行できる。
- 各破壊操作より前に試行番号を永続化。最大3回。途中停止で未実施でも1回消費する場合がある。
- format／journal書込／NVS消去・検証／doneへのrename失敗は、そのbootで停止。
  **失敗では自動再起動しない**。手動再起動で残回数を使って再試行する。
- 3回到達、破損journal、NVS読込不能かつjournalなしはfailed。
  Configをloadせず、BLE/backend・入力を開始せず、出力停止のままAP/Web復旧を試みる。
  Flash/NVS障害そのものによってAPも開始できない場合はWeb Installerへ案内。
- failedから新しい明示POSTが成功するとNVS attempt=0になり、もう一度最大3回の枠を与える。
  malformed/旧pendingもこの新要求を優先してformatする。
- NVS要求をcommitできない場合は消去を開始せず500。無理にNVSを消すfallbackはしない。
- 完了rename後の電源断は次回complete扱い。自動消去の反復はしない。
- 両partitionが同時に破損する等、永続記録自体が失われる障害を原子的に回復する保証はない。

## 変更ファイル

- src/factory_reset_flow.h：独立した試行・再開state machine。
- src/factory_reset.h/.cpp：NVS／LittleFS adapter、要求受付、status。
- src/main.cpp：独立API、通信前のreset、受付後・失敗時の限定guard。
- web/index.html、web/wifi.html、web/maintenance.js：UI配置・確認・状態表示。
- scripts/build.py：共通maintenance scriptを両画面にinline化。
- tests/host/factory_reset*_tests.cpp、tests/reset_platform/*、tests/run_host.ps1：
  state machineと実adapterのhost fault injection。
- tests/test_maintenance.cjs：確認／キャンセル／二重操作／失敗／状態表示／UI配置。

## 検証結果

- host：新resetテストと既存full suite、C3/C5/C6 Capabilities suite成功。
  正常・corrupt・oversized設定、mount失敗、二重要求、要求書込失敗、消去失敗、試行上限、
  各永続操作直後の電源断、部分NVS、破損journal、明示再試行を検証。
  adapter testは本番factory_reset.cppをfake SDK/FSに接続して実行。
- Node：Maintenance、Preset、Rotation Preview成功。
- Python既存14テスト成功。
- 最終ソースのS3/C3/C5/C6 firmware build：4機種すべて成功。
  `tests/check_image.py`も全環境成功（partition範囲、image配置、Flashサイズ、機種名、USB descriptor gating）。
- 電源断は状態保存を模擬したhost試験であり、実Flashへの電源断試験ではない。
  実機のAP復帰・BLE bond消去／再pair・NVS calibration再生成・Flash故障時の挙動は未検証。

ログ：`%LOCALAPPDATA%\Temp\opencode\factory-reset-host.log`、`factory-reset-build.log`、`factory-reset-build-final.log`。
書込み・commit／pushは行っていない。

## 実機での確認

既知のWi-Fi、OSC、Action、Rotation、BLE bondを保存し、設定のExportを必要に応じて実施。
通常Maintenance／AP Maintenanceの両方から実行し、受付→2回再起動→APセットアップ→completeを確認。
PC側Bluetooth登録を削除して再ペアリングする。通常のpanic／Restartも回帰確認する。
WebUIに到達できなければ https://shimez.github.io/ChainPad/installer/ のErase deviceで再インストール。
BOOTを使う案内はUSBダウンロードモードの既存手順であり、物理Factory Reset操作は追加していない。
