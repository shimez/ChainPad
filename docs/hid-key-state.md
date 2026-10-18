# HIDキーの保持・解除仕様

複数のKEYにショートカットを割り当てたり、同時押しを使ったりするときの補足です。
ここでいう「KEY」はChainPadの物理ボタン、「HIDキー」はAやBなどPCへ送るキーを指します。

## KeyUpに修飾キーの指定がない理由

KeyDownで指定したCtrl／Shift／Alt／GUIは、対象のHIDキーとセットで記憶されます。
そのHIDキーのKeyUpで保持状態を解除するため、KeyUp側で修飾キーを指定する必要はありません。

例えば次の設定で、押したときにCtrl＋A、離したときに解除となります。

| イベント | 設定 |
|---|---|
| Press | キーボード → 押す（KeyDown）→ A、L Ctrlを選択 |
| Release | キーボード → 離す（KeyUp）→ A |

PressとReleaseのTransportは同じにしてください。保持状態はUSBとBLEで別々です。

## 同じ修飾キーを複数のKEYで使う場合

KEY1にCtrl＋A、KEY2にCtrl＋Bを設定すると、次のようになります。
ここでは両方ともPressでKeyDown、Releaseで対応するKeyUpを送るものとします。

| 操作 | 操作後の保持状態 |
|---|---|
| KEY1を押す | Ctrl＋A |
| KEY2も押す | Ctrl＋A＋B |
| KEY1を離す | Ctrl＋B |
| KEY2を離す | すべて解除 |

Ctrlは、まだCtrlを必要とするHIDキーが残っている間は維持されます。
これは解除の遅れではなく、残っているキーの操作を維持するための動作です。
L CtrlとR Ctrlなど、左右の修飾キーは別々に扱います。

## 修飾キーは同じTransportのキーボード入力全体に作用する

Ctrlを保持している間に、別のKEYから修飾キーなしのCを送っても、PCにはCtrl＋Cとして届きます。
各KEYのショートカットが独立したキーボードとして動くわけではありません。
独立して実行したいショートカットは、先の操作を解除してから次を実行してください。

## 同じHIDキーを重複して割り当てる場合の例外

保持状態は物理KEYごとではなく、HIDキーのUsageごとに管理します。
同じUsageを押している物理KEYの数は数えません。

例えばKEY1とKEY2の両方にCtrl＋Aを設定した場合、両方を押していても、
一方のKeyUpでAの保持状態が解除されます。他にCtrlを必要とするHIDキーがなければCtrlも解除されます。

同じAでも、片方をCtrl＋A、もう片方をShift＋Aとした場合は別々の保持状態にはなりません。
後から実行したAのKeyDownで、Aに対応する修飾キー設定が更新されます。

したがって「Ctrlを設定した物理KEYがすべて離されるまでCtrlを保持する」という説明は、
重複Usageがある場合には当てはまりません。
正確には、**保持中のHIDキーのうち、Ctrlを必要とするものが残っているか**で決まります。

## 押して離す（KeyDown→Up）との違い

KeyDown→Upは1回のActionで押下・解除を送るため、物理KEYのReleaseイベントを必要としません。
指定した押下時間の後、そのActionで追加したキーと修飾キーを戻します。
既にKeyDownで保持中の同じHIDキーに対するKeyDown→Upは、保持を壊さないようBusyとしてスキップされます。

音量・メディアのConsumer ControlはKeyboardとは別のレポートとFIFOを使用し、
修飾キーを指定しません。詳しくは[HID Consumer Control](consumer-control.md)を参照してください。

## 実装上の根拠

`src/keyboard_backend.cpp`の`held[usage]`と`modifiers[usage]`が保持状態です。
送信時には保持中のHIDキーに対応する修飾キーを合成します。
この文書は割当てと保持状態の仕様を説明するもので、送信遅延や受信OS・アプリの反応時間を保証するものではありません。
