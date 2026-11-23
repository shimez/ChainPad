# C5：A案＋GET用JsonDocumentのPSRAM化（判定：不合格）

通常版C5アプリSHA256 `776f70c9f0889ec7342853bb2216185b139e413dfa26c2498d05601e80c2c052`。A案を保持し、GET /api/configのArduinoJsonアロケータのみSPIRAM|8BIT指定。String、転送単位、設定形式は同じ。ホスト・保存障害スイート、4機種のビルド・ELF・イメージ検査成功。Invocationは全機種296 B、内部EngineはC5のみ80 B、他3機種9,548 B。

同一224枠構成、各試行前後でWi-Fi接続、BLE MIDI/HID購読あり、OBS未接続。初回stage～commitを含め、通常版の保存・待機・status GETの失敗増分0。設定GET単独4回は0/0/0/0、3回の連続保存直後の設定GETは0/1/0。全レスポンスは入力と一致したが、2回目の保存後GETで1,644 Bの内部DMA割当要求に対しfree 5,140 B、largest 1,588 B。要求元PCは通常版では未採取。

Git外の別診断ビルドで同じ7回を測定。GETの実アロケータに計数ラッパーをかけ、PSRAM配置を確認。採取した送信区間のJSON動的領域は最大 内部0／外部2,064 B、partは内部0／外部7,616 B。A案のみの送信時のJSON内部最大2,076 Bに対し内部占有がなくなった。事前検査時の内部partは最大96 B。224枠7 GET中、DMA最大連続空きの採取点最小は入口25,588 B、事前検査25,588 B、送信境界11,764 B、破棄後16,372 B。一部シリアル行欠落、各値は採取できた行の集計であり瞬間谷ではない。診断版は全区間失敗0、全GET一致だが通常版の失敗を取り消さない。

`normal-get.json` は合否判定、`diagnostic-get.json` と `diagnostic-samples.txt` は配置・メモリ解析用（明細の診断読出し時間を含む）。元v0.8.0のアプリ・LittleFSをflash照合し、再起動後の設定GET・capabilities一致、ready・稼働0を確認。バックアップや診断FirmwareはGit外。実行側回帰・S3実機検証は未実施。
