# マニュアルの執筆・公開

- 本文：`site/manual/index.md`
- 画像：`site/manual/images/`
- ページの外枠：`scripts/templates/manual.html`
- 公開予定URL：<https://shimez.github.io/ChainPad/manual/>

`index.md`の「準備中です。」を本文に置き換えてください。章立ては自由です。
Getting Startedは初めて動かすための手順、マニュアルは各機能・設定を調べるための説明として利用できます。

## Markdownの例

```markdown
# マニュアル

[TOC]

## キーの設定

説明を記載します。

![画面の説明](images/key-settings.png)

| 項目 | 説明 |
| --- | --- |
| 種類 | 設定する機能を選択します。 |
```

画像は実ファイルを追加してください。`[TOC]`は見出しから目次を生成します。
表・コードブロックに対応します。GitHub固有のアラート記法やMermaidは自動変換しません。
Getting Startedへのリンクは`[Getting Started](../getting-started/)`と記載できます。

## ローカルプレビュー

リポジトリのルートで実行します。

```powershell
.\.venv\Scripts\python.exe -m pip install -r scripts/requirements-pages.txt
.\.venv\Scripts\python.exe scripts/build_site.py --preview
.\.venv\Scripts\python.exe -m http.server 8773 --directory _site
```

<http://127.0.0.1:8773/manual/> を開きます。
編集後は`--preview`を再実行してブラウザを更新してください。
生成物の`_site/manual/index.html`は直接編集しません。

## GitHub Actionsでの公開

本文・画像などをコミットしてmainへPushすると、既存のPagesワークフローがHTMLを生成・公開します。
`site/**`と`scripts/**`は既に起動対象で、追加のGitHub設定は不要です。
Markdown・画像の生成／コピーは通常の公開、Firmware再利用時のサイト更新、ローカルプレビューで共通です。
マニュアルだけの変更なら、公開済みFirmwareとの差分がない限りFirmwareを再ビルドしません。
未公開のFirmware変更がある場合は既存の規則で再ビルドします。

検証：`.\.venv\Scripts\python.exe -m unittest discover -s tests -p test_build_site.py`
