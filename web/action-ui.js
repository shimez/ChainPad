function oscTextControl(label,value,onchange,limit){
  const wrap=control(label,value,null,onchange),input=wrap.lastChild;
  input.maxLength=limit;
  const counter=document.createElement('small');counter.className='hint';
  wrap.append(counter);
  function update(){
    const bytes=new TextEncoder().encode(input.value).length,over=bytes>limit;
    counter.textContent=`${bytes} / ${limit} bytes（UTF-8）${over?'：上限を超えています':''}`;
    counter.className=over?'danger':'hint';
    input.setCustomValidity(over?`${label}はUTF-8で${limit} bytes以内にしてください。`:'');
    input.setAttribute('aria-invalid',String(over));
  }
  input.oninput=()=>{update();onchange(input.value);changed();};
  update();return wrap;
}
function actionSlots(){
  if(selected>=26)return {used:current().length,limit:8,label:'このEvent'};
  const base=selected-selected%2;
  return {used:config.chains.find(c=>c.input===base).actions.length+config.chains.find(c=>c.input===base+1).actions.length,limit:16,label:'Press / Release合計'};
}
function updateActionSlots(){const s=actionSlots();$('actionSlots').textContent=`${s.label}: ${s.used} / ${s.limit} Actions`;$('add').disabled=s.used>=s.limit;}
const consumerUsageOrder=[233,234,226,205,181,182,183];
function hidKind(a){return a.message==='consumer'?'consumer':a.message==='releaseAll'?'releaseAll':'keyboard';}
function setHidKind(a,kind){
  if(hidKind(a)===kind)return;
  delete a.holdMs;
  // Release All still requires a valid keyboard usage/modifiers in the existing codec.
  Object.assign(a,{message:kind==='keyboard'?'keyDownUp':kind,usage:kind==='consumer'?233:104,modifiers:0});
}
function hidFields(fields,a){
  fields.classList.add('hid');
  const kind=hidKind(a);
  fields.append(control('種類',kind,[['keyboard','キーボード'],['consumer','音量・メディア'],['releaseAll','HID全解除']],v=>{setHidKind(a,v);renderActions();}));
  if(kind==='consumer'){
    fields.append(control('操作',a.usage,consumerUsageOrder.map(u=>[u,Presets.consumerNames[u]]),v=>a.usage=Number(v)));
    const hint=document.createElement('p');hint.className='hint';hint.textContent='選択した操作をこのイベントで1回実行します。（押下→解除）';fields.append(hint);
  }else if(kind==='releaseAll'){
    const hint=document.createElement('p');hint.className='hint';hint.textContent='保持中のHIDキーをすべて解除します。';fields.append(hint);
  }else{
    fields.append(control('押し方',a.message,[['keyDown','押す（KeyDown）'],['keyUp','離す（KeyUp）'],['keyDownUp','押して離す（KeyDown→Up）']],v=>{a.message=v;if(v!=='keyDownUp')delete a.holdMs;renderActions();}));
    fields.append(control('キー（HID Usage）',a.usage,Array.from({length:112},(_,k)=>{const u=k+4;return [u,`${usageNames[u]} (0x${u.toString(16).padStart(2,'0')})`];}),v=>a.usage=Number(v)));
  }
}
function actionSummary(a) {
  if(a.protocol==='obs'){const names={setCurrentProgramScene:'シーン切り替え',startStream:'配信開始',stopStream:'配信停止',startRecord:'録画開始',stopRecord:'録画停止',setInputMute:'ミュート設定',toggleInputMute:'ミュート切り替え',setSceneItemVisibility:'シーンアイテム表示制御'};return `${names[a.operation]||a.operation}${a.sceneName!==undefined?' · '+a.sceneName:a.inputName!==undefined?' · '+a.inputName:''}${a.operation==='setInputMute'?(a.inputMuted?' · ミュート':' · 解除'):''}${a.operation==='setSceneItemVisibility'?' · ID: '+(a.sceneItemId??'未選択')+' · '+a.visibility:''}`;}
  const transport={wifi:'Wi-Fi',usb:'USB',ble:'BLE',both:'USB + BLE'}[a.transport]||'';
  if(a.protocol==='wait')return `${a.delayMs} ms`;
  if(a.protocol==='stopOtherChains')return '他のChainを中断 · このChainは継続';
  if(a.protocol==='osc')return `${a.address} → ${a.type==='string'?JSON.stringify(a.value):a.type==='bool'?(a.value?'True':'False'):a.value} · ${a.type}`;
  if(a.protocol==='midi'){
    if(a.message==='allNotesOn')return `All Notes → Note On · Velocity ${a.value}`;
    if(a.message==='allNotesOff')return 'All Notes → Note Off';
    if(a.message==='allNotesOnOff')return `All Notes On→Off · Velocity ${a.value} · 待ち時間 ${a.holdMs??0} ms`;
    if(a.message==='cc')return `CC · Ch ${a.channel} · Controller ${a.number} → ${a.value} · ${transport}`;
    return `${a.message==='noteOn'?'Note On':a.message==='noteOnOff'?'Note On→Off':'Note Off'} · Ch ${a.channel} · Note ${a.number} · Velocity ${a.value} · ${transport}${a.message==='noteOnOff'?` · 待ち時間 ${a.holdMs??0} ms`:''}`;
  }
  if(a.message==='consumer')return `HID · ${transport} · ${Presets.consumerNames[a.usage]||'Unknown'} · 押下→解除`;
  if(a.message==='releaseAll')return `HID · ${transport} · 全解除`;
  const mods=['L Ctrl','L Shift','L Alt','L GUI','R Ctrl','R Shift','R Alt','R GUI'].filter((_,bit)=>a.modifiers&(1<<bit));
  const key=usageNames[a.usage]||`Usage 0x${a.usage.toString(16)}`;
  return `HID · ${transport} · ${['keyDown','keyDownUp'].includes(a.message)&&mods.length?mods.join(' + ')+' + ':''}${key} · ${a.message==='keyDown'?'押す':a.message==='keyDownUp'?'押して離す':'離す'}${a.message==='keyDownUp'?` · 押下時間 ${a.holdMs??0} ms`:''}`;
}

let editingAction=-1, cancelActionDrag=()=>{};
function moveAction(from,to){
  if(saving||from===to)return;
  const actions=current(), edited=actions[editingAction];
  const [action]=actions.splice(from,1);actions.splice(to,0,action);
  editingAction=actions.indexOf(edited);changed();renderActions();
  $('actions').children[to]?.querySelector('.drag-handle')?.focus();
}
function actionDrag(handle,card,index){
  handle.title='ドラッグで並べ替え（キーボード: ↑ / ↓）';
  handle.setAttribute('aria-label',`Action ${index+1}を並べ替え`);
  handle.onkeydown=e=>{
    if(e.key!=='ArrowUp'&&e.key!=='ArrowDown')return;
    e.preventDefault();const to=index+(e.key==='ArrowUp'?-1:1);
    if(to>=0&&to<current().length)moveAction(index,to);
  };
  handle.onpointerdown=e=>{
    if(saving||e.button!==0||!e.isPrimary)return;
    e.preventDefault();cancelActionDrag();handle.focus();
    const cards=[...$('actions').children],startY=e.clientY;
    let y=startY,to=index,dragging=false,frame;
    handle.setPointerCapture(e.pointerId);
    function mark(){
      cards.forEach(c=>c.classList.remove('drop-before','drop-after'));
      const others=cards.filter(c=>c!==card);
      to=others.findIndex(c=>y<c.getBoundingClientRect().top+c.getBoundingClientRect().height/2);
      if(to<0)to=others.length;
      if(others.length){if(to<others.length)others[to].classList.add('drop-before');else others.at(-1).classList.add('drop-after');}
    }
    function scroll(){
      if(dragging){const speed=y<70?-12:y>innerHeight-70?12:0;if(speed)window.scrollBy(0,speed);mark();}
      frame=requestAnimationFrame(scroll);
    }
    function move(event){
      if(event.pointerId!==e.pointerId)return;y=event.clientY;
      if(!dragging&&Math.abs(y-startY)>5){dragging=true;card.classList.add('dragging');document.body.classList.add('action-dragging');}
      if(dragging)mark();
    }
    function cleanup(){
      cancelAnimationFrame(frame);cards.forEach(c=>c.classList.remove('drop-before','drop-after','dragging'));
      document.body.classList.remove('action-dragging');
      handle.removeEventListener('pointermove',move);handle.removeEventListener('pointerup',end);
      handle.removeEventListener('pointercancel',cancel);handle.removeEventListener('lostpointercapture',cancel);
      window.removeEventListener('keydown',escape);window.removeEventListener('blur',cancel);
      if(handle.hasPointerCapture(e.pointerId))handle.releasePointerCapture(e.pointerId);
      cancelActionDrag=()=>{};
    }
    function cancel(){cleanup();}
    function escape(event){if(event.key==='Escape'){event.preventDefault();cancel();}}
    function end(event){if(event.pointerId!==e.pointerId)return;const apply=dragging;cleanup();if(apply)moveAction(index,to);}
    cancelActionDrag=cancel;
    handle.addEventListener('pointermove',move);handle.addEventListener('pointerup',end);
    handle.addEventListener('pointercancel',cancel);handle.addEventListener('lostpointercapture',cancel);
    window.addEventListener('keydown',escape);window.addEventListener('blur',cancel);frame=requestAnimationFrame(scroll);
  };
}
