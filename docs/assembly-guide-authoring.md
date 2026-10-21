# 組み立てガイドの執筆・公開

- 本文：`site/assembly-guide/index.md`
- 画像：`site/assembly-guide/images/`
- ページの外枠：`scripts/templates/assembly-guide.html`
- 公開予定URL：<https://shimez.github.io/ChainPad/assembly-guide/>

「準備中です。」を本文に置き換えてください。章立ては自由です。
目次は `[TOC]`、画像は `![画像の説明](images/assembly-step.png)` のように記載できます。
画像は実ファイルを追加してください。表・コードブロック・脚注も利用できます。
組み立て後の初期設定には `[Getting Started](../getting-started/)` でリンクできます。

## ローカルプレビュー

リポジトリのルートで実行します。

```powershell
.\.venv\Scripts\python.exe scripts/build_site.py --preview
.\.venv\Scripts\python.exe -m http.server 8773 --directory _site
```

<http://127.0.0.1:8773/assembly-guide/> を開きます。
編集後は `--preview` を再実行してブラウザを更新してください。
生成物の `_site/assembly-guide/index.html` は直接編集しません。

## 公開

mainへPushすると、既存のGitHub ActionsがHTMLと画像を生成・配置し、Pagesへ公開します。
追加のGitHub設定は不要です。
