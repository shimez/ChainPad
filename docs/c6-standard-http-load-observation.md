# C6 Standard：高負荷時の一時的なHTTP応答停止

記録日：2026-10-12。ユーザー実機報告と提供Serial診断に基づく。

**扱い：高負荷時の一時的なHTTP応答停止として記録。現時点では改修対象としない。**

## 条件・観測

- XIAO ESP32C6 / ChainOSCPad PCB、Standard容量。
- 208／224 Actions、最大共有pool16／16、最大Event16。
- 保存済み設定86,653 bytes、Rotation Outputs4／16、storage state=ready。
- Config82,648 bytesを内部RAMへ配置、PSRAMなし。
- WebUIの連続再読込とキー連打を同時実行。
- WebUIが一時的に応答しなくなったが、負荷停止後に復帰した。
- ユーザー報告では再起動・入力overflow・通信queue overflowは観測されず、RAM空きも回復した。

## 提供された診断値（bytes）

| uptime ms | internal free | minFree | largest | allocated | loopStackMin |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 111,493 | 116,960 | 90,988 | 83,956 | 153,404 | 5,376 |
| 135,365 | 121,604 | 90,988 | 98,292 | 148,776 | 5,376 |
| 145,621 | 121,928 | 79,632 | 100,340 | 148,564 | 5,376 |

各snapshotでtransportRetries=0、transportOverflows=0、inputOverflows=0、running=0、rejected=0。
提示された起動情報ではinputsReady=1、Wi-Fi mode=3／status=3、STA=192.168.0.11。
LittleFSはtotal851,968、used98,304、free753,664、active86,653、pending=-1。
後続2snapshotのRotation pending=0、accepted/failed各経路0。

## 解釈の範囲

- free／largestは後続snapshotで増えており、負荷停止後の回復という報告と整合する。
- minFreeは起動後の累積最小値。79,632 bytesを記録した正確な操作・時刻はこのsnapshot列だけでは特定しない。
- HTTP停止の継続時間、client側のエラー、停止中のstack／allocation failure詳細は未取得。
  overflowが0であることを、全メモリ確保成功や全Actionの無遅延・配送成功の証明とは扱わない。
- HTTP停止の直接原因、RAMリークの有無、長時間運用の安定性はこの記録だけで断定しない。
- この観測をC6の公称最大224 Actionsの実機合格や他機種の検証結果へ一般化しない。

本記録に伴うfirmware／WebUIの改修、ビルド、実機書込みは行わない。
