# Phase 3B-2：Call先選択欄のローカル検証

基準は3B-1 `76b4543`。`pio run -e xiao_esp32s3` の通常ビルド成功後、生成された `WEB_UI` を `tests/test_call_editor_browser.py --embedded` でChromiumに読み込んだ。APIは合成応答、実機への書込みなし。

| 確認 | 結果 |
|---|---|
| 候補とID | KEY1 Press編集中、自身を除いた27候補のグループ、絶対ID、入力名、空表示を期待列と全件照合。キー／Encoder Push／回転の順で一致。 |
| 小画面・キーボード | 幅375 CSS pxでLoop内のCallを編集。矢印キーでEncoder CCW→CW→Push Releaseとグループをまたいで移動し、空の先を選択できた。選択欄は画面内に収まり、編集を閉じても要約は `Call → Encoder Push Release`。[画面](mobile-loop-target.png) |
| Key Import | [説明](key-import-hint.png)を表示してからKEY5へKey Presetをファイル取込み。貼付後のPress／Releaseの呼び出し先はそれぞれ元の絶対ID 25／26と一致。明示保存前の書込み0件。 |
| 3B-1回帰 | 既定先循環の[代替選択](choose-target.png)・キャンセル、Loop内での[循環拒否](rejected-target.png)と編集保持、全候補不可、非対応Firmware、正常編集とfixture保存を確認。実DOMシナリオ `edit / blocked / unsupported / mobile` は全件PASS。 |

ホスト検証：`node tests/test_call_model.cjs`、`node tests/test_call_preset_ui.cjs`、`node tests/test_loop_model.cjs`、`node tests/test_presets.cjs` はすべてPASS。初回はWindowsのシェル起動拒否でテストが起動せず、上記はビルド後に実行した結果。実端末のネイティブ候補一覧・タッチ操作・文字サイズによる見え方、および実機での保存・読戻しは未確認。
