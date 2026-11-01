# OBS 音量調整（Action Chain）

KEY、Encoder PushのPress/Release、Rotation DirectionのCW/CCWで、
Action「OBS」→OBS操作「音量調整」を選びます。

- 対象の音声ソース：手入力、または「音声ソース一覧から選択」。既存の音声入力候補取得を使用します。
- 指定値に設定：0〜100%（小数可）。`SetInputVolume` へ `inputVolumeMul = volumePercent / 100` を送信します。
- 相対的に増減：−100〜＋100パーセントポイント（小数可）。`GetInputVolume` で現在値を取得し、加算結果を0〜100%に収めて `SetInputVolume` で設定します。

100%は線形振幅倍率1.0（0 dB）、50%は約−6.02 dB、0%は無音です。
相対+5は「50%→55%」であり、5 dB増加ではありません。ミュート状態は変更しません。
OBS側で100%を超えている場合も取得した実値を計算に使用し、設定結果を0〜100%に制限します。

## 保存形式

```json
{"protocol":"obs","delayMs":0,"operation":"setInputVolume","inputName":"マイク","volumeMode":"absolute","volumePercent":50}
```

相対調整は `volumeMode: "relative"` と符号付き `volumePercent` を指定します。
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
- Rotation ValueへのOBS出力追加はこの段階には含みません。

## メモリ・検証

- ActionのOSC文字列union、CommandのアイテムID領域、Workerの2段階処理スロットを再利用。Action=352 bytes、Command=216 bytesを静的検査。
- 一覧キャッシュ／受信バッファ／キューの容量変更なし。
- ホスト検証：保存復元、境界・不正値、絶対指定、相対計算と上下限、FIFO、Chain非同期継続、古い応答、失敗・タイムアウト・Panic。
- WebUI検証：全28イベント、両モード、Full/Key Preset、候補選択、手入力、非対応機種での保持。
- 実機からOBSへの音量操作は別途確認が必要です。
