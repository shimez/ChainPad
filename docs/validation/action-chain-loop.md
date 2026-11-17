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
- 通常Action編集を共有モジュールとして整理し、モックサーバーによるソース抽出は使用しない。

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
- コミット：レビュー・承認待ち。
