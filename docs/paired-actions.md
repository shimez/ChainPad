# Note On→Off / KeyDown→Up / All Notes On→Off

## 仕様

- MIDI `message: "noteOnOff"` / `"allNotesOnOff"`、HID `message: "keyDownUp"`を追加。それぞれ1 Actionとして数える。
- 3種類とも`holdMs`で待ち時間を指定する。0〜86400000 msの整数、省略時およびUI初期値は0ms。
  UI表示は「OnからOffまでの待ち時間」「DownからUpまでの待ち時間」。
  On/Downをtransport APIが受理した後に計時し、指定時間経過後にOff/Upを送る。
  送信待ちを含む実際の間隔は指定時間以上になり得る。loop全体をdelay()で止めない。
- MIDIは指定Channel/Note/VelocityでNote On、同じChannel/NoteでNote Off（velocity 0）。
  USB/BLE/Bothの既存routingを使用する。All Notesの対象にもこのNoteを含める。
- All Notes On→Offはdispatch時に対象集合をsnapshotし、**全対象On→待機→同じ全対象Off**。
  NoteごとのOn→Off交互送信ではない。重複除去、非active CW/CCW除外、元Actionに従うtransport選択は
  既存All Notesと共通。各transportのFIFOとタイマーは独立し、USB/BLE間の同時刻配送は保証しない。
- HIDは指定Key/ModifiersのDownと、その一時キー／修飾キーを除いたUp reportを送信。
  他Actionが保持中のキー・修飾キーを維持する。同じキーが既に保持中、または6KRO上限の場合はBusyとしてskipする。
- 単独Note On/Off・KeyDown/Upは残す。KEY、Encoder Push、Rotation DirectionのCW/CCWで利用可能。
- Action構造の配列容量・Config v2/Presetの外枠は変更しない。新message値を知らない旧Firmware/UIは
  新Actionを拒否するため、新Actionを含む設定のdowngrade互換はない。旧Actionのみの設定は引き続き読める。

## 送信順・バックプレッシャー

- 各transportのFIFOにOn/Off全件と待機marker分を一括予約してから追加する。
  容量不足ならそのtransportのbatch全体を拒否し既存backlogを保持。Noteごとの部分追加をしない。
- 既存Actionの`delayMs`領域を内部的にhold時間にも使うため、Action/Config固定配列サイズは増やさない。
  JSONでは従来の`delayMs`は非Waitで0のまま、pairの非0待機は`holdMs`として保存する。
  待機markerはMIDIで2 FIFO entries、HIDで1 entryを使い、transportへは送らない。
  MIDI FIFO容量は最大224対象のOn＋Off batch用に288→512 entries（2×MAX_TOTAL_ACTIONS＋64）。
  MIDI payload常駐は2transport合計で1344 bytes増加し、別途各backendにタイマー状態が加わる。
  C3の既存メモリ不足は解決しない。ConfigやAction数を縮小する変更ではない。
- 送信失敗時は既存FIFOの先頭から再試行し、送信済みOn/Downを再追加しない。
- 待機markerは該当protocol/transportのFIFOを止める。待機中に同FIFOへ追加された別Chainの出力も
  Off/Upの後になる。別protocol/transport、入力scan、WebUIは動作を継続する。
  待機中に同transportの他Noteを先行送信する独立タイマー方式ではない。
- Engineはpairを受理した後、該当FIFOが空になるまで後続Actionを実行しない。
  FIFO全体のdrainを待つ保守的な方式なので、他Chainが同FIFOへ追加した送信も待つ場合がある。
  Bothは各transportを独立処理し、両側の残りを待つ。未接続側は従来通りUnavailable。
- pairを含むChainは既存32実行枠を事前予約する。満杯ならprefixを一切送らずChain全体を拒否する。
  意図的なWaitがなくても送信待ちになるため、UI表示を「Chain待機中」に変更する。
- 完了はtransport APIが受理したことを意味し、受信アプリからのackではない。
  切断／epoch変更時のqueue破棄と、Panic/保存時のChain中止・HID解除は既存方針を継承する。
  切断や明示中止後にMIDI Offを再接続先へ自動再送する機能は追加しない。

## 検証

- host: 新messageの保存・再読込、velocity0拒否、Bothの2メッセージ順序、On成功後Offのみ再試行、
  HIDの既存heldキー／modifiers保持、同一heldキー拒否、FIFO残り1件でpair全体拒否、
  MIDI/HID送信待ち中の後続OSC停止とOff/Up後の再開を追加。
- 既存core/storage/rotation/capability host suite成功。
- Preset round-trip、BLE正規化、新message検証と既存Preview suite成功。
- 追加hold/batch実装のhostで、送信backpressure解消後からの待機、millis wrap、全対象On→hold→Offの順序、
  同一snapshot使用、3種のhold保存・再読込、不正hold値拒否を確認。既存host suiteも成功。
  最大224対象×2transportの全On448→全Off448、切断時の遅延Off破棄、HID中止時のresetも確認。
  hold/batch追加後のS3/C3/C5/C6 buildとimage検査も全環境成功。
  外部ログ: `timed-pairs-build.log`、`timed-pairs-host-final.log`。
  C5 application SHA256: `7383b31458bb5fafe4daeb991a7367c8646a7cec77daacf608e392c543b11302`。
  以下はhold/batch追加前のpair版の記録。
- ブラウザfixtureで両Message選択肢、HID修飾キー8個、Action summary、2つのpairが2 Actionsとして
  Preset検証されることを確認。
- 最終S3/C3/C5/C6 buildと`tests/check_image.py`は全環境成功。
  外部ログ: `paired-actions-build-final.log`、`paired-actions-host-final.log`。
  C5 application SHA256: `0494a25218e47eabe5b67bf61323f06d1adb2251bf702f5fdabd58c50ae92f7f`。
- ユーザーより動作確認済み・機能面良好との報告あり。UI文言には今後の調整希望がある。
  機種・待ち時間・計測ログ等の詳細は未採取のため、この報告を全機種の実機Regression合格へ拡張しない。

Encoder画面再構成と追加Action／待ち時間設定、ユーザーによるGetting Started追記を同じ変更の区切りとして記録する。
