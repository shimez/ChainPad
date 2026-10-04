# All Notes（0.5.0）

通常のMIDI Actionとして `message: "allNotesOn"` または `"allNotesOff"` を使用します。共通の `protocol: "midi", delayMs: 0` に加え、Onだけ `value: 1..127` を保存します。自身のChannel/Note/Transportは持ちません。Preset・全体設定も同じ構造です。

実行時のconfigから全28 EventのNote On/Offを収集し、送信先別の512-byte固定ビット集合でChannel＋Noteを重複排除します。CCとAll Notesは無視します。Bothは両方へ登録します。対象は発音履歴や押下状態ではなく設定内容です。対象なしは成功扱いの無操作、対象が全て未接続ならUnavailableです。

USB/BLEそれぞれのFIFOへ個別のNote On/Offを登録します。バッチ内はChannel→Noteの昇順で、Chain中の他のActionとの登録順は保持します。Note OffのVelocityは0。CCは生成しません。片方だけ接続中でもその側は動作し、少なくとも一方が登録成功ならAcceptedです。

最大224対象＋64件の余裕を持つ288件のFIFOを各送信先に用意します。全バッチ分の空きがなければ、その側のバッチを全体拒否しoverflowカウンタを増やします（既存キューは保持）。通常Actionのoverflow処理は従来どおりキュー破棄です。送信失敗時は先頭から再試行し、送信済みメッセージを再送しません。中止・保存・再接続は未送信キューを破棄します。

## 検証

ホストテストPASS：重複Note、異なるChannel、USB/BLE/Both、Encoder側Note、CC/自身除外、片側未接続、最大224対象の送信失敗・再試行・後続Action順序、容量不足時の部分登録防止、On→Wait→Off、対象なし、LittleFS往復、Velocity検証。Presetの往復・不正Velocity拒否もPASS。

実機では重複Noteを設定し、All Notes On→Wait→OffをReceiverで確認してください。各送信先のChannel＋Noteが各1回であること、指定Velocity、CCなし、別キーの同時操作、再起動後の保存設定を確認します。実機確認は未実施です。
