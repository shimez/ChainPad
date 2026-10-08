# Phase E後 C3 / C5 / C6実機Regression

## 範囲・記録規則

- 開始時ソース: `47625b2`。S3のConfig / NimBLE host PSRAM配置変更後の非S3実機を確認する。
- 順序: XIAO ESP32C3 → ESP32C5 → ESP32C6。
- 下表は今回の実機検証のみを記録する。過去の実測、ビルド成功、host test成功で確認済みにはしない。
- 各機種で識別・既存状態のバックアップ後に書き込む。Firmware hash、Flash容量、ポート、試験設定、受信ログ、メモリ計測を記録する。
- Wi-Fi認証情報や全Flashバックアップはリポジトリ外へ保存する。
- Serial接続が再起動を引き起こす機種では、連続動作試験とSerial計測を分け、uptimeで確認する。
- 試験終了時の設定復元とFirmware状態も記録する。

## 実機確認表

| 項目 | C3 | C5 | C6 |
| --- | --- | --- | --- |
| 識別・バックアップ・書込み検証 | 確認済み | 交換後個体で確認済み | 交換後個体で確認済み |
| 起動・Wi-Fi・BLE接続／再接続 | 未確認 | 未確認 | 未確認 |
| 実機配信WebUIの編集・保存・再読込・再起動復元 | 未確認 | 未確認 | 未確認 |
| 通常Action Chain（OSC / MIDI / HID / Wait） | 未確認 | 未確認 | 未確認 |
| 物理キー・Encoder Push・CW/CCW Chain | 未確認 | 未確認 | 未確認 |
| Rotation OSC Int / Float・BLE MIDI CC実受信 | 未確認 | 未確認 | 未確認 |
| Stop / Wrap・Position保持／初期化・Previewとの一致 | 未確認 | 未確認 | 未確認 |
| 最大224 Actions＋16 Outputsの保存・再起動復元 | 未確認 | 未確認 | 未確認 |
| 通常Chainと16 Outputsの併用・入力応答 | 未確認 | 未確認 | 未確認 |
| 内部RAM・最大連続空き領域・stack low-water | 空設定測定済み・入力初期化失敗 | 空設定測定済み・確保失敗あり | 空設定測定済み |
| PCの実機WebUI目視レビュー | 未確認 | 未確認 | 未確認 |
| 実スマートフォンのWebUI目視レビュー | 未確認 | 未確認 | 未確認 |
| 終了時設定復元 | 未確認 | 未確認 | 未確認 |

## WebUI目視レビュー

PCと実スマートフォンでRotation Value画面を確認する。ブラウザのモバイル表示エミュレーションは補助に留める。

- 機種、端末／OS／ブラウザ、画面サイズ・向き、Firmware hashを記録。
- Mode、Range Steps、Initial Position、Boundary、Linked Outputsの追加／編集／削除、重複警告を確認。
- 保存済みRuntime、未保存編集値、ブラウザ専用Previewの区別を確認。
- OSC Int / Float、BLE固定表示、入力エラー、保存・再読込の操作導線を確認。
- PC／スマートフォンのスクリーンショットと再現手順を各指摘へ紐付ける。Wi-Fi認証情報を含めない。

### 指摘票

各指摘は以下の形式で追記する。

- ID / 機種 / 端末環境:
- 分類: 機能不具合、またはUI/UX改善候補
- 操作手順・設定値:
- 期待結果 / 実際の結果:
- スクリーンショット・ログ:
- 対応・再検証結果:

UI/UX改善候補はこのRegression中には修正せず、終了後にユーザーと確認して別途指示を受ける。
保存不可・誤った表示値などの機能不具合は原因を調査し、修正と実機再検証を行う。

## 進捗

以下は調査順の履歴を含む。「ビルド中」等は各段階当時の記録であり、最新の確認範囲は上表と各個体の末尾を参照する。
本記録のコミットはRegression合格・安定版リリースを意味しない。

### C6 — 別個体の比較計測準備

- ユーザー申告により以前のPhase E試験とは別個体。過去のC6合格項目を引き継がない。
- COM42、ESP32-C6FH4 rev0.2、Flash4MB、BASE MAC `10:bd:a3:b2:19:5c`。
- stub方式の全Flash読出しで応答停止。115200 baud・容量4MB明示のROM方式でも
  `Packet content transfer stopped`。手動download mode＋自動resetなしでも同じ転送停止を確認。
  ログ: 外部`regression-c6b-backup-manual.log`。バックアップ未完了。
  USBケーブル・接続経路交換後（USB location 1-4→1-5、同じMAC/COM42）も
  115200 baudのstub読出しで`The chip stopped responding`が再発。
  ログ: 外部`regression-c6b-backup-new-cable.log`。
  深追いせず、別C6個体への交換を依頼する。個体故障とは断定しない。
- この個体への書込みはまだ行っていない。

### C6 — 交換後個体

- COM22、ESP32-C6FH4 rev0.1、Flash4MB、BASE MAC `54:32:04:33:4a:a0`。
- 全Flash読出し成功: 外部`regression-c6c-before-full-flash.bin`、4,194,304 bytes。
  SHA256 `826b631bc629e73bbae8a4c69a92fa08d73d97c0fb305e5068d2d67b90f709fc`。
- 旧partitionはnvs/otadata/factory/config_nvs。新settings領域0x330000以降は全て0xff。
- 共通診断版image検査成功、factory imageを0x0へ書込み、esptool hash検証成功。
  application SHA256 `b5b732211f2bb174c166fdb50a87ee9211e064317ed621ffc6d710b7a29cf743`。
- 自動reset後のSerial診断要求は2回とも無応答。ユーザーによる手動RESET後に診断応答を確認。
  空設定でinputsReady=1、allocation failure=0、内部free133980 / min133304 / largest118772 bytes。
  書込みログ: 外部`regression-c6c-upload.log`。測定ログ: 外部`regression-c6c-memory-manual-reset.txt`。
  メモリ詳細は`c3-memory-investigation.md`。接続・最大設定・物理入力等の機能Regressionは未合格。

- C3の識別・全Flashバックアップ・Phase E書込み検証まで完了。起動後の検証はこれから。

### C5 — 比較計測

- ESP32-C5 rev1.0、Flash8MB、BASE MAC `10:bd:a3:ce:e5:6c`、COM26。
- 最初の個体はstub/ROM/手動boot/分割読出しでも転送停止。書込みなしで交換した。故障とは断定していない。
- 交換後: ESP32-C5 rev1.0、Flash8MB、BASE MAC `10:bd:a3:ce:db:cc`、COM11。
- 全Flashバックアップ8,388,608 bytes: 外部`regression-c5b-before-full-flash.bin`。
  SHA256 `6d4d600916cd71a968175f14ea9da0217976c71a348bf95a9affd3f65e9780c1`。
- 共通診断factory imageを0x0へ書込み、esptool hash検証成功。
  application SHA256 `61060a49d2950ea9b1f2378423164f24ffd3f2ee01d2f66530fb12e8ee9d1b53`。
- 空設定起動の入力初期化は成功。ただし内部RAM不足とloop中の確保失敗を記録。
  現行ボード設定はPSRAM有効であり、PSRAM未使用比較ではない。詳細はメモリ調査資料。
- 診断版を保持、全Flash復元は未実施。接続・送受信・WebUI・最大設定のRegressionは未合格。
- C5 PSRAM配置変更版を同個体へapplication-only書込み・hash検証。
  Config external=1、inputs-ready内部free85284 bytes（前版比+82708）、largest65524、failure0を実測。
  詳細は`c3-memory-investigation.md`冒頭のC5改善節。機能Regressionは継続中で合格扱いしない。
- PSRAM変更版: ユーザーがAP接続・WebUI表示成功を確認。Serialで後続free81012 / min67472 /
  largest61428、failure0を確認。エージェントPCのAP HTTPはtimeout。STA/保存/送信/再接続等は未確認。
- 続いてWi-Fi設定保存・Restartをユーザーが確認。Serialで設定再読込、STA=192.168.0.26、
  free79264 / min61408 / largest57332、failure0を確認。STA HTTP取得成功。
- OSC/WaitのAPI triggerで2 packet実受信一致を確認。ただし先行2試行は受信timeoutで、安定性未確定。
  過大Chainと不完全transactionのHTTP400拒否・旧設定保持を確認。元設定へ復元・GET一致確認済み。
  物理入力・BLE・Rotation・最大設定等は未完了。全体Regression合格ではない。
- ユーザー報告では先行UDP timeout時にFirewall許可ダイアログあり。許可後の再試験は20 Chain実行／
  OSC40 packet全一致。Bluetooth接続報告とstatusのHID接続済み表示も確認。
- BLE MIDIはWindows入力ポートを2回openし、各5組、計10組20 NoteOn/Offを実受信・bytes一致。
  元設定へ復元済み。Bluetoothリンク再接続、HID report実受信、物理操作はまだ未確認。
- 続報: Bluetooth OFF/ON後の再接続とMIDI再受信成功。allocation failure0、internal min46740。
  物理キー12個＋PushのPress/Release、CW3/CCW3でOSC32件一致、キー1のF13 down/upと
  キー2のNoteOn/OffをPCで確認。overflow/retry/rejection0。元設定復元済み。
- 224最大長OSC String Actions＋16最大長address Outputsの保存・GET一致・再起動復元成功。
  保存後のinternal min44108 bytes。元設定復元済み。詳細・未検証範囲はメモリ資料参照。
- 224最大長Actions保持下でCW3/CCW3操作、Rotation16 OutputsのOSC48＋BLE CC48を実受信・値/順序一致。
  最終Position64、pending/overwrite/discard/failed/overflow0。後続Serial failure0、min45908。
- 最大長224 OSC ActionsもChainごとのAPI triggerで全packet実受信一致。各試験後に元設定を復元。
  無線反復耐久・Wi-Fiリンク再接続・境界・OOM/I/O保存失敗等は未完了。全体Regression合格ではない。

### C3 — 識別・書込み

- ESP32-C3 rev0.4、Flash4MB、MAC `1c:db:d4:f0:c1:e4`、COM25。
- 全Flash4,194,304 bytesを外部`regression-c3-before-full-flash.bin`へ保存。
  SHA256: `16fec17bb7c5d56275b2fa29b61cbeacaee200a264f1b0f1167f8fec65a8be18`。
- 旧パーティション: nvs / otadata / factory / config_nvs。
  新LittleFS領域0x330000以降は全て消去状態であることをバックアップから確認。
- Phase E image検査成功、factory image書込み後のFlashハッシュ検証成功。
  `firmware.bin` SHA256: `3bd406492f38e68a9022b250f421b205ff0103c9892eba21c3e8411a76ef453e`。
- 書込み後のSerial snapshotは未取得。通常起動・ネットワークを次に確認する。

### C3-F01 — 起動後のAP接続不安定・LED点滅なし（調査中）

- 分類: 機能不具合。ユーザーがRESET後にLED非点滅、APが見えない／見えても接続失敗を報告。
- Serial実測: uptime=124128ms、Factory config、Wi-Fi mode=3、AP=192.168.4.1、STA未接続。
- 内部RAM free=2,212 / min=196 / largest=1,908 bytes。PSRAM=0。
  Config=82,648 bytes、アドレス0x3fc9af54（内部RAM）。S3用PSRAM配置は非適用。
- LittleFSはmounted、state=missing、total=851,968 / used=8,192 bytes。
  入力／送信overflow=0。空きRAM不足を有力原因として調査中で、LED非点滅の直接原因は未確定。
- ログ: 外部`regression-c3-boot-investigation.txt`。
- 起動各段階のfreeHeapとinputsReadyをSerialで取得する診断を追加。修正後の機能確認は未実施。
- 診断版実測: entry=136,288 → config=134,252 → backends=71,716 → Wi-Fi=6,616 → ready=2,056 bytes。
  `inputsReady=0`を確認。LED点滅にはinputsReadyが必要であり、入力初期化失敗と症状が整合する。
  ログ: 外部`regression-c3-startup-stages.txt`。
- C3限定で未使用のBLE Central / Observerを無効化する試験版をビルド中。
  Peripheral / Broadcaster、最大接続数、224 Actions、16 Outputsは維持。改善効果はまだ未測定。
- 最初のrole指定はSDKヘッダによる再定義警告を検出したため実機比較には使用しない。
  C3環境の強制includeヘッダでSDK読み込み後にNimBLE-Arduinoのroleを指定する方式へ変更し、再ビルド中。
- 旧名`CONFIG_NIMBLE_ROLE_*`による互換ヘッダの再定義も解除した試験版はビルド・image検査・書込み検証成功。
  image SHA256: `2371e431832de5b1ac912a5f060005b816140881b2ce610fce980763cfb18891`。
  ただし実機は起動中にabort・再起動を反復したため、不採用としてrole変更を撤去。
- 試験版ELFでPCを解決: `operator new` / `std::bad_alloc` → `WebServer::on` → `setupWeb`。
  元のメモリ不足は未解決。ログ: 外部`regression-c3-peripheral-stages.txt`。
  元のBLE構成の診断版を再ビルド中。C3は再起動ループを止めてdownload modeで待機。
- 元のBLE構成の診断版へ復旧完了。image検査・書込みハッシュ検証・Serial応答を確認。
  SHA256: `1ef894fcc8f620886f0f9505448924090e455e9ab44da52c5ea084a901f4b07a`。
  起動後10,874msのsnapshot: free=2,088 / min=1,800 / largest=1,908 bytes、inputsReady=0。
  試験版の起動時abort反復からは復旧したが、元のRAM不足・入力初期化失敗は未解決。
  ログ: 外部`regression-c3-baseline-restored.txt`。これは旧Firmware全Flash復元ではなく、Phase E＋起動診断の元BLE構成。
- 共通詳細診断で4096-byte確保の連続領域不足と、input-scanの3072-byte stack確保失敗を直接記録。
  詳細は`c3-memory-investigation.md`の「共通診断版: C3実測」。C3は未合格、通常送信/UI試験は未実施。
