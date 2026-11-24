# C6未完了項目

- Wi-Fi単独切断：APでC6だけを切断できず、同一bootでのSTA再接続はHOLD。BLE復帰・OBS復帰・意図した再起動後のSTA接続成功とは区別する。ルーター全体は停止しない。
- Consumer 7 usageの生レポートとneutral、RotationValueのOSC float／MIDI CC／OBS音量の物理回転比較、Panic中のMIDI保留、隔離先を必要とするOBS操作の全項目について、Phase 3Aの両版比較証拠は未完。OBSの8操作を全操作の合格とはしない。
- メモリ：正式版でDMA largestの独立表示なし。候補版でもBLE再接続やシリアル開閉を避けた一部区間は失敗増分未測定。再起動をまたぐ差分や他区間の値で代用しない。
- 復元は完了。元v0.13.0のfactory（0x10000、0x300000 bytes）とLittleFS（0x330000、0xd0000 bytes）をそれぞれ書き戻し、独立したverify-flashで両digest一致。起動後に元のconfig・capabilities・OBS設定一致、inputsReady、storageState=ready、Wi-Fi・BLE MIDI/HID接続、OBS Ready、稼働0を確認した。元版にはengineReadyフィールドがないため、元設定で空のinput=3を1回triggerし、events+1、拒否増分0、稼働0でEngineの受付を確認。これは物理入力の再測定ではない。
