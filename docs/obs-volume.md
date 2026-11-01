# OBS 音量調整（Action Chain）

KEY、Encoder PushのPress/Release、Rotation DirectionのCW/CCWで、
Action「OBS」→OBS操作「音量調整」を選びます。

- 対象の音声ソース：手入力、または「音声ソース一覧から選択」。既存の音声入力候補取得を使用します。
- 指定値に設定：−100〜＋26 dB（小数可）。`SetInputVolume` へ `inputVolumeDb = volumeDb` を送信します。
- 相対的に増減：−126〜＋126 dB（小数可）。`GetInputVolume` の `inputVolumeDb` に加算し、結果を−100〜＋26 dBに収めて設定します。モード切替時の既定値は＋1 dBです。

0 dBは元の音量、負数は減衰、正数は増幅です。相対＋1 dBなら−20→−19 dBです。
取得値が−100 dBの場合も＋1 dBで−99 dBにします。−60 dBへ飛ぶ例外はありません。
計算・送信ともdBを使用し、倍率0の判定や倍率への変換は行いません。ミュート状態は変更しません。

## 保存形式

```json
{"protocol":"obs","delayMs":0,"operation":"setInputVolume","inputName":"マイク","volumeMode":"absolute","volumeDb":-6}
```

相対調整は `volumeMode: "relative"` と符号付き `volumeDb` を指定します。
旧線形版の `volumePercent` を含む設定との互換・自動変換はありません。旧版はコミット `adce000` に保持しています。
対象名は既存OBS Actionと同じUTF-8で1〜192 bytes、制御文字不可。
数値はfloat32で保存します。モード、範囲、型、余分なフィールドを検証します。
Config、Full/Key Preset、LittleFS保存復元に対応し、OBS非対応機種でも設定は保持します。

## 実行・取得

- 既存のOBS Workerへゼロ待機で受付。Action ChainはOBS応答を待ちません。
- 相対調整の取得・設定は1つの論理Actionとして扱い、その間に別のOBS Actionや一覧取得を挟みません。
- 同じソースへの連続した相対ActionはFIFOで、それぞれ最新値を取得します。
- 現在値取得が失敗／不正な応答の場合、設定要求は送りません。
- タイムアウト・切断後の自動再実行はありません。Panic、設定・接続世代変更による中止は既存の2段階アイテムToggleと共通です。
- OBSや別クライアントによる同時変更とは原子的ではありません（OBS APIに相対音量設定はないため、取得→設定の間の外部変更と競合し得ます）。
- 候補取得は `/api/obs/inputs` と `/api/obs/inputs/refresh` を共用します。候補取得失敗は手入力Actionの実行を妨げません。

## Rotation Value / Action Link

Action Type「OBS 音量調整」を選択し、音声ソース名とStart / End（−100〜＋26 dB、小数可）を指定します。
既存の浮動小数補間処理を共用し、位置0でStart、Range StepsでEndになります。降順・同値にも対応します。
PreviewはdB値を表示するだけで送信しません。OBS音量からのPosition同期や音量取得は行いません。

Rotation設定の`outputs`配列に次のレコードを追加します。

```json
{"protocol":"obs","operation":"setInputVolume","inputName":"マイク","start":-60,"end":0}
```

- 既存OSC/MIDIの保存形式、Config/Full Preset v2、Key Preset v1は維持。OBS非対応機種でも設定を保持します。
- Start/Endはfloat32。ソース名はUTF-8で1〜192 bytes、制御文字不可。ソース一覧はAction Chainの選択UIを共用します。
- OSCアドレス領域をソース名に共用し、RotationOutput=208 bytesを維持します。
- 未送信値は既存のLatest-Stateスロットを利用し、後の回転で最新値に置き換えます。OBS接続世代管理のみ8 bytes追加します。
- OBSの通常FIFOと実行中要求が完了してから受付し、`SetInputVolume(inputVolumeDb)`を送信します。回転値をFIFOへ蓄積しません。
- 接続不可・接続世代変更・Panic・設定適用では未送信値を破棄します。起動・再接続時の自動送信はありません。
- 受付済みの要求はWorkerが所有し、送信済みの操作は取り消せません。通常のActionと同じ接続世代による無効化を適用します。
- OSC/MIDIとの混在、保存復元、上下限・降順・同値、通常FIFO優先、最新値更新、再接続時の破棄を自動テストしています。

## メモリ・検証

- ActionのOSC文字列union、CommandのアイテムID領域、Workerの2段階処理スロットを再利用。Action=352 bytes、Command=216 bytesを静的検査。
- 一覧キャッシュ／受信バッファ／キューの容量変更なし。
- ホスト検証：保存復元、境界・不正値、絶対指定、相対計算と上下限、FIFO、Chain非同期継続、古い応答、失敗・タイムアウト・Panic。
- WebUI検証：全28イベント、両モード、Full/Key Preset、候補選択、手入力、非対応機種での保持。
- 実機からOBSへの音量操作は別途確認が必要です。
