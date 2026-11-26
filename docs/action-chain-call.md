# Call Chain（v0.14.0以降）

Call Chainは、別の入力のAction Chainを呼び出す1枠のActionです。呼び出した先（子）が終わると、呼び出し元（親）の次のActionに戻ります。子の操作をコピーするわけではないので、子を編集すれば、その子を呼ぶすべての親に反映されます。

## 設定例：同じ手順を2回使う

1. `KEY2 Press` に「OSC `/demo/step` → Wait 250 ms」を登録します。
2. `KEY1 Press` に回数2のLoopを作ります。Loop内にActionを追加し、種別を `Call Chain`、呼び出し先を `KEY2 Press` にします。Loopの後にOSC `/demo/end` を置き、「保存・適用」します。
3. KEY1を押すと、子のOSCとWaitを2回実行してから親の `/demo/end` に進みます。KEY2を押す必要はありません。

「呼び出し先（Call target）」はキー、Encoder Push、回転に分かれています。`Empty（空）` も選べます。空の子は何も出力せず親に戻ります。カードを閉じても、要約の `Call → KEY2 Press` で呼び出し先を確認できます。

## 編集・保存・Import

種別と呼び出し先の変更は、その場で編集内容に反映されます。自分自身は選択肢に表示されません。上の例でさらにKEY2からKEY1を呼ぶと、選択値を保持したまま循環経路と「循環を解消しないと保存できません。」が赤字で表示されます。「完了」はカードを閉じる操作です。循環の原因となるCallを変更または削除するまで、本体へは保存できません。

Key Presetは対象キーのPress／Release一組を移しますが、Call先は**絶対ID**のままです。上のKEY1をKEY5にImportしても、呼ぶのはKEY2 Pressです。子の内容も含めて移すには全体設定を使います。無効なFull／Key Import（範囲外ID、貼付後の直接・間接循環など）は、通常編集とは異なり、編集中の内容を置き換えません。

## 保存形式と実行上限

`{"protocol":"callChain","delayMs":0,"targetInput":2}` は `KEY2 Press` を呼ぶ例です。`targetInput` は次の整数値で、Transportは指定しません。

| ID | 呼び出し先 |
|---|---|
| 0～23 | KEY1～KEY12。各キーの偶数IDがPress、次の奇数IDがRelease |
| 24／25 | Encoder PushのPress／Release |
| 26／27 | Encoder CW／CCWのAction Chain（Rotation Valueの出力定義ではない） |

全28入力の呼び出し関係に循環を許さないため、最長経路は27回のCallです。Press／Release共有16枠、CW／CCW各8枠、全体224枠の保存上限は変わりません。親から子を呼んでも同時実行枠をもう1つ消費することはありません。同じ子を別入力から起動した実行は独立です。

32個の実行枠が満杯のとき、Callを含む新規実行は原則として先頭の出力前に拒否します。枠を必要とするActionより前の直線部分で「他のChainを中断」に達する場合は例外です。子の中のStopは親の受付判定の例外にはなりません。実行中に子がStopへ達した場合は、**親子を含む自分の実行は継続**し、他の実行を止めます。Panicは親子をまとめて中止しHID解除を要求しますが、送信済みの操作は取り消さず、MIDI Note Offを自動生成しません。

Full v2／Key v1とConfig schema v2は維持します。v0.13.0で使えた設定は引き続き利用できます。Call入り設定は旧Firmware／旧Configuratorでは扱えないので、旧版へ戻す際にはその版で取得したバックアップを使ってください。
