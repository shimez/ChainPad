# Action Chain Loop：2026-11-18 手動実機試験

## 結論

基本試験1〜8は、以下の確認範囲内でPASS。利用者と結果を確認し、
追加の反復・長時間・混合重負荷試験を一律に要求せず、実機検証を一区切りとした。
厳密なタイミング保証、全通信条件での無欠落を証明する結果ではない。

## 環境と版

- XIAO ESP32C6 / ChainOSCPad PCB、COM18、本体 `192.168.0.15`。
- 受信PC `192.168.0.25:9000/UDP`、`tools/ChainPadReceiver/receiver.py`。
- MIDIはWindowsの`ChainPad IN 0`をBLE MIDIとして接続。
  HIDは同じPCで受信。C6はUSB MIDI／HID非対応。
  ReceiverのHID欄はOS入力を記録するため、CSV上のtransportはunknownとなる。
  test1-3冒頭の「Opened as USB MIDI」はReceiverの表示ラベルであり、USB経路の証拠ではない。
- 基盤コミット：`97494cc`（Phase 3）。Firmware表示版は`0.12.0`。
- test1〜8-3：モックに合わせた縦線・挿入ガイド修正版。
  firmware.bin SHA256：
  `da0f0c00e048954c27b1eed873befe9f95e14516f29d7a2d8451bcb5f17ed269`。
- test8-4：途中のUIレビューで通常Action／Loopの余白を統一し、C6へ再書込みした版。
  RAM 181140 bytes、Application 1931476 bytes。
  firmware.bin SHA256：
  `8bcefb5ec56b2c9224c87bc01798763b2d9f230b82e07a795757e6d051e085a0`。
  Engineの変更はなく、再起動後のWebUI修正配信を確認した。
- RSSI、Windows／Pythonパッケージの詳細版、物理入力エッジ時刻は本試験では記録していない。

## 試験設定

OSCは共通接頭辞`/loop-manual/`、値はInt 1。
MIDIはBLE、Channel 1、Note 60、Velocity 100。HIDはBLE、F13。
通常のReleaseは空。KEY1とEncoder PushのみReleaseにもprobeを割り当てた。

| 入力 | 設定 |
|---|---|
| KEY1 | Press=`probe/press`、Release=`probe/release` |
| KEY2 | `nested/begin`→2回×3回（`nested/tick`＋Wait 200 ms）→`nested/end` |
| KEY3 | 999回×999回のWaitなしOSC `load` |
| KEY4 | `wait/begin`→3回（`wait/tick`＋Wait 1000 ms）→`wait/end` |
| KEY5 | 10回（`mixed/tick`→MIDI On/Off 100 ms→F13 Down/Up 100 ms→Wait 200 ms） |
| KEY6 | 3回（`midi-pair/begin`→MIDI On/Off 2000 ms→`midi-pair/end`→Wait 500 ms） |
| KEY7 | 3回（`hid-pair/begin`→F13 Down/Up 2000 ms→`hid-pair/end`→Wait 500 ms） |
| KEY8 | Stop Other Chains→`stop` |
| KEY9 | 10回（Stop Other Chains→`stop-loop/tick`→Wait 500 ms） |
| KEY10 | 各999回の8段空Loop |
| KEY11 | 3回（`all-notes/tick`→All Notes On/Off 300 ms→Wait 500 ms） |
| KEY12 | Stop Other Chains→HID Release All→All Notes Off→`cleanup` |
| Encoder | Press／Release／CW／CCWの専用OSC |

KEY11の収集対象はNote 60の1音。多数Noteの重負荷試験ではない。
KEY12は後処理用で、今回の独立した合否評価対象ではない。

## 結果

ファイル名は同じディレクトリ内。件数・順序はCSVで確認したもの。
Queue drops、押下回数、画面操作の成否など、CSVにない情報は利用者報告として区別する。

| 試験 | CSV | 確認結果・判定 |
|---|---|---|
| 1 基本入力 | `20261118_test1-3.csv` | Press／Release各1件。PASS |
| 2 ネスト | 同上 | begin／end各1件、tick 6件。tick間約195〜216 ms。PASS |
| 3 最終Wait | 同上 | tick 3件、最終tick→end 1024.411 ms。最終周回も待機。PASS |
| 4 初回 | `20261118_test4.csv` | 5000行の保持上限。Press／Release各14件のみ残り、20回全件の判定は保留。利用者報告Queue drops 0。入力欠落と断定しない |
| 4 再試験 | `20261118_test4.1.csv` | 2133行。load 2112、Press／Release各10件で交互、stop 1件。stop後loadなし。利用者が即時停止・Queue drops 0を確認。PASS |
| 5 初回 | `20261118_test5.csv` | OSC 10、MIDI On／Off各10。HIDなし。利用者がフォーカス忘れを確認し再試験 |
| 5 再試験 | `20261118_test5.1.csv` | 50イベント。OSC 10、MIDI On／Off各10、F13 Down／Up各10、ペア対応に欠落なし。PASS |
| 6 ペア保持 | `20261118_test6.csv` | MIDI／HID各3周、各begin／end 3件。MIDI保持1948.437／1964.939／2101.285 ms、HID保持1948.749／1962.460／2111.855 ms。F13追加Down 83件は保持中のOS自動リピートと整合。全3押下区間にUpあり。PASS |
| 7 手動Panic | `20261118_test7.csv` | MIDI／HID各begin 1件のみ、後続周回・endなし。各停止後のKEY1 Press／Release各1件。停止・再入力PASS。HID Upはフォーカス移動により判定不能のため追加確認 |
| 7 HID追加 | `20261118_test7.1.csv` | begin 1件、F13 Down→117.066 ms後Up。自動Panic計測でも解除・再開なしを確認。PASS |
| 8-1 同時実行 | `20261118_test8-1.csv` | 823行。load 812、Press／Release各5件、stop 1件、stop後loadなし。利用者がKEY3を3回押下・Queue drops 0と報告。PASS。ただし同一OSCアドレスなのでCSV単独ではInvocation数を識別できない |
| 8-2 Loop内Stop | `20261118_test8-2.csv` | load 182、stop-loop/tick 10。最初のtick以降loadなし。tick間430.503〜523.766 ms。自身の継続・他実行の停止を確認。利用者報告Queue drops 0。PASS |
| 8-3 空Loop | `20261118_test8-3.csv` | Press／Release各10件で交互、CW 10／CCW 7、Encoder Push／Release各1。CSV確認後のAPIではrunningChains=1、その後利用者がPanic→API再読込みで0を確認。入力・停止PASS |
| 8-4 All Notes | `20261118_test8-4.csv` | tick 3、Note On／Off各3。保持237.982／284.997／285.012 ms、Off→次tick 489.553／495.156 ms。利用者報告Queue drops 0。回数・ペア対応PASS |

### Panic仕様の確認と案内訂正

Panicは実行・未処理出力を破棄してHID解除を要求するが、**MIDIメッセージは送らない**。
予約中のNote Offも破棄される。test7のNote Off不在は仕様どおり。
当初の案内に含めた「Panicで音が残らないこと」は不適切として訂正済み。
音源・ホットキー登録は使用しておらず、音の停止／ホットキー動作は確認していない。

### HID追加確認の計測

`hid-panic-20261118-114900.json`を保存。
Windowsの`GetAsyncKeyState(F13)`でDownを検出し、`POST /api/panic`を送信。
Receiverのフォーカスを維持して、2秒保持の自然終了より前の解除を確認した。

- Panic HTTP RTT：104.087 ms。
- PCのDown検出から解除観測まで：114.907 ms以内。
- Windows F13解除：true、その後5秒間の再押下：false。
- runningChains：0、BLE Keyboard接続：true。
- 上記は物理入力エッジ遅延ではない。HTTP応答取得後に状態を確認しており、
  解除そのものは観測時刻より前の可能性がある。
- 準備段階の最初の監視はF13押下中検出で中止、再確認時には解除済み。
  次の監視は120秒で入力未検出タイムアウトしPanic未送信。
  これらは成功回数に含めず、状態を整えてやり直した最後の1回のみを評価した。

## 証拠の扱い・限界

- 利用者提供CSV全12件と補助計測JSONを内容変更せず保存。
  初回の判定保留ログも、再試験理由を追跡できるよう保持する。
- CSV時刻は受信PCの時刻。時間差は`delta_ms`を用いる。
  通信・OS処理の影響があり、Firmwareの保持時間が厳密に設定値どおりか、
  物理操作から出力まで何msかは保証しない。
- Receiverの行保持上限5000とQueue dropsは別物。Queue drops 0でも古い行は消える。
- CSV中の受信順がOSCとBLEで前後しても、それだけでFirmware内の実行順違反とはしない。
- Queue dropsの0報告はtest4（再試験含む）、8-1、8-2、8-4で取得。
  その他の試験で未報告の値を0と補完しない。
- 手動試験ではHTTP応答の定量計測を行っていない。先行自動測定と区別する。
- Panic 10回反復、切断・再接続専用試験、実機保存／Preset往復専用試験、
  30〜60分運転、OBS混合、多数Note負荷、物理入力絶対遅延は未実施。
  Firmware書込みに伴う再起動は、保存データ往復の網羅的な試験とは数えない。

## 設定の退避・終了状態

- 手動試験前の元設定はローカルの
  `C:\Users\ctake\AppData\Local\Temp\opencode\chainpad-loop-manual\original-20261118-112504.json`
  に退避。Wi-Fiパスワードは取得せず、省略による保持で適用した。
- 試験用設定は適用後GET一致を確認。利用者指定でOSC送信先をPCの`.25:9000`へ変更。
- この記録作成時点で元設定の復元は行っていない。手動試験設定を実機に残している。
  ネットワークを含む元設定・退避ファイル自体はリポジトリに追加しない。
