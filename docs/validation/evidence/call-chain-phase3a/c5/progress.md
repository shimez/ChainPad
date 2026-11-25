# C5 Phase 3A：実施時点ごとの進捗記録

以下のHOLD・未実施は各時点の状態。最終判定と復元照合は[総合結果](results.md)、途中で停止したペア試験と再試験は[比較記録](pair-order-review.md)を参照。

候補 `574b939` の通常C5ビルド、ホスト・保存障害・OBS関連テスト、イメージ検査を通過。アプリSHA256は `14e8e3dcbf2e9c877489b8d143ccbae6093c684135a5e0798087abe7c7d6dcf0`。ELFでは内部 `engine` が80 B、`sceneBuffers` が8,344 B。動的領域の実配置は未測定。

ユーザー確認済みのUSBポートでESP32-C5 rev.1.0・Flash 8 MiBを識別した。書き込み前のHTTP観測ではv0.8.0、入力準備完了、ストレージready、稼働Chain 0、BLE MIDI/HID未接続。設定・capabilities・OBS設定と全Flashの読み出し（8,388,608 B）をGit外に保存した。

**退避照合未完了のため停止。** `verify-flash` はチップに接続したが、stub flasherの起動時に空応答で終了した。比較処理に達しておらず、データ不一致と判定したわけではない。書き込み・設定変更は行っていない。正式版比較・候補実機試験・224枠ゲート・割当失敗数は未測定。再開には元Flashの実機照合が必要。
ユーザー承認後、同じ全Flash退避ファイルを使い、921600から115200 baudに下げて `verify-flash` のみ再試行した。今回は `No serial data received` でチップ接続前に終了。初回・再試行とも照合結果は得られていない。HOLD継続。

その後、手動でダウンロードモードに入り、対象ポートを再確認した。115200 baud、`--before no-reset` で `verify-flash` を実行し、アドレス0から8,388,608 Bの全域で `Verification successful (digest matched)` を確認した。照合後、ツールはRTSリセットを実行。これにより退避照合のHOLDは解除。Flash書き込み・設定変更はなく、正式版比較・候補版の224枠ゲートは未実施。
正式版C5配布物のSHA256・commit・配置と退避済みパーティション表を照合し、元アプリ領域と設定領域を全Flashコピーから切り出した。書き込み直前、元版の既知のSTAアドレスで `GET /api/status` がタイムアウトしたため書き込みコマンドには進んでいない。通常起動・STA接続の再確認待ち。バックアップの照合結果は引き続きPASS。
手動RESET後、元v0.8.0のWi-Fi・入力・ストレージready、稼働0、退避時設定との一致を確認。正式版v0.13.0のC5アプリのみを `0x10000` に書き込み、書き込み時ハッシュ照合成功。正式版の通常起動と元設定（29 Actions）の全体一致を確認した。

正式版のCallなし基準：試験fixtureの保存・GET一致、単発OSC、5型値（int、float、bool両値、UTF-8文字列）の2回起動、250 ms Wait（PC到着差286.82 ms）、ネストLoop `ABBABBABBZ` を確認。各完了区間で稼働0、割当失敗カウンタ0。BLE未接続。続くRotation ValueのStop設定で、PCスクリプトが物理回転の代わりにCWをHTTP triggerし409を受けたため停止。正式版 `src/main.cpp` の仕様どおりであり、物理Rotationの結果は未測定。失敗した試験区間に終了時メモリ計測はない。現在は正式版＋試験設定、元版への復元は未実施。候補・224枠ゲートへは未移行。
正式版の物理Rotation（Stop）はPASS。CW5・CCW5でPC受信値は `1,2,3,4,3,2,1,0`、続いてKEY12の終端マーカー `22`。端点を越える2クリックで余分な出力なし。マーカー受信時点はKEY12押下中で、その後に全解放・稼働0を確認。入出力overflow増分0、同一起動の後続診断で割当失敗0。Wrap設定の保存・読戻しも一致し、受信器を準備した。
正式版の物理Rotation（Wrap）：PC受信は `1,2,3,4,0,4,3,2,1,0` とKEY12押下 `22` で期待どおり。マーカー直後のstatusはuptime 259141 ms、稼働0、入力／transport overflow各0、pressedMask=2048（KEY12押下中）。解放後の追加status取得はTCP接続で20秒タイムアウト。Wrap出力はPASS、解放・終了時の割当失敗差分は未確認として後続試験を停止。タイムアウトの原因は未確定。
リセットせずstatusを再取得。Wrap終了マーカー直後→解放後はuptime 259141→369039 ms、events 28→29、pressedMask 2048→0、稼働・両overflowは0。続くシリアル診断でも割当失敗0を確認。タイムアウトの原因は未特定だが、Wrapの終了状態は確認できた。正式版の物理入力72イベント用設定を保存・照合し、PC受信器を起動。
正式版・物理入力72イベントはやり直し後の1試行でPASS（最初の途中操作は集計外）。PC受信は `[0,1]×10`、`[0,1,2,3]×10`、`[0,26,26,26,27,27,27,24,25,1,22,23]` の72/72件、欠落・余分・順序違反なし。同一起動でevents 106→178、入力／送信overflow各0、終端はpressedMask=0・稼働0。その後の診断でも割当失敗0。BLE MIDI/HIDはともに未接続で、BLE受信試験は未実施。
正式版でBluetooth接続後、PC側でMIDI入力を開いてMIDI/HID両購読を確認。Note On/Off・CC・holdペア・重複定義を含むAll Notesの期待13イベントが全件順序一致。F13単独、Shift付きDownUp、ReleaseAllをPCキー状態で照合。Consumerは7 usageすべてでReport ID 2の押下→neutral一致。MIDI hold中のPanicで稼働0、保留出力なし、新規CCと明示Offを実受信。各終了時の割当失敗0。実OBSを起動し再接続してReady、専用シーン・入力でシーン、mute／toggle、絶対・相対音量、item可視性の8操作と後続OSCを照合。次の物理Rotation 3出力用の受信準備を完了。
正式版の物理Rotation 3出力はPASS。操作担当からCCWのクリック感について申告があったが、CW4・CCW4の各段でOSC floatは `0.25,0.5,0.75,1,0.75,0.5,0.25,0`、CC23は `25,50,75,100,75,50,25,0`、実OBSは初期−40 dBから `−30,−20,−10,0,−10,−20,−30,−40` dB。入力イベント増分10（回転8＋KEY12押下・解放2）、uptime連続、終了時割当失敗0。観測列に不一致はなく、このケースの再試験は不要。
候補 `574b939` の通常版アプリを対象C5の `0x10000` に書き込み、esptoolの書き込み時ハッシュ照合成功。自動リセット後、Call capabilityあり、inputsReady／engineReady、storage ready、稼働0。正式版の直前設定を変更せずGET完全一致。Wi-Fi・BLE MIDI/HID両購読・OBS Ready時に、内部free 64,240 B、内部largest／DMA largest各42,996 B、Engine slots 9,472 B（32枠）・OBS assembly 16,400 Bの両方で `externalRange=1`、割当失敗0。これは起動時の観測であり、224枠負荷後の値ではない。

224枠ゲートは開始条件でHOLD。試験用PCのOBS終了操作後、BLE両購読は成立したが、最初のstatusでOBSが `Disconnected` ではなく `Connecting` だった。224枠設定は保存しておらず、GET反復・保存反復・別起動比較と後続負荷は未実施。候補版・正式版から引き継いだ設定のまま。元版への復元は未実施。
224枠ゲート再開：PCのOBS終了、C5で `Disconnected` を3回確認。第1起動ではWi-Fi・BLE両購読を維持し、224枠の保存・初期照合、待機・status、設定GET単独4回、保存→読戻し3回を完了。全読戻し一致、15診断点の割当失敗すべて0、PSRAM両領域の位置不変。最終点の内部free 63,132 B、内部／DMA largest各42,996 B。

ソフト再起動後（uptime 275480→7956 ms）、第2起動の開始診断は割当失敗0、224枠設定の全体一致を確認。その直後の状態検査でBLEは両購読のまま、OBSが `Connecting` となり停止。第2起動のGET単独4回・保存反復は未実施。この区間の終了時割当失敗数は未測定。ゲート全体はHOLD、後続負荷へは未移行。実機は候補版・224枠試験設定のまま。
第2起動でOBS設定を変えず、PC側OBS停止のまま約25秒間statusのみを観測した。49点中 `Disconnected` 47点、`Connecting` 2点、`Ready` 0点（uptime 86301→110853 ms、generation 8→9）。観測点の間の状態は未測定。全点で `Disconnected` を要求する従来条件と、単に接続が成立していないことは区別する。この観測はゲート合格には数えない。
承認済みの変更条件（C5側OBS `Disabled`、Wi-Fi接続・BLE両購読）では**224 ActionsメモリゲートPASS**。別々の2起動で各々、初期の全体照合、待機・status、単独GET×4、保存→GET×3を完了。ソフト再起動を挟んでも全体一致。第1起動15点、第2起動14点のすべてで割当失敗0、Invocation／OBS assemblyはPSRAM内で同一起動中の位置不変。[診断点ごとの値](gate-obs-disabled.jsonl)はOBS有効の試行とは別集計。終了後、OBSを元の有効設定に戻して公開設定全体の一致を確認した。候補版の物理入力72イベント用fixtureを保存・読戻しし、PC受信器を起動。
候補版の物理入力は正式版と同じ72/72件が期待順で到着しPASS。実機events 0→72、終了時全キー解放・稼働0、入力／送信overflowとも0。続く診断で割当失敗0。候補版のCallなし単発・型付きOSC列2回・250 ms Wait・ネストLoop `ABBABBABBZ` も確認し、各区間の割当失敗0。物理Rotation Stopの設定一致を確認し、PC受信器を準備した。
候補版の物理Rotation StopはPASS。CW5・CCW5に対して `1,2,3,4,3,2,1,0` と終端 `22` を受信し、正式版と一致。入力イベントは77→89（回転10＋KEY12押下・解放）、全解放・稼働0、マーカーまでの両overflow増分0、解放後の割当失敗0。Wrap設定の保存・照合を完了し、受信器を準備した。
候補版の物理Rotation Wrapは正式版と同じ `1,2,3,4,0,4,3,2,1,0`＋終端 `22` が到着してPASS。events 89→101、KEY12解放後はpressedMask=0、稼働0、両overflow・割当失敗0。候補版の通常MIDI 13イベント、F13・修飾・解放、Consumer 7 usage＋neutral、MIDI hold中のPanic後の保留出力消去と新規出力も期待どおり。実OBS Readyで専用シーン・入力に対する8操作と後続OSCを照合。候補版の物理Rotation 3出力の受信準備完了。
候補版の物理Rotation 3出力はPASS。CW4・CCW4でOSC float、CC23、OBS音量が正式版の8段とすべて一致（初期−40 dBから0 dBを経て−40 dBへ復帰）。入力増分10、割当失敗0。続いて通常ChainとCallを両開始順で実行し、親2本＋同じ子の直接起動も含め出力・復帰順を確認。Call入り／通常Loopの各100件、計200 OSCを受信し競合区間のFIFOを照合した。31→32 Invocationまで稼働、診断時の割当失敗0。満杯時の物理3キーの結果はまだ未測定。
候補版・満杯時の物理KEY9→10→11はPASS。開始32枠、PC受信は `[91,92]` のみ（拒否されたKEY9のprefix 90なし）、拒否+1、取消+32、終了0枠・全キー解放・入力増分6。両overflowと割当失敗は0。[保存済み観測値](physical-full.json)を別プロセスで期待値照合した。受信器はJSON保存後の標準出力で例外となり、同プロセス末尾のassertには未到達。実機の異常やデータ欠落は確認されず、以後の物理受信器は標準出力も永続ファイルへ向ける。
候補版の27段Callで末端Wait中は1 Invocation、OSCは27→0の逆順28件、終了0枠。親子Loopは `子,子,親` ×2。32枠＋F13押下中のPanicで取消+32、稼働0、PC側F13解放。各診断で割当失敗0。OBS Ready・BLE両購読で224 Actionsの設定GETとOSCを重ね、GET全文一致、208/208件順序一致・欠落重複なし（GET区間内3件）、割当失敗増分0。

**子ペア解放→親OSCのPC受信順：今回の試行はFAIL、後続停止。** 500 ms holdの子ペアを別々に実行。PC観測は `Note On → 親OSC → Note Off`、`F13 Down → 親OSC → F13 Up`。親OSCを基準にOffは+41.00 ms、Upは+39.77 ms。Off/Upの欠落ではなく、事前指定の受信順に不一致。MIDI callback、Windowsキー状態ポーリング、UDP受信で測ったため、内部dispatch順は未確定。この区間の終了時診断はassert停止により未取得。実機は候補版＋ペア試験設定、元版への復元前。無断で再試験・修正しない。
承認済みの新基準は [計画書の追補](plan.md) に記録。旧PC順序判定FAIL・全測定値は保持。候補版で実WebUIのFull/Key往復・絶対target貼付・循環Import拒否・Default/Emptyとネットワーク保持・保存を確認。実APIで範囲外IDをstage時、直接／間接循環をcommit時にHTTP 400で拒否し、拒否ごとにactive GET不変。その後の再起動で保存設定全体一致・Call先OSC 42受信。実OBSではgroup表示4操作、playlist次／前、media・scene表示、録画・VirtualCam・ReplayBuffer（保存含む）・ローカルRTMPを照合、試験用プロファイルとシーンを片付けた。さらにBLE両購読中のOBSサーバー停止→再起動・再接続で、同一bootのuptime 32655→33885→38798 ms、復帰後にOBS8操作とOSCを再照合した。BLE単独切断の待受を開始。元版復元・総合判定はまだ未完了。
BLE単独切断を確認。uptime 57541 msでMIDI/HIDとも接続、92581 msでともに未接続、114874 msでも切断継続。各観測点でWi-Fi接続・OBS Ready・稼働0を維持。再接続後の新規出力は別途確認する。
BLE再接続後は同一bootでMIDI 13イベント、F13・修飾・解放をPCで再確認、終了時割当失敗0。OSC追加設定保存直後の複合status検査は不成立（その時点の各値は未保存、原因未特定）。設定変更なしで再確認し、uptime 208189→208682 msの前後ともWi-Fi・両購読・OBS Ready・稼働0・両overflow 0で新規OSC `/p3/ble` int 22を受信。BLE単独切断・復帰後の出力をPASSとする。全期間のOBS状態を連続観測したという意味ではない。
