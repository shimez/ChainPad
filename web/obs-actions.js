// Shared scene/audio-input discovery is input assistance only.
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
    let enabled=false, busy=false, ready=false, generation=null, freshUntil=0, deadline=0, epoch=0;
    let state={state:'unavailable',complete:false,scenes:[]};
    let kind='scenes';
    const listeners=new Set();
    const emit=()=>listeners.forEach(fn=>fn());
    const snapshot=()=>({...state,complete:state.complete && Date.now()<freshUntil});
    async function request(refresh=false) {
      if(!enabled||busy||document.hidden)return;
      busy=true; const token=epoch;
      if(refresh){deadline=Date.now()+20000;state={...state,state:'loading',complete:false};} emit();
      try {
        const response=await fetch('/api/obs/'+kind+(refresh?'/refresh':''),{method:refresh?'POST':'GET',cache:'no-store'});
        const data=await response.json();
        if(token!==epoch)return;
        if(!Array.isArray(data[kind]))throw Error('list request failed');
        data.scenes=data[kind];
        if(!response.ok){state={...data,state:'error',complete:false};return;}
        if(generation!==null && data.generation!==generation){state={...state,state:'stale',complete:false};return;}
        state=data;freshUntil=Date.now()+Math.max(0,60000-(data.ageMs??60000));
      } catch {if(token===epoch)state={...state,state:'error',complete:false};}
      finally {busy=false;emit();}
    }
    function init(capabilities) {
      enabled=capabilities.obsActions===true;
      if(!enabled){state={state:'unsupported',complete:false,scenes:[]};emit();return;}
    }
    function tick(connection) {
      const nowReady=connection?.connectionState==='Ready';
      if(!enabled||document.hidden)return;
      const nextGeneration=connection?.generation??null;
      const transition=nowReady!==ready || nextGeneration!==generation;
      if(transition){
        ++epoch;ready=nowReady;generation=nextGeneration;
        state={...state,state:'stale',complete:false};emit();
      }
      // Poll only an admitted discovery, never an idle Ready connection.
      if(state.state==='loading'&&!busy){
        if(Date.now()>=deadline){state={...state,state:'error',complete:false};emit();}
        else void request(false);
      }else if(state.complete && Date.now()>=freshUntil){state={...state,state:'stale',complete:false};emit();}
    }
    return {init,tick,refresh:()=>request(true),snapshot,busy:()=>busy,
      select(next){if(next===kind)return;++epoch;kind=next;state={state:'unavailable',complete:false,scenes:[]};emit();},
      subscribe(fn){listeners.clear();listeners.add(fn);},clear(){listeners.clear();},
      failed(){++epoch;ready=false;state={...state,state:'error',complete:false};emit();}};
  }
  function render(a, fields, control, changed, rerender, capabilities, scenes) {
    const supported=capabilities.obsActions===true;
    let picker;
    if(['setInputMute','toggleInputMute'].includes(a.operation))fields.className+=' obs-mute'+(a.operation==='toggleInputMute'?' obs-mute-toggle':'');
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
      const label=key==='sceneName'?'シーン':'ミュート対象';
      scenes.select?.(key==='sceneName'?'scenes':'inputs');
      const field=control(key==='sceneName'?'シーン名':'入力名',a[key],null,value=>{a[key]=value;update();});
      const input=field.lastChild, warning=document.createElement('small');
      input.oninput=()=>{a[key]=input.value;changed();update();};
      let list,refresh,refreshField,panel,choicesKey='';
      {
        input.setAttribute('autocomplete','off');
        panel=document.createElement('div');panel.hidden=true;panel.style.gridColumn='1 / -1';
        panel.className='obs-name-picker';picker=panel;
        list=document.createElement('select');list.size=6;list.setAttribute('aria-label','取得した全'+label);list.style.width='100%';
        const apply=document.createElement('button'),cancel=document.createElement('button');
        apply.type=cancel.type='button';apply.textContent='選択した'+label+'を使用';cancel.textContent='キャンセル';
        apply.onclick=()=>{if(list.selectedIndex<0)return;input.value=list.value;a[key]=input.value;changed();panel.hidden=true;update();input.focus();};
        cancel.onclick=()=>{panel.hidden=true;refresh.focus();};
        panel.onkeydown=e=>{if(e.key==='Escape'){e.preventDefault();cancel.onclick();}};
        panel.append(list,apply,cancel);
        refreshField=document.createElement('div');refreshField.style.display='grid';refreshField.style.gridTemplateColumns='minmax(0,1fr)';refreshField.style.gap='5px';refreshField.style.fontSize='13px';
        const caption=document.createElement('span');caption.textContent=label+'候補';
        refresh=document.createElement('button');refresh.className='obs-list-button';refresh.type='button';refresh.textContent=label+'一覧から選択';refresh.style.minHeight='42px';
        refreshField.append(caption,refresh);
        refresh.onclick=()=>{panel.hidden=false;void scenes.refresh();update();};
        scenes.subscribe(()=>{refresh.disabled=!supported||scenes.busy();update();});
        refresh.disabled=!supported||scenes.busy();
      }
      function update(){
        const valid=validName(input.value);input.setCustomValidity(valid?'':'UTF-8で1〜192 bytes（制御文字不可）を指定してください。');
        let message=valid?'':'対象名はUTF-8で1〜192 bytes（制御文字不可）です。',error=!valid;
        if(list){
          const cache=scenes.snapshot();
          const names=cache.state==='ready'&&cache.complete?cache.scenes:[];
          const nextKey=JSON.stringify(names);
          if(nextKey!==choicesKey){
            choicesKey=nextKey;list.replaceChildren();
            for(const name of names){const option=document.createElement('option');option.value=option.textContent=name;list.append(option);}
            list.selectedIndex=-1;
          }
          if(valid){
            const complete=cache.state==='ready'&&cache.complete===true;
            error=complete&&!cache.scenes.includes(input.value);
            message=error?'取得した'+label+'一覧にありません。手入力のまま保存できます。':complete?'':
              !supported?'一覧取得は非対応です。名前はそのまま保存されます。':cache.state==='loading'?'一覧を取得中です。':
              cache.error==='audioCapabilitiesUnavailable'?'このOBSは音声候補の抽出に非対応です。入力名を手入力で保存できます。':
              cache.error==='actionPriorityOrTimeout'?'通常Actionを優先したか、取得待ちが上限に達しました。手入力で保存するか、再取得してください。':
              '自由入力で保存できます。「'+label+'一覧から選択」で最新の候補を取得します。';
          }
        }
        warning.className=error?'error':'hint';warning.textContent=message;
      }
      field.append(warning);fields.append(field);if(refresh)fields.append(refreshField);update();
    }
    if(a.operation==='setInputMute')fields.append(control('ミュート',String(a.inputMuted),[['true','ミュート'],['false','解除']],value=>a.inputMuted=value==='true'));
    if(picker)fields.append(picker);
    fields.append(note);
  }
  return {create,render,defaults,operations,validName};
})();
