# Rotation Value Phase E

## Configurator

- Encoder Mode、Range Steps、Initial Position、Stop / Wrapを編集。
- OSC Int / Float、MIDI CC Linked Outputsを追加・編集・削除。暫定容量16。
- 保存済みRuntimeのActive Mode / Range Steps / Current Positionを、編集中設定・Previewと分離。
- Previewはブラウザのみで計算し、API送信や本体Position変更を行わない。
- 整数はBigIntによる重み付き分子と絶対値の除算を使用し、ちょうど半分はゼロから遠ざける。
- Floatはfloat32へ正規化後、double相当で補間しfloat32へ戻す。両端は直接返す。
- Floatの負のゼロはJSONの`-0.0`として送信・Exportする。整数フィールドは整数トークンを維持する。
- OSC共通送信先＋Address、MIDI Transport重複＋Channel＋CCの重複を警告。重複を許容し、順序・最終値を保証しない。
- 非USB機種のBLE固定・Import変換通知を維持。

## 確認済み

- Firmware host suite、C3/C6/C5 capability suite。
- 最終S3 / C3 / C6 / C5ビルドと全4機種のimage検査（partition / placement / Flash header / board identity / USB gating / production test-hook不在）。
- Preset v2 / Key v1、既存Action Chain関連回帰。
- Preview整数全Position走査（N=1/2/20/65535、int32両極端、逆順、同値、負数中間値）。
- Firmwareから出力した80個のfloat32ビットパターンとの一致（最大値、subnormal、負のゼロ、逆順、同値、両端）。
- ローカルブラウザfixture: 編集・保存・再読込、負のゼロ保持、Both/USB重複、C6 BLE変換、無効Rangeの保存拒否、16件上限と削除、Mode変更時のChain保持。

## 実機検証

C6のConfigとLittleFS設定領域は作業用の外部ディレクトリへバックアップ済み。
C6 / S3のビルド・image検査成功。C6へ書込み後、実機が配信するConfiguratorから3 Outputs
（OSC Int / Float / BLE CC）を編集・保存・再読込し、Initial=5とFloat負のゼロの保持を確認。
Preview Positionを0/1/10/20へ変更しても書込みAPI呼出し0、本体Current=5のままであることを確認。
再起動後も3 Outputs、Float負のゼロ、Initial=5が復元された。

C6物理EncoderをCW10回・CCW10回操作し、Position 6〜15→14〜5の20更新を確認。
PCで受信したOSC Int20件・OSC Float20件・BLE CC20件をブラウザPreview関数と照合し、全件一致。
MIDI Channel16 / CC127も一致。generation=20、Current=5、pending=0、失敗・上書き・破棄・入力overflow=0。
終了後はバックアップから元のNetwork / Chains / Rotation設定を復元しAPI再読込で一致を確認。
FirmwareはPhase E検証版のまま。受信ログは外部作業ディレクトリの
`phase-d-phase-e-c6-ui-retry.json`（既存キャプチャツールによるファイル名）に保存。

### S3書込み前バックアップ

- ESP32-S3、Flash 8MB、MAC `34:85:18:9d:6d:24`。通常時COM30、ROM download時COM20。
- 元パーティションは`nvs / otadata / factory / config_nvs`。現在のLittleFS形式とは異なる。
- 外部作業ディレクトリへFlash全体8,388,608 bytesを保存:
  `s3-before-phase-e-full-flash-rom.bin`
- SHA256: `8c9318745b1baad8c44d504106f819a33ad680e59950c09d61fa732ce35cfda1`
- stub読出しは約0x43000で停止。手動BOOT後も同様。`--no-stub`のROM読出しで全領域取得成功（520.7秒）。
- S3初回image検査成功。`firmware.bin` SHA256:
  `ed0751019de1d4859b4a1c3d9c4d7a8d0d21d5c5969f1ea19b81ba2c0de7f9bd`
- ROM経由でfactory imageを書込み、Flashハッシュ検証成功。
- 初回起動時はLittleFS `ioError`。バックアップ内の新settings領域に旧データ13,725 bytes（先頭0x670000）が残っていた。
  全Flashバックアップ取得後、0x630000〜0x7fffffのみ初期化して再起動し、LittleFS `missing` / total=1,900,544 / used=8,192を確認。
- この時点では内部RAM free=14,040 / min=12,732 / largest=7,668 bytes、PSRAM free=8,381,000 bytes。
  この時点ではPCからSetup APへ接続できず、USB MIDI列挙とS3のBLE広告のみ確認できた。
- Serial診断追加後、AP起動成功（mode=3 / AP=192.168.4.1）を確認。
  ユーザーによるWi-Fi設定後、STA=192.168.0.24、storage=readyとなった。
  `/api/status`は本文受信中に20秒でtimeout。Ping4回は損失0、24〜127ms。
  Serialで内部RAM free=4,428 / min=392 / largest=1,524 bytesに対しPSRAM free=8,373,140 bytes。
  S3限定のNimBLE host external allocation設定を追加して再検証した。
- NimBLE host PSRAM化だけでは不足。初回statusは0.593秒だったが、連続リクエストは再度timeout。
  S3に限り固定容量Config（82,648 bytes）もPSRAMへ配置する変更を追加。
  SDKの`CONFIG_SPIRAM_BOOT_INIT`をビルド時に要求し、確保失敗時は起動を中止する。
  C3/C6/C5とhost testsは従来の静的Configを維持。
- Config PSRAM化後、内部RAM free=89,240〜91,316 / min=79,364 bytes。
  status連続10回が成功（初回2.828秒、以降0.093〜0.203秒）。
  S3実機UIでUSB CC20 / BLE CC21 / Both CC22を保存し、再読込・再起動後の復元を確認。
- CW10回・CCW10回の20更新でUSB40件・BLE40件を実受信。
  USB専用CC20、BLE専用CC21、Both CC22の両側受信を確認し、全80値がPreviewと一致。
  Current=5、generation=20、pending=0、送信失敗・上書き・破棄・入力overflow=0。
  検証後の内部RAM free=87,564 / min=63,152 bytes。
  ログ: 外部`phase-e-s3-capture.json`。

### S3 16 Outputs・通常Chain併用

- N=127 / Initial=64 / Wrap。USB CC・BLE CC・Both CC各1、OSC Int/Float計13。
- Key 1はBoth Note On → Wait20ms → Note Off、Encoder PushはOSC marker。
- 約80秒の記録中に物理Encoderで379 generations（20→399）、Current=67。
- USB CC725件＋通常Note10件、BLE CC728件＋通常Note10件を受信。
  通常Noteは各TransportでOn/Off交互5組。Push marker4件を受信。
- OSC Rotation受信4,486件。全受信値がマッピング可能な値であり、全16 Outputs・両Transportの最終値はPosition67に一致。
- Firmware OSC受付4,513件、OSC失敗235件（破棄）、pending上書き242件。
  `379 × 17 = 4513 + 725 + 728 + 235 + 242`で全送信枠の会計が一致。
  OSC受付後のPC未受信27件も観測。UDPの全件配送・各中間値の配送は保証しない。
- 終了時pending=0。入力overflow=0、Chain受付失敗=0、再起動なし。
  サンプル内最低freeHeap=82,024 bytes、Firmware累積minFreeHeap=62,976 bytes。
- ログ: 外部`phase-e-s3-mixed16.json`。
- 試験終了後はテスト設定を除去し、ユーザーが設定したWi-Fiを含む試験直前Configへ復元・APIで一致確認。
  S3はPhase E Firmwareを維持。旧Firmware・旧設定は全Flashバックアップに保持。

### 容量判断と検証範囲

16 Outputsは引き続き**検証用暫定容量**。C6のPhase D混在16試験と、今回のS3混在16試験で
固定pending領域と通常Chainの併存・最終状態への追従を確認したが、OSC失敗・UDP未受信も発生した。
この結果だけで16を全環境の保証上限や無損失上限とはしない。
最大224 Actions＋16 Outputsの保存／復元はhost suiteとS3実機で確認済み。
S3ではAddress192 bytes・String128 bytesのOSC Actionsを224件、Address192 bytesのOutputsを16件保存し、
API再読込および再起動後に全設定の一致を確認。保存後freeHeap=85,348 / 累積min=53,232 bytes。
終了後は試験前設定へ復元した。最大長224 Actionsを実行しながらの同時送信負荷は未測定。

最終S3実機検証image SHA256:
`06bb02081b0957d69cd11ee7ef623540437cc16a93e5e2e2c522072b649428fe`
