# Phase 1 C6実機結合検証（2026-11-23）

対象 `c87f179304e0738f9670016e1572d7a4e3701a28`、通常C6ビルド、アプリSHA256 `0ab1b46a4cf2038f89ab770f139c04023dda77d50a42acba69222d33d6847570`。COM18。既存設定85 Action。試験起動はHTTPで、物理キーの試験ではない。

| 操作 | 観測 |
|---|---|
| Edgeの実Configuratorで保存・再取得・再起動 | 保存ボタンから28 stageとcommitのHTTP 200を確認。リロード後「保存済み」、再起動後も設定GETが元設定と一致 |
| 保存拒否と実行中の状態 | 既存8重空Loopを1件起動したまま、循環は28本提出後のcommit、不正ID 28はstage、正当な前方参照はcommitでEngine未対応として各HTTP 400。毎回GET一致、running=1、cancelled増分0。Panic後は0 |
| OSC実受信 | 保存拒否後・安全復旧後の両方で、既存KEY2の入れ子Loopが `begin → tick×6 → end` の順にPCへ到着。最初の試行はUDP受信タイムアウトのため元状態へ復元したのち、候補を再投入し再試験 |
| 不正設定起動 | 設定パーティションの複製でKEY1 Releaseだけに自己参照を挿入。シリアルは `Call cycle: key1.release#1 -> key1.release`。セットアップAPのstatusはcorrupt／出力不可／running=0。triggerは409、保存beginは400。読戻しで不正なactiveレコード自体も試験前とバイト一致 |
| 正常復旧 | 正常な退避済みLittleFSを戻し、**候補Firmwareのまま**設定GET・出力許可・OSC受信を確認。初期化は実行していない |
| BLE MIDI／HID実受信 | **再ペアリング後に合格**。初回は候補版・元アプリの双方でWindows MIDIポートを開けず未判定。再試験では候補版で両購読true、Note 60 On/Off・F13 Down/Upとも各10回をWindowsで確認（[受信ログ](ble-retest.txt)） |

BLE再試験ではユーザーの再ペアリング後に改めて全flash・設定を退避し、同一候補版へアプリ領域だけを書き込み。WinMMのChainPad MIDI入力を開き、既存KEY5 Press（入力8）をHTTPで1回起動した。10反復でOSC受信10件、MIDI Ch1 Note 60（On velocity 100）On/Off各10件、WindowsのF13キー状態で押下・解除各10回を観測し、完了時running=0。元アプリを戻し、再試験前のアプリ領域・LittleFSとflash照合、再起動後の設定GET一致・readyを確認。再試験でNVSの書き戻しは行っていない。

通常試験後、内部RAM free/minFree/largest=94,864/70,412/64,500 B、loopTask/input-scan/OBSの最低スタック余裕=5,088/2,652/6,012 B。割当失敗・入力／通信overflow=0。実測点の値で、長時間負荷時の保証値ではない。

全4 MiB flashを退避・書込み前照合し、公開設定も別保存。最終的に元アプリ領域と元LittleFSを戻して両方flash照合し、再起動後の設定GET一致・元のcapabilities・v0.13.0・readyを確認。元アプリ領域SHA256 `a64b6d8f53f6bf4d75555f2edad3b0daab4cc6a544346fd1dce10ea60078044e`、元LittleFS `9b09c44bf2845cd605f40e490380d34e2bdf0f59cc3a80f3a556fefc0f253113`。生のflashと設定はリポジトリ外。`api.log.txt`・`boot-final.log.txt` は試験側の観測、`normal.txt`・`locked.txt` はIPを伏せたシリアル診断。
