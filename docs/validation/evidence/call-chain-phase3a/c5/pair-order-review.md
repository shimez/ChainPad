# 子ペア解放と親OSC：初回FAIL、ソース確認と比較試験

**判定：Phase 3AのPC観測順は2ケースともFAILのまま。C5内部の復帰順違反は未確定。** 以下の差は「親OSCのPC観測時刻 − 解放のPC観測時刻」。正なら解放が先。

| 条件 | Phase 2DのC5ペア試験 | Phase 3Aの今回の試行 |
|---|---|---|
| 比較資料 | [通常版の記録](../../call-chain-c5-phase2d/README.md)、[PCイベント](../../call-chain-c5-phase2d/pc-ble.json)、Git外の `p2d-c5-pc.py` | 候補 `574b939`、Git外の `pair.py` と停止時のassert出力 |
| アプリSHA256 | `6f96daecfe93d52e67cecda098a2270ef5965bc35a7310dfaa93819423d5f4e7` | `14e8e3dcbf2e9c877489b8d143ccbae6093c684135a5e0798087abe7c7d6dcf0` |
| 共通構成 | 両方ともC5・Windows。HTTPで入力12→子14のBLE Note 60 On/Off（ch1、velocity 100）、入力16→子18のBLE F13 Down/Up（修飾なし）を別々に開始。各親の次ActionはWi-Fi OSC int、親に追加Waitなし。 | 同左 |
| hold／OSC識別 | 250 ms。親別address `/p2d/m`、`/p2d/h` | 500 ms。`/p3/test` のint 12／16。送信元と電文も検査 |
| MIDI | RtMidiの `ChainPad IN 1` を開き、PC callbackで時刻付与 | 最初の `ChainPad IN…` を開く（今回の列挙は `IN 0`）。両購読と実イベントを確認。時刻は同じくcallback内 |
| HID・UDP | trigger応答後、F13を `GetAsyncKeyState` で観測し、次に非ブロッキングUDPを読む。1.2秒窓・ループsleep 2 ms | 同じ観測順。1.4秒窓・sleep 1 ms |
| 時計 | PC `perf_counter()` の開始からの相対ms、0.01 msに丸める | PC `perf_counter_ns()`。カーネル着信時刻ではない。例外にイベント列が残り、終了時診断は未取得 |
| 差（MIDI／HID） | **+16.68／+25.26 ms** | **−41.00／−39.77 ms** |

各条件各ペア1回。両版とも解放自体はPCで観測。holdはDown/Onから解放までの設定であって、解放と親OSCの間の待ちではない。holdだけでなくバイナリ・受信ループ・接続セッションも異なる。MIDIの時刻はOSからcallbackが呼ばれた時点、F13は状態遷移に気付いた時点、UDPはキューを読んだ時点。sleep値は実際の観測誤差の上限ではない。

## 通常接続・正常送信時のコード経路（候補版）

1. `src/engine.cpp` の `Engine::advance` はCallで親の入力と次の位置を `frames.suspend` に保存し、**同じInvocation**で子へ進む。
2. `src/midi_backend.cpp`／`src/keyboard_backend.cpp` は On/Down、holdマーカー、Off/Up をtransportのoutboxに積む。Acceptedでも対象ペアに残件があればEngineは `job.pairPending` を設定しWaitingで戻る。
3. `src/main.cpp` は `backendsTick` の後に `engine.tick`。hold期限後のflushは `midiWrite`／`keyboardWrite` が成功してから解放項目をpopする。失敗時はキューに残して再試行する。BLE経路の成功は `src/transports.cpp` のNimBLE `notify()` の戻り値であり、Windowsの受理・callback実行を待たない。
4. 再開時は子の終了判定より先に `pairedActionPending` を確認する。`src/backends.cpp` で検査するのは対象transportのキューで、個々のパケットに対するPCからのACKではない。空になれば子末尾で `frames.restore`、その後に親OSCをdispatchし、`src/osc_backend.cpp` のUDP `endPacket()` に至る。切断時のキュー破棄やPanicは正常完了と別経路。

したがって保証すべきなのは、正常系で**子のバックエンド完了条件を飛び越して親をdispatchしないこと**。別経路のPC到着順まで保証する機構はこの経路にはない。今回の逆転はこの構造と両立するが、BLE・無線・Windowsのどの区間が何msを占めたか、実際のC5内部でどちらが先だったかは観測していない。[C6の既存トレース](../../call-chain-c6-phase2d/trace.txt)は比較参考で、C5の代用ではない。

## 当初の再試験案（後に承認・実施）

同じC5候補・PC・受信器・接続条件のままholdだけ250／500 msに変え、各hold×各ペア5回、計20試行。順序はブロックごとに交替。HTTP trigger前に受信を開始し、MIDI callback・HID状態・UDPを継続収集、共通のPC単調時計で記録。各試行の全イベントを**assertより先に保存**する。機器・購読先、Wi-Fi／OBS状態、uptime・割当失敗・overflowを前後で確認。欠落・重複、各符号付き差と範囲、解放先着／OSC先着／観測上同時を報告する。接続状態変化や計測欠損は別記し、黙って置換しない。初回FAILは上書きしない。逆転を想定内の観測として反復する範囲のみ事前承認し、新規割当失敗・解放欠落などは停止する。

この試験が識別するのは同一候補でのhold条件とPC観測の関係であり、過去版からの回帰や内部順の直接証明ではない。内部順の実証が必要なら、別承認でC5の解放write成功、pair complete、call return、親OSC dispatchを同一Firmware時計で照合する。PC時刻とは直接減算しない。
## 承認後の再試験結果（C5候補通常版）

5ブロック×2 hold×2ペアの20試行を実施。hold順をブロックごとに反転し、ペア順も交替。UDP受信とF13状態監視は別スレッド、MIDIはcallback。trigger前から共通のPC単調時計で観測し、未判定のイベント列を先に保存した。Wi-Fi・BLE両購読・OBS Ready、同一起動。各試行前後で割当失敗0、入力・送信overflow増分0、稼働0。全60イベントに欠落・重複なし。終了時に今回の開始時設定（500 msのペアfixture）との一致を確認した。

| hold | ペア | 親OSC観測 − 解放観測 ms：中央値［最小, 最大］ | PCで先に観測 |
|---|---|---|---|
| 250 ms | MIDI | +14.86［+9.88, +21.38］ | Off 5/5 |
| 250 ms | HID | +19.17［+9.85, +44.75］ | Up 5/5 |
| 500 ms | MIDI | −27.53［−31.61, −2.50］ | OSC 5/5 |
| 500 ms | HID | −30.70［−51.31, −19.12］ | OSC 5/5 |

[試行別データ](pair-order-retest.jsonl)：`events` は各試行の最初のPC観測からの相対msと値、`gapMs` は上記符号の差、`before/after` は試行境界の診断。初回500 msのFAILを維持。同一候補・同一受信器でもhold条件とPC観測順が対応し、過去版との差を仮定しなくても逆転を再現できた。C5内部の解除・復帰順や時間差の発生箇所は未計測。
## Callなしの対照試験（同じC5候補）

前回の親入力12／16をそのままtriggerし、`Call(子) → OSC` を `子から移したペアAction → 同じOSC` に変更。子入力14／18は空にし、全設定中のCallが0件であることを確認した。hold以外のペア引数・OSC電文と宛先、Windows側のUDP受信スレッド・MIDI callback・F13監視、単調時計、条件順は前回と同じ。Wi-Fi、BLE両購読、OBS Ready。構成間は別の測定区間で、Callあり／なしを交互には測っていない。

| hold・ペア | Callあり：解放先着／5、差の中央値 ms | Callなし：解放先着／5、差の中央値［最小, 最大］ms |
|---|---|---|
| 250 ms MIDI | 5/5、+14.86 | 4/5、+29.52［−23.45, +35.51］ |
| 250 ms HID | 5/5、+19.17 | 5/5、+17.81［+8.29, +44.56］ |
| 500 ms MIDI | 0/5、−27.53 | 1/5、−24.97［−36.54, +8.06］ |
| 500 ms HID | 0/5、−30.70 | 0/5、−32.66［−34.86, −25.41］ |

差は後続OSC観測時刻−ペア解放観測時刻。単一Chainでも500 msでは10回中9回OSC先着で、逆転にCall／親復帰は必要ない。250 ms MIDIにもOSC先着が1回、500 ms MIDIにも解放先着が1回あり、holdだけで順序は決まらない。これはBLEペア＋後続Wi-Fi OSCのPC観測特性であり、ペアAction単体の内部不具合や、Callが分布に与える効果の有無までは判定しない。前回のFAILは変更しない。

[単一Chainの全20試行](pair-order-single-chain.jsonl)は前回と同形式。全60イベントが各試行の期待件数・値に一致し、欠落・重複なし。各境界の割当失敗0、両overflow増分0。試験後に開始時のCallあり500 ms設定を戻し、全設定の読戻し一致を確認した。
