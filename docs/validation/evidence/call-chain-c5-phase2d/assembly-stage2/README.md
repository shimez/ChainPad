# C5 第2段階：合格

採用対象はInvocation・設定GETのJsonDocument・OBS assemblyの3領域をC5でPSRAMに配置した通常版。`assembly-normal.json` は27段Call、親子Loop、32枠のStop/Panic、BLEペア解放と親OSCのPC受信順、実OBSとGET＋OSC同時試験の結果。`ac-rebuilt.json` は移動前のA＋C構成の比較用再ビルドで、採用対象ではない。到着間隔の解釈は `osc-get-latency.md` を参照。

`frames.json` は16,384 Bの単一／3分割（5,000＋7,000＋4,384 B）と先頭断片後の切断・再接続の実機記録。末尾側のシーン名32件を順序込みで照合し、再接続後も別の32件が一致。各区間で割当失敗増分0。別途、実OBSの8シーンとWebUIの選択欄が個別に一致した。

assembly確保結果だけをNULLとした注入版では `base=0x0`、OBS `Disabled`・`lastError=-9`、ワーカー未生成。PCでBLE MIDI・HID・親OSCを受信し、WebUI保存・再読込も成功。各実機試験後に元v0.8.0のアプリ・LittleFSのdigest、設定・capabilities・ready・稼働0を照合した。注入版・実設定・バックアップは含まない。
