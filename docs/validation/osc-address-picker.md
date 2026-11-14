# OSC Address一覧選択

## 実装

- OSC ActionのAddress自由入力に「一覧から選択」を追加。モーダルで日本語名・Address・説明を検索し、カテゴリで絞り込む。
- VRChat DocsとVRChat Wiki（OSC / oldid=71908）を参照した87件の固定候補。出典・ライセンス・要約した旨を一覧に記載。
- 読み取り専用・複数引数の項目は候補に含めない。アバター独自Addressは従来どおり自由入力。
- 選択時はAddressとTypeを反映。同じ型ならValue維持、型が変わると0／false／空文字へ変更。値域や解放操作は説明を表示する。
- カタログ情報は設定JSONへ追加しない。既存OSC Actionとして保存する。
- 設定の変更は「保存・適用」で本体へ反映。一覧を開く・選ぶ操作ではOSC送信しない。
- Firmware埋込み処理にカタログスクリプトを追加。マニュアルを更新。

## 検証

- JavaScript全13テストファイル成功。
- 新規カタログテスト：87件のAddress重複なし、検索、型変更、既存フィールド保持、全候補のFull／Key Preset往復、埋込み登録を確認。
- Python全73テスト成功。
- 実際のWeb Configuratorをローカル配信し、サンプル設定をブラウザー内へ投入して検索・選択・型反映・未保存フラグ・自由入力・キャンセル・フォーカス復帰を確認。デバイスAPIは接続していない。
- カテゴリ絞込み・検索0件表示を確認。幅約375pxのiframe内でもページ・モーダルの横溢れがないことを確認。
- S3／C3／C6／C5：最終ソースでビルド成功、`tests/check_image.py`全4機種成功。初回ビルド中のフォーカス復帰調整を反映するため再ビルドし、タイムアウトした確認コマンドの残りC6／C5も個別指定で完了。
- その後、利用者がESP32-C6／0.11.1ベースの開発版、VRChat Build 1938のDesktop／VR双方で実機確認。保存・再起動・Full／Key Preset往復・最新説明文を含めて確認済み。各Addressの結果・未実施項目は[実機チェックシート](osc-address-picker-hardware-checklist.md)に記録。

## v0.12.0 バージョン更新

- Firmware版を0.12.0へ更新。CHANGELOGは承認済みの新機能1項目と補足を記載。
- JavaScript全13テストファイル、Python全73テストを再実行して成功。
- 0.12.0のS3／C3／C6／C5全4機種ビルド成功。C3は初回のesptool環境準備がタイムアウトしたため単独で再実行し成功。再実行時もcertifiパス取得のタイムアウト表示があったが、コンパイル・リンク・イメージ生成は完了した。
- `tests/check_image.py --env xiao_esp32s3 xiao_esp32c3 xiao_esp32c6 xiao_esp32c5`で0.12.0の全4機種イメージ検査成功。パーティション・イメージ配置・Flashサイズヘッダー・機種識別・USB descriptor gatingを確認。
