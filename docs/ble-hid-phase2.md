# BLE HID Phase 2 — 基本計測

対象：XIAO ESP32C6 / Windows / BLE HID。NimBLE-Arduino **2.5.1**。
ブランチ：`diagnostic/ble-hid-phase2`（開始点 `4d0c737`）。

通常のConsumer FIFO、`setValue(report)` → 引数なし`notify()`、HID descriptor/Report ID、
設定JSON、Presetは変更しない。ライブラリ内部フックは**未適用**。
計測は配送保証の改善ではなく、現状の観測である。

## ビルドと書き込み

```powershell
pio run -e xiao_esp32c6_hid_trace -j 2
pio device list
# COM番号は接続したC6のものに置き換える。
pio run -e xiao_esp32c6_hid_trace -t upload --upload-port COM7
pio device monitor -e xiao_esp32c6_hid_trace --port COM7 --baud 115200
```

この作業では実機への書き込みは実施していない。`erase`やFactory Resetは計測に不要。
計測用firmwareは`.pio/build/xiao_esp32c6_hid_trace/firmware.bin`。
同envはC6の4MB用partitionを継承する。通常版へ戻す場合は`xiao_esp32c6`でbuild/uploadする。

通常envでは`CHAINPAD_HID_TRACE=0`（デフォルト）で、記録用バッファ・コールバック計測・
計測コマンドは入らない。計測envだけ`CHAINPAD_HID_TRACE=1`。

## 実機での操作

Windowsを1台だけ接続し、BLEの接続・購読と初期解除が落ち着いてから開始する。
計測中に設定保存やWeb操作を行う試験は、通常の入力試験と別の区間にする。

| Serial入力（半角1文字） | 動作 |
|---|---|
| `1` | カウンターのみで新しい計測を開始。前回結果を消す |
| `2` | カウンター＋詳細リングで新しい計測を開始。前回結果を消す |
| `0` | 記録停止・結果固定。HID処理は継続 |
| `p` | 停止済み結果をダンプ（1 loopに1行） |

コマンドは受信直後に処理され、開始／停止の応答文字は出さない。改行は無視される。
端末によっては文字入力後にEnterが必要。計測中の`p`は無視され、ダンプしない。
`1`/`2`は進行中のダンプを中止し新しい計測を始める。

推奨手順：

1. `1` → Phase 1と同じ操作を行う。
2. 操作を止めて1〜2秒待つ（遅延通知を取り込むため。配送完了を保証する待ち時間ではない）。
3. `0` → `p` → `TRACE END`まで保存する。
4. `2`で短い同一操作を繰り返し、同様に停止・ダンプする。
5. 通常版／カウンターのみ／詳細記録の動作を比較する。

計測中は手動診断・周期診断・保存時の`printDiagnostics()`出力を抑制する。
記録フックはSerial出力、動的確保、ファイル/NVS書込みをしない。
停止は入力やConsumer FIFOを消さず、panicも発行しない。
ダンプ中の操作は計測外であり、Serial負荷もかかるため避ける。

## RAMと同期

- 256件の固定長リング（現ターゲットで1件24 bytes、6,144 bytes）。満杯なら最古を上書き。
- 累積カウンター、Usage別要求数、入力別件数、FIFO深さ、計測専用ID sidecarを別途保持。
- `lost`はリングから上書きされた**計測イベント数**。カウンターはリングが満杯でも継続。
- `fifo_overflow`はConsumer FIFO満杯によるAction拒否数。`lost`と別物。
- loop・入力scanner・BLE callback間は短いcritical sectionで記録を保護。
- 時刻は`esp_timer_get_time()`の起動後マイクロ秒。Serial出力と通知API呼出し中はロックしない。
- 計測自体のCPU/RAM負荷・同期・コード配置による影響はゼロではない。

## 出力の読み方

先頭の`TRACE`に開始／停止時刻、残った詳細件数、`lost`、通知戻り値とcallback結果の総数を出す。
`COUNT`はイベント別総数、`INPUT`はinput番号別の認識数。
`FIFO`はtransport別の開始残量・停止時残量・最大使用量。
`USAGE`はUsage別の`notify()`要求数（0には初期／安全解除も含む）。

詳細CSV：

```text
us,event,transport,usage,report_id,epoch,value
```

`transport`: 0=USB、1=BLE、255=入力などtransport非依存。
`epoch`: Consumer接続／購読世代。callback行はcallback時に観測した世代であり、
遅れて来た結果の発生元世代を証明するものではない。0は不明／対象外にも使用。

| event | usage | report_id | value |
|---|---|---|---|
| input / input_queued / input_dropped / input_consumed | input番号 | 0 | 0 |
| dispatch | Consumer Usage | **input番号** | Chain内Action番号（0始まり） |
| action_accepted | Consumer Usage | 0 | 0 |
| action_rejected | Consumer Usage | 0 | 1=不正Usage、2=未接続/未購読、3=FIFO不足 |
| chain_rejected | input番号 | 0 | 0（実行slot不足。Consumer以外のChainも含む） |
| push / pop | Consumer Usage | 計測用レポートID | 操作後のFIFO深さ |
| discard | 破棄したUsage | 計測用レポートID | 1（破棄レポート数） |
| fifo_overflow | 要求Usage | 0 | FIFO深さ |
| reset | 消去前FIFO深さ | 0 | 1=接続/世代変更、2=Release All、3=panic |
| neutral | 0 | 0 | 0（FIFO外の安全解除の送信試行） |
| notify_call | 要求Usage | 計測用レポートID、または安全解除の0 | 0 |
| notify_return | 要求Usage | 同上 | 1=true、0=false |
| status | 0（Usage不明） | **接続ハンドル** | NimBLE status（0=成功、他=エラー） |
| subscribe | 0 | 0 | CCCD購読値 |
| disconnect | 0 | 0 | 0 |

`report_id`列はHID Report IDではない。`dispatch`と`status`では表に示した別の識別子を格納する。
通常のレポートIDはloop内で単調増加し、計測の開始でもリセットしない。
Consumerペアの押下IDと解除IDは連番。ワイヤデータには一切追加しない。
同IDのnotify試行が複数あれば再試行として読めるが、`status`とIDの対応付けは禁止。

入力番号0〜23はKEY1〜12のPress/Release（偶数Press、奇数Release）、24/25はEncoder Push、
26/27はCW/CCW。`input`は**デバウンス後の認識イベント**であり、電気的な生の接点数ではない。
`input_consumed`はloopがキューから取り出した数で、出力禁止などによりActionが実行されない場合も含む。
Action受理・拒否の計測はConsumerが対象。Chain slot拒否は別途集計し、Action拒否へ重複計上しない。

## 件数・時間の判定

同一計測区間、両transport合計で：

```text
FIFO開始残量 + COUNT push
  = COUNT pop + discarded_reports + FIFO停止残量
```

計測境界が処理の途中に入った場合は開始/終了をまたぐActionを考慮する。
受理1件につき2回pushする。`pop`は送信関数がtrueを返して取り出したことを意味し、受信成功ではない。
初期解除やpanic後の解除はFIFO外なので、`notify_call == pop`とは限らない。

`PAIR`は、対応する非ゼロUsageと次の解除IDが**両方とも計測区間に現れた**ペアについて、
notify要求時刻間の平均・最大を表示する（カウンターのみモードでも計測）。
`notify_call`時刻は`setValue()`直前。`setValue()`と`notify()`の間にフックは挿入しない。
したがってAPI開始時刻の近似であり、Windows上の押下時間／無線配送時間ではない。
discard/reset/neutral/購読変更で不完全ペアを終了し、勝手に組み合わせない。
リングの`lost > 0`なら残存CSVだけから全件一致を主張できない。短い区間で再測定する。

## notifyとcallbackの制約

引数なし`notify()`はCharacteristic更新を予約する。trueは個別レポートの配送成功ではない。
接続情報付き`onStatus()`だけをoverrideする（旧形式も同時に数えると二重計数になる）。
status=0はNimBLEのATT通知送信処理が成功を返したことを示し、Windowsの受信／処理確認ではない。
callbackにはUsageも計測IDもないため、**notify回数とcallback回数の1対1対応は保証しない**。
callback内の`getValue()`を送信済みUsageとして記録しない。
Keyboardのwire内容とcallbackは今回記録しない。Consumerと共存する負荷として従来どおり動作する。

基本計測で分かるのは入力→Action→FIFO→notify要求列と通知結果件数まで。
NimBLEが実際に取り込んだUsage列は内部フック、Windows受信列はWindows側取得が必要。
callback件数一致のみで上書き／欠落なしと断定しない。

## 内部フック案（基本計測の実機確認後。未適用）

対象：NimBLE-Arduino 2.5.1の
`src/nimble/nimble/host/src/ble_gattc.c`内`ble_gatts_notify_custom()`。

1. `ble_att_svr_read_handle()`成功後、`ble_att_clt_tx_notify()`直前で、通知用`txom`の
   **コピー済み2バイト**を取得する。Characteristicの現在値を読み直さない。
2. 通知試行ID、conn_handle、chr_val_handle、Usage、時刻を固定長RAMへ記録。
3. `ble_att_clt_tx_notify()`の結果を同じ試行IDで記録。関数がバッファを消費するため、送信後にtxomを読まない。
4. 読出し／割当て前の失敗もpayloadなしの試行結果として記録する。
5. Consumerのvalue handleだけを対象にし、他のCharacteristicや送信条件を変更しない。
6. callbackは従来どおり残す。API/結果/フックの列を分けて比較する。

再現可能な管理方法：

- 将来の専用env `xiao_esp32c6_hid_trace_hook`を追加し、2.5.1を固定。
- env固有の`.pio/libdeps/xiao_esp32c6_hid_trace_hook/NimBLE-Arduino`だけを対象とする。
- `tests/hid_trace/nimble-2.5.1-notify-observe.patch`と適用スクリプトをリポジトリ管理する。
- `pio pkg install -e xiao_esp32c6_hid_trace_hook`後、build前に適用する。
- スクリプトはlibrary.propertiesのversion、元ファイルのSHA256、適用後SHA256を照合。
  適用済みなら何もしない。未知の内容なら失敗し、推測でpatchしない。
- C側フックは専用macroでguardし、C linkageの計測関数へ渡すだけ。
- build前の検査で未適用／想定外のソースを拒否し、結果ログにpatch識別子を記録する。
- 通常env、今回の基本計測env、他のlibrary copyは変更しない。

この段階ではパッチ／適用スクリプト／hook envを追加・適用していない。
基本計測の実機結果を見てから、上記の観測フックを別変更として実装する。

## ローカル検証

`tests/run_host.ps1`は既存回帰に加え、計測有効の同じConsumer送信順序テストと、
production recorderのモード切替・停止snapshot・FIFO境界・時間集計・ring上書き・
複数producerテストを実行する。Hostのcallback/時計はアダプターであり、BLE配送の検証ではない。

実施結果：

- 既存Host／Factory Reset／C系列の能力検査：PASS。
- 計測有効のConsumer送信順序回帰、recorder単体テスト：PASS。
- S3/C5/C6条件のH1〜H4保存保護・builtin・WebUI回帰：PASS。
- C6通常／計測ビルド、4MB header・partition・image配置検査：PASS。
- C6通常ELFに`chimera::hidtrace`のシンボルがないことを確認。

| C6ビルド | リンク後RAM | Flash |
|---|---:|---:|
| 通常 | 141,548 bytes | 1,591,004 bytes |
| 計測 | 149,588 bytes | 1,595,294 bytes |
| 差分 | +8,040 bytes | +4,290 bytes |

上記はローカル検証結果。実機のfree heap・Windowsのレポート単位の受信は未測定。

## 実機の正常系基準データ（2026-10-18）

ユーザーが`8eb308c`をC6へ書き込み、Windows BLE HIDで実施した試験の提供資料を、
[BLE HID Phase 2 — チャット共有テストログ](validation/ble-hid-phase2-c6-2026-10-18.md)に原文のまま保存した。
今後、異常が再現した際の比較基準とする。

- 5試験、合計49 Consumer Action／98通知要求。拒否・FIFO overflow・破棄・通知要求失敗・statusエラーは報告なし。
- 通常操作・同一Usage連打・異なるUsage交互操作はWindowsの動作も正常。
- Encoder操作・高速連打のWindows側の厳密な反映回数は未確認。
- FIFO最大深度は3。基本計測機能のC6実機動作を確認したものと評価する。

資料にはTest 1〜4の集計とTest 5の生ダンプが含まれる。完全な詳細CSVがない試験の
個別レポート順序・時刻を復元したり、件数一致だけからBLE通知内容の完全性を断定したりしない。
PAIRとCSVの微小な差は、それぞれ別に時刻を取得している実装と整合する。

Phase 2基本計測の検証はここで一区切りとし、NimBLE内部フックは未適用のままとする。
今後の異常再現時に同じ計測で差分を確認し、必要性が明確になった段階で内部フックや
Windows側のレポート取得、自動負荷試験を検討する。
