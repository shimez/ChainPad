// Optional browser-only lookup. Never blocks device status or configuration work.
const FirmwareUpdate = (() => {
  const metadataUrl='https://shimez.github.io/ChainPad/build.json';
  const installerUrl='https://shimez.github.io/ChainPad/installer/';
  const releasesUrl='https://github.com/shimez/ChainPad/releases';
  const maxBytes=256*1024;
  const families={
    'XIAO ESP32S3 / ChainOSCPad PCB':'ESP32-S3',
    'XIAO ESP32C6 / ChainOSCPad PCB':'ESP32-C6',
    'XIAO ESP32C5 / ChainOSCPad PCB':'ESP32-C5'
  };
  function version(value){
    if(typeof value!=='string'||! /^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$/.test(value))return null;
    const parts=value.split('.').map(Number);
    return parts.every(Number.isSafeInteger)?parts:null;
  }
  function newer(latest,current){
    const a=version(latest),b=version(current);
    if(!a||!b)return false;
    for(let i=0;i<3;i++)if(a[i]!==b[i])return a[i]>b[i];
    return false;
  }
  function element(tag,text){const node=document.createElement(tag);if(text!==undefined)node.textContent=text;return node;}
  function external(url,text){const link=element('a',text);link.href=url;link.target='_blank';link.rel='noopener noreferrer';return link;}
  function records(data,info,current){
    if(!data||data.schemaVersion!==1||data.firmwareVersion!==info.version||data.firmwareCommit!==info.commit||
      !Array.isArray(data.releases)||!data.releases.length||data.releases.length>256)throw Error('history metadata');
    const seen=new Set();
    for(const entry of data.releases){
      if(!entry||!version(entry.version)||seen.has(entry.version))throw Error('history version');
      seen.add(entry.version);
      for(const key of ['features','improvements']){
        if(!Array.isArray(entry[key])||entry[key].length>64)throw Error('history items');
        for(const text of entry[key])if(typeof text!=='string'||!text.trim()||[...text].length>1024||
          /[\u0000-\u001f\u007f\ud800-\udfff]/u.test(text))throw Error('history text');
      }
    }
    if(!seen.has(info.version))throw Error('missing latest');
    return data.releases.filter(entry=>newer(entry.version,current)&&!newer(entry.version,info.version))
      .sort((a,b)=>newer(a.version,b.version)?-1:1);
  }
  async function readHistory(response,bytes){
    if(!response.ok||!response.body)throw Error('history response');
    const reader=response.body.getReader(),chunks=[];let size=0;
    try{
      while(true){const {done,value}=await reader.read();if(done)break;
        size+=value.byteLength;if(size>maxBytes||size>bytes)throw Error('history size');chunks.push(value);
      }
    }catch(error){void reader.cancel().catch(()=>{});throw error;}finally{reader.releaseLock();}
    if(size!==bytes)throw Error('history size');
    const payload=new Uint8Array(size);let offset=0;
    for(const chunk of chunks){payload.set(chunk,offset);offset+=chunk.byteLength;}
    return JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(payload));
  }
  function renderRelease(entry){
    const block=element('section');block.className='firmware-release';
    const heading=element('h3');heading.append(external(`${releasesUrl}/tag/v${entry.version}`,`v${entry.version} ↗`));block.append(heading);
    for(const [key,label] of [['features','追加機能'],['improvements','修正・改善']]){
      if(!entry[key].length)continue;
      block.append(element('h4',label));const list=element('ul');
      for(const text of entry[key])list.append(element('li',text));block.append(list);
    }
    if(!entry.features.length&&!entry.improvements.length)block.append(element('p','追加機能・修正改善の記載はありません。'));
    return block;
  }
  async function loadHistory(container,info,current){
    let timer;const controller=new AbortController();
    try{
      const ref=info.changes;
      if(!ref||ref.path!=='changes.json'||ref.schemaVersion!==1||!Number.isInteger(ref.bytes)||ref.bytes<=0||ref.bytes>maxBytes||
        typeof ref.sha256!=='string'||!/^[0-9a-f]{64}$/.test(ref.sha256)||typeof info.commit!=='string'||!/^[0-9a-f]{40}$/.test(info.commit))throw Error('history reference');
      const timeout=new Promise((_,reject)=>{timer=setTimeout(()=>{controller.abort();reject(Error('history timeout'));},5000);});
      const request=async()=>readHistory(await fetch(new URL('changes.json',metadataUrl).href,
        {mode:'cors',credentials:'omit',cache:'no-store',signal:controller.signal}),ref.bytes);
      const entries=records(await Promise.race([request(),timeout]),info,current);
      const details=element('details'),summary=element('summary','追加・改善された内容を見る');
      details.append(summary);
      for(const entry of entries.slice(0,3))details.append(renderRelease(entry));
      if(entries.length>3){
        const more=element('details');more.className='firmware-more';more.append(element('summary',`ほか${entries.length-3}バージョン分を表示`));
        more.addEventListener('toggle',()=>{if(!more.open||more.dataset.loaded)return;
          more.dataset.loaded='true';for(const entry of entries.slice(3))more.append(renderRelease(entry));});
        details.append(more);
      }
      container.replaceChildren(details);
    }catch(_){
      container.replaceChildren(element('span','変更履歴を取得できませんでした。 '),external(releasesUrl,'GitHub Releasesで確認 ↗'));
    }finally{if(timer!==undefined)clearTimeout(timer);}
  }
  function create(panel){
    let started=false;
    async function check(status){
      let timer;
      try{
        if(started||!panel||!status||!version(status.version)||
          !Object.prototype.hasOwnProperty.call(families,status.hardware))return;
        started=true;
        const current=status.version,family=families[status.hardware];
        const controller=new AbortController();
        timer=setTimeout(()=>controller.abort(),5000);
        const response=await fetch(metadataUrl,{mode:'cors',credentials:'omit',cache:'no-store',signal:controller.signal});
        if(!response.ok)return;
        const info=await response.json();
        if(!info||!newer(info.version,current)||!Array.isArray(info.files)||
          !info.files.some(file=>file&&file.chipFamily===family))return;
        const text=document.createElement('span');
        text.setAttribute('role','status');
        text.textContent=`新しいファームウェア v${info.version} が公開されています（現在：v${current}）。 `;
        const link=document.createElement('a');
        link.href=installerUrl;link.target='_blank';link.rel='noopener noreferrer';
        link.textContent='Web Installerを開く ↗';
        panel.replaceChildren(text,link);panel.hidden=false;
        const history=element('div','変更履歴を読み込み中…');history.className='firmware-history';panel.append(history);
        void loadHistory(history,info,current);
      }catch(_){
        // Offline, timeout, CORS and malformed metadata are intentionally silent.
      }finally{if(timer!==undefined)clearTimeout(timer);}
    }
    return {check};
  }
  return {create};
})();
