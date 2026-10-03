# Phase 2 verification

## 自動・ブラウザ検証

- FirmwareホストテストPASS: Wait中のRelease・別キー・再入力、連続Wait、0 ms、millis周回、32実行上限時の部分実行防止、取消、LittleFS保存・復元。
- 既存OSC/MIDI/HID/Engine、C3/C5/C6のBLE適応・保存テストPASS。
- `node tests/test_presets.cjs`: Key/Full往復、Action順保持、Encoder/通信設定、BLE適応、不正ファイル拒否、元データ不変を確認しPASS。
- C6ブラウザfixture: Wait編集と警告、Key1からKey4へのPress/Release転記、未保存Wi-Fiを含むExport、無効Importの編集保持、分割保存・再読込、Encoder回転でのKey Preset無効化を確認しPASS。

## 実機確認手順（未実施）

1. PressにNote On → Wait 1000 → Note Offを設定。待機中に別キーを操作し、即時出力を確認する。
2. PressをWait 1000 → Note On、ReleaseをNote Offとし、短く押す。Note Offが先に届くことを確認する。終了後Panicで解除する。
3. 同じキーをWait中に複数回押し、それぞれのChainが独立して完了することを確認する。
4. 長いWait中に保存・Panic・Restartを行い、待機後のActionが出力されないことを確認する。
5. Key PresetをExportし別キーへファイルImport。Press/Release、WaitとAction順が保存・再起動後も一致することを確認する。
6. 全体設定をExportし別個体へImport。全キー・Encoder・OSC・Wi-Fiの復元を確認する。S3→C系列はBLE適応表示も確認する。
