# GitHub Pagesの構成と更新

- `/`：ChainPadポータル（`site/index.html`）
- `/installer/`：Web Installer（`site/installer/index.html`）
- `/getting-started/`：Markdownから生成するガイド（本文：`site/getting-started/index.md`）。[執筆方法](getting-started-authoring.md)
- `/manifest.json`、`/build.json`、`/firmware/`：Firmware配布データ。既存URLを維持します。
- `/site-build.json`：PagesコンテンツのコミットID。`build.json`のコミットIDはFirmwareのものです。

## Pagesだけを変更する場合

`site/` を編集してmainへpushします。ワークフローは公開中の `build.json` にあるFirmwareコミットと、今回のコミットを比較します。Firmware入力が同じならPlatformIOのインストール・4機種ビルド・イメージ検査をスキップします。

公開中のmanifest・build情報・配布対象3機種（S3/C6/C5）の結合binを取得し、ファイル一覧・サイズ・SHA256・manifestの参照先／versionを検証してそのまま再配信します。FirmwareのコミットID・ハッシュを書き換えず、`site/` を再帰コピーして新しいページを公開します。

C3は動作上の問題により対応・配布を一時停止しています。CIの4機種ビルド・image検査は継続しますが、C3はmanifest・build情報・配布binに含めません。公開済みデータの対象機種が現在の配布対象と異なる場合（旧4機種からの切替を含む）は、自動的に再ビルドして3機種をパッケージします。

「直前のpush」ではなく「公開済みFirmware」と比較するため、先行するFirmwareビルドが失敗した後のPages更新でも未公開のFirmware変更を見落としません。ワークフロー全体の同時実行を制限し、配信データの競合を避けます。

## Firmwareを変更する場合

`src/`、`web/`、`include/`、`lib/`、`boards/`、`platformio.ini`、`partitions*.csv`、`scripts/build.py` が比較対象です。`web/` は本体へ埋め込むConfigurator／Wi-Fi設定画面なので、変更にはFirmwareビルドが必要です。

Pagesのfaviconは `site/favicon.svg`、本体のfaviconは `web/favicon.svg` と独立させています。Pagesの画像更新だけでFirmwareビルドが発生することはありません。今回の初回更新は埋め込み画像の参照先変更も含むため、Firmwareをビルドします。

## 手動実行・復旧

Actionsの **Build firmware and deploy Web Installer** → **Run workflow** で実行できます。初回公開や公開ファイルが破損／取得不能の場合は `force_firmware` をONにすると4機種をビルド・検査し、配布対象の3機種を公開し直します。

通常実行で公開データの取得・検証に失敗した場合は公開を停止します。ただし配布対象一覧の変更は明示的な再ビルド条件です。Firmwareを欠いたサイトは公開しません。

## ローカル確認

```powershell
.\.venv\Scripts\python.exe -m pip install -r scripts/requirements-pages.txt
.\.venv\Scripts\python.exe -m unittest discover -s tests -p test_build_site.py
.\.venv\Scripts\python.exe scripts/build_site.py
.\.venv\Scripts\python.exe -m http.server 8770 --directory _site
```

通常のパッケージ生成には `.pio/build/` のS3/C6/C5の結合binが必要です。ポータルとInstallerの相互リンク、favicon、Installerからのmanifest・ビルド情報・ダウンロード先を確認してください。manifest・配布ディレクトリにC3が含まれないことも確認します。
