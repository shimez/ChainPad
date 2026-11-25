# S3 Invocation外部配置・第1段階　最終結果（PASS承認済み）

対象は[第1段階計画](invocation-psram-stage1.md)の通常イメージ（SHA-256 `dc21cbdd1dd3e4e8a2f2a1a9967acbd3af57a0846c0c2a2416ca7047584fcc41`）。**第1段階はレビューでPASS承認。** OBS受信組立領域とGET用JSONのPSRAM化は見送る。これは対策後イメージでの計画範囲の結果で、旧候補 `f78d912` のReady共存FAILを置き換えない。

- **配置・Disabledゲート：** 32枠・9,472 Bの全範囲がPSRAM内、各起動内の配置不変。起動1と新しい独立起動2で各GET×4、保存→GET×3の全内容一致、割当失敗増分0。最初の起動2はHTTP statusのTCP接続タイムアウトで中断。原因は未特定であり、再試行成功を原因解消とはしない。[中断前の記録](stage1-gate-partial.json)・[再試行とReady 3起動](stage1-memory-results.json)。
- **Ready共存：** 独立3起動でGET単独一致。各起動ともOSC単独・GET並行は各208/208件の電文・順序一致、並行GET中に各15件を受信。割当失敗・overflowは0。保存後静止時の内部空きは53,296～53,336 B（正式版51,808、旧候補43,864）、並行後の起動内累積最小空きは15,400～20,228 B（正式版16,136、旧候補9,020）。並行後のDMA最大連続空きは31,732 Bであり、送信中の瞬間最小値ではない。[対策前との比較条件](ab224-results.md)。
- **機能回帰：** 27段Call、親子Loop、並列・FIFO、250 ms Wait、32枠満杯での拒否・即時実行・Stop、USB/BLEのペア・Panicを確認。USBで測ったWaitのPC受信間隔は5回249.93～251.52 ms。BLEの解放と親OSCのPC到着順は判定対象外、欠落・重複・解放漏れなし。[実受信記録](stage1-functional-results.json)。
- **復元：** 元factoryとLittleFSの実Flashダイジェストを領域別に照合済み。2026-11-26 08:08 JST、通常通常CDCで同一個体・`v0.5.3`、元の全設定（0 Actions）・capabilities・OBS API応答一致。storage/input ready、稼働0・全解放・両overflow 0、Wi-FiとUSB MIDI/HID ready、BLE MIDI/HID未接続、OBS Disabled。Windowsの `obs64.exe` は0件。元イメージ・設定・識別情報入りの生記録はGit外に保管。

**見解：** 試験条件ではInvocation外部配置だけで内部空きが回復し、Ready共存の合格条件を満たした。現時点ではOBS受信組立・GET用JSONの外部化を追加する必要を示す結果はない。異なる負荷や長時間運転での余裕、旧失敗の根本原因解消を保証するものではない。C5/C6の判定は変更しない。
