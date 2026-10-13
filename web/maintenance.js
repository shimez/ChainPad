// Independent of Config loading: recovery remains available when that fails.
(()=>{
  const button=document.getElementById('factoryReset');
  const message=document.getElementById('factoryResetMessage');
  let busy=false;
  const accepted='初期化を受け付けました（消去完了ではありません）。自動再起動後に消去し、もう一度再起動します。ChainPad-Setupへ再接続してWi-Fiを設定してください。';
  button.onclick=async()=>{
    if(busy||!confirm('Wi-Fi・OSC・Action・Rotation・本体のBluetoothペアリング情報をすべて削除します。元に戻せません。Wi-Fi再設定とPC側の登録解除・再ペアリングが必要です。初期化しますか？'))return;
    busy=true;button.disabled=true;message.textContent='初期化要求を送信中…';
    try{
      const r=await fetch('/api/factory-reset',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({confirm:'factory-reset'})});
      const data=await r.json();if(!r.ok)throw Error(data.message||'初期化要求に失敗しました。');
      message.textContent=accepted;
    }catch(e){message.textContent='受付を確認できません: '+e.message+' 再接続して状態を確認してください。';busy=false;button.disabled=false;}
  };
  fetch('/api/factory-reset').then(r=>{if(!r.ok)throw Error();return r.json();}).then(data=>{
    if(busy)return;
    if(data.state==='accepted'){busy=true;button.disabled=true;message.textContent=accepted;}
    else if(data.state==='complete')message.textContent='前回のFactory Resetは完了しました。Wi-FiとBluetoothを再設定してください。';
    else if(data.state==='failed')message.textContent='Factory Resetは失敗または試行上限で停止しました。再実行するか、Web InstallerのErase deviceを使用してください。';
  }).catch(()=>{});
})();
