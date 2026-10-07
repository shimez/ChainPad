# Getting Startedの執筆・公開

## 編集するファイル

- **本文：`site/getting-started/index.md`**
- 画像：`site/getting-started/images/`
- ページの外枠・配色：`scripts/templates/getting-started.html`
- 公開予定URL：<https://shimez.github.io/ChainPad/getting-started/>

本文はMarkdownで編集します。先頭の `# Getting Started` をページ見出しとし、
「準備中です。」を自分の本文へ置き換えてください。本文の章立ては自由です。
`_site/getting-started/index.html` は自動生成物なので直接編集しません。

## 記法例

````markdown
## 見出し

説明文。**太字**や[Web Installer](../installer/)へのリンクを記述できます。

1. 手順を記載
2. 次の手順を記載

![画像の説明](images/setup.png)

```text
コードや設定例
```

| 項目 | 説明 |
| --- | --- |
| 例 | 内容 |
````

目次が必要なら本文に `[TOC]` を置くと、見出しから自動生成します。
GitHub固有のアラート記法・タスクリスト・Mermaidなどは自動変換しません。
画像は実ファイルを追加してください（上記の `setup.png` は記法例です）。
日本語ファイル名も利用できますが、画像名は半角英数字とハイフンにすると扱いやすくなります。

## ローカル確認

リポジトリのルートで実行します。

```powershell
.\.venv\Scripts\python.exe -m pip install -r scripts/requirements-pages.txt
.\.venv\Scripts\python.exe scripts/build_site.py --preview
.\.venv\Scripts\python.exe -m http.server 8773 --directory _site
```

<http://127.0.0.1:8773/getting-started/> を開いてください。
編集後は `--preview` のコマンドを再実行してブラウザを更新します。
このプレビュー生成はFirmwareをビルドせず、配布用binを作成・更新しません。
既存の配布データが `_site` にない場合、Installerによるインストールは確認できません。

## GitHub Pagesへの反映

既存のPagesワークフローにMarkdown変換を組み込んでいます。
本文・画像をコミットして `main` へpushすると、ActionsがHTMLを生成して公開します。
GitHubのPages設定は既存どおり **Source: GitHub Actions** を使います。

`site/**` と `scripts/**` は既にワークフローの起動対象です。
本文・画像・テンプレートだけの更新なら、公開済みFirmwareに変更がない限り
Firmwareを再ビルドせずにPagesだけ更新します。未公開のFirmware変更も一緒にpushする場合は
既存の比較規則に従ってFirmwareビルドも実行されます。

この準備作業自体ではpush・公開は行いません。
