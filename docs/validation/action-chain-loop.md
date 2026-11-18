# Action Chain Loop：実装計画・検証記録

## 起点とレビュー単位

- 起点：`0db11eb`（`docs: add interactive Loop Configurator mockup`）。
- 作業ブランチ：`feature/action-chain-loop`。作成時の作業ツリーはクリーン。
- 公開済み互換性の基準：v0.12.0。Firmware版とCHANGELOGは公開準備時に更新する。
- 各Phaseで実装・テスト・レビューを行い、利用者の承認後にコミットする。
- mainへのmerge、リモートへのpush、タグ・Release作成は別途明示承認が必要。

## 採用仕様

### 保存表現

```json
[
  {"protocol": "loopStart", "delayMs": 0, "count": 3},
  {"protocol": "wait", "delayMs": 100},
  {"protocol": "loopEnd", "delayMs": 0}
]
```

- `count`は内部の**合計実行回数**。1〜999の整数。0・負数・小数・文字列・booleanを拒否。
- Headerは上記3フィールド、Footerは上記2フィールドだけを持つ。
  Loop ID・ジャンプ先・折りたたみ状態・ランタイムカウンタは保存しない。
- 回数は`Action`のOSC Intと同じサイズのunion領域を利用し、224個のActionスロットを拡大しない。
- 相手のHeader／Footer位置は検証時に計算する。対応は同一Event内の入れ子順序で決める。
- 空Loopを許可。ネスト専用の深さ制限は設けない。
  16枠から導かれる最大8段、CW／CCWの8枠から導かれる最大4段を固定領域で扱う。
- Header／Footerは各1枠。Press／Release合計16、CW／CCW各8、全体224スロットを維持する。
- Loopを展開してActionをコピーしない。RAM使用量は実行回数に依存させない。

### 形式の互換性

- 追加Action種別として扱い、Config schemaVersion 2、LittleFS storageVersion 2、
  Full Preset version 2、Key Preset version 1を維持する方針。
- 新Firmwareは既存設定・Presetを引き続き読み込む。Loopなし設定の保存形状は変更しない。
- 旧Firmware／旧ConfiguratorはLoop Actionを認識できない。Loop入りデータを旧版へ
  読み込ませる互換性は提供しない。ダウングレードする場合はLoop導入前のバックアップ等を使う。
- 不明なActionや壊れた構造を黙って削除・修復して実行しない。
- ブラウザー表示用のCapabilitiesは実行・UI統合時に追加する。

### 実行（Phase 2）

- Invocationごとの固定長スタックに残り回数・再開位置を持つ。
- 各周回でWaitも実行し、最終周回のWaitを省略しない。
- MIDI／HIDペア送信の完了待ち、Backendの受付・失敗・キュー動作は既存仕様を維持する。
- Busy／Unavailable／Failedは記録して次へ進む。成功回数を満たすまでの再試行はしない。
- OBSの受付と最終完了は引き続き非同期。
- 再入力は独立Invocation。Stop Other Chainsは毎回その時点の他Invocationを停止し、
  自分のLoopを継続。Panic・設定適用時はLoopの再開状態も破棄する。
- 制御マーカーも実行量に数え、WaitなしLoop・連続Stopも実行予算を迂回できないようにする。
- Invocation単位とメインループ単位の実行量を管理し、公平な再開と入力・通信処理への復帰を検証する。
  命令数／時間の具体的な予算値はPhase 2の負荷計測で確定する。
- LoopなしChainの既存の同期実行・再開順序・満杯時受付を回帰テストで維持する。
- Loop入りChainは原則として開始前に実行枠を確保する。
  満杯時の例外は、最初のLoop Header／Wait／ペア送信より前にStopへ到達する直線部分を対象とする。
  拒否したChainから先頭Actionだけ送信することは認めない。
- 単独のBackend呼び出しを実行途中で強制中断するものではない。All Notesなど重いActionも測定対象。
- 回数上限だけではネストによる長時間実行を防げない。応答性を実行予算で確保し、
  総実行回数による自動打切りは導入しない。

### UI（Phase 3）

コミット済みモックの最終操作仕様を採用：

- 「＋Add Action」→編集欄のActionプルダウンからLoopへ変更。
- 見出しは単に「Loop」。通常Actionのボタン順は↑／↓／移動先…／編集／削除。
- 初期展開、ネストごとの開閉、650msホバー展開。展開中の見出しドラッグも全体移動。
- 内外の移動、自己／子孫への移動禁止、キャンセル時の配置保持。
- ドラッグ中のみ挿入ガイドを表示。「Loopここまで」は表示しない。
- Loop削除は内部ごと、Loop解除は内部を残す。内部追加も同じ「＋Add Action」。
- 折りたたみ・編集IDはUIのみ。保存／Presetへ混入させない。
- Input一覧の追従を維持。モックの説明欄・平坦配列プレビューは正式UIへ追加しない。
- 通常Action編集を再利用し、正式UIではモックサーバーによるソース抽出を使用しない。
  当初の別ファイルへの共有モジュール抽出は、Phase 3では既存の編集関数を
  `renderFlatActions()`として再利用する構成へ調整した。ツリー操作は独立モジュールに置く。

## Phase

### Phase 1：データ・検証基盤（今回）

- モデルへLoopStart／LoopEndを追加し、OSC IntとLoop回数をunion化。
- `src/chain_loop.h`に独立した厳密Codecと対応表解析を実装。
- 不正値・余分な属性・NUL混入・対応なしFooter／Header・ネスト・空Loop・枠超過を検証。
- `ChainView`の逆向きReleaseスロットも論理添字で扱う。
- 解析失敗時は対応表を空にし、不完全な対応表を返さない。
- 既存の保存／API CodecはまだLoopを受け付けない。
  Engine未対応の状態でLoopが1回だけ実行される等の中間状態を作らず、
  Phase 2でCodecとスケジューラを一緒に接続する。
- FrontendのAction作成・Preset受付の変更はPhase 3で行う。

### Phase 2：Firmware実行・保存への接続

- Phase 1のCodec・構造検証を全読込み／保存経路へ接続。
- スケジューラのLoopスタック、実行予算、中断・再開、公平性、受付判定を実装。
- Stop／Panic／失敗／ペア送信／Wait／時刻折り返し／設定適用の回帰確認。
- 保存往復・破損データ拒否・従来設定の読込み、4機種のサイズと実行負荷を確認。
- この段階はFirmware側の検証単位。正式Configuratorからの利用はPhase 3完了後。

### Phase 3：Configurator・Preset・全体検証

- ツリー編集、ドラッグ、移動ダイアログ、通常Action編集の再利用。
- Full／Key Presetと本体保存の往復、構造検証と枠計算の一致。
- マニュアル、ブラウザー操作、モバイル幅、既存JavaScriptテスト。
- 全4機種ビルド・イメージ検査、必要な実機確認項目を整理。

## Phase 1 検証

- `tests/host/loop_tests.h`を既存の`tests/run_host.ps1`へ組み込み
  （`core_tests.cpp`から呼出し。通常／HID trace有効の両ビルドで実行）。
- 既存の`sizeof(Action) == 352`を維持する静的検査。
- Loop対応表は18 bytes、解析用スタックは8 bytes。常駐対応表はまだ配置しない。
- `tests/run_host.ps1`：全成功。通常／HID trace有効のLoop基盤・既存Firmware回帰、
  HID trace単体、Factory Reset、Wi-Fi setup、C3／C6／C5 capabilityテストを含む。
  Action=352、Chain=5636、Config=82992 bytes（ホスト環境）を確認。
- `tests/test_*.cjs`：既存13ファイルすべて成功。
- `node docs/mockups/loop-configurator/model.test.cjs`：成功。
- `git diff --check`：成功（LF→CRLFの通知のみ）。
- 途中でPowerShellの起動エラーが再発したが、上記JavaScript・Git確認はその後実行できた。
- 全機種Firmwareビルド・イメージ検査・実機確認は後続Phaseで実施する。
- コミット：レビュー承認後、`74dd329`に確定。

## Phase 2 実装・検証（レビュー待ち）

### 実行制御

- `decodeChain`／`encodeChain`へLoop Codecと構造検証を接続。
  Config全体・Event単位API・LittleFSのstaging／scan／loadが同じ検証を利用する。
- `maxLoopCount: 999`をCapabilitiesへ追加。形式バージョンは維持。
- Invocationごとに最大8段の固定長フレーム（残り回数と先頭位置）を持つ。
  共有設定へカウンタを置かず、再入力を独立実行する。
- Loop入りInvocationは1回の再開につき最大16命令、
  全体でメインループ1回につき最大256命令。Header／Footer、Wait 0、Stopも消費する。
- `beginCycle()`はHTTP処理より前に1回だけ呼ぶ。trigger・tick・Stop継続では予算をリセットしない。
- tickでは残り予算を後続Invocationにも配分する。LoopなしChainは従来の同期実行と
  安定した再開順序を維持し、この追加のLoop予算では遅延させない。
- 満杯時、Loop Header／Wait（0も含む）／ペア送信より前のStopだけを受付例外とする。
  この有限の直線prefixは予算切れでもStopまで実行し、他Invocationを解放する。
  prefix分を残り予算から差し引き（最低0）、Loop本体は予算がなければ次cycleへ送る。
  したがって256はLoop実行の予算であり、既存の直線Chainとこの受付prefixを含む
  メインループ全体の命令数上限ではない。
- 制御マーカーはBackendへ送らず、accepted／skippedの送信統計にも加えない。
  内部の通常Action・Wait・Stopは従来どおり計数する。
- 時間ではなく命令数で区切る。単独のBackend呼出し時間や実機通信遅延の上限は保証しない。

### 重点検証

- 32枠満杯でHeader前の出力が漏れないこと、Loop内Stopでは受付を救済しないこと。
- 予算切れで満杯の場合も直線prefixのStopで受付し、本体は次cycleまで待つこと。
- ネストごとの回数初期化、Release逆向きスロット、Wait最終周回・時刻折返し。
- 最大回数・最大ネスト・32独立Invocation、連続Stopでの予算迂回防止。
- ペア送信の各周回完了待ち、Backend unavailableのスキップ継続。
- 全EventのConfig／LittleFS往復、不正構造のstaging拒否と既存設定の保持。

### 検証進捗

- 初回`tests/run_host.ps1`：通常版・HID trace版のLoop/既存回帰、
  trace単体、C3／C6／C5 capabilityを含め全成功。
- 追加した「予算切れ状態でもLoopなしChainを同期実行」
  「Loop中Panic後のHID解除と再入力」を含む最終`tests/run_host.ps1`：全成功。
  ホスト環境のEngineサイズは1440 bytes。Action=352、Chain=5636、Config=82992 bytesを維持。
- PowerShell起動エラーが途中で再発したが、ホスト検証終了後は起動できた。
- `pio run`：S3／C3／C6／C5の全4機種成功。
- `tests/check_image.py --env xiao_esp32s3 xiao_esp32c3 xiao_esp32c6 xiao_esp32c5`：
  全機種成功（partition／image配置／Flash header／機種識別／USB descriptor）。
- `git diff --check`：成功（LF→CRLF通知のみ）。
- Phase 2はレビュー承認後、`e4d283c`にコミット。

| 機種 | 静的RAM（bytes） | Application（bytes） |
|---|---:|---:|
| S3 | 139708 | 1680310 |
| C3 | 137524 | 1518199 |
| C6 | 181004 | 1917372 |
| C5 | 105168 | 1971565 |

検証した`firmware.bin`のSHA256：

```text
S3 7bf4d3e55fdca00a9f5b191a4398efe79f9dd6a085b27bf6745563778e8d898c
C3 1dcd1f7a9dc9c99cd98c4b88d09343490bfc43732a5409b32799b0291987f7fe
C6 2bdacc8d74adb767a128bbfc074bda825e284c2aa025968dde5739c7920a145c
C5 5828a2c0fbbd330c7f7717238915e8060f89dae0618320ec0740ca1973cbac67
```

### 後続の確認事項

- 実機への書込み・実機負荷計測は未実施。16／256命令という区切りの
  動作はホストテストで確認したが、実機の最大応答遅延は未測定。
- Phase 3統合後に、WaitなしLoop実行中のHTTP／入力／Panic応答と、
  All Notes等の重いAction、USB／BLE／OBS併用時の応答性を実機確認する。
- Configurator／PresetはPhase 3で接続する。本Phaseの成果はFirmware側の
  実装・自動検証単位であり、Loop機能全体の公開準備完了を意味しない。

## Phase 3（レビュー承認済み）

利用者のWebUIレビュー・修正確認後、Phase 3のコミット承認を取得。

- `web/loop-model.js`／`web/loop-editor.js`で正式Configuratorのツリー編集を実装。
  通常Actionの編集欄を再利用し、Header／Footerは保存配列にのみ現れる。
- Loopへの変換、追加、ネスト、回数、個別開閉、移動先ダイアログ、
  ↑／↓、Loop解除、内部ごと削除を接続。ドラッグはサブツリー単位。
- 入力一覧の追従を編集領域内に限定。モック専用説明・JSON表示は追加しない。
- Presetに厳密なLoop属性・対応関係の検証を追加。Full/Keyの形式番号は維持。
- 新規Loopモデル/Presetテストと既存JavaScriptテスト計14ファイルは成功。
- ブラウザーfixtureでLoop変換・内部追加・ネスト・回数変更・開閉・
  Key Preset往復・本体保存API往復・Loop解除・自己/子孫への移動先除外を確認。
- 実機試験案：`action-chain-loop-hardware.md`。
  対象はユーザー確認済みESP32C6、COM18、`192.168.0.15`。
  接続確認時はv0.12.0、BLE未接続。実機測定結果は同文書に記録。
- `tests/loop_hardware_probe.py`は実機の設定退避・試験用設定適用・
  HTTP RTT/OSC受信・Panic確認・元設定の復元とGET一致確認を行う。
  物理入力エッジ遅延とHTTP起動の遅延を混同しない。
- 実機負荷測定により、Phase 3でLoop全体の命令数予算を256→64へ調整。
  64命令版でも連続OSC32実行でHTTP遅延・送信失敗が残り、空Loop32個では再現しなかった。
- 利用者の方針承認後、Loop入りInvocationのOSCに全体4 ms間隔の受付制御を追加。
  到達時に連番を与え、待機中Invocationの先頭から許可する。待機ではnext／Loop回数／
  accepted／skippedを変更しない。実際のBackend失敗は従来どおりスキップする。
- Stop／Panicで破棄したInvocationは待機順からも除外する。
  LoopなしChainと満杯時Stop直線prefixの同期動作を維持する。
  公平性・待機中の統計・時刻折返し・送信失敗・Stop/Panic後の再入力を含む
  ホスト検証は通常/HID trace/機種別すべて成功。
  Engineは1580 bytes（ホスト）、Action/Chain/Configサイズは維持。

### Phase 3 最終確認

- 実ポインターのドラッグで、展開中Loopを子Actionごと移動できることを確認。
  挿入ガイドの高さを常時確保し、ドラッグ開始時の位置ずれを解消。
- 合成PointerEventによる650 msホバー展開とEscapeキャンセルを確認。
  キャンセル後の折りたたみ状態・保存配列は元に戻る。
- ブラウザー上のDOM操作で↑／↓のサブツリー移動、移動先ダイアログによる
  Loop内への移動、解除時の子Action保持、内部ごと削除、通常Action編集、
  16枠満杯時の追加・Loop変換禁止を確認。
- 8段ネストの390／700／1150 px幅iframeで横はみ出しなし。
  既存見出しのgrid／nowrap指定をツリー側のflex／wrapで上書き。
  390 px幅・7段ネスト内でOSC／MIDI／HID／OBS／Waitの編集欄を開いても
  横はみ出しなし。確認時のブラウザーconsole errorは0。
- C6自動5ケースは全成功。32同時OSC Loop中、HTTP p95 93.78 ms、
  Panic RTT 17.36 ms、probe OSC 200/200。各ケースの設定復元を確認。
- C6物理KEY2試験：1秒に1回・20回押下でOSC 20/20、Press／Release 40イベント。
  HTTP 1200/1200、skipped／入力overflow増加0、Panic停止と設定復元成功。
- 物理入力エッジから出力までの絶対遅延、BLE／HID／OBS混合負荷、
  All Notesの重負荷実測、Panic 10回反復は未実施。実機応答性の確認範囲は
  上記OSC負荷条件に限る。
- 正式UIは`renderFlatActions()`をLoopEditorから呼び出し、通常編集欄を再利用する。
  参照のなくなった旧フラットドラッグ関数を削除し、保存時のキャンセルをツリー側へ接続。
  モックのソース抽出は記録用モックサーバーのみで使用する。
- JavaScript 14ファイル成功。保存テストのLoopEditorキャンセル用stubを更新し、
  Stop保存／Wi-Fi passwordテストを再実行して成功。
  モックサーバーの共有素材抽出も成功。
- 最終UIを埋め込んだ`pio run`と全4機種の`tests/check_image.py`は成功。
  利用者承認後、この最終ビルドをCOM18のC6へ書込み、ハッシュ検証成功。
  再起動後にstatus応答とWebUIのHTTP 200、最終レイアウト・LoopEditor・
  保存時キャンセルの配信を確認した。Engineは実測版と同じ。

| 機種 | 静的RAM（bytes） | Application（bytes） |
|---|---:|---:|
| S3 | 139836 | 1692994 |
| C3 | 137660 | 1530947 |
| C6 | 181140 | 1930084 |
| C5 | 105304 | 1984277 |

最終`firmware.bin` SHA256：

```text
S3 bd4d5a4bc897d94993e796e0d6f123aec439ea1a19a79a3c5beefb81e2b7266e
C3 3965b2ea293308449f8aeeebcdff9e9a4a1f084f54ede61d28a8cad71e4d401a
C6 65f017093a51a6c2a619eb3fd9cd7dffc6c59beebf0fc05d616dbfbc5b3a8f0c
C5 aadd3cea78f6faa6613a7f173a9fc30eaf2d574f3a0a57c769d623580b7601c0
```

### WebUIレビュー後：モックとの表示統一

- Loop内の縦線をモックと同じ`1px solid var(--line)`に変更。
- 挿入ガイドはモックのラベル生成条件を採用し、絵文字・空白・全角記号を含め統一。
  内側末尾、Loop直後、Chain末尾、直前がLoopの場合の複合表示も対応。
- ガイドの通常／末尾境界の高さ、ドラッグ中の表示、無効な移動先の透過、
  選択中の枠・背景・文字色、700 px以下の表示もモックに合わせた。
- 同じ構成を正式UIとモックに配置し、ネスト・空Loop・連続Loop・全Action種別の
  ガイド文字列、縦線、選択中ガイドのcomputed styleが一致することをブラウザーで確認。
- 上記SHA256はこのレビュー修正より前の全機種検証・実機書込み版を示す。
- 修正版はLoopモデルテスト・diff検査・C6ビルド・イメージ検査に成功。
  利用者承認後COM18へ書込み、ハッシュ検証成功。再起動後のstatusとHTTP 200、
  グレーの縦線・新ガイドの配信、旧ガイド文字列の除去を確認。
  C6 RAM=181140、Application=1931388 bytes。
  SHA256：`da0f0c00e048954c27b1eed873befe9f95e14516f29d7a2d8451bcb5f17ed269`。
