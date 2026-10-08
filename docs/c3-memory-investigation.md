# C3 RAM調査 — 現行容量維持と共通容量削減の比較

上限変更・単一Action化は未実装。以下の容量削減量は試算であり、縮小構成の実機合格を意味しない。

本資料は調査順の記録。初期節の「未確認」「ビルド中」は当時の状態を示し、最新結果は
「共通診断版: C3/C5/C6実測」以降を参照する。この記録のコミットはRegression合格・安定版リリースではない。

## 方針更新: Standard / Mini候補とC5 PSRAM改善

ユーザー決定により、製品優先はC6/S3 Standard（224 Actions／16 Outputs）。C5もPSRAM活用で
Standardを目指す。C3のみ共通コード内のMini候補（共有pool12→必要なら10→8、CW/CCW各8、Outputs16）。
以下の過去の共通容量削減推奨はこの決定で置き換える。今回はC3容量変更を実装しない。
エンコーダ排他化、Action union、単一Action化、保存形式変更は保留。

### C5 Config配置変更の実装調査

- 原因: `model.h/.cpp`のPSRAM分岐は`CONFIG_IDF_TARGET_ESP32S3`限定で、C5は静的`Config config`だった。
- 共通PlatformIOはpioarduino 55.03.311、Arduino/IDF SDKは各targetのprebuilt libraryを使用。
  C5ボードは`BOARD_HAS_PSRAM`、SDK `esp32c5/qio_qspi/include/sdkconfig.h`は
  `CONFIG_SPIRAM=1`、QUAD/80MHz、USE_MALLOC、ALWAYSINTERNAL4096、RESERVE_INTERNAL0。
  C5には`CONFIG_SPIRAM_BOOT_INIT`定義がない。S3のboot-init条件をC5へ偽装して有効化しない。
- Arduino core `esp32-hal-misc.c`のsystem init hookが`psramInit()`、その後`initArduino()`で
  `psramAddToHeap()`を呼ぶ。PSRAM認識とheapへの登録は別段階。
  S3のboot-initによるグローバルconstructor確保をC5へ直接コピーすると早過ぎる。
- 実装: 共通`allocateConfig()`の明示SPIRAM＋8BIT確保とplacement newを再利用。
  C5のみ`activeConfig()`内のfunction-local static referenceで初回確保し、初回呼出しは
  `setup → loadConfig`（Arduino初期化後）。参照取得箇所をaccessorへ置換し、データモデルは変更しない。
  他target/hostではaccessorは既存globalを返すinline関数。S3は従来のglobal reference確保とboot-init guard、
  C3/C6は静的内部Configのまま。S3限定NimBLE外部確保flagも変更しない。
- 確保失敗時は既存S3同様`abort()`し、nullへplacement newせず、不完全なConfig参照を返さない。
  内部RAM fallbackはしない。これは停止／再起動になるfail-fast方針で、PSRAM故障時のUI復旧機能ではない。
- Serial snapshotに`esp_ptr_external_ram`によるConfig外部判定とSPIRAM heap総量を追加。
  C5のentry checkpointはConfig確保前になるため、旧版entryとの差を定常改善量として扱わない。
  config-loaded以降と同条件snapshotで比較する。
- DMA等のcaps=0x80c要求は外部PSRAMへ転送していない。内部RAM解放が該当heapの余裕を改善するか実測する。
- rename後apply失敗リスクは既存課題のまま。今回storage transactionの順序・形式は変更しない。

### 検証状況

実装後のS3/C3/C5/C6 buildは全環境SUCCESS、`tests/check_image.py`も4環境PASS。
既存host core/storage/rotation/capability suiteは終了コード0。
hostは静的Configを使うためC5の実PSRAM初期化順・確保成功の証明にはならない。
ELF symbol確認: C5は82648-byte静的Configがなく、`activeConfig()::instance`4 bytes＋guard8 bytes。
S3は従来同様Config参照4 bytes。C3/C6は`config`が0x142d8＝82648 bytesのBSSとして残る。
これは静的配置確認であり、C5の実配置先はSerialのexternal判定で確認する必要がある。
C5 application SHA256 `95a016ffb457f7b1ef8c116e8dc3dd727a57ab1aa108c12c37db34c662f795f8`。
ログは外部`c5-psram-config-build.log`と`c5-psram-config-host.log`。
C5実機結果は次節。機能Regressionは継続中。commit/push未実施。

### C5 PSRAM移動後の実機測定

前回と同じCOM11 / MAC `10:bd:a3:ce:db:cc`。書込み前の現状全Flash8MBを外部
`c5-before-psram-full-flash.bin`へ追加バックアップ、SHA256
`f5ad9e4eee08fe0e414ae8a08de7294bcedcfb481a4ac5b8db1e7e0febca9974`。
元バックアップ`regression-c5b-before-full-flash.bin`のハッシュ一致も確認。
変更版applicationのみ0x10000へ書込み、hash検証成功。設定領域・partitionは書き換えていない。
最初のSerial要求は無応答、後の再要求で取得。ログは外部`c5-psram-memory-retry.txt`。

Config address=`0x42190908`、`esp_ptr_external_ram`によるexternal=1、PSRAM heap総容量8388608 bytes。
Action352 / Chain5636 / Config82648 bytes、224 Actions / 16 Outputsを維持。

| 段階 | internal free | minimum | largest |
| --- | ---: | ---: | ---: |
| entry（今回はConfig確保前） | 197820 | 197676 | 172020 |
| config loaded | 194716 | 193916 | 172020 |
| BLE host ready | 164884 | 159860 | 139252 |
| BLE services ready | 160924 | 159860 | 139252 |
| BLE advertising ready | 159168 | 159168 | 139252 |
| backends ready | 159208 | 159168 | 139252 |
| Wi-Fi mode ready | 105880 | 105392 | 86004 |
| AP/DNS ready | 98196 | 98196 | 81908 |
| STA requested | 98196 | 98196 | 81908 |
| mDNS ready | 92420 | 92420 | 73716 |
| Web ready | 89972 | 89716 | 73716 |
| inputs ready | 85284 | 85284 | 65524 |

起動checkpoint同士の比較: inputs-ready free2576→85284（**+82708 bytes**）、
largest2292→65524（+63232 bytes）。Configサイズ分に概ね一致する改善で、
差分は非同期処理・診断出力コード・allocator等も含みConfigのsizeofと完全一致するものではない。
config-loaded freeは117348→194716（+77368 bytes）。entryは確保時点が異なるため比較基準にしない。

後続snapshotはuptime13742395ms（約3.8時間）、internal free85692 / min83708 / largest65524。
前版snapshot free2976との差は+82716 bytesだが、前版uptime14766msとは時間条件が違う。
PSRAM free8381464→8298800（82664 bytes減）、allocated4736→87384（82648 bytes増）。
PSRAM min=0は以前からの表示で、今回もPSRAM枯渇と解釈しない。
inputsReady=1、AP=192.168.4.1、STA未接続、BLE起動checkpointは完了、allocation failure総数0。
以前のcaps=0x80c要求失敗は今回の記録期間では未再現。接続／再接続負荷での解消保証は未確認。

stack low-water bytes: loopTask5720、input-scan2660、nimble_host4076、wifi4868、tiT3892、
esp_timer8268、IDLE2064。overflow/retryカウンタ0。空設定、保存recordなし。
約3.8時間の経過中の接続・操作条件は連続追跡しておらず、長時間Regression合格とはしない。
APクライアント接続・WebUI・STA/OSC・BLE送信・物理入力・高負荷保存・失敗保護は未実施。

続報: ユーザーがAP接続とWebUI画面表示成功を報告。エージェントPCからのAP HTTP要求はtimeoutのため、
API応答・画面内容の自動検証は未完了。Serial後続測定（uptime13884052ms、リセットなしの継続値）では
internal free81012 / min67472 / largest61428、failure0、inputsReady=1、Config external=1。
ログ: 外部`c5-psram-after-ap-ui.txt`。Wi-Fi task low-water4292 / tiT3780 bytes。
AP/UI確認後も空きRAMは維持されたが、保存・STA/OSC・BLE接続送信等の合格を意味しない。

続報: ユーザー操作でWi-Fi設定保存・Restart成功。Serialで`Loaded LittleFS settings`、
storage ready、STA=192.168.0.26 / status3、inputsReady=1、Config external=1を確認。
uptime30469ms、internal free79264 / min61408 / largest57332、failure0。
ログ: 外部`c5-psram-after-sta-reboot.txt`。STA側HTTP status/configもエージェントから取得成功。
現設定（認証情報を含む）は外部`c5-psram-pre-functional-config.json`へ退避し、リポジトリへ入れない。

一時的にkey1.pressへOSC int51→Wait30ms→OSC int52を保存し、GET完全一致を確認。
API trigger後、PC 192.168.0.22:9000でOSCのaddress/type/valueを含む2 UDP packetの完全一致を確認。
初回19005と次の9000試行では受信timeout、3回目9000で成功。原因未確定のため連続通信安定性は未合格。
17-action Chain stageはHTTP400、同tokenの不完全commitもHTTP400で拒否し、各失敗後に
GET configが有効設定と完全一致することを確認。これはvalidation失敗保護のみで、OOM/I/O/電源断や
rename後apply失敗を検証したものではない。finallyで元の設定へ復元しGET完全一致を確認。
外部スクリプト`c5_psram_basic_regression.py`、結果`c5-psram-basic-results.json`。
実物キー操作によるOSC送信、BLE MIDI/HID、Rotation Value、高負荷設定、再接続は引き続き未確認。

続報: ユーザーがBluetooth接続成功を報告し、HTTP statusでもbleKeyboard=trueを確認。
先行UDP timeout時にはPC Firewall許可ダイアログが出ていたとのユーザー報告あり。
許可後の再試験でOSC/Waitを20回API triggerし、期待した40 UDP packetが全て一致した。
Firewall待ちによる破棄は有力な説明だが、当時のpacket/filter traceはないため原因確定とはしない。

BLE MIDI: 接続中の個体はadvertising scanで見つからず、Bleakのaddress接続はdevice-not-found。
Windowsの既存MIDI入力`ChainPad Chimera IN 0`をmidoで開く方式へ変更し、Firmware bleMidi=trueを確認。
一時的なNoteOn60/value100→Wait30ms→NoteOff60を5回ずつ2セッション、計10組20メッセージ実受信・bytes一致。
MIDI入力ポートclose/openを検証したもので、Bluetoothリンク自体のdisconnect/reconnectではない。
試験後は元設定へ復元しGET一致。外部`c5_ble_midi_check.py` / `c5-ble-midi-results.json`。
BLE HIDは接続確認まででキーreportの実受信はまだ。物理入力、Rotation、高負荷、無線再接続は未確認。

BluetoothをPC側でOFF→ONした後、ユーザーが再接続を確認。MIDI入力を再度開き、さらに10組20
NoteOn/Offのbytes一致を実受信。元設定へ復元した。Serial uptime610562ms、internal free75304 /
min46740 / largest53236、failure0、inputsReady=1、Config external=1。ログは外部
`c5-psram-after-bt-reconnect.txt`。stack low-water: loop5240、input2664、NimBLE2760、wifi4156、
tiT3708、esp_timer8236、IDLE2064 bytes。この1回の再接続確認を反復耐久試験の合格へ拡張しない。

物理入力試験: ユーザーがキー1〜12各1回、Encoder Push1回、CW3/CCW3クリックを操作。
OSC入力IDは0〜25各1回、26×3、27×3の計32件で期待通り。キー1のBLE HID F13は
Windows GetAsyncKeyStateのdown/upを取得、キー2のBLE MIDI NoteOn60/100→NoteOff60/0も実受信一致。
input/transport overflow、transport retry、rejectedChainsは0。元設定復元・GET一致確認。
外部`c5_physical_capture.py` / `c5-physical-results.json`。

最大設定保存試験: 13共有pool×16＋CW/CCW各8＝224 Actionsすべてに独立した192-byte address、
128-byte String（全文字0x01、JSONで各6-byte escape）を設定し、16 OSC Outputs（各address192 bytes）を保存。
mode=RotationValue、Range127、Initial64、Wrap。保存後GET完全一致、Restart後のGET完全一致・
inputsReady=true・Position64を確認し、元設定へ復元してGET一致確認。大量Actionsの送信試験ではない。
HTTP status実測: 保存後free72000/min44108/largest40948、再起動後free76556/min68240/largest59380、
元設定復元後free73888/min45908/largest49140 bytes。minは各bootからの累積で瞬間save専用計測ではない。
外部`c5_max_storage_check.py` / `c5-max-storage-results.json`。
最大設定保存前後のallocation failure総数はこの試験単独では採取しておらず、再起動でhook記録はリセットされる。

Rotation高負荷実機: 224最大長OSC String Actionsを保持したまま、OSC Int4＋Float4＋BLE MIDI CC8の
計16 Outputsを設定。Range127/Initial64/WrapでユーザーがCW3→CCW3を約1秒間隔で操作した。
全16宛先でPosition列65,66,67,66,65,64に一致。OSC48件（Int24/Float24）はfloat32を含めpacket bytes一致、
BLE CC48件は値・順序一致。generation6、pending0、overwrite/discard/unavailable0、failed=[0,0,0]。
input/transport overflow、Chain rejection0。元設定へ復元・GET一致。
試験終了時HTTP free71656 / min45908 / largest49140。後続Serial uptime188514msでは
free74836 / min45908 / largest49140、failure0、inputsReady=1、Config external=1。
stack low-water: loop5160/input2656/NimBLE4052/wifi4076/tiT3708/esp_timer8236/IDLE2064 bytes。
外部`c5_rotation_capture.py`、`c5_rotation_verify.py`、`c5-rotation-results.json`、`c5-after-rotation-memory.txt`。
この試験はWrap境界を跨いでおらず、境界挙動や高速連続回転の耐久合格を意味しない。

最大長Action送信: ActionChainモードで224件を保存し、各非空Chainを順にAPI trigger。
192-byteの各独立address＋128-byte String（0x01）のOSC packetを224件すべて実受信・完全一致。
一斉に全Chainを起動した試験ではない。元設定へ復元・GET一致。
外部`c5_max_send_check.py` / `c5-max-send-results.json`。

### C5改善の現時点のまとめ

- ConfigのPSRAM配置と起動後内部RAM約82.7KB改善を実測し、224/16を変更せず基本通信・物理入力・
  最大設定保存／再起動復元・16 Outputs配信・最大長224 Actions送信を確認できた。
- 確認したhook記録でcaps=0x80cを含む確保失敗は0。観測した内部minは最大設定保存を含むbootで44108 bytes。
  数値は限られた負荷条件の実測であり、全ケースの下限保証ではない。
- 設定は試験前のユーザーWi-Fi＋空Action/Outputsへ復元済み。PSRAM変更Firmwareを保持。
  旧全Flashへは戻していない。Flash/認証情報付きconfig/生ログは外部保存のまま。
- 未完了: Wi-Fiリンク切断からの再接続、反復・長時間複合負荷、Stop/Wrap境界とPreviewのC5実機照合、
  OOM/I/O/電源断時の保存保護、PSRAM確保失敗の実機注入、S3/C3/C6の変更版実機Regression。
  C5ではvalidation失敗保護を確認したが、既存rename後apply失敗問題は未修正・未検証。
- 推奨: C5のPSRAM配置方式を維持して上記残試験を進める。現時点でC5容量削減の必要性は示されていない。
  S3初期化方式とC3/C6配置は保持し、4機種build/host成功を実機合格と混同しない。
  全体Regression合格・安定版認定はまだ行わない。C3 Mini実装、commit/pushは未実施。

## 実測した問題

Phase E後のC3（空設定、Wi-Fi AP＋BLE）で、起動時freeHeapは以下だった。

| 段階 | bytes |
| --- | ---: |
| setup入口 | 136,288 |
| 設定読込後 | 134,252 |
| backend初期化後 | 71,716 |
| Wi-Fi初期化後 | 6,616 |
| Web／入力初期化後 | 2,056 |

`inputsReady=0`。別サンプルでは最小空き196 bytes、最大連続空き1,908 bytes。
Configは82,648 bytesで内部RAMにあり、S3用PSRAM配置は非適用。
PSRAMはない。LED非点滅・AP接続不安定が報告された。

BLE Central / Observerの無効化を試したが、Web API登録時の`std::bad_alloc`で再起動する結果となり不採用。
BLE変更はソースから撤去済みで、元構成の診断版への実機復旧も完了した。
復旧後もfree=2,088 bytes、inputsReady=0で、元の問題は未解決。
この試験から削減効果を確定できておらず、SDK／ライブラリ設定変更だけで解決すると見なさない。

## 現行仕様を維持する候補

| 候補 | 削減量／効果 | 必要な変更・制約 |
| --- | --- | --- |
| Action payloadをprotocol/type別unionへ整理 | RV32配置計測で352→336 bytes、224件で3,584 bytes削減 | Action参照・コピー・初期化・全protocolのserialize/validateを変更。外部形式は維持可能だが、この削減だけでは余裕が不足 |
| 1件単位の設定検証・書込み | 現行Chain scratch 5,636 bytesの一部、理論上約5,280 bytesをピークから削減可能 | Chain全体scratchを1 Actionへ変更。検証・正規化・原子的適用を維持する設計が必要。常駐RAMは減らず、JSON/HTTP領域は別途必要 |
| HTTP本文・JSONの重複保持削減 | 未測定。リクエスト内容に依存 | 最大24,576-byteレコードに対し本文String・JSON・scratchが併存する。ストリーム処理等の検討が必要。無効入力拒否・失敗時旧設定保持を回帰確認する |
| 文字列の可変長化／共有 | 短い・重複した設定では大きく減らせるが、最悪ケース保証は改善しない | 全224件が独立した最大長OSC StringならAddress＋Valueだけで72,128 bytes。断片化・コピー所有権も増えるため、最大構成対策として単独採用しない |
| 起動順序の変更 | 総使用量は減らない。断片化や初期化成否が変わる可能性 | 入力を先に確保してもWeb／無線側に不足を移すだけになり得る。安定化の根拠にしない |
| 無線ドライバのbuffer調整／SDK再構成 | 未測定 | Wi-Fi/BLE同時動作と接続仕様を保つ必要がある。既成SDKの設定上書きは慎重に扱い、対応するlibrary/controllerの整合と実機測定が必要 |
| Flashを使った常駐設定の再設計 | 大幅削減の可能性があるが定量化未了 | 保存処理・実行時アクセス・キャッシュ・耐障害性に跨る大規模変更。小規模なメモリ最適化とは別の設計判断になる |

union配置計測は実際のRV32コンパイラによる**候補型のサイズ測定**であり、候補型を組み込んだFirmwareの実機測定ではない。
最大OSC文字列長やAction数を黙って制限する案は含めない。

## 全機種共通の上限削減試算

現行の固定pool方式を維持し、13組のPress/Release共有poolを各K件、CW/CCWを各E件とすると、
総Action数は `13K + 2E`、Configサイズは現行RV32レイアウトで `3,800 + 352 × 総Action数` bytes。
3,800 bytesには16 Linked OutputsとNetwork／ChainView等を含む。

| 共有pool K | 回転Event E | 総Action数 | Config bytes | 現行からの常駐削減 bytes |
| ---: | ---: | ---: | ---: | ---: |
| 16 | 8 | 224 | 82,648 | 0 |
| 12 | 8 | 172 | 64,344 | 18,304 |
| 10 | 8 | 146 | 55,192 | 27,456 |
| 8 | 8 | 120 | 46,040 | 36,608 |
| 8 | 4 | 112 | 43,224 | 39,424 |
| 6 | 4 | 86 | 34,072 | 48,576 |

単に総数チェックだけを下げても固定poolが残ればRAMは減らない。pool配列と各Event上限も整合して変更する必要がある。
共有poolを下げれば、1 ChainあたりのJSON／HTTP／scratchの最悪ピークも下げられる。
例えばMAX_ACTIONSも16→8にする案ではChain scratchは5,636→2,820 bytes（RV32配置計算）。
総数だけを減らして1 Event上限16を残す案では、この追加効果は得られない。

必要な変更はFirmwareのpool／上限検証／capabilities、WebUIの表示・追加制限・Preset検証、
保存レコード上限、ドキュメント、host／実機テスト。
既存の新上限超過Preset・保存設定をどう扱うかも明示的に決める必要があり、無断切捨ては行わない。
S3ではConfigがPSRAMにあるため同じ削減量が主にPSRAM側へ効き、C3/C5/C6では内部RAM側へ効く。

## 判断と次の測定

- 現行仕様維持の小規模案で計算できる常駐削減はunion化の約3.5KiB。現状の不足に対し十分とは判断できない。
- 共通上限の候補として8件共有poolは約36〜39KiBを常駐RAMから減らせるため、比較測定する価値がある。
  これは採用提案の確定でも、安定動作保証でもない。12件案の約18KiBだけで十分かも未検証。
- 起動直後の空きへ削減量を単純加算して合格とはしない。現在は入力初期化に失敗しており、正常化すれば入力task等の追加確保も発生する。
- 最大長の独立したOSC Actionsと16 Outputs、Wi-Fi STA＋AP、BLE MIDI/HID接続、
  WebUI連続読込／保存／再起動、物理入力併用まで含め、minFreeHeap・largest block・stack low-waterと確保失敗を測る。
- ピーク処理後も余裕が残ること、保存失敗時に旧設定を保つこと、再起動・入力欠落がないことを確認する。
  必要な安全余裕はこれらの実測から判断し、現時点で特定の空き容量だけを合格条件にしない。
- C5/C6実機は今回まだ未確認。機種共通上限を決める前に両機種の結果も必要。

## ChainOSCPad直接比較による再評価

### 調査基準と証拠の区分

- 旧ソース: `C:\Users\ctake\OneDrive\Arduino\ChainOSCPad`、commit `0c1d76c`（Release v1.2.3）、調査時working tree clean。
  以下の「旧src」はこのリポジトリを指す。旧Firmwareは今回のC3へ書き込んでおらず、旧実装の実機RAM値は未測定。
- 現行: `47625b2`＋起動段階別Serial診断。BLE role変更は撤去し、実機も元のBLE構成へ復旧済み。
- **実測**: C3 Serial、書込み検証、abortログ。**配置測定**: RV32コンパイラ／ELFの構造体・symbolサイズ。
  **計算**: 配列数・JSON bytes等からの算術。**未検証推定**: 実装前の削減効果・断片化・最大負荷時の挙動。

### 1. 現行RAMの分類

| 分類 | 根拠・量 | 区分・注意 |
| --- | --- | --- |
| Config常駐 | `src/model.h::Config`、82,648 bytes | Serial実測・ELF配置測定。空設定でも全領域を消費 |
| Action固定pool | 同`slots[MAX_TOTAL_ACTIONS]`、352×224=78,848 bytes | 配置測定＋計算。13共有pool×16＋回転2×8 |
| Linked Outputs | `src/rotation.h::RotationSettings`、全体3,348、うちOutput208×16=3,328 bytes | 配置測定。Config内に含むので二重加算しない |
| Network・ChainView等 | 82,648−78,848−3,348=452 bytes | 計算。Config内部の残り |
| Rotation実行・pending | Runtime20、sender全体652、うちpending576 bytes | 実測・配置測定。senderへpendingを二重加算しない |
| 通常送信・Chain engine | `src/midi_backend.cpp::states` 1,760、`src/keyboard_backend.cpp::states` 1,520、`Engine`288 bytes | 現行C3 ELF配置測定。C3でも現在は2 Transport分の状態がある |
| 入力 | `src/inputs.cpp::inputsBegin`、128件queueのpayload1,024 bytes、task stack3,072 bytes | ソース計算。queue制御、TCB、timer、allocator分は別。現状初期化失敗のため正常時と同じ消費量ではない |
| Web route | `src/main.cpp::setupWeb`から`WebServer::on`。通常14登録＋notFound callback、probe有効時のみ追加1登録 | ソース計数。route objectは各80 bytes、計1,120 bytesにURI等が加わる。総heap実測は未実施 |
| HTTP本文 | WebServer `Parsing.cpp::_parseRequest`の`plainBuf`→`arg.value`、さらに`web.arg("plain")`の返却String | ソース確認。B-byte本文はパーサ／呼出し段階で概ね2Bの併存があり得る。capacity丸め・header等は別 |
| JSON | `stageConfigChain`等の`JsonDocument`、GETの`encodeChain`と`part` | 動的。現行deserialize対象はconst Stringなのでzero-copyではない。実際のJ bytesは未計測、固定量扱い不可 |
| 保存一時領域 | `src/config_store.cpp::stageConfigChain/scan`のChain5,636、Rotation scratch3,348 bytes | 配置測定。scan内ではChainを解放してからRotationを確保。両者の単純加算は誤り |
| backend起動差分 | 設定読込後134,252→backend後71,716、差62,536 bytes | C3実測。`backendsBegin/transportsBegin`全体の差分であり、BLE内部の各確保サイズではない |
| Wi-Fi起動差分 | 71,716→6,616、差65,100 bytes | C3実測。AP・DNS等の区間と非同期処理を含む。Wi-Fiライブラリだけの厳密な内訳ではない |
| その他startup消費 | entry時点でfree136,288。RTOS／Arduino／SDKのtask・stack・heap・静的領域など | 起動前確保の個別追跡未実施。linkerのRAM使用量とfreeHeapは同じ量ではない |

`loopStackMin`は元構成で5,776〜5,780 bytes。これはloop taskのみで、BLE/Wi-Fi/input各taskの余裕ではない。
特にinput taskは初期化失敗中で、正常入力時のstack測定は未確認。
元構成の復旧後はfree=2,088 / min=1,800 / largest=1,908 bytes、別のAP接続試行を含む観測ではmin=196 bytesだった。
異なるuptimeの値を同時刻の値として合成しない。

### 2. Web API登録のbad_allocの特定

不採用BLE試験版のabort stackを、その時点のELFでaddr2line解析した結果:

`operator new` → `WebServer::on(Uri const&, HTTPMethod, fn, ufn)` → `setupWeb`（status route登録付近）。
Arduino core `libraries/WebServer/src/WebServer.cpp:329`の
`new FunctionRequestHandler(fn, ufn, uri, method)`に対応する。

復旧版ELFの同じ関数を逆アセンブルすると、`li a0,80`に続いて`operator new(unsigned int)`を呼ぶ。
従って**外側のroute objectの要求は80 bytes**と絞り込める（復旧版のコード確認）。
trialの保存済みstackも同じ確保経路と整合する。ただしtrial ELFを別名保存しておらず、
failed-allocation hookで要求量・caps・その瞬間の最大blockを採取したわけではない。
URI cloneやString／callbackの別確保、およびheap管理のoverheadはこの80 bytesに含まない。
「API登録自体が数十KBを一度に要求した」のではなく、無線初期化後に小さな確保も成立しない状態となった可能性が高い。
元構成のlargest=1,908という別時点の値から、trialの80-byte失敗を断片化だけの問題と断定はできない。

### 3. 旧実装との具体的比較

| 項目 | ChainOSCPad | ChainPad | 評価 |
| --- | --- | --- | --- |
| 機能・容量 | 旧`src/input_settings.h`: OSCのみ、各ButtonのPress＋Release合計8、12 keys＋Pushで最大104メッセージ。Encoder専用設定とSequenceあり | OSC/MIDI/HID/Wait、224 Actions、Rotation16 Outputs、pending・通常FIFO・独立入力task | 旧の動作実績を現行224＋BLEの保証に転用不可 |
| 常駐設定 | 旧`input_settings.cpp::keySettings/encoderSetting`は全入力を常駐。`OscMessageSetting`はString address/value＋type | `model.h::Action/Config`は最大長char配列を全slotに常駐 | 旧もmetadataは固定配列。旧はファイルから送信時に遅延読込する設計ではない |
| 未使用領域 | 旧Buttonにpress/release各8の配列（合計16 metadata）、有効数は合計8まで。Stringの本文は使用長等に応じて確保 | 使用数0でも224×352 bytes | 旧の利点は主に可変長文字列。String object自体とdefault/sequence文字列は残る |
| 数値値 | 旧`main.cpp::sendConfiguredMessage`はStringの数値を`inputParseFloat/Int`して送信 | 現行はint32/float/boolを型付き保持 | 旧方式へ戻すと送信時parseとvalidation責務が増える。型付きpayloadは維持したい |
| 使用中だけ処理 | 旧`main.cpp::sendButton`のcount loop、`input_settings.cpp::addButton`もcount分serialize | `engine.cpp::advance`、`model.cpp::encodeChain`等もcount分だけ処理 | 現行も既に実施。未使用slotの処理省略だけでは常駐RAMは減らない |
| 保存単位 | 旧`keyPath/saveKeyFile/saveEncoderFile`: `/keyN.bin`とEncoderファイル、version＋長さ付きbinary | `config_store.cpp::writeRecord/scan`: v2 header＋28 Chain＋Rotationをレコード分割して1組のfileへ | 現行も既にChain単位で分割され、Config丸ごとのJSON DOMは通常保存に作らない |
| 読込buffer | 旧`loadFile`はfile全体をBytes(vector)へresizeし、`decodeKey/Encoder`でStringへコピー。file上限65,535 | `RecordReader/readRecord`は1レコードをFileから直接deserialize、上限24,576、深さ8 | 現行の方がraw file全コピーを避ける。旧のbinary化と全file bufferは別々に評価すべき |
| 一時コピー | 旧`inputSettingsSetup`は1 key/encoder候補をcopy。`inputSettingsSaveKey`はencode Bytes＋readback候補 | 現行Chain scratch＋JSON。`Config`はコピー演算子を持つが通常transaction経路で全Config複製はしない | 旧の「単位を小さくする」考え方は参考になるが、現行も大部分を実施済み |
| 単位保存保護 | 旧`saveFile`はtmpへwrite、bytes確認、flush/close後rename。保存後readbackしてactive RAM更新 | 現行commitはSTAGED全体validate後atomic rename、再scanで適用 | 旧の単位renameは参考になる。現行の全体transactionをキー別renameへ単純置換しない |
| 全体更新の保護 | 旧`network_manager.cpp::importSettings`と`/inputs/save-all`はcandidateとoldKeysの全12件vector、Encoder新旧、global旧値を保持。失敗時は順次rollback書込 | 現行begin→28 Chain→Rotation→commit、旧fileはcommitまで維持 | 旧は複数copyでピーク増。rollback自体の失敗・途中電源断では全体atomicityを保証できない |
| JSON Import | 旧`importPreset/importSettings`は本文Stringを得てmutable `body.begin()`でdeserialize | 現行保存はconst Stringをdeserialize | 旧のmutable-buffer parseは文字列zero-copyの参考例。ただしWebServer本文保持・候補Stringへのcopyは残る |
| HTML/JSON送信 | 旧`sendInputsPage`はprefix→keyCard→encoderCard→suffixをString生成してchunk送信。`exportSettings`も入力ごと | 現行HTML/JSはPROGMEM＋send_P、`/api/config`は1 Chainずつencode→String→sendContent | 旧は全ページStringを回避。現行HTMLはさらにRAM複製が少なく、この部分を旧式に戻す利点はない |
| Web route | 旧`network_manager.cpp::routes`のserver.onは13登録 | 現行production14登録 | route数の差は主因ではない。SDK/core世代の違いもあるため旧の実allocation量は未確定 |
| 無線・SDK | 旧platformio C3はespressif32@6.9.0、ArduinoOSC＋ArduinoJson。旧srcにBLE/MIDI/HID backendなし | 現行は別Arduino/IDF世代、NimBLE＋Wi-Fi＋複数送信backend | メモリ差を保存方式だけの成果と見なさない。BLEを外すことは今回の解決案にしない |

旧`LittleFS.begin(true)`はmount失敗時formatを許可する。現行`config_store.cpp::mount`は領域全消去を確認した場合のみ初回formatし、破損設定を自動消去しない。
旧のmount方式を取り入れることは、今回のデータ保護条件に適さない。
旧`saveFile`のrename後readback失敗も、既にdisk上は新fileとなっている点に注意する。

### 4. 最適化候補の再評価表

表中のピーク削減を単純に合算しない。同じ重複bufferを別案で除く場合や、異なるphaseのピークがある。

| 候補 | 常駐／ピーク効果と根拠区分 | 最大連続block・最悪ケース | 難易度／変更範囲・互換性・リスク |
| --- | --- | --- | --- |
| 1 Action scratch | 常駐0、Chain scratchから理論最大5,280 bytesをピーク削減 | 小さい確保へ分割できる。JSON/HTTPの大blockは残る | 中。decode/normalize/encode/scanの変更。順序・共有slot制限・transaction保護を維持し、途中失敗でactiveを更新しない設計が必要 |
| mutable JSON parse | 常駐0、JSON内文字列copyを削減する可能性。旧Importが参考 | 長い文字列で効果、metadata allocationは残る。B→Jの関係は未測定 | 中。本文の所有権・寿命を管理する。現行のencodeChainはdocをclearするためzero-copy参照を使い終える順序に注意 |
| HTTP raw受信 | 常駐は小buffer分増え得る。本文全長Bの保持／複製を減らす可能性 | 特に大きな連続本文allocationを避けられる | 中〜高。現行core `Parsing.cpp`には`canRaw/raw`＋HTTPRawによる分割受信経路がある。長さ・token・timeout・切断・片付け・一時file枯渇の検証が必要。JSON/API外形維持は可能だが実装未検証 |
| 分割保存の深化 | 現行は既にChain単位。さらにAction単位なら本文とDOMを小さくできる | 最大件数に依存せず小さいblock化の余地 | 中〜高。新endpoint/transaction手順、UI save側、順序・欠落・重複検証。旧binary単位保存の採用だけでは常駐RAMは減らない |
| protocol/type union | 常駐3,584 bytes削減（RV32候補型測定）、16件scratchで256 bytes減 | 固定量削減は最悪値でも有効。空きheapの連続性改善量は未測定 | 中。全payload参照とコピー・初期化を修正。外部v2形式維持可能。inactive member誤読に注意 |
| 可変長payload/String | 通常設定では大幅減の可能性、最悪224長OSCでは有利とは限らない | 小allocation多数は断片化を招く。224×322=72,128 bytesは文字列終端込み内容だけで必要 | 高。所有権、変更時旧新の二重保持、OOM rollback、copy/move全域。個別Stringよりbounded arena＋offsetの方が断片化管理を設計しやすいが、最悪保証にはならない |
| 未使用USB状態省略 | C3/C5/C6で概算1,640 bytes：MIDI880＋HID760（現行2要素symbolの半分） | 固定削減。全体不足を解消する量ではない | 中。transportIndex、loop、Both正規化、bulk/snapshotの回帰確認。機能として元々非対応のUSBのみを除き共通設定仕様は維持可能。今回未実装 |
| Web route統合 | route objectの一部を削減可能、現行14×80=1,120 bytesの一部＋URI等 | 小allocation減。Wi-Fi/BLE後の数十KB不足を解消しない | 低〜中。custom handlerでdispatchを集約可能。URL／method拒否・body処理を取り違えるリスク。routeごとの計測を先行 |
| 起動順序・事前確保 | 総量削減は保証なし | fragment配置・優先確保には効果の可能性 | 低〜中。input初期化だけ通してWeb/Wi-Fiを失敗させても不合格。失敗を検知し状態表示する改善は別に有用 |
| 無線メモリ調整 | 未検証。62.5KB/65.1KB区間のうち削減可能部分は未分離 | contiguous block、controller制約も関係 | 高。role試験は不採用。SDK設定を一律overrideするのでなく、対応するbuild/libraryとallocation内訳の検証が必要。接続数や既存無線機能の削減を前提にしない |
| Flash-backed active data | 大幅削減の可能性、定量化未了 | 常駐を小さくできるがアクセス遅延・cacheを別途予算化 | 非常に高。LittleFS fileはそのまま連続mmapできる前提にできない。partition/cache/更新・電源断保護の設計が必要。今回実装範囲外 |

scratch1件化とunion化は同じscratchに効くので、各案の最大値をそのまま足さない。
可変長方式は旧104件での利点を現行224件の最大構成に外挿せず、独立した最大長文字列で測る。

### 5. 224 / 172 / 146 / 120件の保存ピーク比較

以下は各共有pool上限K=16/12/10/8、Encoder E=8、Output16を維持する**未実装の比較モデル**。
ASCIIの192-byte address＋128-byte value、現行action項目を持つJSONをコンパクト生成した本文長をBとする。
Unicode escape・制御文字・allocator容量丸めなどを含む最悪上限ではない。

| 総件数 | Config常駐bytes | 1 Chain scratch | ASCII例B bytes | 本文2B＋scratchの小計 | 使用上の制約 |
| ---: | ---: | ---: | ---: | ---: | --- |
| 224 | 82,648 | 5,636 | 6,583 | 18,802 | 現行の各共有pool16、CW/CCW各8 |
| 172 | 64,344 | 4,228 | 4,943 | 14,114 | 各共有pool12、CW/CCW各8 |
| 146 | 55,192 | 3,524 | 4,123 | 11,770 | 各共有pool10、CW/CCW各8 |
| 120 | 46,040 | 2,820 | 3,303 | 9,426 | 各共有pool8、CW/CCW各8 |

この小計は保存全ピークではない。JSON DOMのJ、LittleFS、TCP/Wi-Fi、response等を加える必要がある。
パーサ段階の2Bとhandler段階の2Bは別phaseであり、4Bとして加算しない。
GETは別のピーク（1 Chain DOM＋serialized part）、commit/boot scanではHTTPの大本文がない別のピークとなる。
Rotation16件のJSON/scratchはどの案でも維持され、Action側を減らしても全経路のピークが同率で減るわけではない。
現行record上限24,576 bytesを小さくする場合は、正当な最大escape入力が入ることを再計算・テストする。

MAX_TOTAL_ACTIONSが縮むなら`midi_backend.cpp::MIDI_CAPACITY=MAX_TOTAL_ACTIONS+64`も変わり、
2 FIFOのmessage内容は172/146/120案でそれぞれ312/468/624 bytes減る計算（alignment等は別）。
上表のConfig削減量にはこの効果を含めていない。
将来のKey Press／MIDI Note単一Action化による節約は一切計上していない。

### 6. 結論と推奨順序

**224維持は不可能と断定できないが、現在確認できた小規模・最悪保証ありの常駐削減だけでは不足する。**
unionと非USB状態省略を併用しても計算上約5.2KBに留まり、入力が正常化した後のWeb保存・BLE接続余裕を保証できない。
HTTP/JSON改善は重要だが、元構成はHTTP要求を処理する前から入力初期化失敗なので、ピーク対策だけでも解決しない。
224維持には無線区間の追加削減、または常駐設定のより大きな再設計の裏付けが必要。

推奨する次の順序（今回実装しない）:

1. 復旧済み元構成を基準に、最小限のallocation-failure hookで要求size/caps/callerを固定長領域へ記録する。
   hook内でString/new/大量printfを使わない。各初期化段階にfree/min/largestを揃え、taskごとのstackも取得する。
2. backend/Wi-Fi区間を細分化し、SDKのheap traceが利用可能なら確保元を確認。未対応SDKで無理に有効化しない。
   C5/C6も同じ計測点で比較し、C3固有の不足と共通設計の消費を分ける。
3. 仕様維持候補はunion／非USB状態の除去を配置測定し、HTTP本文・JSONの実際のpeakをallocator計測で確認する。
   旧実装からは小単位処理とmutable-buffer parseを参考にし、全体copy＋rollback方式は取り込まない。
4. その結果をレビューしてから、現行224案と全機種共通172/146/120案の実機試験を決定する。
   常駐と1 Chainピークの両方を下げる120案は比較価値が高いが、未測定のまま採用しない。
5. 各候補は全slotを独立した最大長OSC値で埋め、16 Outputs、AP＋STA＋BLE MIDI/HID、WebUIの保存・再読込・再起動、
   物理キー／Encoderと通常Chain・Rotationの混在まで行う。短い同一文字列の反復だけでは最悪caseを満たさない。
6. invalid JSON、過大本文、通信切断、保存失敗時の旧設定保護、再接続時の出力、PC／実スマートフォンのUIも再検証。
   minFreeだけでなくlargest block、task stack、allocation failure、入力overflow、実受信と応答時間を合格判断に使う。

現時点でC3 Regressionは未合格、C5/C6は今回未確認。上限変更も大規模実装も実施していない。

## 追加計測の準備

C3/C5/C6共通の起動診断を追加し、ビルド中。まだこの診断版での実機結果はない。

- 起動checkpointは最大16件。内部RAM free / minimum / largestを記録する。
- BLE host初期化、service作成、advertising、backend完了、Wi-Fi mode、AP/DNS、STA開始、mDNS、Web登録、入力初期化を区分。
- failed allocation callbackは最初の4件について要求size/caps、確保API名、処理phase、失敗時の該当capsのfree/min/largestを保持し、総失敗回数も数える。
- callback内ではnew/String/Serial出力をしない。記録配列はRV32計算で368 bytes、制御変数・lock・コードは別途。
  以前のstartup free配列20 bytesは置き換える。診断による消費増を無視して旧値と比較しない。
- Serial `h`で後から記録とtask stack low-waterを出力する。取得は名前で検索できたtaskのみ。
  `not found`はstack測定不能を意味し、正常性の保証ではない。
- hookのfunction名はallocator APIであり完全なbacktraceではない。非同期処理中のphaseは起動処理の時間帯を示し、常にその処理が確保元とは限らない。
- 記録はRAM上であり、abortによる再起動を跨いで残らない。この場合はSerial crashとELF解析で補完する。
- 比較条件は元BLE設定、224 Actions、16 Outputs、空設定／AP＋BLEを基本とする。接続後・最大設定は別phaseで比較する。
- エンコーダ設定排他化は実装せず、C3/C5/C6追加実測の後に設計・サイズ・移行条件を評価する。

### 共通診断版: C3実測

3機種の共通診断版build/image検査が成功。C3へ書込みハッシュ検証後、空設定／AP＋BLEで測定。
Firmware SHA256: `922916ad7624958b8ed71f9659c0fb9ec0e8a99e8dc644785ad3a6ce06ea8e8d`。
ログ: 外部`regression-c3-detailed-memory.txt`。記録配列の実機表示は368 bytes。

| 段階 | free | minimum | largest |
| --- | ---: | ---: | ---: |
| entry | 136184 | 136184 | 114676 |
| config loaded | 133208 | 132384 | 114676 |
| BLE host ready | 75036 | 69756 | 61428 |
| BLE services ready | 71052 | 69756 | 61428 |
| BLE advertising ready | 70804 | 69756 | 61428 |
| backends ready | 70844 | 69756 | 61428 |
| Wi-Fi mode ready | 14160 | 13672 | 7668 |
| AP/DNS ready | 5764 | 5764 | 3060 |
| STA requested | 5764 | 5764 | 3060 |
| mDNS ready | 5112 | 4500 | 3060 |
| Web ready | 2604 | 2140 | 2036 |
| inputs FAILED | 1188 | 1188 | 948 |

今回の失敗hook記録（capsは共に0x804、allocator APIは`heap_caps_malloc`）:

1. phase=`wifi/STA-requested`、要求4096、free=4716 / min=4716 / largest=3060 bytes。
   総空きは要求より大きいが連続領域不足。空SSIDで実際のSTA接続要求は出していないため、
   phase名からSTA taskの確保と断定しない。Wi-Fi/mDNS等の非同期処理の正確なcallerは未採取。
2. phase=`inputs/task`、要求3072、free=1188 / min=1188 / largest=948 bytes。
   `inputsBegin`のinput-scan task stackサイズと一致。`inputsReady=0`、task検索でもinput-scan不在を確認。

stack low-water実測bytes: loopTask5776、nimble_host4024、wifi4876、tiT4008、esp_timer8288、IDLE2064。
これらは空設定起動時点の余裕で、最大負荷時の保証ではない。
今回Web登録は成功し、Web登録でのallocation failureは記録されなかった。
不採用BLE試験版でのWeb登録abortと混同せず、現行条件では入力初期化失敗を直接再現したものと扱う。
後続snapshot（uptime15565ms）はfree=1220 / min=972 / largest=948 bytes。
C3は未合格。C5/C6の同条件実機比較は接続待ち。

### 共通診断版: C5実測（交換後個体、現行ボード設定）

COM11、ESP32-C5 rev1.0、MAC `10:bd:a3:ce:db:cc`。全Flashバックアップ後に
共通診断版を書込み・hash検証。ログは外部`regression-c5b-detailed-memory.txt`。
最初のCOM26個体はFlash読出し停止で交換し、書込みしていない。故障原因は未確定。

| 段階 | free | minimum | largest |
| --- | ---: | ---: | ---: |
| entry | 115584 | 115440 | 94196 |
| config loaded | 117348 | 112388 | 94196 |
| BLE host ready | 82272 | 82096 | 65524 |
| BLE services ready | 78296 | 78256 | 61428 |
| BLE advertising ready | 76540 | 76540 | 59380 |
| backends ready | 76580 | 76540 | 59380 |
| Wi-Fi mode ready | 23168 | 22680 | 15348 |
| AP/DNS ready | 15484 | 15484 | 11252 |
| STA requested | 15484 | 15484 | 11252 |
| mDNS ready | 9708 | 9708 | 7668 |
| Web ready | 7264 | 7008 | 6644 |
| inputs ready | 2576 | 2576 | 2292 |

空設定、AP=192.168.4.1、STA未接続、inputsReady=1。uptime14766ms snapshot:
internal free2976 / min1972 / largest2292 bytes。Action352 / Chain5636 / Config82648 bytes。
Config address=`0x40821e6c`（内部RAM）。Web登録と入力task作成は成功したが、機能Regression合格ではない。

failure総数7、保持された先頭4件はいずれもphase=loop / heap_caps_malloc / caps=0x80c。
要求338 / 338 / 242 / 338 bytes、該当caps freeは8 / 8 / 32 / 8、largestは0 / 0 / 16 / 0。
該当capsの空きと上表のinternal 8bit空きは条件が違うため混同しない。正確なcallerは未採取。
stack low-water bytes: loopTask5272、input-scan2660、nimble_host2868、wifi4872、tiT3908、
esp_timer8304、IDLE2068。空設定起動時の値で最大負荷時の保証ではない。

**比較条件の補足:** 現行`seeed_xiao_esp32c5.json`は`BOARD_HAS_PSRAM`を定義する。
実機にもPSRAM free8381464 / allocated4736 bytesが表示された。
Configは内部RAMでも、C5全体がPSRAM未使用という前提は成立しない。
本結果は「同じ診断・Action224/Outputs16・元の各ボード設定」の比較であり、
厳密なPSRAM未使用比較は未実施。PSRAM minFree=0を内部RAM枯渇の根拠にはしない。
少なくとも現行C5でも内部RAM余裕不足と確保失敗が生じ、問題をC3だけに限定できない。
C6測定とPSRAM条件差の評価が残る。容量変更・排他化の実装・commit/pushは行っていない。

### 共通診断版: C6実測と3機種比較

交換後COM22、C6FH4 rev0.1、MAC `54:32:04:33:4a:a0`。全Flashバックアップと書込みhash検証後、
自動resetではSerial無応答、手動RESET後に以下を取得した。
application SHA256 `b5b732211f2bb174c166fdb50a87ee9211e064317ed621ffc6d710b7a29cf743`。
ログ: 外部`regression-c6c-memory-manual-reset.txt`。

| 段階 | free | minimum | largest |
| --- | ---: | ---: | ---: |
| entry | 250084 | 249940 | 233460 |
| config loaded | 251792 | 247024 | 229364 |
| BLE host ready | 217176 | 216996 | 200692 |
| BLE services ready | 213212 | 213172 | 196596 |
| BLE advertising ready | 211456 | 211456 | 196596 |
| backends ready | 211496 | 211456 | 196596 |
| Wi-Fi mode ready | 154892 | 154404 | 139252 |
| AP/DNS ready | 147208 | 147208 | 131060 |
| STA requested | 147208 | 147208 | 131060 |
| mDNS ready | 141440 | 141440 | 124916 |
| Web ready | 138992 | 138736 | 122868 |
| inputs ready | 134040 | 134040 | 118772 |

空設定、AP=192.168.4.1、STA未接続、inputsReady=1、記録時failure=0。
uptime20593ms snapshot: internal free133980 / min133304 / largest118772 bytes、PSRAM各値0。
Config82648 bytes、内部address `0x4082128c`。stack low-water bytes:
loopTask5344 / input-scan2668 / nimble_host2936 / wifi4948 / tiT3908 / esp_timer8300 / IDLE2064。

| 同じ診断点の差分（bytes） | C3 | C5 | C6 |
| --- | ---: | ---: | ---: |
| config loaded → BLE host ready | 58172 | 35076 | 34616 |
| BLE host ready → backends ready | 4192 | 5692 | 5680 |
| backends ready → Wi-Fi mode ready | 56684 | 53412 | 56604 |
| Wi-Fi mode ready → AP/DNS ready | 8396 | 7684 | 7684 |
| AP/DNS ready → mDNS ready | 652 | 5776 | 5768 |
| mDNS ready → Web ready | 2508 | 2444 | 2448 |
| Web ready → inputs結果 | 1416（失敗） | 4688 | 4952 |

差分は計測点間のnet変化であり、非同期taskの確保・解放も含む。各libraryの専有量ではない。
C3は4096-byte失敗を伴うため、mDNS区間が小さいことを効率優位と解釈しない。
C6の余裕はPSRAMなしでも確認できた。一方C5はボード既定でPSRAM使用あり、内部RAMは逼迫。
したがって「非PSRAMなら全機種同様に不足」でも「C3だけの問題」でもない。
共通上限はC3とC5の両方を制約として決める必要がある。
3機種とも空設定起動の計測のみで、無線接続／最大設定／保存ピーク／物理入力試験は未完了。
厳密なC5 PSRAM無効条件、失敗caller backtrace、Web登録失敗の境界条件は未測定。

## エンコーダ設定の排他化 — 設計調査のみ

### 現配置と共有可能な範囲

`src/model.h: Config`は13組（12キー＋Encoder Push）のPress/Release共有pool各16と、
CW/CCW各8の計224 Actionを単一`slots`に保持する。Rotation設定は別領域。
`src/rotation.h`のAxis12、Output208×16、outputCount4、mode＋padding4でSettings3348 bytes。

| 現行Configの内訳（RV32） | bytes |
| --- | ---: |
| NetworkSettings | 116 |
| ChainView×28 | 336 |
| キー＋Push pool: 208×352 | 73216 |
| CW/CCW pool: 16×352 | 5632 |
| EncoderRotationSettings | 3348 |
| 合計 | 82648 |

案: modeを共通tag（padding込み4 bytes）として外へ出し、
`union { Action cwCcw[16]; ValuePayload value; }`で5632と3344 bytesを共有する。
ValuePayloadはAxis12＋count4＋Output3328。unionの大きさは大きい側の5632。
Configは79304 bytesとなり、**削減3344 bytes（約3.27KiB）**。
CW/CCW5632 bytes全部が消えるわけではない。固定unionではどちらのモードでも最大側を予約する。

外部`encoder_exclusive_layout.cpp/.o`で実ヘッダとRV32 compilerを用いた配置専用計測を実施。
sizeof: network116 / views336 / value3344 / union5632 / Config82648、提案K16/12/10/8は
79304 / 61000 / 51848 / 42696。これは**コンパイラ配置測定**で、動作実装・実機節約測定ではない。
ブラウザの編集保持だけでは現行固定配列は縮まず、Firmware削減0 bytes。

union化は可能だが単なる型置換ではない。Action等はデフォルトmember initializerを持つため、
active memberの構築・破棄・copyを明示する必要がある。mode tagとgetterを整合させ、
ChainView[26/27]の参照をActionモード時だけ有効にする。現Config copyは全slotsをコピーし、
全28 Chainを読み出すため、そのままでは非active union member参照になる。
runtime適用・送信pending破棄・Engineの実行停止順もモード切替transactionに含める。
Rotation runtime20、sender652、MIDI/HID状態はこの排他化だけでは減らない。

### 総容量の定義

Actionモードでは従来通り13K＋CW8＋CCW8。Rotation ValueモードではCW/CCWを保持しないため、
保存可能Actionsは13K、別枠でOutputs16。unionの余剰をキーpoolへ貸し出す案ではない。

| 案の呼称 | Actionモード | Valueモード | キー／Push各Press＋Release |
| --- | ---: | --- | ---: |
| 224＋排他 | 224 | 208 Actions＋16 Outputs | 16 |
| 172＋排他 | 172 | 156 Actions＋16 Outputs | 12 |
| 146＋排他 | 146 | 130 Actions＋16 Outputs | 10 |
| 120＋排他 | 120 | 104 Actions＋16 Outputs | 8 |

「224＋排他」は224 Actionsと16 Outputs両方の設定を保存できる意味ではない。
capabilities/status/WebUI上限はmode別に表示し、未保存の無効側ドラフト数を保存済み合計へ加算しない。
CW/CCW各8は各案で維持。固定16のエラーメッセージ・Preset検証値も共通定数へ追従させる必要がある。

### Config v2 / Preset / WebUI / transaction

- 現`config_store.cpp: scan`はheader→28 Chain→Rotationの順。Rotation modeが最後なので、
  CW/CCWをapplyしてからRotationを書く方式をunion化すると重なった領域を壊す。
  begin/headerでmodeを確定し、modeに応じて26または28 ChainとValue payloadを検証する新手順が必要。
- `stageConfigChain`/`stageConfigRotation`は順番・件数を固定検証する。mode別期待record数と
  必須／禁止payloadを明示し、無効側を送ったrequestは黙って無視せず明示的に拒否する。
  `saveWifiConfig`も全28 Chain＋Rotationを保存しているため同時に対応が必要。
- RAMだけ共有して旧形式で両payloadを保存すると「Flashにも有効モードのみ」の仕様を満たさない。
  modeだけのAction-mode recordや空の構造用placeholderと、無効設定payloadは区別する。
  LittleFSは論理的に旧recordを削除しても物理Flashの旧page消去を保証しないため、ここでの破棄は
  active設定からの論理削除とする（transaction中は旧activeとpending両方が必要）。
- Config v2読込adapterは旧fileを変更せず全内容を検証し、有効mode側だけRAMへ移す。
  自動で旧Flashを破壊的変換せず、新形式保存成功時に置き換える。旧exportは事前に保持する。
  新形式はmode-firstかつ無効payload省略なので、storageVersion/schema/preset version更新を推奨。
  v2互換adapterと新versionの分離で旧Firmwareが誤読するのを防ぐ。旧Firmwareへのdowngradeには
  旧export/backupによる復元が必要。勝手な切詰め・無効値の受容はしない。
- `web/index.html: saveConfig`は今は全ChainとRotationを送る。編集モデルは両modeを保持し、
  送信用snapshotだけ有効側に投影する。mode切替だけでは削除せず、commit成功後に無効ドラフトを破棄。
  通信失敗時にはブラウザドラフトを維持。commit応答だけ失われた場合はGETで保存結果を照合し、
  未確認のまま成功扱い／ドラフト破棄をしない。再読込では保存された有効側のみ復元する。
- `web/presets.js: full/read`は現v2で両modeを保持・検証する。旧Preset importは両ドラフトを保持可能だが、
  保存時に有効側のみになることを明示。新full exportを保存形式に合わせるなら有効側のみとし、
  両ドラフト保存は別形式の設計判断となる。キーPresetの影響とEncoder選択時の扱いも明示する。
- transactionは旧activeを保持してpendingを検証し、renameまでruntimeを触らない原則を維持する。
  **現行も追加確認が必要:** `commitConfigSave`はrename後に`scan(ACTIVE,true)`で再確保・I/Oし、
  失敗するとdefaultsにしてエラーを返す。rename前失敗の保護と、commit後apply失敗は同じではない。
  「どの保存失敗でも旧設定維持」を満たすには、commit前に必要scratchを確保し適用失敗をなくす設計、
  または回復可能なcommit状態管理が必要。全Config二重保持は約80KBで不適切。
  ここは今回の低RAM条件で未検証の既存リスクとして記録し、排他化でも悪化させない。

### 常駐量・保存ピーク・容量案の比較

| 案 | Config bytes（配置測定／現行） | 現行比削減 | Chain scratch | ASCII例の2B＋scratch |
| --- | ---: | ---: | ---: | ---: |
| 現行224 | 82648 | 0 | 5636 | 18802 |
| 224＋排他 | 79304 | 3344 | 5636 | 18802 |
| 172＋排他 | 61000 | 21648 | 4228 | 14114 |
| 146＋排他 | 51848 | 30800 | 3524 | 11770 |
| 120＋排他 | 42696 | 39952 | 2820 | 9426 |

2B＋scratchは前掲ASCII request例を使った部分的計算で、JSON DOM / escape増大 / TCP / FS /
allocator / Wi-Fi/BLE接続ピークは除外。新mode-first headerの増分も未算入。
排他化してもキーChainの最大requestは同じで、このピーク自体は減らない。
Value保存scratchは現3348 bytesが目安、16 Outputsを維持するため容量縮小に比例して減らない。
特にK8ではRotation側が最大となる可能性があり、上表を保存全体の最大ピークと呼ばない。
旧activeのunionをstaging scratchとして使うことは旧設定保護に反する。

以下は起動後snapshotへ静的削減量を足し、上記部分ピークを引いただけの**参考算術値**。
最大設定を保存できる残量でも、最悪ケース下限でもない。

| 案 | C3参考残量 | C5参考残量 | C6参考残量 |
| --- | ---: | ---: | ---: |
| 現行224 | -17582 | -15826 | 115178 |
| 224＋排他 | -14238 | -12482 | 118522 |
| 172＋排他 | 8754 | 10510 | 141514 |
| 146＋排他 | 20250 | 22006 | 153010 |
| 120＋排他 | 31746 | 33502 | 164506 |

C3は入力task等が未初期化なので、さらに3072-byte stack＋TCB/timer等が必要。
C5はPSRAMを既に使用しており、caps別DMA等の確保失敗は総free増だけで解決すると断定できない。
配置変更後のlargest blockは単純加算で予測できない。**全案で最悪ケースの実メモリ余裕は未測定**。
escapeを含む最大request、最大16 Outputs、接続再接続、設定再読込が追加の支配要因となる。

難易度: 現行224の維持は仕様変更なしだが、C3/C5で大きなメモリ最適化と保存経路対策が必要。
224＋排他も約3.3KBだけでは不足。排他化そのものはunion lifetime・保存形式・移行・UI更新を伴い
中〜大規模で、削減量に対して安価な変更ではない。
172/146/120＋排他は同じ基本改修に上限変更と旧設定の超過拒否・ユーザー整理が加わる。
120は試算の余裕が最も大きいが仕様縮小も最大。172は部分ピーク後の余裕が小さい。

### 暫定推奨と残作業

1. 224＋排他だけで安定化する案は推奨しない。排他化は保存仕様整理としての価値と約3344-byte節約を分けて判断する。
2. 共通容量の実験を許可する場合、**146＋排他を最初の評価候補、120＋排他を余裕重視の比較候補**とする。
   172を採用可能と判断するにはHTTP/JSONピーク対策を含む追加根拠が必要。どれも現時点で採用確定ではない。
3. C5 PSRAM有無の条件差、caps別失敗caller、保存commit後apply失敗対策を先に詰める。
   診断hookはallocator API＋phaseまでで、完全な確保元特定は未完了。C3のWeb登録失敗を
   元BLE構成で再現するために意図的に更にRAMを削る試験はまだ行っていない。
4. 実装判断後、最大OSC文字列（escape含む）＋16 Outputsのモード別保存、旧設定移行、OOM/書込失敗時保護、
   WebUI・Wi-Fi/BLE接続・再起動・物理入力をS3/C3/C5/C6で確認してから共通仕様を確定する。

今回は排他化と容量変更をFirmwareへ実装していない。診断・資料と外部配置専用測定のみ。
C3 Regression未合格、C5/C6も起動測定を機能Regression合格として扱わない。

## コミット前レビュー: 診断の通常動作への影響

- 対象は診断コード5ファイルと本資料・実機Regression記録の計7ファイル。
  Flashバックアップ、Serial生ログ、候補レイアウトobject、ビルド成果物はリポジトリ外またはignore対象で、コミットしない。
- 診断配列368 bytesに制御変数とlockが加わる常駐負荷がある。計測値はこの負荷込み。
  以前の20-byte診断配列との比較と、診断なしの親commitとの比較を混同しない。
- 起動checkpointはheap照会、phase変更は短いcritical sectionを追加する。
  allocation failure hookは動的確保・出力をしないが、失敗ごとにheap free/min/largest照会を行うため
  heap走査・lock時間が加わる。記録件数は固定でも実行時間がゼロ／定数時間とは保証しない。
- 通常loopに周期的な追加heap照会はない。既存の周期ログは既定OFF。
  Serial `h`は明示要求時にheap記録とstackを出力し、送信timeoutを一時的に20msへ変更する。
  stack走査と複数回出力によりloopが遅れる可能性があり、全コマンド処理が20ms以内とは限らない。
  連続送信・入力タイミング試験中に`h`を実行した結果を無計測時の性能と同一視しない。
- 起動記録は16件、失敗詳細は先頭4件のみ、総数はunsignedカウンタで永続化しない。
  task名検索で得られたtaskのみstack測定する。完全なallocation backtraceは取得しない。
- Action224/Outputs16、保存形式、Wi-Fi/BLE設定、Config配置はこの変更では変更しない。
  C5のPSRAM改善は別作業。診断版S3の実機Regressionは未実施。
- コミット前確認: S3/C3/C5/C6のPlatformIO buildは4環境すべてSUCCESS。
  `tests/check_image.py --env xiao_esp32s3 xiao_esp32c3 xiao_esp32c5 xiao_esp32c6`も全環境PASS。
  C3/C5/C6のapplication hashは上記実測版と一致。S3は
  `95741f043fea139a49d0a50ecc9f2b7a3009586fb1ebe992891908129f5a4bc1`。
  buildログは外部`memory-diagnostics-precommit-build.log`。実機への追加書込みは行っていない。
