# Web Installer：公開最新版の変更履歴表示

実施日：2026-11-06。v0.10.0のmain Push前に追加。

## 実装

- `site/installer/index.html`に初期折りたたみの履歴欄を追加。
- `site/installer/metadata.js`へ表示データ処理を分離。
- 同じ`build.json`をビルド表示・ダウンロード一覧・履歴判定に利用。
- Configuratorと同じ`changes.json`から、buildの版と完全一致する1件を表示。
- 取得ごとの5秒タイムアウト、no-store、サイズ制限、schema・版・コミット・SHA256検証を実施。
- 旧形式ではJSONを取得せずReleasesへ案内。失敗時も既存のダウンロード情報を保持。
- HTMLとして解釈せず、履歴本文を`textContent`で描画。
- 既存のESP Web Toolsスクリプト、manifest属性、書き込みボタンのマークアップを維持。
- Configurator・Firmware側は変更なし。GitHub Actionsは新規テストの実行とトリガーパスだけ追加。
- CHANGELOGの0.10.0にInstaller対応を追記。履歴は5版・3,390 bytesになりました。

## 自動テスト

- `node --test tests/test_installer_metadata.cjs`：10テスト成功。
  最新版の完全一致選択、未来・過去版を選ばないこと、プレーンテキスト描画、空分類、旧形式、
  欠落・重複・不正な版・不正参照、受信サイズ・項目数・文字数、版・コミット・SHA256不一致、
  404・ネットワーク失敗・不正JSON／UTF-8、fetch／本文停止時のタイムアウト、遅延成功による上書き防止を確認。
- Python全67テスト：成功。InstallerのJSがPages成果物へコピーされることも確認。
- Configurator通知・履歴の既存10テスト：成功。
- `actionlint -shellcheck= .github/workflows/pages.yml`：成功。

## 通常Chromeの表示・操作確認

ローカルHTTPサーバーで実HTML／JSを配信し、現行CHANGELOGから製品と同じPython生成処理で作成したJSONを使用。
正常、旧形式、履歴404、コミット不一致、履歴8秒遅延、build 503の6条件を確認しました。

- 正常：公開版0.10.0だけを表示。初期は閉じ、見出し・日本語・2分類・個別Releaseリンクが正しく表示。
- 旧形式：Releases案内。ビルド表示・3機種のダウンロードリンクを維持。
- 404／不一致／タイムアウト：履歴失敗案内。ビルド表示・3機種のリンクを維持。
- build失敗：ビルド取得失敗と履歴を表示できない旨を案内。書き込みボタンは有効。
- 全6条件で既存ボタンを操作し、実ESP Web Toolsから`navigator.serial.requestPort()`が呼ばれることを確認。
- 履歴取得待機中も、同じポート選択呼び出しを確認。
- ポート選択APIのみをテスト用キャンセル応答に置換。実ポート接続・実書き込みは行っていません。
- Enterキーによる折りたたみ開閉を確認。
- Light／Dark両配色で320／390／760／1280 pxを確認。横はみ出しなし。
  390／1280 pxのスクリーンショットも目視確認しました。

## 公開処理との整合性

新しいJSON・生成手順・ユーザーの手動作業は追加していません。
既存のサイトコピーと、mainでの公開済みJSON保持、タグからのJSON生成を利用します。
初回main Push後も公開版が0.9.0の間は、Installerは履歴未対応として案内し、
0.10.0の未公開履歴を先行表示しません。

今回の追加は`site/`・テスト・Actions・CHANGELOG・文書だけです。
直前のv0.10.0全4機種ビルドからFirmware入力に変更がないため、追加のFirmware再ビルドは不要です。
実公開データを取得してmainのサイト更新処理を実行し、公開中の0.9.0（履歴参照なし）を保持することを確認しました。
未公開のchanges.jsonは生成せず、Installerのmetadata.jsがそのままコピーされ、配布検証を通過しました。

実装コミット`1d16154`で、検証済みv0.10.0の実binを使って最終包装も実施しました。
Firmware入力が前回ビルドから変わっていないことをGit差分で確認し、3機種のbinと
build／manifest／changesの6添付ファイルを検証しました。
`publish_release.py --validate-only --require-tag`は、タグ実行環境変数だけを模擬して成功。
版0.10.0・コミット・ハッシュ・実コミットのCHANGELOGとの一致、5版・3,390 bytesの履歴、
新しいInstaller JSの収録を確認しました。実タグや公開操作は行っていません。

## 残る確認

実GitHub Actions／Pages配信／Release公開は承認後です。
実書き込み・ポート接続そのものは今回の追加試験の対象外です。
公開更新をまたぐページでは表示版と、後からESP Web Toolsが取得するmanifestの対象が変わる可能性があります。
Pages→Releaseの時間差も従来どおりです。
Push・タグ作成・公開は承認待ちです。
