# シリアルでのLittleFS・RAM診断

USBシリアルモニターを115200 baudで開きます。S3はUSB CDC、C3/C6/C5はUSB Serial/JTAGです。接続待ちで起動を止めません。

| 入力（小文字） | 動作 |
|---|---|
| `m` | 現在の診断を1回表示 |
| `d` | 5秒間隔＋保存処理の診断をON/OFF（起動時はOFF） |
| `?` | 操作説明 |

改行あり／なしのどちらでも使用できます。設定の変更・保存・初期化を行うコマンドではありません。未マウントのLittleFSは診断によってマウント／フォーマットしません。SSID・パスワード・Actionの内容は出力しません。

## 出力項目

- `t`: 起動からのミリ秒、`reason`: 手動／定期／保存段階、`board`: 対象機種。
- `RAM internal`: 内部8-bit RAMの空き`free`、起動以来の最低空き`minFree`、現在の最大連続空き`largest`、ヒープ割当済み`allocated`。単位はbyte。複数ヒープ領域の最低値は各領域の最低値の合計です。
- `loopStackMin`: setup/loopが動作するタスクの起動以来の最小未使用スタック量（ESP-IDFのbyte単位）。BLE等の別タスクのスタックは含みません。
- `RAM PSRAM`: PSRAMの同じ指標。未搭載／未初期化では0。内部RAMとは別に評価してください。
- `Static sizes`: `sizeof(Action/Chain/Config/Engine)`と1レコードの上限`recordLimit`。静的領域全体のサイズではありません。
- `Actions`: 保存・適用済み構成の総数、1キー（Encoder Pushを含む）の最大Press/Release合計、1 Eventの最大件数、実行中Chain数・拒否回数。保存途中は旧構成を表示します。
- `LittleFS`: 総量`total`、使用量`used`、残量`free`、`/config.records`の論理サイズ`active`、`/pending.records`の論理サイズ`pending`。単位はbyte。ファイルサイズの-1は不存在、-2はopen失敗です。使用量にはファイル以外の管理領域も含まれるため、ファイルサイズ合計とは一致しません。
- `Counters`: Transportの再試行・overflow、入力overflowの累積値。

定期診断ON時は、設定GET後、保存begin後、各stage後、commit前後、Wi-Fi保存前後、保存失敗時も表示します。`save-commit-before`で旧activeと完成したpendingが共存する状態を確認できます。成功後はpendingがactiveへrenameされます。失敗／中断後のpendingが残ることもあります。

## 上限評価の手順

1. 対象機種ごとに再起動し、`m`で起動・読み込み後の基準値を記録します。
2. `d`で診断をONにし、全キーを上限まで設定して保存します。片側へ集中した16/0や0/16、Encoder回転も含め、長いOSC Address/String（JSONエスケープで長くなる文字も含む）の構成を試します。
3. 同じ最大構成をもう一度保存し、activeとpendingの共存時のLittleFS使用量を確認します。設定GET、Wi-Fi保存、再起動後の読み込みも確認します。
4. Wi-FiとBLE、対応機種ではUSBを使用し、Waitを含む同時実行・再トリガー・All Notesも試します。最低ヒープ、最大連続空き、スタック余裕、拒否／overflowを記録します。
5. `d`でOFFにして通常の操作・タイミングも確認します。ログ出力とファイルサイズ照会には処理時間・一時メモリが必要です。シリアル送信タイムアウトは0なので、モニターが追いつかない場合は出力が欠けることがあります。

診断は現在値のサンプルです。ヒープ最低値は短時間の割当も反映しますが、最大連続空きの一時的な最低値やLittleFSの内部的な瞬間ピークを連続計測するものではありません。最低値は再起動までリセットしません。

現在のConfigは各キー／Encoder Pushに共有16枠、Encoder回転に各8枠、合計224枠の固定長Action領域を確保しています。空のキーを増やしてもこの静的RAMは減りません。診断の`Chain`は保存・読み込み時にヒープへ一時確保する16件の作業用型で、Config内に28個存在するわけではありません。上限変更後は再ビルドして`sizeof(Config)`とビルド時RAM使用量も比較してください。LittleFS残量だけでなく、1レコード上限、保存・読み込み中の内部ヒープと連続領域、スタック、出力キュー容量を合わせて評価し、実測の残量ぎりぎりを上限にしないでください。
