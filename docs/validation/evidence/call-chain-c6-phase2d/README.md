# Phase 2D：C6の通常ビルドと診断ビルド

ベース `8678540` + 2D作業ツリー。通常C6のアプリSHA256 `e48b3f2964d91322ff05d1c2916d51a2805d08c21220a1b4dd9de959cdaf9f90`。COM18。HTTPトリガー、Edge Configurator、Windows MIDI入力、F13キー状態、UDP受信を使用。

- 通常版が `callChainSupported=true` を返すこと、Call設定をEdgeで保存・再取得し再起動後も一致することを確認。親・子・孫のLoop/WaitでOSCが `A B C B C Z`。27段Callの末端Wait中は1 Invocation、復帰時は27→0の28個のOSCマーカーを順序通り受信。
- 子Wait中の32 Invocationを実測。直線Stop→Call後のOSCを受信、また別試行でPanic、どちらも32→0で取消件数+32。不正ID 28はstage、間接循環はcommitで各HTTP 400、GETしたactive設定は元のCall設定と一致。
- `pc-normal.json` と `pc-trace.json` は各試行開始からのPC時刻msと受信イベント。`trace.txt` は別クロック（C6、µs）。子末尾のペア送信で既存バックエンドの完了条件を満たした箇所 `pair_complete` と親のPC復元後 `call_return` を記録。MIDIは9208996→9209009、HIDは10524146→10524160 µs。PCでは対応するNote Off／F13 Upを各親のOSCより前に観測。無線到着時刻と内部時刻を直接比較していない。
- 通常版で長いOSC文字列を含む224枠をOBS Readyから2回保存し、Edge再保存・再取得、再起動後の設定一致、OBS実シーン8件の取得を確認。通常版の保存チェックポイントは2セッションで計147件、累積minFreeは最小42,732 B、loopTask最低余裕4,960 B。32枠Stop直後と再起動後OBSは別採取（`stop-panic.txt`、`224-obs.txt`）。
- `save-internal.txt` は仮予約なしの保存診断版で同じ224枠を2回保存。観測最小free 53,464 B、largest 32,756 B。2回目のapply-chain行はシリアルログ欠落のため当該点は未集計。保存・GETは成功。通常版の瞬間最小値を保証するものではない。
- 初回のOBS一覧更新はテスト側が正常なHTTP 202を200と誤判定。退避状態へ戻した上で、期待ステータスを修正して再試験。2セッションとも全flashと公開設定を事前退避し、終了後に元アプリとLittleFSのflash digest、再起動後の設定GETとcapabilitiesの一致を確認。バックアップはGit外に保存。
