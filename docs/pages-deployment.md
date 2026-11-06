# GitHub PagesとタグによるRelease公開

## 公開サイトの構成

- `/`：ChainPadポータル（`site/index.html`）
- `/installer/`：Web Installer（`site/installer/index.html`）
- `/getting-started/`、`/manual/`、`/assembly-guide/`：Markdownから生成するガイド
- `/manifest.json`、`/build.json`、`/firmware/`：正式公開Firmware。既存URLを維持します。
- `/changes.json`：FirmwareコミットのCHANGELOGから自動生成する履歴（対応版から配信）。
- `/site-build.json`：サイトのコミットID。`build.json`のコミットIDはFirmwareのものです。

## mainへのプッシュ

ワークフロー **Validate main and publish tagged firmware** は、mainの対象パス変更で起動します。
ファームウェア入力（`src/`、`web/`、`include/`、`lib/`、`boards/`、`platformio.ini`、
`partitions*.csv`、`scripts/build.py`、`scripts/builtin_config.py`）をpush前のコミットと比較し、
変更があれば4機種をビルド・イメージ検査します。複数コミットのpushでも全体の差分を確認します。

**mainでビルドした開発版は、Web InstallerやReleaseへ配布しません。**
公開中のmanifest・build情報・S3/C5/C6用binを取得し、サイズ・SHA256・参照先を検証して維持します。
その上にmainのサイト本文をコピーしてPagesを更新します。マニュアルだけの変更ならFirmwareビルドを省略します。
公開データの取得・検証に失敗した場合は停止し、開発版への置き換えは行いません。

Web Installerの`installer/metadata.js`も公開済みbuild／changesを共用します。
公開版1件だけを折りたたみ表示し、履歴がない旧版や取得失敗時にはReleasesへ案内します。
履歴の取得・表示はESP Web Toolsの書き込み処理から独立しています。

CHANGELOGの変更もワークフローを起動しますが、mainでは形式検証のみです。
履歴対応版の`build.json.changes`がある場合、公開済み`changes.json`を取得し、
サイズ・SHA256・版・コミットを検証してそのまま維持します。mainのCHANGELOGから再生成しません。
参照情報のない旧版はJSON未対応として許容し、取得を行いません。参照があるのに欠落・不整合なら停止します。
`site/`の同名ファイルが配布メタデータやFirmwareを上書きすることも防ぎます。

`README.md`や`docs/`だけの変更ではこのワークフローは起動しません。
`web/`は本体に埋め込むConfiguratorなので、サイト本文の`site/`とは異なりFirmwareビルド対象です。
C3はビルド・検査のみ継続し、Web Installer／Releaseの配布対象には含めません。

## 正式公開：バージョンタグをプッシュ

1. `src/main.cpp`のバージョンを`major.minor.patch`形式で更新する。
2. `CHANGELOG.md`へ同じバージョンの見出し（例：`## 0.10.0`）と変更内容を追加する。
3. mainへコミット・pushし、検証結果を確認する。
4. 公開するコミットにタグを付けてpushする。

```powershell
git tag v0.10.0 <公開するコミットSHA>
git push origin v0.10.0
```

タグpushではパスによる絞り込みは適用されません。タグ名・ソース内バージョン・変更履歴が一致し、
コミットがmainの履歴に含まれることを検査してから、4機種をビルド・検査します。

- 新しいバージョン：Web Installerを更新し、デプロイ成功後にGitHub Releaseを公開します。
- 公開中より古いバージョン：Releaseだけを公開し、Web Installerとサイト本文は更新しません。
- 公開中と同じバージョン：同一コミットのみ再実行可能です。異なるコミットなら停止します。

ReleaseはタグのFirmwareコミットを対象とし、S3/C5/C6用の統合bin、`build.json`、`manifest.json`、`changes.json`を添付します。
バージョン・コミット・配布機種・サイズ・SHA256・manifest参照先を公開前に検査します。
新規タグ配布の履歴は必須です。Pages成果物アップロード前とRelease公開前に、
FirmwareコミットのCHANGELOGから再生成した内容との一致も検査します。
リリースノートはそのコミットの`CHANGELOG.md`から生成します。過去の安定版より新しい版だけをLatestにします。

履歴生成はActionsが自動処理します。Astraによるバージョン更新・CHANGELOG更新・Commit／Push、
ユーザーの公開承認後のタグ作成／Pushという運用は変わりません。JSONの手編集・手動生成は不要です。
CHANGELOGの記法と参照情報の詳細は[変更履歴データ仕様](update-history.md)を参照してください。

公開済みの同名Releaseは変更しません。修正版を配布する場合は、新しいバージョンとタグを作成してください。
Configuratorの更新通知はWeb Installerの`build.json`を参照するため、正式公開版に連動します。

## 再実行・復旧

- 新規ReleaseはDraftで作成し、全添付ファイルのアップロード後に公開します。
- 途中失敗時は失敗ジョブを再実行するとDraftから再開します。既存タグ／Draftが別コミットを指す場合は停止します。
- Release用成果物は7日間保持します。期限後は元のタグを選択してワークフロー全体を手動実行してください。
- mainを選択した手動実行は検証とサイト本文更新のみです。`force_firmware`で検証ビルドを強制しても正式公開はしません。
- このタグ対応ワークフローを含まない過去コミットのタグは、自動公開の対象にならない場合があります。過去版の公開は個別対応してください。
- Release公開が失敗しても、成功済みのPagesデプロイは巻き戻しません。

mainとタグの実行は共通の同時実行グループで直列化し、旧版タグによるダウングレードも防ぎます。
認証は標準の`GITHUB_TOKEN`を使用し、`contents: write`はReleaseジョブだけに付与します。
追加のSecretは不要です。

## ローカル確認

```powershell
.\.venv\Scripts\python.exe -m pip install -r scripts/requirements-pages.txt
.\.venv\Scripts\python.exe -m unittest discover -s tests -p test_build_site.py
.\.venv\Scripts\python.exe -m unittest discover -s tests -p test_release_plan.py
.\.venv\Scripts\python.exe -m unittest discover -s tests -p test_publish_release.py
.\.venv\Scripts\python.exe -m unittest discover -s tests -p test_changelog.py
.\.venv\Scripts\python.exe -m unittest discover -s tests -p test_history_distribution.py
```

サイトのローカル生成は`python scripts/build_site.py`、プレビューは
`python -m http.server 8770 --directory _site`で実行できます。
通常生成には`.pio/build/`のS3/C5/C6の結合binが必要です。
