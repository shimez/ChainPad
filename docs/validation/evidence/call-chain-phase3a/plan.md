# Phase 3A：C6先行・既存機能回帰計画（未実施）

候補 `7308e474be4af031adb660bee9644c9ba0f76caa`、Callなしの比較基準は正式版 `v0.13.0`。実機の退避版が別バージョンなら比較基準とは区別する。C6レビュー後にC5、C5レビュー後にS3。C3はホスト・通常ビルド・静的検査のみ。

## 検証マトリクス

各ケース名を証拠のIDにする。下記は期待値であって実測値ではない。

| ID | 入力・条件 | 観測値・合格条件 |
|---|---|---|
| A1 OSC | Callなし。単発と連続で負のint、float 0.5、bool両値、UTF-8 string。さらに同じキーを2回 | UDPのaddress・`,i/f/T/F/s`・引数・パディングをデコードし全件照合。単発は1回、連続は設定順、再押下は先頭から。汎用OSC Toggle／押下ごとのSequenceは実装なしのためN/A |
| A2 MIDI | 単独Note On/Off、CC、hold付きNote OnOff、定義済みノートのAll Notes On/Off/OnOff。対象ノートを2個＋重複定義 | PC MIDIイベントのchannel・note/CC・value・順序・件数を比較。Allは全128音やCC123ではなく、対象transportの有効な定義を重複排除した集合。通常完了時のOffを実受信 |
| A3 HID | F13単独Down/Up、修飾付きDownUp、ReleaseAll、Consumer | PCでDown/Up、修飾・解放を確認。Consumerは音量±、mute、再生/一時停止、次、前、停止の7 usageとneutral。OSの音量表示だけではreleaseの証拠にしない。受信器が取得できなければ該当行HOLD |
| A4 Wait/Loop | `A → Wait(250ms) → B`、`Loop(3){A,Loop(2){B}} → Z` | 前者はAの後に待機してB。後者は `ABBABBABBZ` で終了、running=0。単独時の間隔と件数を基準版と比較。PC到着時刻はFirmware時計ではない |
| A5 OBS | 専用シーン・入力でscene変更、mute設定／2回toggle、音量絶対／相対、scene/group item可視性、media操作。通常OSCを後置 | OBS実状態を読戻し、toggleは元に戻ること。OBSはワーカー経由なのでOSC到着をOBS処理完了の証明にしない。配信・録画、VirtualCam、ReplayBufferのStart/Stop等は隔離プロファイルと受け先のある操作だけ実機。未確認操作は個別HOLD、検出系GetをユーザーActionとして数えない |
| A6 Encoder | ActionChainのCW/CCW各1ノッチ、Push press/release。RotationValueはrange=4、initial=0、OSC int 0..4、Stop／Wrapを別々に設定 | イベント26/27、24/25を別マーカーで照合。Valueの低速操作は位置・出力値・端点を照合。OSC float、MIDI CC、OBS音量も出力先別に測る。高速時の最新値集約を全ノッチ分の通信欠落と誤判定しない |
| B1 並行・直接呼出し | 通常 `N1 Wait N2` と親 `P1 Call(T) P2` を開始順を反転して重ねる。子 `T1 Wait T2` を直接入力からも起動。別の親Qも同じTへ | 親ごとの前後と子の必要個数を別markerで照合。子の直接入力は親末尾を生成しない。呼出し自身は追加Invocationを消費しない。単独対照＋開始をずらした実行で、同一子の識別不能なパケットに架空の実行IDを割り当てない |
| B2 並行Loop | `Loop(2){Call(T),P}` とCallなし `Loop{N}` を同時に待機させる | 各Chainの個数・順序・終了を照合。Loop到達可能経路では共有OSC FIFO。両者が継続して券待ちになる区間を作り、片側の追越し／飢餓を調べる。無関係なInvocationの全順序やPC到着間隔の厳密な4msは要求しない |
| C1 物理入力 | 操作者が指定のキーを各10往復、次に2キーを交互10組、キー保持中にCW/CCW・Push押下／解放 | デバウンス10msを十分超える操作間隔。操作者の回数表、入力別OSC、pressedMask、overflow前後で照合。最後に全解放。HTTP triggerとは別に判定 |
| C2 満杯 | APIで長いWaitを31→32件。物理キーで先頭marker付きの待機Chainを33件目として投入。続いて即時ChainとStop | 32維持・拒否+1・拒否されたprefixは出ない。枠不要の即時Chainは動作。先頭Stopは満杯でも他を取消し、自身の後続markerだけを送る。取消し済みの遅延出力なし |
| C3 Panic | 別試行で32件をPanic。HID押下中・MIDIの明示Off後／保留中も別試行 | running=0、HID neutral、古いキュー再生なし。PanicはMIDI送信待ちを消去し、送信済みOnに汎用Offを自動生成する仕様ではない。保留中に既送信Onが残れば観測結果を明記し、通常ペアのOff漏れと区別。試験受信先をリセット |
| D 通信 | Wi-Fi＋BLE購読、さらにOBS Ready。OSC連番＋WebUI GET、保存後の再実行。BLEとOBSを個別に切断／再接続、Wi-Fi復帰後にも再送 | 接続中に発行した連番は全件一致。切断中に利用不能だった出力と復帰後の新規出力を別計数。OBSシーン一覧と画面表示を照合。GET時の空白は記録するが、欠落やDMA失敗は既知制約に含めない |
| E 保存・Import | 旧版由来のCallなしschemaVersion 2を候補で読込、224枠保存・再起動。実WebUIのFull/Key export→import、組込Preset、別キーへの貼付、不正ID・直接/間接循環 | 公開設定の正規化後の内容・再実行結果が一致。Full/Key固有の対象範囲やWi-Fi秘密値除外を尊重。CallありKey貼付は絶対targetInputを保持。UI拒否で編集中設定を、stage/commit拒否でactive設定を保護 |

根拠：`src/model.{h,cpp}`、`osc_backend.cpp`、`midi_backend.cpp`、`keyboard_backend.cpp`、`obs_command.h`、`obs_backend.cpp`、`rotation*.{h,cpp}`、`inputs.cpp`、`engine.cpp`、`chain_loop.h`、`main.cpp`、`capabilities.h`、`web/presets.js`。12キー＋Pushはpress/release共有16枠、CW/CCW各8枠、全224。Loop count 1..999、ローカル最大8段、Call最大27段。C6/C5/C3のUSB MIDI/HIDはN/A（USBシリアルは別）。S3ではUSB/BLE別試行とMIDI Bothを追加。C3のOBS実行は非対応。

## C6の順序と記録

1. 現物識別→Git外にFlash全域・アプリ・LittleFS・公開設定・capabilitiesを退避し実機照合。各書換後はBLE再接続待ち。基準版のCallなし設定と候補で、PC・受信器・接続・操作票をそろえる。基準版で設定したデータを残して候補に切替え、互換読込も確認する。
2. 候補の通常ビルドSHA、起動・入力・Engine・接続を確認。CallなしA→物理C1→混在B→満杯／停止C2,C3→通信D→保存E。設定を段階別に入れ替え、意図しないキーreleaseやRotationValueが別ケースを起動しないようにする。
3. 各ケース前後のシリアル `m/h`、status、起動ID/uptime、内部free/min、DMA largest、loopTask/OBS stack low-water、failures、running/rejected/cancelled、入出力overflowを記録。再起動をまたぐカウンタ引算はしない。境界のlargestは送信中の下限ではない。C6の現行通常版にはDMA largestの出力がない（`src/diagnostics.cpp` の対象はC5/S3）。内部8-bit largestで代用せず、測定方法の承認までこの項目はHOLD。
4. エラー時は再試行で失敗を消さず、ケースと受信ログを保全。復元後にFlash照合、元設定・capabilities、入力・通信・稼働0を確認。旧版にengineReadyがなければ実行可能性を別の無害な観測で確認し、API値と区別する。
5. 各行に期待値・実測値・PASS/FAIL/HOLDまたは理由付きN/Aを付け、匿名化したPC受信・物理操作票・メモリ・復元結果を `c6/` に保存。C6を報告しレビューで停止。C5/S3は未着手。C3のホスト・通常ビルドは実機判定に含めない。

## Phase 2Dから再利用するもの

`../call-chain-c6-phase2d/` のPC受信順・最大Call・満杯・保存・OBS・復元の試験設計、`../call-chain-c5-phase2d/assembly-stage1/` の224枠fixtureと区間定義、`assembly-stage2/` のOSC連番・OBS・既知GET遅延の区別、`../call-chain-s3-phase2d/` のUSB/BLE別イベント記録形式。既存記録は参照専用。Phase 3Aの物理入力・Callなし基準比較・混在ケースの合否を過去の合格で埋めない。
