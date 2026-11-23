# C5 A案：GET時の一時領域（診断版、配置・送信粒度は変更せず）

Git外の候補版複製で、GETのJsonDocumentに通常どおりmalloc/realloc/freeする計数アロケータを使用。GET内の境界で `INTERNAL|DMA|8BIT` のfree/largest、JSONのlive割当・Stringバッファの実サイズと配置を集計。19 B mallocカウントは送信フェーズ・loopTask・内部配置に限定（コールサイトでの厳密な分類ではない）。失敗PCは同caps・GET中のみ取得。計測表はPSRAM、GET中のシリアル出力なし。

同一224枠設定・Wi-Fi/BLE MIDI/HID接続・OBS未接続。GET単独4回の失敗増分0/0/0/0、保存→GETの3回は保存0/0/0、GET 0/1/0。待機・statusも0、全GETで内容一致。通常A案は同条件のGET単独で1件、保存後GETで2件であり、診断版の頻度と混同しない。

`samples.txt` の `@@GET 17` が失敗を含む保存後2回目のGET。phase 1=開始、2/3=事前検査のJSON構築/文字列化後、4/5=送信パスのJSON構築/文字列化後、6=HTTP送信区間、7=JSONとpart破棄後。free/largestは各境界の最小、`jsonI/E`, `partI/E`, `simultaneousI` は各境界の内部/外部占有の最大（B）。一部行はシリアル欠落。失敗時は `0x4000f506`、`loopTask`、phase 6、要求1674 Bに対しlargest 1652 B。C5 rev1.0のROM ELFで直前の命令は `esf_buf_alloc_dynamic`。両時計を用いた時間比較はしていない。`counters.json` の計測時間にはプローブ読出しが入りGETレイテンシには使用不可。

224枠で送信中のJSONは最大2076 Bが内部、partは7616 Bが外部。追加対策候補はC5のGET用JsonDocumentのみ外部アロケータ化。2 KB程度の占有を内部から外せる可能性はあるが、DMA連続空きの増加量は未検証。通常コードへの変更なし。元アプリ・LittleFSのflash照合、v0.8.0再起動後の設定・capabilities一致を確認。
