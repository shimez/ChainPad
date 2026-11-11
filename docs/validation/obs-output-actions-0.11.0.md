# v0.11.0 リリース準備

## 対象と差分

- 直前の正式版：公開済みタグv0.10.0（`d44b70603535f950f7a228d3a2f0a7af88ff72d7`）。リモートタグ一覧で確認。
- 新版：0.11.0。互換性を維持したOBS Actionの追加としてminorを更新。
- 仮想カメラのStart／Stop／Toggle、リプレイバッファのStart／Stop／Toggle／Saveを追加。
- 既存設定のoperation名・列挙値・保存形式は維持。移行不要。
- src/main.cppの版、CHANGELOGの新版項目、利用マニュアルのOBS操作説明を更新。
- CHANGELOGは新機能2項目に集約。開発中の修正・検証経緯や公開処理の内部変更は独立項目にしていない。
- v0.10.0以前のCHANGELOG本文はタグ版と一致することを確認。

## 検証

- 実装時のOBSプロトコル／Worker／制御テスト、全ホスト構成、JavaScript全12ファイルは成功。詳細は`obs-output-actions.md`。
- 利用者から、全7操作についてOBS実機での正常動作確認の報告を受領。
- リリース準備後のPython全73テスト、JavaScript全12ファイルは成功。
- 既存処理による履歴生成：6版、3,942 bytes。配布用JSONは手編集していない。
- バージョン変更後の`pio run`：S3／C3／C6／C5の全4機種で成功。
- 全4機種の`tests/check_image.py`：パーティション・統合イメージ配置・Flashヘッダー・機種識別・USB descriptor gatingの検査に成功。

### 最終firmware.bin SHA256

| 機種 | SHA256 |
|---|---|
| S3 | `58d5757518367c2e80b108bdfb4861f4df04799c78e75cfb260f02ca19788c28` |
| C3 | `92662900543cff2211a0e9de47d80c39fbd871a8161fded8dec32e5261334857` |
| C6 | `542fbc9af2efde06aa8307f3f5a580aeac55aea50ba7caa94b02d4b5783fc898` |
| C5 | `e0ce0fdef0484fc0acca8f8dab0b08ce133f9ec60b48017536c1ab7283d7dbb1` |

## 公開の段階

Phase 1はローカルの準備・検証・コミットまで。
Phase 2のmain Push、Phase 3の正式タグ作成・Pushは、それぞれ利用者の明示承認後に実施する。
新しいタグpushからmainへの公開引き継ぎは、Phase 3で実運用確認する。
