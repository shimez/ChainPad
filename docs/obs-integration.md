# OBS WebSocket統合・最終確認

## 標準ビルド

S3/C5/C6でOBSを標準有効化。C3は従来どおりOBS非対応。
試験用環境は標準構成へ統合したため、以後は次のコマンドを使う。

```powershell
pio run -e xiao_esp32s3 -e xiao_esp32c5 -e xiao_esp32c6
# 接続機種だけ指定。COM5は実ポートへ変更。
pio run -e xiao_esp32c6 -t upload --upload-port COM5
```

出力: `.pio/build/<環境名>/firmware.bin`。
S3専用USB設定をC5/C6へ流用せず、WebSockets依存・パッチ・受信上限のみ共通化。
C6 traceも標準C6のOBS設定を継承する。
`platformio.obs-test.ini`は明示的な開発用コンソール／trace検証用、
`platformio.obs-settings-link.ini`は設定永続化のリンク評価用として維持する。
標準ファームウェアにTESTコンソールやリンクprobeは入らない。

## 機能・互換性

- シーン切り替え、配信開始／停止、録画開始／停止、入力ミュート設定／トグルの7操作。
- 接続設定はConfigと別保存。シーン一覧は手動取得のみ。
- 非同期Worker、単一結果consumer、固定2シーンバッファ、Assembly16400 B、stack8192 Bを維持。
- 一覧JSONのNoMemoryは取得失敗として処理し、接続と通常Actionを優先。
- 既存Action Chain容量を変更しない。キー／PushはPress+Release合計16 Actions、
  回転は各方向8 Actions。イベントごとの上限を16より増やさない。
- Config／Full／Key Preset形式、OSC・MIDI・BLE-HIDの既存経路を維持。

## 実機承認（ユーザー最終報告）

| 対象 | OBS動作 | メモリスモーク |
|---|---|---|
| S3 | PASS | PASS |
| C5 | PASS | PASS |
| C6 | PASS | PASS |

C6の混在OSC → OBS → BLE-HID、Delay順序、保存・再起動後のChain／OBS設定復元、
再起動後の混在実行も全てPASS。S3/C5で同じ統合回帰を再実施することは要求しない。
測定値は[ S3記録 ](obs-memory-test.md)、[ C5/C6試験記録 ](obs-c5-c6-experimental.md)を参照。

### 既知の留保事項

C5で`MSPI Timing: Failed to allocate dummy cacheline for PSRAM memory barrier!`
が過去に1回観測された（最終ユーザー報告）。原因未特定。書き込み後にUSB抜き差しあり。
追加テストでは再現せず、予期しない再起動も確認されなかった。
今回のマージの必須阻害条件とはしないが、再現した場合は評価を停止して調査する。
長時間・高負荷、繰り返し起動の評価は継続事項。全7操作の網羅や全機種の同時負荷を
診断ログのみから証明したとは扱わない。

## 公開範囲

今回はローカルmainへの統合まで。Push、タグ、GitHub Release、Web Installer更新は
別の指示で実施する。試験記録は履歴として保持し、実験専用ビルド構成は本番へ残さない。

## 最終自動回帰（0.6.0ソース）

- OBS protocol/auth/frame/Worker/Engine/console: native、C5、C6全てPASS。
- OBS HTTP/controller、settings/store: PASS。
- Serial output全18項目: PASS。
- Host通常／trace／C3/C5/C6: PASS。16-slot制限、Wait、OSC/MIDI/HID、保存互換性を含む。
- Storage fault S3/C5/C6: PASS。H1–H4、Wi-Fi秘密情報、既定設定、再起動復旧を含む。
- Node UI全9ファイル: PASS。OBS、Preset、HID、Wi-Fi、Maintenance、Rotation等。
- WebSocketsパッチのハッシュ／冪等性／不明編集拒否: PASS。

最終検査で古いテストfixtureの不足2件を修正（シリアル診断のmemory snapshot stub、
Wi-Fi UIのscene manager初期化stub）。該当テストの再実行はPASS。
本番処理を変更してテストを回避したものではない。

生成物検査もPASS（標準S3/C5/C6のOBSリンク、C3のWorker非搭載、
標準版からTESTコンソール排除、S3 TEST/traceの配置、シリアル出力所有権）。
Phase 1限定だった検査条件は統合後の仕様に更新。C3にも保存互換用のoperation名と
診断用タスク名が残るため、それらの文字列ではなくWorker／protocol／socketの
実行コードがリンクされないことを検査する。

| 最終標準ビルド | 結果 | 静的RAM (B) | Flash (B) |
|---|---|---:|---:|
| S3 | PASS | 137820 | 1612554 |
| C5 | PASS | 103288 | 1903319 |
| C6 | PASS | 179116 | 1849106 |
| C3（OBS無効） | PASS | 136364 | 1462209 |

S3 OBS TEST / OBS traceもPASS。最初のC5ビルドはVS Code側のPlatformIO初期化との
競合でPython依存更新に失敗。VS Code／monitor終了後、clickの要求範囲内への修復と
pip check PASSを経て再ビルド成功。これは起動時PSRAMエラーとは別のホスト環境問題。
