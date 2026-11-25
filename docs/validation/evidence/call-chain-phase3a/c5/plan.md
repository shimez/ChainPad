# Phase 3A / C5統合回帰計画（実施済み）

当初の手順と承認後の変更を記録する。[総合結果・復元](results.md)参照。224枠ゲートはOBS有効時の再接続試行を除くため、承認のうえOBS無効で2起動分を実施した。

対象はC5のみ。C6は移行判定が条件付きPASS、総合HOLDのまま。C6の[補完結果](../c6/supplement.md)は[共通計画](../plan.md)の操作・観測方法に使い、C5の実測には数えない。S3実機はC5レビュー後。C3は通常ビルド・ホスト・ELF／イメージ検査のみ。GET遅延は[完了した別調査](../../get-latency-investigation/README.md)を参照し、版間速度比較を繰り返さない。

## C5ケース6の合格条件改訂（比較試験後・承認済み）

下表6の「PCでNote Off／F13 Up先着」は当初の基準として保持する。以後のC5判定では、正常送信時に子のペアActionがバックエンド完了条件を満たしてから親を再開することを保証対象とする。BLEとWi-Fi間のPC観測順は保証せず、出力の欠落・重複・解放漏れは引き続きFAIL。単一Chainでも同じ逆転を観測したためであり、当初のFAILを取り消すものではない。[比較試験・内部経路](pair-order-review.md)参照。C5内部順はソースで確認、実機では出力・解放を確認したが内部イベントは直接計測していない。C6判定には適用しない。

## 使用版と保護

- 候補：`feature/call-chain` の `574b93975558e465e1e9fab1dd68e35c6ac58ff0`、PlatformIO通常環境 `xiao_esp32c5`。Phase 2D終了時 `7308e47` からのFirmware差分は既存DMA診断の対象にC6を加えた条件式のみ。C5の配置・送信処理は変更されていない。過去と同じバイナリだとは仮定しない。承認後にホスト・保存障害・OBSテスト、通常ビルド、ELFとイメージ検査を実施し、依存版・ビルド元・実際に書くアプリSHA256を記録する。トレース版・注入版は使わない。
- 動作基準：正式Release `v0.13.0` の **C5** 配布品（ソース `1d2310296e4ce649ecd4246d0174ad57dc3ffa0a`）。Releaseメタデータ・bin・offsetを照合。Callなし共通機能を同じC5・同じ受信器・設定で比較。Call自体は正式版に投入せず、候補の仕様期待値と比較する。元Firmwareは別物（前回はv0.8.0、今回の現物は未確認）。
- 対象COM・USB個体・チップ・容量・IPの対応を確かめ、元の全Flash、パーティション表、アプリ、LittleFS、公開設定、capabilities、OBS設定・起動状態をGit外に退避。読み出しと実機digestの一致前は書き込まない。設定形式と配置を確認し、元アプリが正式版と一致すれば基準版の書き換えは省く。各書き換え後はBluetooth再接続待ち。正式版で作ったCallなしfixtureを候補がそのまま読み込むことも確認する。

## 実施順と合格条件

| ID | 操作・照合 | 必須の証拠 |
|---|---|---|
| 1 基準版 | CallなしのOSC int/float/bool/string、単一・逐次、250 ms Wait、`Loop(3){A,Loop(2){B}}→Z`。下記の物理入力・ペア・Rotationの共通fixtureも採取 | PCで値・型・件数・順序（Loopは `ABBABBABBZ`）、入力操作票、MIDI/HIDの解放、OBSの実状態。版にない診断は未測定 |
| 2 候補起動・配置 | `ready`、`engineReady`、入力、Wi-Fi、BLE MIDI/HID購読、稼働0、旧設定の一致。ELF/mapと `m/h` で下表の配置を確認 | 再起動ごとの配置・カウンタ起点を確保。一致しなければ負荷試験に進まない |
| 3 メモリ先行ゲート | Wi-Fi＋BLE両購読、OBS未接続でPhase 2Dの224枠fixtureを再利用。待機・status、設定GET単独4回、保存→GET 3回。再起動して設定全体を再照合し、別起動でも同じ区間を測る | 各GETの全内容一致、各区間の割当失敗増分0。失敗・不一致・監視不能なら実行負荷へ進まず原因と復元状態を報告 |
| 4 通常Chain・物理・Rotation | 基準版と同じ操作。KEY1を10往復、KEY1→KEY2を10組、KEY1保持中CW3・CCW3・Push往復、最後にKEY12。Rotation Valueは別fixtureでStop/Wrap各CW5・CCW5。float・CC23・OBS音量はCW4・CCW4 | 操作票と入力別OSC72件、最後に全解放。低速で0–4の端点・折返し、float 0–1（0.25刻み）、CC 0–100（25刻み）、OBS −40–0 dB（10 dB刻み）を各段照合。高速集約と取りこぼしは区別 |
| 5 混在・実行限界 | 通常 `N1 Wait N2` と `P1 Call(T) P2` を両開始順で重ねる。別親と直接入力から同じ子を起動。27段Call末端Wait、親子Loop、Call入りLoopと通常Loop各100 OSC。31→32枠の後、物理KEY9（待機prefix）、KEY10（即時）、KEY11（先頭Stop） | 27段が1 Invocationのまま逆順復帰。子直起動による余分な親末尾なし。各Chainの順・個数、競合中の共有FIFO、終了0。33件目は拒否+1でprefixなし、即時出力あり、Stopは取消+32と自身の後続出力。全Invocation間の任意の全順序は要求しない。112 Loopフレーム仕様を維持 |
| 6 BLE・Panic | Note On/Off/CC、All Notes対象の重複排除、holdペア、F13・修飾・ReleaseAll、Consumer全7 usage。子ペア完了→親OSC。別試行で32枠＋HID押下中のPanic、MIDI hold中のPanic | PCでNote Off／F13 Up先着、Consumer Report ID 2のE9/EA/E2/CD/B5/B6/B7各押下→neutral。Panic後0枠、HID解放、保留Off/CCは再生されず新規CC・明示Offは届く。送信済みOnすべてへの自動Offは期待しない。USB MIDI/HIDはC5非対応でN/A |
| 7 OBS・通信共存 | 実OBS Ready、シーン一覧のWebUI個別照合。専用シーン・入力でmute、音量、scene/group item、media、通常OSC後続。録画・配信・VirtualCam・ReplayBufferはC6同様に隔離プロファイル・ローカル出力で開始/停止等を照合。BLE購読＋OBS Readyで224 Action GETと208連番OSC。BLE、OBSを一方ずつ切断・再接続 | OBS側readbackで作用確認（OSC先着を完了の代替にしない）。GET全体一致、PCで連番208/208・順序一致、割当失敗増分0。復帰後に新規MIDI/HID/OSC・OBS操作を実受信。出力先が隔離できない操作は個別HOLD |
| 8 保存・互換 | 実WebUIのFull/Key export→import、組込Default/Empty、別キーへのCall貼付。ID範囲外、直接・間接循環を試す | schema v2、絶対targetInputを保持。無効Importは編集内容不変、stage/commit拒否後もactive GET不変。保存・再起動後の一致と出力を確認 |
| 9 復元 | 元アプリ・LittleFSを復元・flash照合。変更した設定・OBS/PC環境も戻す | 元版・公開設定・capabilities・OBS設定一致、入力準備・通信・稼働0。元版にengineReadyがなければ安全な空Chain受付を別証拠にする。照合未完は完了扱いにしない |

### C5で再確認する配置と引用範囲

| 領域 | 今回の確認 | Phase 2Dから引用する証拠 |
|---|---|---|
| Invocation | 内部Engine 80 B、296 B×32＝9,472 Bの固定プール。起動後に両端がPSRAM、満杯・Stop/Panic・保存でも同一boot内でアドレス不変 | 配置前後の比較・一回確保の実装。実行上限は削らない |
| config GET用JsonDocument | C5の通常ビルドが `ConfigGetAllocator`（SPIRAM＋8BIT、内部fallbackなし）を使うことをソースで照合。通常版の224枠GETの結果・ヒープ・失敗増分を測定する | [A＋C診断](../../call-chain-c5-phase2d/plan-ac/README.md)で送信中のJSON動的占有が内部0 B／外部最大2,064 B。通常版の前後スナップショットだけで、全割当先を再証明したとはしない |
| OBS assembly | 16,400 Bの両端がPSRAM。内部の二面sceneBuffers（8,344 B）は別。OBS Ready・一覧更新・再接続、GET＋OSC同時負荷で安定性を再確認 | [第1段階](../../call-chain-c5-phase2d/assembly-stage1/README.md)のELF/map・実配置、[第2段階](../../call-chain-c5-phase2d/assembly-stage2/README.md)の16,384 B単一／分割、断片途中切断・復帰、確保失敗注入。今回の通常版に注入経路を足さず、異常や関連差分がなければ境界試験を全面再演しない |

### 計測・通信環境・判定

- PC：UDP生受信・decode、BLE MIDI、F13等のキー状態、Windows HID APIのConsumer collection、実OBS WebSocket readback、ブラウザ、USBシリアル、Flash照合ツール。別のChainPadを誤認しないようUSBとBLE受信先を事前識別する。診断は一つのプロセスでポートを保持し、負荷中に再open・逐次出力しない。物理操作時は受信と事前条件の成立を確認してから依頼する。
- 各区間にID・boot/uptime・接続条件を付け、前後の `m/h` とstatusから内部8BIT／PSRAMそれぞれのfree・min・largest、内部DMA (`INTERNAL|DMA|8BIT`) largest、取得可能な全taskのstack low-water（bytes）、alloc failuresと記録済みcaps/要求サイズ、running/rejected/cancelled・overflowを採る。32枠中・OBS Ready・切断復帰時も採取。同一bootの差分のみ有効。境界値は瞬間下限ではなく、Phase 2Dの観測値を固定の合格閾値にはしない。
- Wi-Fi：C5も現行の共通実装では任意のSTA強制切断用APIなし。30秒切断後のAP-only移行・再起動後の接続は別仕様。**今回の専用APの有無・個別切断権限は未確認。** 専用APまたはC5だけを切れる環境があれば30秒未満のリンク断で、AP側イベント、同一uptime、STA復帰、連番OSCを確認。共有AP全体の停止や他機への干渉はしない。不可なら該当行HOLD、起動時STA・BLE/OBS再接続は独立に記録。
- PASS＝必要な受信・状態・設定・配置が一致し、測定各区間で割当失敗増分0、復元完了。FAIL＝新規割当失敗（先頭で採取し重い試験を停止）、出力欠落・重複・保証順序違反、通常ペアの解放漏れ、Invocation残留、破損等。HOLD＝前提不成立、受信／必要な診断不足、説明不能な差、復元未照合。明確な不具合を計測不備で上書きしない。既知の同期GET時の到着空白だけはFAIL根拠にしない。
- 証拠は本ディレクトリにケース別の期待・実測・判定、匿名化したPC時刻／OSC列・MIDI/HID・OBS値、設定一致のハッシュ、メモリ前後・復元結果を保存する。原本・資格情報・識別子・実設定・binはGit外。旧証拠と今回の直接観測を区別し、未測定を0としない。C5終了時に総合判定案を報告し、S3着手前にレビューを待つ。
