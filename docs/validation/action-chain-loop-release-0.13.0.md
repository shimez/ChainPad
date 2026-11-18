# Action Chain Loop：0.13.0リリース準備

## 段階と承認

- 利用者が次期版0.13.0と段階②の準備作業を承認。
- 作業ブランチ：`feature/action-chain-loop`。起点：`b1188d0`。
- 本記録は段階②の変更・検証。CHANGELOG本文は個別査読で承認済み。
  指摘に従い、Loop全体の移動と内部Actionを残すLoop解除の表現を明確にした。
- 準備コミットと段階③の最終検証は承認済み。検証結果報告後はレビューで停止する。
  mainへの統合、main push、タグ作成・push、Release公開は未承認・未実施。
- 既存Phaseコミットを維持し、mainへの統合はfast-forwardを基本とする。

## 準備内容

- `src/main.cpp`のFirmware表示版を0.12.0から0.13.0へ変更。
- `CHANGELOG.md`に、公開v0.12.0からの追加機能としてLoopを記載。
  開発途中の調整を個別の修正項目にはしない。
- README、Getting Started、公開マニュアル、Loop詳細説明を更新。
  main pushによるマニュアル先行更新を考慮し「v0.13.0以降」を明記。
- 回数・ネスト・枠消費、編集操作、OSC受付間隔、停止・MIDI解除の仕様、
  旧版ではLoop入りデータを読み込めないことを説明。
- `.github/workflows/pages.yml`に`tests/test_loop_model.cjs`の起動対象パスと実行stepを追加。
  既存のmain／タグ／手動起動条件は維持。featureへのpushで起動する構成ではない。
- S3／C5／C6配布、C3ビルド・検査のみという構成は維持。
- 配布JSON・Releaseノートは生成・手編集していない。

## 段階②の検証

| 検証 | 結果 |
|---|---|
| `tests/test_*.cjs` 全14ファイル | PASS |
| `test_changelog.py` | 19テストPASS |
| `test_history_distribution.py` | 8テストPASS |
| `test_build_site.py` | 10テストPASS |
| `test_release_plan.py` | 9テストPASS |
| `test_publish_release.py` | 14テストPASS |
| `test_package_tagged.py` | 3テストPASS |
| `test_verify_deployment.py` | 3テストPASS |
| 実際のCHANGELOGを0.13.0で解析・履歴生成（メモリ内、検査用SHA） | PASS |
| 公開マニュアルのMarkdown変換・Getting StartedからのLoopアンカー | PASS：`4-7-loopv0130` |
| ワークフローの対象パス・Loopテスト実行step | 静的確認PASS |

Pythonの公開テストはモックを用い、実際のRelease公開やPages更新は行わない。
ワークフローの確認対象パス：
`src/main.cpp`、`web/loop-model.js`、`web/loop-editor.js`、`web/presets.js`、
`tests/test_loop_model.cjs`、`scripts/build.py`、`site/manual/index.md`、
`site/getting-started/index.md`、`CHANGELOG.md`、`.github/workflows/pages.yml`。
README／docsのみの変更でActionsを起動しない既存方針は維持する。
実際のActions起動・成功確認はmain push承認後に行う。

## 既存エビデンスとの関係

- 基本実機試験1〜8：
  [手動試験記録・CSV](evidence/action-chain-loop-20261118/README.md)。
- CSV12件・Panic計測JSONは原本を維持。試験時の版表示は0.12.0で、
  実測したバイナリSHAは同記録に記載済み。0.13.0の実測結果に読み替えない。
- 今回はFirmwareの版文字列、文書、CIのみを変更し、Loopの実行処理・保存形式は変更していない。
- Config schemaVersion 2、storageVersion 2、Full Preset version 2、Key Preset version 1は維持。
- 過去の自動検証・実機検証の範囲は維持し、Panic反復・長時間・OBS混合・多数Note負荷などの
  未実施項目を完了扱いにしない。利用者判断により追加実機試験を一律には要求しない。

## 後続段階

1. CHANGELOG個別査読・必要な修正、準備コミットの承認。
2. 段階③で最終候補のホスト検証、4機種ビルド・イメージ検査、サイト生成確認を行い、
   版・候補SHA・成果物のハッシュを記録する。
3. 必要なC6書込み・短い確認は実施前に承認を得る。基本実機試験一式を無条件には再実行しない。
4. main統合、main push、正式タグ作成・pushは各段階で承認を得る。
5. main push後はv0.12.0の配布維持、タグ公開後はv0.13.0の実配信・Release整合性を確認する。
