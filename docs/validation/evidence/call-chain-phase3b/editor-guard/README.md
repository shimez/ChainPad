# 3B-1：編集中のCallガード（ローカル検証）

対象：`d8772b1` 上の未コミット差分。Chromium実DOM・合成設定・インターセプトしたAPI応答で測定。実機の画面や保存結果ではない。

| 操作 | 観測結果 |
|---|---|
| WaitからCallへ。既定先が自分に戻る | 元のWaitのまま、理由・有効な候補・キャンセルを表示。選択欄にフォーカス。キャンセル後も設定全体・編集行・dirtyは一致。[画面](choose-target.png) |
| 未保存のWait編集がある状態で、Loop内Callの先を変更 | 2入力／3入力の循環を即時拒否。経路とAction番号を表示。元の `Encoder CCW`、要約、編集中の設定全体とdirtyは一致。[画面](rejected-target.png) |
| 拒否後に有効な空Chainを選択 | 要約が更新され、拒否理由は消える。並行して編集したWait値は350 msのまま。種別変更の場合も別の有効な先を明示的に選んで確定できた。 |
| 通常の種別変更・保存 | Call→Wait、追加したOSC→既定先へのCallが可能。明示的な保存前の書込み0件。保存時はfixtureが全28入力を受領し、変更した絶対IDと保存済み表示を確認。 |
| 27候補すべてが自分に戻る／非対応Firmware | 全候補不可では理由を表示し、元のWaitとclean状態を維持。能力フラグが偽なら種別リストにCallなし。いずれも書込み0件。 |

再現：Playwright PythonとChromiumを用意し、リポジトリ直下で `python tests/test_call_editor_browser.py`。既存のChromiumを使う場合は `--browser <実行ファイル>`、画像出力には `--screenshots <ディレクトリ>` を指定する。ホスト回帰は `node tests/test_call_model.cjs`、`test_call_preset_ui.cjs`、`test_loop_model.cjs`、`test_presets.cjs`、`test_obs_ui.cjs`（いずれも `tests/` 内）。全てPASS。

実WebUIでは組込み資産で同じ編集・拒否・継続操作ができること、小画面での経路表示とフォーカス、実機保存後の読戻しを確認する。3Aからの範囲外ID／間接循環のImport拒否時の実画面保護、OBS一覧照合は未検証のまま。
