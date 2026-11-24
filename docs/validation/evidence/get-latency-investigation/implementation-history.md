# 実装履歴と試験対象

| 時点 | GETと実行方式 | 今回との関係 |
|---|---|---|
| `eb0f438`（初期コミット） | `src/main.cpp` で全設定を1つのJsonDocumentに構築・String化して送信。`loop()` が `web.handleClient()` の後に入力を取り出し、`Engine::trigger()` で即時dispatch。まだ `Engine::tick()` はない | HTTP処理と次の入力実行が同じloopにある構造は古い。ただし初期版の到着間隔は測っていない |
| `e367f5e` → `3cfcde5` | LittleFSの段階保存と、レコード単位で生成・チャンク送信するGETを導入。その後Waitと `Engine::tick()` が加わり、同じloopでHTTPの後に待機中のChainを再開 | Waitは後述のA/B/Cすべてで使用可能 |
| `3b29a08`、`581b30e`、`1f6934e` | キーのPress/Releaseでの枠共有、OSC容量拡大、schema v2・Encoder Rotation・設定用JSON writer | 同じ「224枠」だけでなく値・文字列長・送信バイト数も揃える理由 |
| A → B（下表の親子） | Aはヘッダ送信後、network・28 Chain・rotationを順に生成・文字列化・送信。Bは同じレコードを**送信前にすべて検査し、送信時に再生成** | 旧版でのJSON生成失敗・設定欠落を黙って成功扱いにしないための変更 |
| `6faa864`、`f68b08a`／`6a82a09`、`e4d283c`／`97494cc` | 公開設定からのWi-Fi秘密値除外、OBSワーカーとAction統合、Loop実行とOSCペーシングを順次導入 | Cは後世代の正式版。Loopの非存在を比較の障害としないよう共通負荷を設計 |

根拠：[AのGET・loop](https://github.com/shimez/ChainPad/blob/35fa5b0/src/main.cpp)、[Bの親子差分](https://github.com/shimez/ChainPad/commit/b134b6fcac961d70fc7007a7e5cd6e77093a0534)、[CのJSON writer](https://github.com/shimez/ChainPad/blob/v0.13.0/src/json_wire.h)。`checkedSettingsJson()` は各回で長さを計測してから文字列化する。Bはヘッダ前ならエラー応答、送信開始後の生成失敗なら接続を中断する。2パスは信頼性のための検査であり、A/B差分にはcodec等の変更も含む。`sendContent()` の滞在中も当該loopが次のActionを実行するわけではないが、生成・送信・TCP待ちのどこに何ms費やしたかは推測に留まる。

## 識別と由来

| ID | ソースコミット | 通常版バイナリの由来 |
|---|---|---|
| A | `35fa5b03b1f95affb3bca225a3c8e5c650c8a061` | 変更直前、1パス。無改変ソースから再ビルド |
| B | `b134b6fcac961d70fc7007a7e5cd6e77093a0534` | 直後、2パス。Aと同じ環境から独立ビルド |
| C | `1d2310296e4ce649ecd4246d0174ad57dc3ffa0a` | [v0.13.0 Release](https://github.com/shimez/ChainPad/releases/tag/v0.13.0) の配布品。再ビルドではない |

A/Bとも表示版は `0.5.1-chimera`。対象SHAを証明できる当時の公開binを特定できず、表示版で代用しなかった。Windows、Python **3.11.7**、PlatformIO Core **6.1.19**、環境 `xiao_esp32c6`（release）。platform `55.03.311`、Arduino core `3.3.11`、framework libs `5.5.5+sha.b774170ff46`、RISC-V toolchain `14.2.0+20260121`、esptool `5.3.0`、NimBLE-Arduino `2.5.1`、ArduinoJson `7.4.2`。WiFi・LittleFS・DNSServer・ESPmDNS・WebServerはcore付属 `3.3.11`。CはReleaseの `build.json`、`manifest.json`、配布binのdigestを照合。

| ID | 結合イメージSHA256（先頭 `0x0`） | 実際に照合したアプリ領域SHA256（`0x10000`） |
|---|---|---|
| A | `1d8dac800f0f8ea8110b3c495bee5857575d4e6f2466935830b6735166b3c579` | `0a18792d6a66370c061298f0acdb36667b713a48390edf40eb7cfea10eebc783` |
| B | `084a51a999eecb28cdb9dff2724026f46d3baf9120d78bbbc264c84014ce64a3` | `2241d654a956b47df34027508cab27b5b8dce0c0dd6b178cab617109f81bb732` |
| C | `1f369a7f446ff2dc011945c8fb2d7183d715b7b34a86470822567461e091a24c` | `7927ee43f59d953c856017f8a266aff8bdeb7c81c436d094c3a1760bc4e8dfdb` |
