# A案：C5のInvocation外部配置、GET合否判定

通常C5アプリSHA256 `39cda57e709ac5560f7b05f4251c2e9503bd76b9d061b74e7b87604826310cef`。ELFのEngineは内部 `.dram0.bss` 80 B（従来9,548 B、差−9,468 B）。実機 `Engine slots: base=0x421f4c24 bytes=9472 count=32 externalRange=1`。C6/S3/C3のEngineは9,548 Bのまま、全4機種のビルド・ELF・イメージ検査成功。

比較と同じ224 Action設定（`get-tests.json` にSHA256）、Wi-Fi・BLE MIDI/HID接続、OBS未接続を各試行前後に確認。追加のGET対策なし。失敗数はシリアル `h` の区間差分。

| 条件 | 割当失敗の増分 |
|---|---|
| 小さい設定GET、初回stage～commit | 0、0 |
| アイドル、status GET | 各4回すべて0 |
| 224 Action GET単独 | 0、1、0、0（変更前12、13、13、13） |
| 続けて同じ設定を3回保存→GET | 保存はいずれも0、GETで0、2、0 |

全GETで取得内容一致。3失敗の要求サイズ／同条件largestは順に1,674/1,268、1,648/1,012、1,686/1,204 B。すべて `heap_caps_malloc`、`caps=0x80c`。失敗はGETに集中しゼロ件基準不合格。件数はこの起動・接続条件での実測であり長期失敗率ではない。実行側の回帰・冷起動・確保失敗注入には進まず終了。元アプリ／LittleFSと退避イメージのflash digest一致、元v0.8.0の設定GET・capabilities一致、ready・稼働0を確認。NVS書き戻しなし。
