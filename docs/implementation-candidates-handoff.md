# 実装候補・検討事項の引継ぎ

更新日：2026-10-31

## 1. この資料の位置づけ

OBS WebSocket機能の追加と、VRChatからのOSC受信について相談した内容を記録する。
この資料の作成時点では、以下の機能の実装着手は依頼されていない。
候補としての記録を実装承認と解釈しないこと。

### ユーザーの方針

- 複雑な機能、後戻りできない機能はなるべく実装したくない。
- 日常的に使う、設定・操作が分かりやすい機能を優先する。
- 作成・削除・設定の大幅な書き換えなどを伴う機能は積極的に広げない。

### 検討状況

| 項目 | 状態 |
|---|---|
| 実装候補1：OBSメディア制御 | ユーザーが明示的に候補1として指定 |
| 実装候補2：OBSメディアの再生位置変更 | ユーザーが明示的に候補2として指定 |
| 録画一時停止／再開 | ユーザーから肯定的な反応。候補番号・実装範囲は未確定 |
| 仮想カメラ開始／停止 | ユーザーから肯定的な反応。候補番号・実装範囲は未確定 |
| VRChatからのOSC受信 | 実現可能な機能例を説明した段階。採用・優先順位は未確定 |

## 2. 実装候補1：OBSメディア制御

実OBSでの事前検証用に、独立した[OBS Media Tester](../tools/ObsMediaTester/README.md)を用意した（2026-11-01）。
候補1・2のAPIをPCから直接試し、要求・応答・状態・処理時間を記録できる。
このツールの追加は、ChainPad Firmwareへの実装着手とは別。

### 目的とUI案

OBS内のメディアソースを、ChainPadのキーから操作する。

```text
OBS操作：メディア制御
対象ソース名：一覧から選択、または手入力
再生操作：再生／一時停止／停止／先頭から再生／次へ／前へ
```

UI名・保存用のoperation名・JSONスキーマは未決定。

### API

`TriggerMediaInputAction`を使用する。対象はシーン内の配置ではなく入力ソースそのもの。
`sceneName`や`sceneItemId`は不要。名前指定では以下のrequestDataを送る。

```json
{
  "inputName": "オープニング動画",
  "mediaAction": "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_RESTART"
}
```

| 操作 | mediaAction |
|---|---|
| 再生／一時停止から再開 | `OBS_WEBSOCKET_MEDIA_INPUT_ACTION_PLAY` |
| 一時停止 | `OBS_WEBSOCKET_MEDIA_INPUT_ACTION_PAUSE` |
| 停止 | `OBS_WEBSOCKET_MEDIA_INPUT_ACTION_STOP` |
| 先頭から再生 | `OBS_WEBSOCKET_MEDIA_INPUT_ACTION_RESTART` |
| 次のプレイリスト項目 | `OBS_WEBSOCKET_MEDIA_INPUT_ACTION_NEXT` |
| 前のプレイリスト項目 | `OBS_WEBSOCKET_MEDIA_INPUT_ACTION_PREVIOUS` |

- 各操作は1回のAPI要求で実行できる。
- PLAYとRESTARTを区別する。停止・終了後のPLAYの挙動はソース実装に依存する。
- NEXT／PREVIOUSはVLCなどプレイリスト対応ソース向け。
- ソースの表示／非表示は変更しない。
- 複数シーンで共有された同じ入力ソースは、再生状態も共有される。
- ブラウザソース内の動画などを汎用的に操作できるAPIではない。
- ソースのアクティブ／非アクティブ時の再生設定とも相互作用する。
- API成功だけで、すべてのソースが実際にその操作に対応したことを保証しない。
- 現行公式仕様には`inputUuid`指定もあるが、今回の相談はソース名指定を基本とした。

「再生／一時停止トグル」は専用APIがなく、状態取得と操作の2段階処理が必要。
候補1で合意された操作一覧には含めず、追加する場合は別途相談する。

### 対象ソースの候補取得

`GetInputList`から以下のinputKindを抽出する案。

| OBSソース | inputKind |
|---|---|
| メディアソース | `ffmpeg_source` |
| VLCビデオソース | `vlc_source` |

- 全入力を1回取得して抽出するか、inputKindを指定して個別取得するかは未決定。
- 一覧から選択と手入力を併用する。既存の候補取得UIを参考にする。
- この2種類だけの抽出では、メディア制御対応の外部プラグインは候補から外れる。
- ミュート対象一覧の「音声を持つソース」という判定をそのまま流用しない。

## 3. 実装候補2：OBSメディアの再生位置変更

### APIとパラメーター

| 操作 | API | パラメーター |
|---|---|---|
| 絶対位置へ移動 | `SetMediaInputCursor` | `inputName`と`mediaCursor`（ミリ秒、0以上） |
| 現在位置から進む／戻る | `OffsetMediaInputCursor` | `inputName`と`mediaCursorOffset`（ミリ秒、正負可） |

30秒地点へ移動するrequestData：

```json
{"inputName":"オープニング動画","mediaCursor":30000}
```

10秒戻すrequestData：

```json
{"inputName":"オープニング動画","mediaCursorOffset":-10000}
```

### 動作条件

- 再生中と一時停止中の両方で位置変更できる。
- 再生中は移動先から再生を続ける。一時停止中は位置変更だけを行い、再開時にその位置から再生する。
- 停止中・再生終了後などは、現在の公式実装ではInvalidResourceStateとなる。
- シーク可否・位置精度はソースとメディア形式に依存する。
- 絶対位置は0以上を検証するが、メディアの長さを超えるかはAPI側で検証しない。
- 相対移動は現在の公式実装では0未満を0へ補正する。末尾側の上限確認は行わない。

### 関連する状態取得API

`GetMediaInputStatus`に対象ソース名を渡すと、以下が返る。

```json
{
  "mediaState": "OBS_MEDIA_STATE_PLAYING",
  "mediaDuration": 120000,
  "mediaCursor": 15000
}
```

長さ・位置はミリ秒。現在の公式実装では再生中／一時停止中以外は両方null。
状態はPLAYING、PAUSED、STOPPED、ENDED、OPENING、BUFFERING、ERROR、NONEなど。

### 未決事項

- UIは秒入力にしてミリ秒へ変換するか、ミリ秒入力にするか。
- 時間入力の上限、丸め、整数検証。
- 候補1と同じソース一覧UIを共有するか。
- 長さ取得と上限補正を追加するか、1回の位置変更要求に留めるか。
- エンコーダーとの連携は未決定。候補指定だけでRotation Valueへの対応まで広げない。

## 4. 肯定的な反応のあった追加操作

| 機能 | API | 対象名などの追加パラメーター |
|---|---|---|
| 録画一時停止 | `PauseRecord` | 不要 |
| 録画再開 | `ResumeRecord` | 不要 |
| 仮想カメラ開始 | `StartVirtualCam` | 不要 |
| 仮想カメラ停止 | `StopVirtualCam` | 不要 |

既存の録画開始／停止と同じように、個別のOBS操作として追加する案を提示済み。
トグル追加は未決定。録画一時停止の可否はOBSの録画設定等に依存し、停止中の映像は記録されない。

## 5. VRChatからのOSC受信：相談段階の機能案

ユーザーから「VRChatのOSC送信をChainPadが受信できた場合、どのような機能を実装できるか」と相談があった。
以下は提案例であり、採用済みの要件ではない。

| 案 | 受信する情報の例 | 実現できること |
|---|---|---|
| 状態表示 | `MuteSelf`、`AFK`、`/usercamera/Mode`、`/usercamera/Streaming` | WebUIや本体LEDで状態確認 |
| Rotation Value同期 | `/usercamera/Zoom`、`Exposure`、`FocalDistance`、`SmoothingStrength` | VRChat側で変更した値からエンコーダー調整を続け、値飛びを抑える |
| Boolトグル | カメラのStreaming・SmoothMovement・Lock、独自アバターパラメーター | 最後に受信した状態を反転して送信 |
| 状態変化によるChain実行 | `/avatar/parameters/AFK`、`MuteSelf`、カメラモード | 離席中表示やOBSアイコンを連動 |
| アバター別設定 | `/avatar/change` | アバターIDごとのOSCアドレス・操作セットの切り替え |
| ジェスチャー／連続値連携 | GestureLeft／Right、Voice、VelocityMagnitude、Grounded | ジェスチャー起動や外部演出への値変換 |

表中のMuteSelf・AFK・GestureLeft等は`/avatar/parameters/`配下。

### 提案した優先順位（ユーザー未承認）

1. 受信値のWebUI表示。
2. Rotation Valueと受信値の同期。
3. 受信状態に基づくBoolトグル。

特にカメラのズーム・露出を実際の現在値から調整する案は、既存のエンコーダー操作と相性がよい。

### 受信・状態管理で検討する事項

- VRChatの標準OSC送信先はPC自身。ChainPad宛てへの変更かPC側中継が必要。
  固定送信先の指定例：`--osc=9000:192.168.1.42:9001`（IPはChainPadのアドレス）。
- 他のOSCアプリも併用する場合の配送方法は別途設計する。
- `/input/*`やChatbox入力などのwrite-only項目を受信情報として扱わない。
- 基本的に自分のアバターのパラメーターが対象。他人のチャットを取得する機能ではない。
- UDPの欠落・順序逆転・未受信・受信値の古さを扱う。最後の受信値を常に現在値と断定しない。
- アバター変更時に、以前のアバターの状態を持ち越さない。
- 値受信による内部状態更新と、ユーザー操作による送信を区別し、送り返しループを作らない。
- 受信イベントでChainを起動する場合は、値の変化検出・頻度制限・初回受信時の扱いが必要。
- 連続値は必要なアドレスだけ保持し、高頻度受信がEngineやOBS Workerを阻害しないようにする。
- OSCQuery、初期値取得、状態の再同期まで含めるかは未決定。
- LED表示は既存の状態表示との優先順位を決める。キー別LEDはハードウェア対応が必要。
- 複数のRotation Outputを持つ場合、どの受信値から共通の回転ポジションへ戻すかは未設計。

## 6. 既存コードと実装時の参照先

OBS Actionは現在、シーン切り替え・配信開始／停止・録画開始／停止・ミュート設定／切り替え・シーンアイテム表示制御に対応。
UIの「グループ内アイテム表示制御」は別の保存operationではなく、`setSceneItemVisibility`と`containerType: group`で保持する。

| ファイル | 主な役割 |
|---|---|
| `src/obs_command.h` | 操作・コマンド・パラメーター |
| `src/obs_protocol.cpp` | OBS要求の生成・応答解析 |
| `src/obs_backend.cpp` | Worker・直列実行・通信状態 |
| `src/obs_actions.cpp` | Action受付・結果・一覧キャッシュ |
| `src/obs_http.cpp` | 候補取得HTTP API |
| `src/model.h` / `src/model.cpp` | 設定モデル・JSON保存復元 |
| `web/obs-actions.js` | OBS編集UI・候補管理 |
| `web/presets.js` / `web/action-ui.js` | Import／Export検証・表示 |
| `tests/obs/` / `tests/obs_control/` | プロトコル・Worker・HTTP試験 |
| `tests/test_hid_action_ui.cjs` / `tests/test_obs_scenes_ui.cjs` | 編集・保存・候補管理の回帰確認 |

既存方針として、候補取得は手動、候補取得失敗時も手入力可能、ChainはOBS応答待ちで停止しない。
キュー受付・Worker実行のタイミング、再接続後の自動再実行なしの方針を踏襲する。
メディアの相対移動やNEXT等は重複実行で結果が変わるため、自動リトライを安易に追加しない。

シーンアイテム実装時は、受信上限16 KiB・Worker stack 8 KiB・共有候補バッファを維持した。
メモリ／受信方式の大幅変更は相談事項。実装時は最新コードと以下の資料を確認する。

- [OBSシーンアイテム表示制御](obs-scene-items.md)
- [OBS入力候補](obs-inputs.md)
- [OBS統合状況](obs-integration.md)
- [リソース診断](resource-diagnostics.md)

## 7. 次に実装を依頼された場合

1. 対象候補・操作範囲・UIを確認する。
2. 現行コードに合わせて保存スキーマ、検証、候補取得方式を決める。
3. 必要な操作だけを追加し、保存復元・非対応機種での保持・Worker順序・失敗時の挙動を確認する。
4. 関連テストと対象機種ビルドを行い、実OBSでソース別の挙動を確認する。
5. 実機未確認事項、メモリ増加、API／OBSバージョン条件を報告する。

## 8. 参照情報

APIの挙動は2026-10-31時点で参照した公式仕様・実装に基づく。リンク先masterは更新されるため、実装時に利用対象バージョンを再確認する。

- [OBS WebSocket：Media Inputs Requests](https://github.com/obsproject/obs-websocket/blob/master/docs/generated/protocol.md#media-inputs-requests)
- [OBS WebSocket：メディア制御の公式実装](https://github.com/obsproject/obs-websocket/blob/master/src/requesthandler/RequestHandler_MediaInputs.cpp)
- [OBS WebSocket：GetInputList](https://github.com/obsproject/obs-websocket/blob/master/docs/generated/protocol.md#getinputlist)
- [VRChat Wiki：OSC](https://wiki.vrchat.com/wiki/OSC)

メディア操作・状態取得・位置変更の各API自体はWebSocket 5.0.0から存在する。
名前指定とUUID指定の対応時期を混同しないこと。
