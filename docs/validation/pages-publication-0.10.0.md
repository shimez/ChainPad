# v0.10.0 Pages公開経路の修正と復旧

## 観測

- main `d44b706`の実行42165269147は公開済みv0.9.0を保持してPagesを更新。
- タグ実行42165870770は4機種ビルドと配布検証に成功したが、Pages環境がmainのみを許可していたため拒否。
- 承認後にv0.10.0タグを許可し再実行。Pages／Releaseジョブは成功し、Releaseは2026-11-06 05:10:47 UTCに公開。
- Pages artifact内のbuild.jsonはv0.10.0／d44b706だが、公開URLはv0.9.0／0de0c3を返した。
- タグ上の手動実行42167056291も成功したが、公開URLは旧版のまま。
- キャッシュ回避用クエリ付きでも旧版。Last-Modifiedはmain公開時の05:00:35 UTCのままだった。
- actions/deploy-pages#383の同一SHA・異なるイベントで旧成果物が残る報告と整合するが、GitHub内部の原因は断定していない。

## 修正

- タグpushはmainのworkflow_dispatchへrelease_tagを引き継ぐ。デプロイの実行コンテキストをmainへ統一。
- 公開ツールと対象ソースを分離。対象タグのソース・版・コミット・CHANGELOGを検証。
- 公開済み版の復旧はReleaseの添付ファイルをそのまま使用。既存タグ・Release・binは変更しない。
- 公開URLの6配布ファイルとsite-build／Installer HTML・JSを期待成果物とバイト単位で照合。
- 公開URLが不一致・取得失敗なら新規Releaseへ進まない。古いタグのReleaseのみ公開は従来どおり。

## ローカル検証

- Python 73テスト成功（従来67＋新規6）。
- 旧版・混在・不正bin・異なるソースコミットの拒否、取得失敗の有限リトライ、公開済みバイト保持を確認。
- actionlint成功。

## 実環境検証

- 修正コミット：`cdec41b`。
- main実行42168666377：通常の公開済みFirmware保持、Pages更新、公開URL照合が成功。
- main上の手動実行42168792169（release_tag=v0.10.0）：4機種ビルド・検査、公開済みRelease添付ファイルの再利用、Pages更新、公開URL照合、既存Release保持が成功。
- ローカル環境からも公開URLを取得し、版0.10.0・コミット`d44b70603535f950f7a228d3a2f0a7af88ff72d7`を確認。
- build.json／manifest.json／changes.json／S3・C5・C6のbin全6ファイルについて、Release APIが返す添付ファイルのサイズ・SHA256と一致。
- v0.10.0がDraft・PrereleaseではないLatest Releaseであることを確認。
- 公開済みタグ・Releaseの添付ファイルは変更していない。履歴JSONは3,370 bytes。
- 新しいタグpushからmainへの自動引き継ぎは構文検証済み。実タグを追加する試験は行っておらず、今回の実環境検証はmain手動実行からの経路。
- GitHub内部の根本原因は未確定だが、mainコンテキストへの切り替えによる配信更新と実配信照合の成功を確認した。
