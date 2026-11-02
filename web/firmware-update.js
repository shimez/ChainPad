// Optional browser-only lookup. Never blocks device status or configuration work.
const FirmwareUpdate = (() => {
  const metadataUrl='https://shimez.github.io/ChainPad/build.json';
  const installerUrl='https://shimez.github.io/ChainPad/installer/';
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
        text.textContent=`新しいファームウェア v${info.version} が公開されています（現在：v${current}）。 `;
        const link=document.createElement('a');
        link.href=installerUrl;link.target='_blank';link.rel='noopener noreferrer';
        link.textContent='Web Installerを開く ↗';
        panel.replaceChildren(text,link);panel.hidden=false;
      }catch(_){
        // Offline, timeout, CORS and malformed metadata are intentionally silent.
      }finally{if(timer!==undefined)clearTimeout(timer);}
    }
    return {check};
  }
  return {create};
})();
