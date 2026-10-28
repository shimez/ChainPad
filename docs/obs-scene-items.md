# OBSシーンアイテム表示制御（開発版）

## Actionと対象

```json
{"protocol":"obs","delayMs":0,"operation":"setSceneItemVisibility","containerType":"group","sceneName":"配信情報","sceneItemId":12,"visibility":"toggle"}
```

- `containerType`: `scene` / `group`。候補取得と編集復元に使用。
- `sceneName`: 直接の所属シーン／グループ名。UTF-8 1〜192 bytes、制御文字不可。
- `sceneItemId`: 0〜9007199254740991の整数。OBSの64-bit IDを、WebUIの安全な整数範囲内で保持する。
- `visibility`: `enable` / `disable` / `toggle`。
- 同名ソースの配置はIDで区別する。不存在はOBSの失敗応答として記録し、名前検索・自動代替は行わない。
- グループ全体は親シーン内のグループアイテムを指定する。子アイテムはグループ名＋子IDを指定する。
- Enableはアイテム自身の表示フラグだけを変更する。非表示の親を自動的に表示しない。
- IDはシーン構成・コレクションを跨ぐ永続識別子ではない。削除・再作成・所属変更後は再確認する。
- Config v2 / Full v2 / Key v1の外側の形式は維持する。既存7操作のJSONは変更しない。
  旧ファームウェアは新操作を拒否する。C3は設定の保存・読込・エクスポートのみ対応する。

## 実行

- Enable/Disable: `SetSceneItemEnabled`を1回送る。
- Toggle: Workerが`GetSceneItemEnabled`のbooleanを検証し、反転値を`SetSceneItemEnabled`で送る。
  通信request IDは段階ごとに異なり、結果は元の論理Action IDで1回だけ通知する。
- 2段階の途中に自機の後続OBS要求を入れない。OBS UI・他クライアントとの原子性は保証しない。
- 読取失敗・不正booleanでは書込しない。Panic・切断・設定世代変更では未送信の書込を破棄する。
  タイムアウト・応答喪失・再接続後に自動再実行しない。
- Chainは従来どおりキュー受付時点で進む。OBS応答待ちをEngineに追加しない。
- 候補キャッシュの状態・有無はActionの受付条件に使用しない。通常の接続／出力許可条件のみ使用する。

## WebUIと候補API

- 所属先種類、所属先名、ID、表示操作を編集する。所属先とアイテムに手動取得ボタンを設ける。
- 編集画面を開くだけでは取得せず、保存済みIDを維持する。同じ所属先を再選択してもIDを維持する。
  名前または種類が変わった場合だけIDを未選択（編集中はnull）へ戻し、保存前に有効なIDを要求する。
- アイテム候補は「ソース名 — ID: 数値」。同名を統合しない。自由入力は候補取得失敗時にも保存できる。
- スマホは2列。候補パネルは項目群の後の独立した全幅行。開閉で編集項目を移動しない。

| GET（キャッシュ参照） | POST（手動更新） | OBS要求 |
|---|---|---|
| `/api/obs/scenes` | `/api/obs/scenes/refresh` | `GetSceneList` |
| `/api/obs/groups` | `/api/obs/groups/refresh` | `GetGroupList` |
| `/api/obs/scene-items?sceneName=...` | `/api/obs/scene-items/refresh` | `GetSceneItemList` |
| `/api/obs/group-items?sceneName=...` | `/api/obs/group-items/refresh` | `GetGroupSceneItemList` |

アイテム更新POSTは`Content-Type: application/json`で`{"sceneName":"所属先名"}`を送る。
既存のRaw本文上限1024 Bを適用する。Arduino WebServerのRaw POSTではURL引数が解析されないため、
更新時はクエリ引数を使用しない。シーン／グループ名一覧の更新POSTは従来どおりbodyなし。
キャッシュ参照GETの所属先名はURLエンコードする。アイテム応答は`items:[{sourceName,sceneItemId}]`、
`sceneName`と`containerType`を含む。名前・種類・接続世代を照合し、別対象の遅延応答を採用しない。
所属先が異なる取得の同時受付はBusy。通常Actionによる中断・250msの補助応答待ち上限は既存方針を継承する。
これらのOBS APIはWebSocket 5.0.0から存在し、音声候補用の`inputKindCaps`には依存しない。

## メモリと受信上限

- `Action=352 B`、target ABIの`Config=82648 B`は維持。OSC string領域とアイテム設定を共用する。
  IDは8-byteのバイト配列に保存し、64-bitアラインメントによる全Actionの肥大化を避ける。
- `Command=216 B`（従来195 B）。要求FIFOは4件のまま、領域は864→960 B。
- `sceneBuffers=8344 B`（2面）、Assembly=16400 B、Worker stack=8192 Bは維持。
  アイテム一覧では既存4096 B領域の256 Bを32件のIDに共用し、名前領域を3840 Bとする。
  名前のみの一覧は従来どおり4096 B。どちらも最大32件。
- 新たな固定状態はToggleコマンド216 B、要求先／公開キャッシュ先の名前193 B×2等。
- 受信上限16384 B、通常応答上限2048 B、候補JSON予算32768 Bは拡張しない。
  アイテム一覧はIDとソース名だけをフィルター解析し、transform等をJSONツリーに保持しない。
- 件数・名前領域・ID形式・解析予算の超過は取得失敗とする。不完全な候補を公開しない。
  フィルターはネットワーク受信量を減らさない。16384 Bを超える応答は非対応であり、
  既存のWebSocket／フラグメント受信保護により接続が切れる場合がある。接続復旧後は手入力のActionを実行できる。
  フラグメント超過等でErrorになった場合は明示的に再接続する。
  この制約を超えて一覧を取得するには、上限増加または受信方式変更を別途検討する必要がある。

### ビルド結果

| Target | Static RAM (B) | 従来比 | Flash (B) |
|---|---:|---:|---:|
| S3 | 138532 | +712 | 1632398 |
| C5 | 104000 | +712 | 1923485 |
| C6 | 179836 | +720 | 1869318 |
| C3 | 136364 | 0 | 1472063 |

実機のヒープ最低値・最大連続空き・Worker stack high-water・OBS往復遅延は未測定。
既存の手動memory probeはアイテム候補要求にも適用可能。実際のOBS構成による一覧サイズも実機確認対象。

## 検証

アイテム更新POSTのJSON本文対応後も、4機種の再ビルド・生成物検査、HTTP回帰試験、
アイテム候補管理／Action編集のNode試験はPASS。Raw POSTでURL引数を取得できない条件を再現し、
JSON本文による受付とGETクエリによる結果取得を確認した。実機での不具合解消確認は未実施。

以下はホスト／実ブラウザでPASS。実OBSとの接続を使ったハードウェア試験ではない。

- プロトコル：ID境界、boolean型、同名／重複ID、グループ名、空一覧・件数制限、不要transformのフィルター。
- Worker：Enable/Disable、不存在、Toggle反転、異なる通信ID、遅延応答、最終結果1回、FIFO、
  読取エラー、各段階タイムアウトと再接続後の再実行なし、Panic、対象別キャッシュ、取得失敗後の手入力Action。
- HTTP：新ルート、method/body、所属先のUTF-8長・NUL、要求先と種別の振り分け。
- 保存：新3モードのencode/decode・LittleFS保存復元、IDの不正値・余分なフィールド拒否、非対応機種での保持。
- WebUI：Full/Key round-trip、既存IDの維持、所属先変更時の解除、同名選択、手入力fallback、遅延応答の除外。
- 実ブラウザ：320/390/700/900/1280 px。横はみ出しなし、パネル開閉で項目位置不変、パネルは下段、
  自動取得なし、保存済みIDの維持、同名のID別表示を確認。
- `tests/run_obs.ps1`のprotocol/auth・frame・Worker・console各試験PASS。
  Workerの追加試験は同スクリプトのコンパイル条件で再実行し、読取完了と書込の間のPanic／設定変更、
  書込失敗、Engineの非待機も確認。16384 Bのフィルター解析成功／16385 B拒否を確認。
- `tests/run_obs_control.ps1` PASS。`tests/run_host.ps1`の通常／trace／C3／C6 PASS。
  実行時間制限で中断したC5部分は同じスクリプトのC5コンパイル・実行部分を分離してPASS。
- Node UI／Preset全9ファイルPASS。4機種のビルドおよび`tests/check_image.py`による生成物検査PASS。
- `tests/measure_rotation_layout.ps1`と3機種のELFシンボルで上記サイズを確認。`git diff --check` PASS。
