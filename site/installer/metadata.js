// Optional display data only. ESP Web Tools owns manifest loading and flashing.
(() => {
  const releasesUrl='https://github.com/shimez/ChainPad/releases';
  const maxHistoryBytes=256*1024;
  const build=document.getElementById('build'),downloads=document.getElementById('downloads');
  const summary=document.getElementById('historySummary'),content=document.getElementById('historyContent');
  function element(tag,text){const node=document.createElement(tag);if(text!==undefined)node.textContent=text;return node;}
  function link(url,text){const node=element('a',text);node.href=url;node.target='_blank';node.rel='noopener noreferrer';return node;}
  function fallback(text){content.replaceChildren(element('p',text),link(releasesUrl,'GitHub Releasesで確認 ↗'));}
  function validVersion(value){return typeof value==='string'&&/^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$/.test(value)&&value.split('.').every(part=>Number.isSafeInteger(Number(part)));}
  async function timed(task){
    const controller=new AbortController();let timer;
    try{return await Promise.race([task(controller.signal),new Promise((_,reject)=>{
      timer=setTimeout(()=>{controller.abort();reject(Error('timeout'));},5000);
    })]);}finally{clearTimeout(timer);}
  }
  async function bytes(url,signal,limit,expected){
    const response=await fetch(url,{credentials:'omit',cache:'no-store',signal});
    if(!response.ok||!response.body)throw Error('response');
    const reader=response.body.getReader(),chunks=[];let size=0;
    try{
      while(true){const {done,value}=await reader.read();if(done)break;
        size+=value.byteLength;if(size>limit||(expected!==undefined&&size>expected))throw Error('size');chunks.push(value);
      }
    }catch(error){void reader.cancel().catch(()=>{});throw error;}finally{reader.releaseLock();}
    if(expected!==undefined&&size!==expected)throw Error('size');
    const result=new Uint8Array(size);let offset=0;
    for(const chunk of chunks){result.set(chunk,offset);offset+=chunk.byteLength;}
    return result;
  }
  function decode(data){return JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(data));}
  function latest(data,info){
    if(!data||data.schemaVersion!==1||data.firmwareVersion!==info.version||data.firmwareCommit!==info.commit||
      !Array.isArray(data.releases)||!data.releases.length||data.releases.length>256)throw Error('history metadata');
    const seen=new Set();let selected;
    for(const entry of data.releases){
      if(!entry||!validVersion(entry.version)||seen.has(entry.version))throw Error('history version');
      seen.add(entry.version);
      for(const key of ['features','improvements']){
        if(!Array.isArray(entry[key])||entry[key].length>64)throw Error('history items');
        for(const text of entry[key])if(typeof text!=='string'||!text.trim()||[...text].length>1024||
          /[\u0000-\u001f\u007f\ud800-\udfff]/u.test(text))throw Error('history text');
      }
      if(entry.version===info.version)selected=entry;
    }
    if(!selected)throw Error('missing latest');
    return selected;
  }
  async function history(info){
    summary.textContent=`公開版 v${info.version} の変更内容`;
    if(!Object.prototype.hasOwnProperty.call(info,'changes')){
      fallback('この公開版の変更履歴はGitHub Releasesで確認できます。');return;
    }
    try{
      const ref=info.changes;
      if(!ref||ref.path!=='changes.json'||ref.schemaVersion!==1||!Number.isInteger(ref.bytes)||ref.bytes<=0||ref.bytes>maxHistoryBytes||
        typeof ref.sha256!=='string'||!/^[0-9a-f]{64}$/.test(ref.sha256))throw Error('history reference');
      const data=await timed(async signal=>{
        const payload=await bytes('../changes.json',signal,maxHistoryBytes,ref.bytes);
        // Pages is HTTPS; localhost previews also support Web Crypto. No dependency.
        const hash=new Uint8Array(await crypto.subtle.digest('SHA-256',payload));
        if(Array.from(hash,byte=>byte.toString(16).padStart(2,'0')).join('')!==ref.sha256)throw Error('checksum');
        return decode(payload);
      });
      const entry=latest(data,info),nodes=[];
      for(const [key,label] of [['features','追加機能'],['improvements','修正・改善']]){
        if(!entry[key].length)continue;
        nodes.push(element('h3',label));const list=element('ul');
        for(const text of entry[key])list.append(element('li',text));nodes.push(list);
      }
      if(!nodes.length)nodes.push(element('p','追加機能・修正改善の記載はありません。'));
      nodes.push(link(`${releasesUrl}/tag/v${info.version}`,'このバージョンのGitHub Releaseを開く ↗'));
      content.replaceChildren(...nodes);
    }catch(_){fallback('変更履歴を取得できませんでした。');}
  }
  return (async()=>{
    let info;
    try{
      info=decode(await timed(signal=>bytes('../build.json',signal,64*1024)));
      if(!info||!validVersion(info.version)||typeof info.commit!=='string'||!/^[0-9a-f]{40}$/.test(info.commit)||
        !Array.isArray(info.files)||!info.files.length||info.files.length>3)throw Error('build metadata');
      const seen=new Set(),links=[];
      for(const file of info.files){
        if(!file||!['ESP32-S3','ESP32-C5','ESP32-C6'].includes(file.chipFamily)||seen.has(file.chipFamily)||
          file.path!==`firmware/ChainPad-${file.chipFamily}.bin`)throw Error('download path');
        seen.add(file.chipFamily);const a=element('a',file.chipFamily);a.href='../'+file.path;a.download='';links.push(a);
      }
      build.textContent=`${info.version} · ${info.commit.slice(0,7)}`;downloads.replaceChildren(...links);
    }catch(_){
      build.textContent='ビルド情報を取得できませんでした。再読み込みしてください。';
      fallback('公開版を確認できないため、変更履歴を表示できません。');return;
    }
    await history(info);
  })();
})();
