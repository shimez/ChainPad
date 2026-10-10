# Getting Started

<!-- ここから本文を編集してください。執筆方法は docs/getting-started-authoring.md を参照。 -->

## 1. PCとの接続について

ChainPadでは、使用する機能によって必要な接続が異なります。

| 機能              | 接続      |
| ----------------- | --------- |
| OSC               | Wi-Fi     |
| MIDI              | Bluetooth |
| HID（キーボード） | Bluetooth |

※XIAO ESP32S3の場合はMIDI/HIDをUSB接続で利用することも可能です

## 2. Wi-Fiへの接続

OSC送信機能を利用するには、VRChatを利用しているPCと同じLANにChainPadを接続する必要があります。

> ChainPadは2.4 GHz帯のWi-Fiアクセスポイントに接続できます。<br>
> XIAO ESP32C5を使用している場合は、5 GHz帯のWi-Fiにも接続できます。

ChainPadにUSBケーブルを接続して電源を供給すると（PC接続もしくはACアダプタ接続）、ChainPadのエンコーダーのLEDがゆっくりと点滅します。<br>
この状態のChainPadは自身が無線APとして起動しています。
スマホやPCなどをChainPadに接続してください。

- AP名：`ChainPad-Setup`
- パスワード：`chimera-pad`

接続するとChainPadのWi-Fi設定画面が表示されます。
Wi-Fi設定画面が自動表示されない場合は<http://192.168.4.1/> を開いてください。

ご自宅で利用しているWi-FiのSSID/パスワードを入力し「保存して再起動」ボタンを押してください。

ChainPadがWi-Fiに接続されるとエンコーダーのLEDが点灯状態となります。

## 3. Bluetoothのペアリング

MIDI送信機能、HID送信機能を利用するにはBluetoothのペアリングを行う必要があります。

Bluetoothで『ChainPad Chimera』とペアリングしてください。

※1 XIAO ESP32S3の場合はMIDI/HIDをUSB接続で利用することも可能です<br>
※2 「すべてのデバイスを表示」を押さないと『ChainPad Chimera』が表示されない場合があります<br>
※3 「すべてのデバイスを表示」を押しても『ChainPad Chimera』が表示されない場合、ChainPadに接続しているUSBケーブルを抜き差ししてください

## 4. ブラウザで設定画面を表示する

ブラウザで<http://chainpad.local>にアクセスすることで、ChainPadの設定画面が表示されます。

Windowsの場合 chainpad.local でのアクセスができない場合があります。
その場合、以下の手順でChainPadのIPアドレスを確認し、 http://IPアドレス でアクセスしてください。

1. スタートメニューから「Windows PowerShell」を起動する
2. 起動したPowerShellで「Resolve-DnsName chainpad.local」コマンドを投入する
3. 表示された IPAddress を確認する

上記手順でChainPadのアドレスが確認できない場合はスマートフォンで<http://chainpad.local>にアクセスし、一番上のカード（枠）に表示されているIPアドレスを確認してください。

## 5. 設定を行う

### 5-1. OSC送信設定

OSCを用いてVRChatを操作する場合は、OSC送信先にVRChatを実行するPCのIPアドレスを入力してください。

VRChatで使用する場合、OSC Portは通常変更する必要はありません

### 5-2. Keyへのアクションの登録

設定したいKEYを選択し、「+ Add Action」ボタンを押すことでアクションを追加可能です。

#### (1)OSC

Actionで「OSC」を選ぶと、OSCメッセージを送信する設定が可能です。

OSC Address、送信する値のType、Valueを入力してください

#### (2)MIDI

Actionで「MIDI」を選ぶと、MIDIメッセージを送信する設定が可能です。

メッセージの種類（Note On/OffやControl Changeなど）、Channel、Note、Velocityなどを設定してください。

「All Notes → Note On」と「All Notes → Note Off」では、ChainPadの設定内の有効なMIDI NoteをまとめてOn/Offできます。<br>
Noteが意図せずOnのままになった場合の緊急回避ボタンなどに利用できます。

#### (3)HID

Actionで「HID」を選ぶと、キーボードの操作を行う設定が可能です。

Messageで「KeyDown（キーを押す）」「KeyUp（キーを離す）」などを選択し、Keyで入力するキーを選択してください。

チェックボックスで修飾キー（左右のCtrl/Shift/Alt/GUIキー）などとの同時押しも設定可能です。

> OBSなど一部ソフトではKeyDown単体では入力したとみなさず、同じキーのKeyUpイベントが発生して初めてホットキーが動作します。その場合はKeyDownとKeyUpをセットで設定してください。

### 5-3. Encoder

#### (1)回転方向モード

回転方向モードは回した方向に応じてアクションを行うモードです。回転量は考慮されません。

回転方向モードでは、時計回り・反時計回りそれぞれに8件のアクションを登録可能です。<br>
アクションに登録できる内容はKeyと共通です。

#### (2)回転量モード

回転量モードは回転量に応じてOSCメッセージ/MIDI CCの値を増減させるモードです。<br>
1つのエンコーダーを回転させるだけで複数のOSCパラメータ/MIDI CCの値を同時に変化させることが可能です。

### 5-4. 設定の保存

Web設定画面を下にスクロールすると表示される「保存・適用」ボタンを押すことで設定が保存され、設定が有効となります。

## 6. PC側の各種設定

### 6-1. OSC設定

VRChatでOSCを利用するには設定の有効化が必要です。

1. VRChatを起動し、アクションメニューを開く（デスクトップモードではRキー、QuestコントローラーではBボタン長押し）
2. 「オプション」→「OSC」を選択する
3. 「有効」となっている場合は操作不要。「無効」となっている場合、「有効」に変更する

### 6-2. MIDI設定

VRChatでは、MIDI対応ワールドでPCに接続されているMIDIデバイスのうち1台が利用可能です。<br>
PCに複数のMIDIデバイスを接続している場合は、VRChatの起動オプションを設定することでChainPadが利用可能となります。

1. Steamのライブラリ画面でVRChatを選択
2. 歯車マークをクリックして「プロパティ」を選択
3. 表示されたプロパティ画面の「一般」にある起動オプションの入力欄に「--midi=chainpad」を追加

### 6-3. HID設定

HIDを利用して操作する例として、OBSがあります。

OBSのホットキーはメニューの「ファイル」→「設定」を選択して表示される設定画面の「ホットキー」の項目を選択することで設定可能です。

ChainiPadに設定したキーと同じキーをOBS側に設定することで、ChainPadからOBSの操作が可能となります。

> OBSのホットキーはKeyDownとKeyUp両方のイベントが発生することで動作します。KeyDownだけでは動作しない点に注意してください。<br>
> OBSのホットキー設定では実際にキーを入力する必要があるため、あらかじめChainPadにF13などのKeyDown+KeyUpを設定し、そのキーを押すことでホットキー設定を行うことをおすすめします。

## 7. ファームウェアを書き換える（必要な場合のみ）

ChainPadのアップデートがあった場合やはファームウェアの更新が可能です。<br>
また、ChainPadと共通の基板を利用するChainOSCPadシリーズのファームウェアへの書き換えも可能です。

1. Web Installerのページにアクセスし、「Connect」や「Install」のボタンを押す
2. シリアルポートの一覧が表示されるため、「USB JTAG/serial debug unit」と書かれたポートを選択する
3. 画面の指示に従い、ファームウェアを書き込む
4. ファームウェア書き換え後はBluetooth接続が不安定になる場合があります。PCのBluetooth設定で『ChainPad Chimera』を一旦削除し、改めて接続しなおしてください。

※1「USB JTAG/serial debug unit」と書かれたシリアルポートが複数ある場合、ChainPadを抜き差しし、抜いている間表示が消えたポートを選択してください<br>
※2「USB JTAG/serial debug unit」と書かれたシリアルポートがない場合や、書き込みに失敗した場合は、XIAOのBOOTボタンを押しながらUSBケーブルを接続してください。
