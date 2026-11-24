# C6候補版：BLE切断・再接続（リンク観測）

Wi-Fi経由のstatusで変化を記録。時刻はPCの測定開始からの秒数、uptimeはC6のms。

| PC時刻 | uptime | Wi-Fi | BLE HID | BLE MIDI | OBS |
|---:|---:|---|---|---|---|
| 0.08 | 85916 | 接続 | 接続 | 接続 | Ready |
| 60.92 | 146764 | 接続 | 切断 | 切断 | Ready |
| 71.33 | 157123 | 接続 | 接続 | 接続 | Ready |

切断と復帰の観測点は10.41秒差。C6のuptimeは連続して増加した。これはリンク状態の証拠で、再接続後のMIDI・HID・OSCの実受信や割当失敗増分の証拠とは区別する。

復帰後にPCでMIDI 13メッセージ、F13・修飾キーの押下／解放、OSC連番4件を別途照合した（`candidate-reconnect-midi.json`、`candidate-reconnect-osc.json`）。この再測定ではシリアルを開いていないため、区間固有のDMA値・割当失敗差分は未測定。

別試行でOBS reconnect時にReady(8)→Connecting(9)→Ready(9)を観測。初回のUDP確認は受信器の固定ポートと設定の送信先ポートが異なり判定不能。保存設定から受信ポートを取得して再測定し、OBS側の8件の状態読戻しと後続OSCが一致した。設定APIでID 28をstage、自己循環をcommitで拒否（各HTTP 400）、それぞれactive設定のGETは不変。組込Default/Emptyを実WebUIでそれぞれ読込、通信設定の保持・Full Export/Import往復も確認した。
