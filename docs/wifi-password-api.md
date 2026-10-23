# Wi-FiパスワードのAPI非開示

## API契約

- `GET /api/config`: networkは`ssid`, `oscHost`, `oscPort`, `passwordConfigured`。
- `GET /api/wifi`: `ssid`, `passwordConfigured`。
- 両GETとも`password`プロパティを返さない。伏字文字列も返さない。
- `GET /api/capabilities`: `wifiPasswordUpdate: "omit-preserves"`で更新契約を識別できる。
- `passwordConfigured`は現在の有効設定のパスワードが非空かを示す。接続成功や起動時の資格情報を示すものではない。

`POST /api/config/begin`のnetwork、および`PUT /api/wifi`:

| password | 動作 |
|---|---|
| 省略 | 現在の有効設定の値を保持 |
| 非空文字列 | 新しい値へ変更（従来どおり最大64バイト） |
| 空文字列 | 明示クリア |
| null／文字列以外／上限超過／埋め込みNUL | 拒否 |

SSID等、password以外の必須項目は従来どおり。
`passwordConfigured`は読取専用の状態表示で、更新の指示としては使用しない。
保持値は保存開始時に解決し、pendingレコードに固定する。commit時に現在値を取り直さない。
別の保存開始やWi-Fi保存は以前のトークンを無効化する。
起動時の資格情報ではなく、保存済み・未Restartの変更も含む現在値を使用する。

## UI

通常ConfiguratorとWi-Fiセットアップで同じ編集処理を使用する。
既存パスワードは表示せず、設定済み状態と「未入力なら保持」を表示。
明示クリアのチェックボックスは空文字列を送る。通常の空欄はpasswordを送らない。
保存成功後は入力欄・送信スナップショットから新規パスワードを消去する。
失敗時は入力欄とクリア指定を保持して再試行できる。
全体Preset Importでも未保存のパスワード編集状態を保持する。

## 互換性

- LittleFSのstorageVersion 2とPreset形式は変更しない。
- 保存用encodeNetwork/decodeNetworkは引き続きpasswordを必須として扱う。保存ファイルの欠落を保持と解釈しない。
- 旧クライアントの明示的なpassword文字列は引き続き利用可能。
- GETのpasswordに依存するクライアントは更新が必要。欠落値を空文字列へ置換して送るとクリアになる。
- ファームウェア更新後は古いConfiguratorタブを再読み込みする。
- `/api/config`のJSONバックアップにパスワードは含まれない。Factory Reset後・別端末への復元時は別途入力する。
- Presetは従来どおりSSID・パスワードを含まない。LittleFS/全Flashバックアップは保存資格情報を含む。
- 未対応保存形式からの明示置換では、有効な保持元がないためpasswordの明示入力が必要。UIでは新規入力または明示クリアを選ぶ。
- Factory Resetの処理と、保存後のRestart要否は従来どおり。

## 検証

- `tests/run_host.ps1`: PASS（通常/trace、C3/C5/C6、Factory Reset・Wi-Fiフローを含む）。
- `tests/test_*.cjs`: PASS。新規`test_wifi_password.cjs`は本番ページ処理で保持/変更/クリア、失敗再試行、成功後消去、Import編集状態保持を検証。
- `tests/run_storage_fault.ps1`: PASS（S3/C5/C6、各932件のH4障害注入とH1/H2/H3、実ルートのGET非開示・両更新API・保存開始時固定・トークン無効化・途中保存破棄・再読込）。
- 通常S3/C3/C5/C6ビルドと`tests/check_image.py`: PASS。
- `src/web_assets.h`は通常のビルドスクリプトで再生成（Git管理対象外）。

保存後の再読込と起動時Wi-Fiフローはhostで検証。
UI調整後、ユーザーから今回のAPI非開示対応の実機動作確認成功の報告を受領。
個別試験のログは未提示のため、物理的な電源断・実機Factory Reset等の各項目を
すべて実機検証済みとするものではない。
