# Phase 3A / XIAO ESP32-S3 統合回帰計画（当初計画）

目的はCall追加後も、既存ChainとUSB・BLE・Wi-Fi出力が同じS3上で共存することの確認。C6・C5はともに次機種移行が条件付きPASS、総合HOLD。S3は独立に判定する。[共通計画](../plan.md)のケースIDを引き継ぐ。

## 対象と既存証拠

- 候補：`feature/call-chain` の `f78d9123eec7a466bc348183d6f7aa9f26f03762`、通常環境 `xiao_esp32s3`。承認後に依存版とビルドSHAを記録し、ホスト・保存障害・OBSテスト、通常ビルド、ELF/map、`tests/check_image.py --env xiao_esp32s3`（8 MiB・配置・USB記述子等）を確認。トレース版は使わない。
- Callなしの基準は正式Release `v0.13.0` の**S3用配布品**。タグ・Releaseメタデータ・binのSHA256と配置を照合し、同じ個体・PC・fixtureで比較。復元先はその時点の現物であり、[Phase 2DのS3](../../call-chain-s3-phase2d/README.md)の元版v0.5.3とは決め打ちしない。
- Phase 2Dの[224枠記録](../../call-chain-s3-phase2d/normal-get.json)、[実行上限](../../call-chain-s3-phase2d/normal.json)、[USB](../../call-chain-s3-phase2d/usb-events.json)・[BLE](../../call-chain-s3-phase2d/ble-events.json)は構成・比較値として引用する。[C6補完](../c6/supplement.md)の物理操作票・Consumer生レポート・OBS隔離手順、[C5結果](../c5/results.md)のゲートと復元手順を再利用。これらをS3候補の実測値に転記しない。GET速度の版間比較、確保失敗注入、C5の250/500 ms×各5回の順序試験は繰り返さない。

## 1. 個体保護と書き換え（元版→基準版→候補→元版）

| モード | 用途と切り替え |
|---|---|
| **通常：通常CDC** | 初めにユーザーと対象個体を確認。版、チップ、設定全体・capabilities・OBS設定、Wi-Fi/BLE/USB/OBS接続、入力ready・稼働数を記録。USB MIDI・Keyboard・ConsumerはCOMとは別のインターフェースとしてPCで識別する。 |
| **書き込み：書込用CDC** | バックアップ・各書き込みの直前にシリアル・USB受信器を閉じ、準備ができてからユーザーに「BOOTを押したままRESETを押して離し、その後BOOTを離す」と依頼。書込用CDC再列挙、USB物理位置・チップ・容量と対象の対応を確認。不一致なら停止し、通常CDCへの書き込みや別ポートの総当たりはしない。 |
| **通常へ復帰：通常CDC** | 書き込み・照合完了後、必要時のみBOOTを押さずRESETを1回依頼。通常CDCとUSB複合デバイスの再列挙、HTTPでの版・能力・入力準備を確認し、MIDI/HIDのハンドルを開き直す。BLE再接続が必要ならこの時点で依頼する。 |

初回は書込用CDCで**全8 MiBを読み出して実Flashとのdigest一致**を確認し、パーティション表から元factoryとsettings（LittleFS）を切り出し照合する。バックアップ・生設定・元OBS/PC環境はGit外。未照合では書かない。元アプリが基準版と一致する場合は不要な書き換えを省く。表・容量が適合する場合のみアプリ領域を書き換え、都度digest照合。異なる場合は進め方をレビューする。正式版のCallなし設定を保持して候補へ切り替え、全体読戻しで互換性を確認する。

## 2. 先行ゲート：内部RAMと224 Actions

S3はPSRAM搭載でNimBLE外部メモリ確保を設定する一方、Phase 2DのEngineは**内部9,548 B**。C5の「InvocationはPSRAM配置」という基準を移植しない。候補ELF/mapでEngineサイズ・配置、通常起動時にPSRAM・USB列挙・Engine準備を確認する。

1. Wi-Fi接続、USB MIDI/HID列挙・受信器準備、BLE MIDI/HID両購読、**OBS連携Disabled**を確認。単なるサーバー停止でConnectingが混ざる条件とは分ける。Phase 2Dと同じ224 Action列（通信先は試験用）を保存し、Action数・正規化ハッシュとGET全内容を照合する。
2. 待機・status、設定GET単独4回、保存→GET 3回を順次測定。再起動後に設定を変えずに照合し、接続を揃えてもう1起動分繰り返す。各区間の前後で同一uptime系列、内部free/min/largest、内部DMA largest、PSRAM使用量、各取得可能タスクのstack low-water（bytes）、割当失敗・overflow・稼働を採る。再起動をまたいで差分を取らない。
3. **2起動とも設定一致、割当失敗増分0、接続・配置維持、予期しない再起動・overflowなしが移行条件。** Phase 2DのDMA largest 27,636～31,732 B、満杯時loopTask 4,052 B・OBS worker 5,984 Bは比較値であって最低保証値ではない。境界測定を送信中の瞬間下限と扱わない。失敗または必要な診断欠測なら以降の候補負荷試験を停止。合格後にOBSを試験条件へ戻す。

## 3. 実行ケースとS3での確認方法

Callなしケースは基準版で対照を先に採り、候補はゲート後に同じ操作を行う。USB/BLE/Coreの設定と実測をケースID・transport別に分ける。

| ID | 操作 | 判定に使う証拠 |
|---|---|---|
| A1/A4 | OSC int負値・float・bool両値・UTF-8、再押下、250 ms Wait、親子Loop | UDP電文の型・値・順序・件数。Loopは `ABBABBABBZ`、終了0。 |
| A2/A3 | USBのみ、BLEのみでNote On/Off・CC・holdペア、重複定義を含むAll Notes、F13 Down/Up・修飾・ReleaseAll、Consumer全7 usage。MIDI Bothでは両入力を同時に開く | 実際の送信先ごとに期待列を照合。Bothの各イベントは各対象に1回、片側だけのActionは反対側に出ない。Keyboardは送信先別試行、Consumerは該当HID collectionと**そのtransportのdescriptor**で押下・neutralを照合。OS音量表示だけでは合格にしない。 |
| B1/B2・ペア | 通常WaitとCallを両開始順で並行。2親と直接入力から同じ子を起動。27段Call末端Wait、親子Loop・競合OSC FIFO。USBとBLEそれぞれで子末尾のMIDI／HIDペア→親OSC | 27段は1枠で逆順復帰。直接起動に親末尾なし。各Chainの順・件数と競合中のFIFOを確認。ペア判定は下記。 |
| C1/A6 | KEY1を10往復、KEY1→2を10組、KEY1保持でCW3・CCW3・Push往復、最後にKEY12。Stop/Wrap各CW5・CCW5、float/CC/OBSはCW4・CCW4 | PC受信を開始してから操作依頼。72イベントと全解放、端点・折返し、0.25刻み／CC25刻み／10 dB刻み。 |
| C2/C3 | 31→32枠を待機させ、物理KEY9で33件目、KEY10即時、KEY11先頭Stop。別試行で32枠＋HID押下のPanic、保留MIDI中のPanic | 拒否+1、拒否prefixなし、即時出力、取消+32、Stop自身の後続、稼働0。USB/BLEのHID解放を区別。保留出力が再生されず新規CC・明示Offが届くこと。既送信Onへの自動Offは要求しない。 |
| A5/D | Wi-Fi＋BLE購読＋USB準備＋OBS Ready。224枠の共通CallなしfixtureでGETと208連番OSCを重ねる。専用シーンで操作し、一覧をWebUIと比較 | 全設定と208/208件順序一致、失敗増分0。OBS側でscene/group、mute、音量、mediaの実状態を確認。録画・配信・VirtualCam・Replayは隔離プロファイルとローカル出力でのみ実施。既知GET空白のみではFAILとしない。 |
| D 再接続 | BLE・OBSを個別に切断・復帰。USBは給電断と区別できる条件で再列挙／受信器再オープンを確認。STAはS3だけを短時間（30秒未満）切れる環境がある場合のみ | uptimeと他経路の状態、復帰後の新規出力を照合。給電断後のUSB再起動や起動時STA接続を同一boot再接続の証拠にしない。共有APしか使えなければSTAは個別HOLD。 |
| E | 実WebUIのFull/Key往復、Default/Empty、別キーへのCall貼付、範囲外ID・直接/間接循環、保存・再起動後に再実行 | 絶対target維持、拒否時の編集内容／active設定不変、保存内容と再起動後出力一致。 |

**ペアActionの保証対象**は、正常送信時に子のバックエンド完了条件を満たした後で親を再開すること。[C5のCallあり／なし比較](../c5/pair-order-review.md)に基づき、USB/BLEの解放とWi-Fi OSCのPC観測順は合否条件にしない。候補ソースで `pairPending`・該当transportのキュー完了・親フレーム復帰・後続dispatchの順を確認し、実機ではOn/Down→Off/Upと後続OSCがそれぞれ期待回数届くかを確認する。**欠落・重複・解放漏れはFAIL**。PC時刻は記録するが内部時刻とはしない。内部順を直接測っていない場合は「ソース確認」と明記する。

## 4. 復元・報告・終了条件

試験終了または中断時は負荷を止め、試験用OBS出力・シーン・プロファイルを戻す。操作準備後に上記手順で書込用CDCへ移行し、退避時と同じ個体・領域であることを再確認。元アプリ・LittleFSを復元し**両領域を個別に実Flashとdigest照合**する。全Flash退避を理由にNVS・ペアリング情報まで一律上書きしない。通常CDCに戻し、元版・全設定・capabilities・OBS設定、入力ready／稼働0、Wi-Fi・BLE・USB・OBSが試験前状態と一致することを確認。旧版にEngine準備表示がなければ無害な空Chainの受付を別証拠とする。

ケースごとに条件・期待・実測・判定・復元状況を記録。出力と設定・カウンタが期待どおりならPASS。新規割当失敗・設定破損・保証順違反等はFAIL、前提不成立・測定不能・復元未照合はHOLD。異常時は後続試験に進まず報告し、再試行で初回結果を消さない。匿名化した受信列・メモリ境界値・照合結果のみ証拠化し、実設定・生ログ・bin・識別子はGit外に保持する。復元照合後、未検証項目を明記してS3の総合判定案をレビューに出す。
