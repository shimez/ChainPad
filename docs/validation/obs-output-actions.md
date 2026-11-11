# OBS Virtual Camera／Replay Buffer Action検証

実施日：2026-11-11。正式公開前の追加機能。Firmwareバージョンは0.10.0のまま。

## 変更

- OBS ActionへStartVirtualCam／StopVirtualCam／ToggleVirtualCamとStartReplayBuffer／StopReplayBuffer／ToggleReplayBuffer／SaveReplayBufferを追加。
- Configuratorは「仮想カメラ」「リプレイバッファ」の2グループと、それぞれの操作選択で構成。
- 保存データは既存のprotocol／delayMs／operationを使用。既存operation名・列挙値・保存形式は維持し、新しい列挙値は末尾へ追加。
- 内部照会用operationはActionの入力として引き続き拒否。
- OBSの既存送信・応答・Worker・Action Chainを使用。Toggleは対応リクエストを直接送信。
- Save失敗時は既存の失敗結果を使用し、リプレイバッファの自動開始や再試行は行わない。

## 自動検証

- JavaScript全12テストファイル：成功。
- UI：2グループ・7操作、全28イベント、要約、再編集、Full／Key入出力、非対応機種での保持、余分なパラメータの拒否を確認。
- `tests/run_obs.ps1`：成功。7操作のリクエスト・成功／失敗応答、既存WorkerとChainからの送信、直接Toggle、Save失敗時の自動開始／再試行なしを確認。
- `tests/run_obs_control.ps1`：成功。既存OBS設定・API・保存と適用の回帰を確認。
- ホストの保存／再読込テストには、既存操作と新しい7操作の混在、LittleFS再読込、パラメータ・内部照会操作の拒否を追加。

- `tests/run_host.ps1`：通常構成・HID trace構成・C3／C6／C5構成すべて成功。新旧OBS操作を含む保存・再読込と既存機能の回帰を確認。

- `pio run`：S3／C3／C6／C5の全4機種で成功。
- `tests/check_image.py --env xiao_esp32s3 xiao_esp32c3 xiao_esp32c6 xiao_esp32c5`：全機種成功。パーティション・統合イメージ配置・Flashヘッダー・機種識別・USB descriptor gatingを確認。

### firmware.bin SHA256

| 機種 | SHA256 |
|---|---|
| S3 | `ce0a47de0bd0568176d8b7fd715aa741690dc7f6471548d4f3d7b831b83fb591` |
| C3 | `b9fc7bf5c702dda0b2cf788ea22bef471d801a6311b830a5e5abcdf98ee2148a` |
| C6 | `d9037736866324ecf343fa45341bb7740d4435c78b6d81c16153cbfeace3a74c` |
| C5 | `464c5e2e3f3dc962aa99281d38c8eea9ec6807ad7f4c0510bfbb20d0ba2cdb01` |

## 実機確認

初回実装時点ではOBS実機確認・Firmware書き込み・バージョンバンプ・CHANGELOG更新・Push・タグ公開は未実施。

その後、利用者から仮想カメラのStart／Stop／ToggleとリプレイバッファのStart／Stop／Toggle／Saveの全7操作について、実機で正常動作を確認したとの報告を受領した。
この実機確認は利用者によるものであり、上記ハッシュのビルドとの同一性やOBSのバージョンは記録していない。
続くv0.11.0のリリース準備・最終検証は`obs-output-actions-0.11.0.md`に記録する。
