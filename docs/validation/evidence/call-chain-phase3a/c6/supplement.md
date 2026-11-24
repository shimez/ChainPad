# C6補完：基準版と候補版（実機復元済み・総合HOLD案）

元のv0.13.0のFlash 4 MiBを改めて退避・実機照合し、アプリ・LittleFSを別保存。再起動後の公開設定・capabilities一致。生Flash・実設定はGit外。

- Windows HID APIのChainPad Consumer Control collection（Usage Page 0x0C、Usage 1）で、Report ID 2に続くリトルエンディアン16bitを直接読んだ。E9/EA/E2/CD/B5/B6/B7の各押下に対し、直後のレポートは `02 00 00`。OSの音量変化やFirmware内の送信ログではない。
- MIDIはNote On 61受信後、1,600 ms hold中に別CCを投入してPanic。hold期限後まで自動Off、同じChainの後続CC、別Chainで既に投入したCCがいずれも出ず、稼働0。明示Offと新規CCの実受信を確認した。
- 先行試験のCallを含む224枠設定は正式版では実行不能。同条件比較用に別途、両版で使えるCallなし224枠・13連番×16回のLoopを使用。基準版3試行でGET一致・208件順序一致・割当失敗増分0。GET 1819–1844 ms、最大OSC到着間隔1805–1833 ms。1.64秒と並べて因果推定せず、候補版はこの新しい共通fixtureで比較する。

## 補完結果

- ConsumerとPanicは候補版でも基準版と同じ実受信結果。物理RotationValueは両版ともCW4/CCW4のOSC float・CC23・OBS絶対音量がすべて一致。候補版再試行でCCWのうち2ノッチは連続操作との申告あり。中間値も独立に受信したが、ノッチ間隔の評価には用いない。候補版初回の受信タイムアウトは測定不成立。正式版の回転区間ではCOM18が開けずメモリ未測定。
- 共通設定のchainsハッシュ一致。候補版3試行はGET 1708–1743 ms、最大到着間隔1703–1740 ms、すべて208/208・順序一致・GET一致・失敗増分0。PC受信時刻で、Engine側とUDP側は未分離。
- OBSの追加実機検証は候補版。scene itemのenable/disable、group itemのenable/disable/toggleを直接読戻し。mediaのpause/play/stopは状態、restartはcursor巻戻り、VLC next/previousは異なる長さの無音ファイルのduration切替で確認。録画、VirtualCam、ReplayBuffer、配信も隔離した一時プロファイル／ローカル出力で確認（配信先は127.0.0.1）。Replay Saveは保存先パスの応答、その他は各outputActiveを読戻し。初回は録画状態不変・Replay未準備で試験不成立。プロファイルを再読込して再測定した。元のOBSプロファイル、シーン、試験対象group itemの表示状態を戻した。実運用の配信先に接続していない。
- `supplement/memory-index.json` は既存＋追加ログの37区間・部分区間を出典付きで索引化したもの。両端の失敗数が記録された区間に増分なし。不足値は推定しない。候補のConsumer/Panic/RotationのDMA largestは各65,524→65,524 B。個別のBLE復帰直後やすべてのOBS操作に対応する完全な採取点があるわけではない。
- **Wi-Fi単独の同一boot再接続はHOLD**。通常版は起動時に自動再接続を有効化するが、任意のSTA切断・再接続を指示する手動診断はない。30秒切断後のAP-only fallbackは再起動まで保持されるため代用不可。共有APで個別切断不可、既存LANを巻き込むAP停止は実施しない。代替証拠は複数回の起動時STA接続・OSC到達であり同一boot再接続の証拠ではない。次の直接試験にはC6だけを収容する試験APで短時間（30秒未満）のリンク断を作り、C6のuptimeとSTAイベント、復帰後の連番を同時観測する。
- 補完試験後もfactory/LittleFSの両領域を独立にverify-flashし、元v0.13.0で公開設定・capabilities・OBS設定一致。Wi-Fi、BLE MIDI/HID、OBS Ready、inputsReady、storageState=ready、稼働0を確認。元版にengineReadyがないため、空Chainの受付events+1、拒否増分0で補足した。バックアップ・実設定はGit外。
