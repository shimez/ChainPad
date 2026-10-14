# 組み込みConfig（Default / Empty）

## 操作

Configuratorの「設定ファイル Import / Export」からDefault / Emptyを読み込めます。
確認後、KEY・Encoderの全ChainとEncoder Rotationを編集中の設定へ置き換えます。
Wi-Fi・OSC送信先は、フォーム上の未保存編集も含めて保持します。
本体への保存・適用は通常の「保存・適用」で行います。ファイルImportの既存動作は変更しません。

- Default：BLE-HID。KEY1〜10は1〜0、KEY11は`-`、KEY12はShift＋`-`。
  JIS配列ではKEY12は`=`（US配列では`_`）。押下はKeyDown、解放はKeyUp。
  Encoder PushはEnterの押下・解放、CW/CCWはRight/LeftのKeyDown→Up。
- Empty：28 ChainのActionsをすべて空にします。
- 両方ともRotationはAction Chain、rangeSteps=20、initialPosition=0、stop、Outputsなし。

## 起動時の優先順位と保護

1. Factory Resetの起動処理を先に実施。失敗時は従来どおりMaintenanceへ入り、Configをロードしません。
2. 保存済みactiveがあれば従来の全件検証・適用を実施します。Defaultの更新で既存Configを移行・上書きしません。
3. 正常なmountとH3の存在確認でactiveがAbsent、かつH1/H3保存ロックがない場合のみ、DefaultをRAMへ適用します。
4. 自動保存はしません。初回の通常SaveまたはWi-Fi Saveで、その時点のConfigが保存されます。

保存状態は初回適用後も`missing`です。保存済みファイルがないままファームウェアを更新すると、
次の起動では新ファームウェアに含まれるDefaultを使います。**一度保存されたDefaultやEmptyはユーザーConfigとして優先**します。
SSID・パスワードは空、OSC送信先は既存初期値`192.168.1.100:9000`のままです。
添付JSONのnetworkはWeb用・ファームウェア用の生成データに含めません。

Config破損・open/read失敗・statエラー・mount失敗ではDefaultを適用しません。
Defaultのdecode/scratch/JSON allocation失敗も、空の安全状態・出力停止・H3保存ロックへ移行します。
H1のrename後適用失敗処理で使う空のdefaultsは変更しません。

H3の検証済み全FFパーティションだけに対する初期formatを維持します。
正常にマウントしたFSでactiveが本当に消失している場合、完全再起動後には初回起動と区別できません。
新たな永続marker・独自の存在判定は導入していません。同一プロセスのH1/H3ロック後のAbsentでは適用しません。

Factory Resetの消去順序（LittleFS初期化、journal、NVS消去・全域検証、完了記録、再起動）は変更しません。
Wi-Fi/BLEを含むNVSリセットとOSC初期値も維持し、正常完了後の再起動で上記Absent経路を使用します。

## 正本と生成

正本は添付JSONと内容一致の以下のファイルです。

- `web/builtin/ChainPad-Configuration-Default.json`
- `web/builtin/ChainPad-Configuration-Empty.json`

`python scripts/builtin_config.py`で以下を生成します（生成物は直接編集しないこと）。

- `src/builtin_config_data.h`：DefaultのChain/Rotation別JSON。既存decoderで1レコードずつ読込み。
- `web/builtin-config-data.js`：Default / Empty。既存Presetsの検証処理を再利用。

PlatformIOビルドでも生成を実行し、Webデータを組み込みます。
`python scripts/builtin_config.py --check`で生成物の一致を検査します。
Config全体の二重保持・新しい保存形式・HTTP APIは追加していません。

## 検証

- `tests/run_host.ps1`：通常Host、Factory Reset flow/platform回帰。
- `tests/run_storage_fault.ps1`：S3/C5/C6 Standard条件、H1〜H4回帰、初回Default、Reset完了後Default、
  Chain/Rotation scratch・JSON allocation失敗時の保存保護。
- 上記runnerはDefaultのKEY1を変更した別実行ファイルもコンパイルし、保存済みDefault・ユーザーConfig・Emptyを
  byte単位で保持することと、Absent時だけ新Defaultが使われることを検査します。
  これはファームウェア更新のHostモデルで、実機OTA/書込み試験ではありません。
- `tests/test_builtin_config.cjs --firmware`：正本・Web生成物・起動/Reset後の実GETの全Chain/Rotation一致。
  読込み確認・取消、編集中のWi-Fi/OSC保持、HTTP送信なし、保存中/未読込時拒否、不正設定拒否。
- 既存Presets、Rotation Preview、Maintenance、H1/H4 WebUIテスト。

実機のBLEペアリング・全キーの押下/解放・Encoder操作、実flash更新とFactory Reset、
電断・実機低heapでの確認は別途必要です。

### 実施結果

上記Host・故障注入・WebUI検査はすべてPASS。
S3/C5/C6 Standardビルドと`tests/check_image.py`のimage/partition検査もPASS。

| Standard | RAM bytes | Flash bytes |
|---|---:|---:|
| S3 | 95,708 | 1,362,834 |
| C5 | 65,392 | 1,640,943 |
| C6 | 141,220 | 1,587,344 |

リンク後RAM使用量はH1完了時と同じです。初回Default読込みの一時heapは既存decoderと
Chain/Rotation scratchを順番に使用し、Config全体は複製しません。
実機でのheap最大使用量は未計測です。
