# C5/C6 OBS WebSocket試験実装

> この文書は試験段階のビルドと計測記録です。最終確認でS3/C5/C6のOBS動作・
> メモリスモークをユーザーがPASS承認。C5/C6は標準`platformio.ini`へ統合し、
> `platformio.obs-experimental.ini`は削除しました。以下の試験用コマンドは過去の記録です。
> 現行のビルド方法と既知の留保事項は[統合状況](obs-integration.md)を参照してください。

S3承認済み基準: `463d998`、[実機結果](obs-memory-test.md)。
C5/C6の初回実機ログを受領（下記）。C6はログ範囲で良好、C5は起動時エラーの評価待ち。

## ビルドと実装

```powershell
pio run -c platformio.obs-experimental.ini -e xiao_esp32c5_obs_experimental
pio run -c platformio.obs-experimental.ini -e xiao_esp32c6_obs_experimental
```

出力は `.pio/build-obs-experimental/<環境名>/firmware.bin`。
`-e`を必ず指定する。省略すると継承元の標準4機種が選択される。
通常UI／OBS接続設定APIを使用し、TESTコンソールではない。
標準C5/C6はOBS無効のまま、試験環境のみ明示的に有効にする。
WebSockets 2.7.2と既存のハッシュ検証付きパッチを使用。
C6は既存4 MB用partition／3 MBアプリ領域を継承する。

7操作、Action JSON、Config／Full／Key Preset、OSC、BLE-HID、Action Chainの
既存経路を使用。CシリーズのUSB Serial/JTAGをUSB HID/MIDIとして扱わない。
非同期Worker（priority 1、Arduino running core）、要求FIFO4、結果FIFO8、
in-flight1、通常ビルドの単一結果consumerを維持する。

シーン一覧は手動のみ。固定2スロット8344 B、Assembly16400 B、
Worker stack8192 Bを維持。JSON予算32768 Bまたは実ヒープ不足で
`NoMemory`になった場合は一覧要求をcode=-10で失敗とし、旧候補を非確定のまま保持。
部分的な一覧を公開せず、接続を維持して次の通常Actionを処理する。
この処理は共通バックエンドなのでS3にも適用される。
構文／プロトコル異常、フレーム上限超過は従来の扱い。
WebSocketライブラリ側の受信領域確保失敗は切断を伴う場合があり、
あらゆるメモリ不足で接続維持できる保証ではない。

## 機種別の条件

- **C5**: ボード定義はPSRAMあり、240 MHz、Flash8 MB。
  Config82648 BはArduino PSRAM初期化後の遅延確保。内部RAMへのfallbackなし。
  OBS用固定領域は内部RAM。PSRAMの総空きだけで内部RAM／DMAの余裕を判断しない。
- **C6**: 160 MHz、Flash4 MB、PSRAMなし。Config82648 Bも内部RAM。
  過去のOBS非搭載・空設定・STA未接続でfree133980 B、largest118772 Bだったが、
  今回のOBS接続時や最大設定時の空きとして流用できない。

## 実機確認手順

機種ごとに同じConfig／OBSシーン条件とWi-Fi・BLE状態を記録する。
[S3計測手順](obs-memory-test.md)の `m/h/o/O` を使用する。

1. 起動後 `h` と `m`。C5はConfig external=1、C6はexternal=0／PSRAM total=0を確認。
2. Disabled、Ready、通常7操作、手動一覧取得、再接続後を個別に記録。
   `o` → 一覧取得 → `O` → `m` で3点と回復を確認する。
3. OSCとBLE-HIDを含むAction Chainを実行し、Panic、OBS停止・復帰、Wi-Fi切断・復帰を確認。
4. 最大に近いConfig、一覧上限、一覧取得を繰り返す条件で、free/minFree/largest、
   Worker/loop HWM、確保失敗、input/transport overflow、Ready復帰を確認。
5. 通常操作の回復確認に加え、長時間・同時負荷は別途実施する。

静的リンクサイズだけでは動的ヒープ余裕や接続の安定性を判定しない。
機種別の実測で不足が確認された場合に、追加のメモリ最適化を判断する。

## 回帰検証

- `tests/run_obs.ps1`（native、`-Target C5`、`-Target C6`）: 全てPASS。
  本番Worker／Engineを使用し、一覧JSON予算超過後の旧キャッシュ保持、
  Ready／世代維持、後続Action成功を確認。C5/C6はUSB無効もcompile-time検証。
- `tests/run_obs_control.ps1`: PASS（HTTP、保存／適用、Panic、Config分離）。
- `tests/run_obs_settings.ps1`: PASS（設定形式、password、保存失敗、Config分離）。
- `tests/run_host.ps1`: PASS（通常／trace、標準C3/C5/C6、OSC・HID・Action Chain、保存互換性）。
- Node `test_obs_ui.cjs` / `test_obs_scenes_ui.cjs` / `test_presets.cjs`: PASS。
- Hostは実機のPSRAM初期化、Wi-Fi/BLE共存、RISC-Vスタック使用量を再現しない。

## リンクサイズ（bytes）

同一ソースの標準ビルドもS3/C3/C5/C6全てPASS。

| 環境 | 静的RAM | Flash |
|---|---:|---:|
| 標準S3 | 137820 | 1612554 |
| 標準C3 | 136364 | 1462209 |
| 標準C5（OBS無効） | 65768 | 1677995 |
| 標準C6（OBS無効） | 141580 | 1624378 |
| C5 OBS試験版 | 103288 | 1903487 |
| C6 OBS試験版 | 179116 | 1849274 |

標準ビルドも今回の比較では `.pio/build-obs-experimental/` 配下に生成。
Flashはアプリ使用量。C5の動的PSRAM Configは静的RAM値に含まれない。

OBS有効ビルドは両機種PASS。標準版との差分はC5がRAM +37520 B／Flash +225492 B、
C6がRAM +37536 B／Flash +224896 B。
ELFの`nm -S -C`でも両機種のsceneBuffers=8344 B、Assembly=16400 B、
Worker stack=8192 B、sceneMemory=108 Bを確認。
C5はactiveConfigの遅延確保経路、C6は静的Config=82648 Bを確認した。
固定領域の動的化やスタック削減は行っていない。

ビルド／Host回帰は完了。残る評価は実機の起動、OBS認証・7操作・再接続、
Wi-Fi/BLE/OSC同時使用、機種別の内部RAM free/minFree/largestとWorker HWM、
長時間・高負荷時の安定性。現在のリンク結果だけで実機PASSとはしない。

## 書き込み

プロジェクトルートのPowerShellで実行。COM5は対象機のポートへ変更し、
シリアルモニターを閉じる。

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -c platformio.obs-experimental.ini -e xiao_esp32c5_obs_experimental -t upload --upload-port COM5
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -c platformio.obs-experimental.ini -e xiao_esp32c6_obs_experimental -t upload --upload-port COM5
```

接続した機種に対応する1行だけ実行する。本作業では実機書き込みを行っていない。

## 初回実機ログ（ユーザー提供）

| 指標（bytes） | C6 | C5 |
|---|---:|---:|
| Disabled internal free | 98568 | 50048 |
| 最初のReady internal free | 94324 | 47572 |
| 接続前後差分 | -4244 | -2476 |
| 最終Ready internal free | 94272 | 47612 |
| 最終internal minFree | 80916 | 13580 |
| 最終internal largest | 75764 | 20468 |
| Worker最小未使用stack | 3772 | 3812 |
| loop最小未使用stack | 5384 | 5176 |
| 記録された確保失敗／transport overflow／input overflow | 0/0/0 | 0/0/0 |

C6はAction29/224、Config external=0、PSRAM total=0。
一覧2回のbefore/json-alive/after freeは86732/90968/94272および
90972/90968/94280。最終Ready、generation=7、lastError=0。
今回のログ範囲で継続的なヒープ減少・Workerスタック不足の兆候なし。

C5はAction2/224、Rotation2/16、Config external=1、PSRAM total=8388608。
PSRAM free=8298800、largest=8257524。PSRAM minFree=0は由来未確認で、
実際にPSRAMを使い切った証拠とはしない。
一覧1回目はbefore39908/json-alive44296、afterのinternal行が共有ログで欠落。
直後のmは47612。2回目は47632/44296/47632で回復。
この差分3336 Bも全システムのサンプル差であり、JSON単独の厳密な消費ではない。
計測末尾のReadyはgeneration=7、lastError=0。

ただしC5ログ末尾にSW_CPU再起動と
`MSPI Timing: Failed to allocate dummy cacheline for PSRAM memory barrier!`、
`No core dump partition found!` が複数回ある。再起動が手動操作／書き込みに伴うかは未確認。
今回のC5 ELFでSaved PC=0x40800aceは`esp_restart_noos_inner`へ解決されるが、
これは再起動経路であり、呼び出し元や原因を特定するbacktraceではない。
起動ログのBuiltin Default loadedとLoaded LittleFS settingsの切り替わりも
操作履歴を確認するまで設定消失とは断定しない。
アプリ診断のfailures=0は、その後の再起動時・診断開始前の確保失敗を否定しない。

C6のメモリスモークはログ範囲で良好。C5の通常動作中の回復は確認できるが、
総合PASSは起動エラーと再起動経緯の確認まで保留。
機種間で設定量が違うため同一負荷比較ではない。7操作の実際の効果、同時負荷、
長時間安定性はこの診断ログだけでは判定しない。

### C5追加確認

ユーザーより、先の書き込み後にはUSBの抜き差しを行ったとの補足あり。
先の起動ログだけで動作中の自発的再起動とは判定しない。一方、SW_CPUの原因や
PSRAM barrier用確保エラーの原因が抜き差しと確定したわけではない。

抜き差し・Resetなしで数分待ち、OBS操作後にm/hを取得する手順への追加ログを受領。
uptime293148 ms（約4分53秒）、Ready、generation=3、lastError=0。
Action29/224、Rotation0/16、Config external=1。
internal free47924 / minFree21188 / largest26612 B、PSRAM free8298800 B。
Worker最小未使用3812 B、loop5240 B。確保失敗記録、transportRetries、
transportOverflows、inputOverflowsはいずれも0。共有された追加ログに再起動出力なし。

今回の追加確認では運転中の異常を示す兆候はなく、通常操作メモリスモークは良好。
先のConfig（Action2/224、Rotation2/16）とは異なるため、低水位やlargestの改善を
同一条件の改善効果とは扱わない。単一の最終snapshotから全期間の再起動有無や
リーク不在を証明するものではない。起動時PSRAMエラーの原因・再現性は未解決で、
長時間／高負荷／繰り返し起動の評価と分けて残す。

### C6実機回帰テスト：全項目PASS（ユーザー報告）

- OSC → OBS → BLE-HIDの混在Action Chainを実行：PASS。
- Delayを含むAction Chainが順序どおり実行：PASS。
- 設定保存・C6再起動後、Action ChainとOBS接続設定の復元：PASS。
- 再起動後の混在Action Chain実行：PASS。

C6では初回メモリ計測に加えて、既存トランスポートとの混在、遅延順序、
永続化・再起動後の実行を実機で確認済み。C5の同等回帰、OBS全7操作の網羅、
長時間・高負荷については、この報告から確認済みとはしない。
