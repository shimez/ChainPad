# Call Chain Phase 0：commit前レビュー

対象は `feature/call-chain`、ベース `19c6fad`。Phase 0はC6のメモリ予備検証のみ。通常設定での実Engineのサイズ・実行時間・スタックはPhase 2以降に再検証する。

| 実機検証 | 証拠 | 結論 |
|---|---|---|
| 1. v0.13.0基準 | [基準計測](evidence/call-chain-c6-baseline-20261122/README.md) | 稼働32をHTTP・シリアルで確認。最小の連続空きは32枠待機時ではなく224 Action保存時に観測 |
| 2. 保存内部 | [診断結果](evidence/call-chain-c6-save-internal-20261123/README.md) | デコード・一時ファイル書き込み中の `largest` は最小37,876 B |
| 3. 内部RAM予約 | [map・実機結果](evidence/call-chain-c6-reserve-20261123/README.md) | 7,936 Bの `.bss` 予約下で224 Action保存3回・WebUI・OBS取得に成功。保存中 `largest` 最小23,028 B。B案採用の予備根拠 |

## レビュー対象

- `platformio.ini`、`scripts/patch_websockets.py`：明示選択するC6診断環境2種、および同環境で通常C6と同じOBS依存パッチを許可。
- `src/save_mem.{h,cpp}`、`src/config_store.cpp`、`src/main.cpp`：固定長の計測表、保存時の採取点、保存前後の `v`／`V` 操作。サンプル中に動的確保・ログ出力はしない。
- この索引と上記3ディレクトリの計測記録。設定バックアップ・実機イメージ・生のAPIレスポンス・PC側作業ディレクトリは対象外。

## 分離とログの確認

- 通常4機種の `default_envs` に追加なし。保存計測マクロは診断環境だけ、予約マクロはその派生環境だけで定義。無効時は採取マクロが引数を評価しないno-op、シリアル操作・計測関数・配列は条件コンパイル外。
- 通常 `pio run -e xiao_esp32c6` が成功。通常C6のELF定義シンボルに `chimera::savemem::`／`callChainReserve` はなく、配布用アプリbinにも診断固有の `SAVE armed`／`SAVE %s n=`／`RESERVE bytes=` はない。予約診断環境のELF／binでは同じ検査ですべて存在。実バイナリ比較はC6が対象。
- 証拠3ディレクトリの全12ファイルについて、出力項目と認証情報・生IP/MAC・ローカルユーザーパス等のパターンを点検。公開候補に認証値・設定JSON・OBSシーン名なし。シリアル内のIPは `[address]` に置換。mapおよびRAMログの16進数はデバイス上のメモリアドレス、READMEのSHA256は照合用ダイジェスト。
