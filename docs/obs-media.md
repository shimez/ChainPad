# OBS メディア制御

**ChainPad v0.7.0以降**の機能です。v0.6.0では利用できません。

`TriggerMediaInputAction` を使い、指定したメディア/VLCビデオソースを操作します。

```json
{"protocol":"obs","delayMs":0,"operation":"triggerMediaInputAction","inputName":"動画","mediaAction":"restart"}
```

`mediaAction` は `restart` / `stop` / `pause` / `play` / `next` / `previous`。
OBSの対応する `OBS_WEBSOCKET_MEDIA_INPUT_ACTION_*` に変換します。
PLAYは一時停止解除です。停止・終了後の再生にはRESTARTを使用します。
応答成功は実際の再生開始を保証しません。次・前の動作はソースの対応状況に依存します。

## 一覧取得

- `POST /api/obs/media-inputs/refresh`（本文なし）で明示的に取得開始。
- `GET /api/obs/media-inputs` で状態と `media-inputs` 配列を取得。
- OBS `GetInputList` の `unversionedInputKind`（なければ `inputKind`）が
  `ffmpeg_source` または `vlc_source` の候補だけを表示。
- 既存の共有キャッシュを利用。最大32候補、名前プール4096 bytes、対象名はUTF-8で192 bytesまで。
- 受信上限16 KiB、一覧JSON割当予算32 KiBは既存値を維持。
- 一覧取得に失敗しても、手入力したActionを実行可能。

Actionは既存WorkerのFIFOで実行し、ChainはOBS応答を待ちません。
状態の事前取得や再接続時の自動再実行は行いません。
Actionの追加情報は既存unionを利用します。

## 検証

- UIの6操作、候補のダブルクリック選択、Full/Key保存復元、入力検証。
- Protocolの6操作変換、候補抽出と上限、HTTPルート、Workerのキャッシュ分離、手入力実行、FIFO、失敗とタイムアウト時の再実行防止。
- ホストのLittleFS保存復元、S3/C3/C5/C6ビルド・イメージ検査：PASS。
- ブラウザで幅1200/700/360pxのフィールド配置を確認。
- 実機からOBSへのメディア制御は未確認。
