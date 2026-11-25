# S3 Phase 3A 最終判定（承認済み）

**総合HOLD、第1段階のメモリ配置対策はPASS承認済み。** [S3当初計画](plan.md)・[共通ケース](../plan.md)を基準とする。正式版 `v0.13.0` はCallなし対照、旧候補は `f78d912`、対策後の実装・検証記録は `e5630a9`。復元先は比較用正式版ではなく試験前の `v0.5.3`。未実施は証拠のない操作、HOLDはそのため終結できない項目や測定前提がない項目。旧版で確認した機能は対策後に再測定したものと区別した。

| 計画項目 | 旧候補での結果 | 対策後の実測・確定判定 |
|---|---|---|
| 静的検査・配置 | 内部Engine 9,548 B。Host・通常ビルド・イメージ検査PASS | PASS。Host・4機種ビルド／イメージ、S3内部Engine 80 B。実機でプール9,472 B／32枠の全域PSRAMを確認。[第1段階](invocation-psram-stage1-results.md) |
| 224件・OBS Disabled | 独立2起動PASS | PASS。起動1＋新規の独立起動2でGET×4・保存→GET×3。初回起動2のTCPタイムアウトは原因未特定の中断として維持。 |
| A5/D・OBS Ready共存 | **FAIL**。内部DMA割当失敗2件。後の[正式版・候補の同条件比較](ab224-results.md)では非再現 | **PASS**。独立3起動とも単独・並行208/208、GET一致、失敗0。Ready静止時内部空き53,296～53,336 B。旧候補のFAILを上書きしない。 |
| A1 OSC | PASS。型・単発・再押下 | 対策後は文字列208連番、親子マーカー等PASS。全型の再試験は未実施。codec変更なし、既存の型別証拠を採用。 |
| A2 MIDI / A3 HID | PASS。USB/BLE個別、Both、All Notes、F13・修飾・ReleaseAll、Consumer各7種。[受信列](candidate-transport-results.json) | USB/BLE子末尾ペア・解放、Panic後の明示Offと新規CCはPASS。Both・Consumerなど全マトリクスは再実施せず。バックエンド／記述子に変更なし。 |
| A4 Wait/Loop | Loop・出力順PASS。OSC時間は経路依存、USBの5試行は250 msと整合 | PASS。USB CC間隔249.93～251.52 ms、親子Loopは期待列一致。内部Wait時間を直接測定した結果ではない。 |
| A5 OBS操作 | シーン・mute・音量・scene item表示と後続OSCの8試行PASS。group item、media操作、録画・配信・VirtualCam・Replayの隔離試験は未実施 | **補完25/25 PASS**。実OBS状態と後続OSCの双方を照合し、29回の診断で割当失敗・両overflow 0。 |
| A6 Encoder / C1物理入力 | PASS。物理72件、Stop/Wrap、OSC・MIDI・OBSの回転3出力 | 全操作の再実施なし。物理押下経路は対策後の満杯試験で確認。入力・Rotationコード不変につき全面再試験は不要と判断。 |
| B1並行・直接Call / B2 Loop・FIFO / ペア | PASS。2親と直接起動、27段、親子Loop、FIFO、ペア | PASS。配置変更の直接影響範囲を再実測。USB/BLEペアで欠落・重複・解放漏れなし。子バックエンド完了→親復帰はソース根拠でありPC到着の異経路順序ではない。[機能回帰](stage1-functional-results.json) |
| C2 満杯・Stop / C3 Panic | PASS。33件目拒否、即時・Stop、保留MIDIとキー解放 | PASS。32枠を実際に使用し、物理33件目拒否・即時・取消32、両transportの保留取消とHID解放を確認。 |
| D BLE / OBS再接続 | ChainPad側はPASS。BLEはPC受信プロセス再起動後に同一実機起動で実出力。OBSはタスクマネージャー終了後の再起動でReady・8試行PASS | 対策後の個別切断は未再試験。元PC受信器の自動復帰は未達・原因未特定、調査終了。OBSの通常終了による切断は成功扱いしない。 |
| D USB再列挙・STA復帰 | 通常起動後のUSB再列挙・受信器open・実出力は確認。同一給電・同一起動のUSB切断→再列挙→実出力、S3だけの短時間STA切断→復帰後の出力は未実施 | **HOLD**。起動時の接続・他端末の結果・単なる受信器再openでは代替しない。 |
| D OBS一覧 | 実OBS状態と後続OSCは確認。S3の実WebUI一覧とOBS側との照合結果なし | **HOLD／未実施**。保存保護試験は一覧一致の証拠ではない。 |
| E 設定・保存・Import | PASS部分：旧設定引継ぎ、224件永続化、WebUI Full/Key・Default/Empty・絶対target貼付・直接循環時の編集保護、APIで範囲外・直接／間接循環拒否とactive保持、再起動後Call出力 | 対策後は224件永続・全GET一致PASS。実WebUI上での範囲外ID・間接循環の編集不変は個別の実測なし（API確認と区別）。この画面側の2条件は**未検証・3B引継ぎ**。 |
| 復元 | 書換え前の全Flash照合済み | **PASS**。元factory・LittleFSの個別digestと起動後全設定・能力・接続状態一致、BLE未接続、PCの試験用OBS終了。[復元記録](invocation-psram-stage1-results.md) |

**終結判断。** 配置に直接依存する実行・共存・初期化は再検証済み（確保失敗時は静的確認、実機注入ではない）。変更の影響外は旧候補の証拠を引き継ぐ。STA再接続・同一起動内のUSB再列挙はHOLD。実WebUIのOBS一覧照合、範囲外ID・間接循環拒否時の編集内容保護は未検証のまま3Bへ引き継ぐ。[横断最終判定](../phase3a-review.md)を参照。

## 補完試験の経過（各段落はその時点の状態）

旧候補の証拠は変更の影響範囲を考慮して引き継ぐ。対策後通常版 `e5630a9` でGroup Item・Media・録画／配信・Virtual Camera・Replay Bufferを補完する。配信はローカル受け先、録画／Replayは隔離プロファイル・試験ディレクトリを使用し、OBS側実状態と後続Chain出力を別々に照合する。各区間で割当失敗・overflowも確認する。独立給電を確認できた場合だけ同一bootのUSB再列挙を測定し、不可ならHOLD。STA復帰はC5/C6共通HOLD。WebUIのOBS一覧照合と範囲外ID・間接循環の編集保護は未実施のままPhase 3Bへ引き継ぐ。この方針を実測結果としては数えない。
追加試験の準備：ユーザー判断により同一給電のUSB再列挙試験は未実施・HOLDとする。OBS補完用に同一S3・8 MiB・イメージSHAを照合し、対策版アプリのみを書き込み。対象範囲の実Flash digest一致、LittleFSの書き込み前後のdigest一致。OBS各操作の判定はまだ行っていない。
OBS補完の初回：元の0 Actions設定の引継ぎ、engineReady、外部32枠（9,472 B）を確認。試験用OBSを起動し、ChainPad側がReadyとなった境界で割当失敗0。Group Item試験の前提確認で、PCから実OBSへ送った `GetStreamStatus` が成功せずコード207を返した。シーン生成・Chain設定保存・triggerより前に停止。事後の設定GETは元の全内容と一致し、OBS Ready、稼働0・両overflow 0。原因未特定。Group Item以降の操作は未実施であり、実機側のOBS操作FAILとは判定しない。
OBS 207の読取専用切り分け：OBS Studio 33.0.1／obs-websocket 5.7.6。公式[プロトコル定義](https://github.com/obsproject/obs-websocket/blob/5.7.6/docs/generated/protocol.json)のRequestStatus 207は `NotReady`。WebSocket待受が08:31:48.006、scene dataクリアが08:31:48.272、初回PC試験接続が08:31:48.883～.894、ソース読み込み記録は08:31:50.602まで続いた。起動時のシーン読み込みと要求が重なった説明に整合するが、当時の個別応答コメントは未保存。08:34:44の再照会では同一セッションの再起動なしで4出力の各Getすべて成功・inactive、Group一覧も取得成功。試験アクション・設定変更なし。次回は出力状態4件の取得成功と全停止を前提にし、起動準備中の207は期限付きの読取待機のみとする。
OBS補完再試験1：PC側の4出力の状態読取りにすべて成功し、全停止を確認。初回の207は再現しなかった。操作前に通常CDCを開き直して `?m`／`h` を送ったが受信0バイトで、今回のPSRAM配置・割当失敗カウンタを取得できず停止。割当失敗の検出ではない。HTTPではOBS Ready、稼働0・両overflow 0、元の全Chain設定と一致。操作はすべて未実施。初回接続時の診断値をこの再試験の値に流用しない。復元は未着手。
CDC診断回復：候補版をHTTPで通常再起動。uptimeのリセット、同じ物理位置での通常CDC再列挙、全Chain設定一致とOBS Readyを確認した。新しい起動で開いた単一ハンドルを保持し、`?m`／`h`を3回連続取得。3回とも32枠・9,472 Bの全域PSRAM配置と同一ベース、割当失敗0・両overflow 0。これらは新しい起動の開始値であり、以前の受信0バイト区間の診断値ではない。再open欠測の根本原因は未確定。通常再起動による列挙は同一起動内のUSB再列挙試験には数えない。
OBS補完再試験2（対策後候補、PASS）：同一CDCハンドルを維持し、操作前にOBSの4出力全停止を確認。Group Itemの有効／トグル／有効／無効4回、プレイリスト次／前2回（45→75→45秒）、メディアの一時停止／再生／再開始／停止4回、scene item無効／有効2回、録画開始／停止2回、Virtual Camera開始／停止／トグル2回の計4回、Replay Buffer開始／保存／停止／トグル2回の計5回、配信開始／停止2回。25操作すべてでOBS側読戻しと操作番号付き後続OSCが一致。試験用プロファイルの配信先はループバックRTMP、録画とReplayの非空MKVを試験用ディレクトリに確認。開始・操作後・各区間末の29回すべてでPSRAM枠不変、割当失敗0・両overflow 0・稼働0。後確認でも全出力停止、プロファイル・シーン・Groupの件数は開始前と一致。補完対象のOBS操作HOLDを解消。207中断・CDC欠測と成功した再試験を区別する。USB再列挙／STAのHOLD・WebUIの3B引継ぎは維持。今回の書換え後の復元は未完了。
OBS補完後の復元書込み：USB物理位置・ESP32-S3の個体・8 MiB容量を再照合し、退避済み全FlashのSHA-256と両抽出物を確認。元factory（`0x10000`, 6,291,456 B）とLittleFS（`0x630000`, 1,900,544 B）のみ書き戻した。通常起動前に両領域の全長を個別に実Flash MD5で照合し、いずれも退避データと一致。通常起動後の確認・PC後片付けはこの時点では未完了。
復元後通常起動：通常CDCの物理位置、`v0.5.3`、全設定・capabilities・OBS全応答が元記録と一致。storage/input ready、Wi-Fi・USB MIDI/HID ready、BLE未接続、OBS Disabled。押下・稼働・両overflow 0。空Chain受付でeventsが1増え、稼働0・全解放を維持。実機復元PASS。PCの試験用OBS通常終了待ち。
最終照合：ユーザーによる通常終了後、Windowsの `obs64.exe` は0件。読取りのみでS3の元版・全設定・capabilities・OBS全応答・接続／待機状態が退避時と一致することを再確認。今回の実機・PCの復元完了。
