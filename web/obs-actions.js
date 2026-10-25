// Scene discovery is input assistance only. Never writes an Action or Config.
const ObsActionsUI = (() => {
  const operations = [['setCurrentProgramScene','シーン切り替え'],['startStream','配信開始'],['stopStream','配信停止'],['startRecord','録画開始'],['stopRecord','録画停止'],['setInputMute','ミュート設定'],['toggleInputMute','ミュート切り替え']];
  function defaults(operation='setCurrentProgramScene') {
    return {protocol:'obs',delayMs:0,operation,...(operation==='setCurrentProgramScene'?{sceneName:''}:operation==='setInputMute'?{inputName:'',inputMuted:true}:operation==='toggleInputMute'?{inputName:''}:{})};
  }
  function validName(name) {
    return typeof name==='string' && name.length>0 && new TextEncoder().encode(name).length<=192 &&
      !/[\x00-\x1f\x7f]/.test(name) && ![...name].some(c=>{const n=c.codePointAt(0);return n>=0xd800&&n<=0xdfff;});
  }
  function create() {
    let enabled=false, initialized=false, busy=false, ready=false, generation=null, freshUntil=0;
    let state={state:'unavailable',complete:false,scenes:[]};
    const listeners=new Set();
    const emit=()=>listeners.forEach(fn=>fn());
    const snapshot=()=>({...state,complete:state.complete && Date.now()<freshUntil});
    async function request(refresh=false) {
      if(!enabled||busy||document.hidden)return;
      busy=true; if(refresh)state={...state,state:'loading',complete:false}; emit();
      let newConnection=false;
      try {
        const response=await fetch('/api/obs/scenes'+(refresh?'/refresh':''),{method:refresh?'POST':'GET',cache:'no-store'});
        const data=await response.json();
        if(!response.ok || !Array.isArray(data.scenes))throw Error('scene request failed');
        newConnection=!refresh && generation!==null && generation!==data.generation;
        generation=data.generation;state=data;freshUntil=Date.now()+Math.max(0,60000-(data.ageMs??60000));
      } catch {state={...state,state:'error',complete:false};}
      finally {busy=false;emit();if(newConnection&&ready)void request(true);}
    }
    function init(capabilities) {
      enabled=capabilities.obsActions===true;
      if(!enabled){state={state:'unsupported',complete:false,scenes:[]};emit();return;}
      if(!initialized){initialized=true;void request(true);}
    }
    function tick(connection) {
      const nowReady=connection?.connectionState==='Ready';
      const becameReady=nowReady&&!ready; ready=nowReady;
      if(!nowReady){state={...state,complete:false};emit();}
      if(!enabled)return;
      // The existing visible-page status poll drives cache polling, no second timer.
      if(becameReady){void request(true);return;}
      void request(false);
    }
    return {init,tick,refresh:()=>request(true),snapshot,busy:()=>busy,
      subscribe(fn){listeners.clear();listeners.add(fn);},clear(){listeners.clear();},
      failed(){ready=false;state={...state,state:'error',complete:false};emit();}};
  }
  function render(a, fields, control, changed, rerender, capabilities, scenes) {
    const supported=capabilities.obsActions===true;
    const note=document.createElement('p');note.className='hint';note.style.gridColumn='1 / -1';
    note.textContent=supported?'OBS接続設定を共通利用します。':'この機種ではOBS Action非対応です。設定は保持・編集できますが、実行時はスキップします。';
    fields.append(control('OBS操作',a.operation,operations,op=>{
      if(op===a.operation)return;
      const next=defaults(op);
      if('inputName' in next && 'inputName' in a)next.inputName=a.inputName;
      for(const key of Object.keys(a))delete a[key];Object.assign(a,next);rerender();
    }));
    const key=a.operation==='setCurrentProgramScene'?'sceneName':['setInputMute','toggleInputMute'].includes(a.operation)?'inputName':null;
    if(key){
      const field=control(key==='sceneName'?'シーン名':'入力名',a[key],null,value=>{a[key]=value;update();});
      const input=field.lastChild, warning=document.createElement('small');
      input.oninput=()=>{a[key]=input.value;changed();update();};
      let list,refresh;
      if(key==='sceneName'){
        list=document.createElement('datalist');list.id='obs-scene-candidates';input.setAttribute('list',list.id);
        input.setAttribute('autocomplete','off');field.append(list);
        refresh=document.createElement('button');refresh.type='button';refresh.textContent='シーン一覧を更新';refresh.style.alignSelf='start';
        refresh.onclick=()=>scenes.refresh();
        scenes.subscribe(()=>{refresh.disabled=!supported||scenes.busy();update();});
        refresh.disabled=!supported||scenes.busy();
      }
      function update(){
        const valid=validName(input.value);input.setCustomValidity(valid?'':'UTF-8で1〜192 bytes（制御文字不可）を指定してください。');
        let message=valid?'':'対象名はUTF-8で1〜192 bytes（制御文字不可）です。',error=!valid;
        if(list){
          const cache=scenes.snapshot();list.replaceChildren();
          for(const name of cache.scenes){const option=document.createElement('option');option.value=name;list.append(option);}
          if(valid){
            const complete=cache.state==='ready'&&cache.complete===true;
            error=complete&&!cache.scenes.includes(input.value);
            message=error?'取得したシーン一覧にありません。手入力のまま保存できます。':complete?'候補選択・手入力のどちらも可能です。':
              !supported?'一覧取得は非対応です。名前はそのまま保存されます。':cache.state==='loading'?'一覧を取得中です。':
              cache.state==='stale'?'一覧が古いため、シーンの存在を確認できません。更新してください。':'一覧を取得できません。保存済みの名前は保持しています。';
          }
        }
        warning.className=error?'error':'hint';warning.textContent=message;
      }
      field.append(warning);fields.append(field);if(refresh)fields.append(refresh);update();
    }
    if(a.operation==='setInputMute')fields.append(control('ミュート',String(a.inputMuted),[['true','ミュート'],['false','解除']],value=>a.inputMuted=value==='true'));
    fields.append(note);
  }
  return {create,render,defaults,operations,validName};
})();
