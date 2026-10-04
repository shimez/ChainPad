function actionSlots(){
  if(selected>=26)return {used:current().length,limit:8,label:'このEvent'};
  const base=selected-selected%2;
  return {used:config.chains.find(c=>c.input===base).actions.length+config.chains.find(c=>c.input===base+1).actions.length,limit:16,label:'Press / Release合計'};
}
function updateActionSlots(){const s=actionSlots();$('actionSlots').textContent=`${s.label}: ${s.used} / ${s.limit} Actions`;$('add').disabled=s.used>=s.limit;}
function actionSummary(a) {
  const transport={wifi:'Wi-Fi',usb:'USB',ble:'BLE',both:'USB + BLE'}[a.transport]||'';
  if(a.protocol==='wait')return `${a.delayMs} ms`;
  if(a.protocol==='osc')return `${a.address} → ${a.type==='string'?JSON.stringify(a.value):a.type==='bool'?(a.value?'True':'False'):a.value} · ${a.type}`;
  if(a.protocol==='midi'){
    if(a.message==='allNotesOn')return `All Notes → Note On · Velocity ${a.value}`;
    if(a.message==='allNotesOff')return 'All Notes → Note Off';
    if(a.message==='cc')return `CC · Ch ${a.channel} · Controller ${a.number} → ${a.value} · ${transport}`;
    return `${a.message==='noteOn'?'Note On':'Note Off'} · Ch ${a.channel} · Note ${a.number} · Velocity ${a.value} · ${transport}`;
  }
  if(a.message==='releaseAll')return `Keyboard · Release All · ${transport}`;
  const mods=['L Ctrl','L Shift','L Alt','L GUI','R Ctrl','R Shift','R Alt','R GUI'].filter((_,bit)=>a.modifiers&(1<<bit));
  const key=usageNames[a.usage]||`Usage 0x${a.usage.toString(16)}`;
  return `Keyboard · ${a.message==='keyDown'?'KeyDown':'KeyUp'} · ${a.message==='keyDown'&&mods.length?mods.join(' + ')+' + ':''}${key} · ${transport}`;
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
