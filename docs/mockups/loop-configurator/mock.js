/* Browser-only adapter around the production Action editor. No API or storage. */
'use strict';
const $=id=>document.getElementById(id), M=LoopModel;
let selected=0, saving=false, nextId=1, leafRefs=[], leafData=[], drag=null, suppressClickUntil=0;
const collapsed=new Map(), history=[];
const capabilities={usbMidi:true,usbKeyboard:true,defaultMidiTransport:'both',midiTransports:['both','usb','ble'],keyboardTransports:['usb','ble'],obsActions:true};
const chains=Array.from({length:28},()=>[]);
const names=Array.from({length:12},(_,i)=>`KEY ${String(i+1).padStart(2,'0')}`).concat('Encoder Push','Encoder');
const leaf=(protocol,extra={})=>({id:`n${nextId++}`,kind:'action',data:{...defaults(protocol),...extra}});
const loop=(count,children=[])=>({id:`n${nextId++}`,kind:'loop',count,children});
const roots=()=>chains[selected];
const current=()=>leafData; // Only used by the original, unmodified leaf editor.
const waitWarning=()=>{};
const message=text=>{$('message').textContent=text;};
const obsScenes=(()=>{
  let kind='scenes',listener=()=>{};
  return {
    clear(){listener=()=>{};},busy:()=>false,subscribe(fn){listener=fn;},
    select(value){kind=value;},refresh(){listener();},
    snapshot:()=>({state:'ready',complete:true,scenes:kind.endsWith('-items')?
      [{sourceName:'Camera',sceneItemId:1},{sourceName:'Overlay',sceneItemId:2}]:
      kind==='scenes'?['Main','Camera','Ending']:kind==='groups'?['Overlay Group']:['Mic','Desktop Audio','Video']})
  };
})();
function walk(nodes,fn){for(const n of nodes){fn(n);if(M.loop(n))walk(n.children,fn);}}
function syncLeaves(){leafRefs.forEach((n,i)=>{if(!M.loop(n))n.data=leafData[i];});}
function actionSlots(){
  const base=selected-selected%2;
  return selected>=26?{used:M.slots(roots()),limit:8,label:'このEvent'}:
    {used:M.slots(chains[base])+M.slots(chains[base+1]),limit:16,label:'Press / Release合計'};
}
function updateActionSlots(){
  const s=actionSlots();
  $('actionSlots').textContent=`${s.label}: ${s.used} / ${s.limit} Action枠（このEvent: ${M.slots(roots())}）`;
  document.querySelectorAll('[data-cost]').forEach(b=>b.disabled=s.used+Number(b.dataset.cost)>s.limit);
}
function changed(){
  syncLeaves();
  for(const n of leafRefs){
    const el=document.querySelector(`[data-node="${n.id}"] .action-summary`);
    if(el)el.textContent=actionSummary(n.data);
  }
  updateActionSlots();updateWire();
}
function updateWire(){$('json').textContent=JSON.stringify({input:selected,actions:M.flatten(roots())},null,2);}
function focusNode(id){document.querySelector(`[data-node="${id}"] .node-handle`)?.focus();}
function structural(fn,text,focusId){
  syncLeaves();
  const before=JSON.stringify(chains);
  if(fn()===false)return false;
  history.push(before);if(history.length>30)history.shift();
  $('undo').disabled=false;renderActions();message(text);if(focusId)focusNode(focusId);
  return true;
}
function resetSample(){
  cancelDrag();leafRefs=[];leafData=[];editingAction=-1;collapsed.clear();history.length=0;nextId=1;
  chains.forEach(c=>c.splice(0));
  chains[0]=[
    leaf('osc',{address:'/input/Voice',value:1}),
    loop(3,[
      leaf('midi',{message:'noteOnOff',holdMs:60}),
      leaf('wait',{delayMs:200}),
      loop(2,[leaf('keyboard'),leaf('wait',{delayMs:100})]),
      leaf('obs',{sceneName:'Main'})
    ]),
    leaf('wait',{delayMs:500})
  ];
  chains[1]=[leaf('keyboard',{message:'releaseAll'})];
  chains[2]=[loop(5,Array.from({length:13},()=>leaf('wait')))];
  chains[3]=[leaf('wait')];
  chains[26]=[loop(2,[leaf('midi',{message:'cc'}),leaf('wait')])];
  $('undo').disabled=true;selectGroup(0);message('サンプルを読み込みました。KEY 01で内外移動、KEY 02で枠上限を試せます。');
}
function selectGroup(group){
  syncLeaves();cancelDrag();selected=group*2;leafRefs=[];leafData=[];editingAction=-1;
  $('title').textContent=names[group];
  $('event').replaceChildren(...(group===13?['Clockwise（時計回り）','CounterClockwise（反時計回り）']:['Press（押した時）','Release（離した時）']).map((text,i)=>{
    const o=document.createElement('option');o.value=String(group*2+i);o.textContent=text;return o;
  }));
  [...$('inputs').children].forEach((b,i)=>b.classList.toggle('active',i===group));
  renderActions();
}
function labelOf(n){return M.loop(n)?'Loop':actionLabels[n.data.protocol];}
function addRow(parentId){
  const row=document.createElement('div');row.className='add-row';
  function add(){
    const s=actionSlots();if(s.used+1>s.limit)return;
    const n=leaf('osc');
    structural(()=>{M.destination(roots(),parentId).push(n);if(parentId)collapsed.set(parentId,false);},'Actionを追加しました。編集欄のActionから種類を選べます。',n.id);
    editingAction=leafRefs.indexOf(n);renderActions();
    document.querySelector(`[data-node="${n.id}"] .action-details select`)?.focus();
  }
  const addAction=button('+ Add Action',add);addAction.dataset.cost='1';
  if(parentId)addAction.setAttribute('aria-label',`${labelOf(M.locate(roots(),parentId).node)}内にActionを追加`);
  row.append(addAction);return row;
}
function enableLoopChoice(card,n){
  const field=[...card.querySelectorAll('.action-details > .fields > label')]
    .find(label=>label.firstChild?.textContent==='Action');
  const select=field?.querySelector('select');
  if(!select)return;
  const option=document.createElement('option');option.value='loop';
  const s=actionSlots();option.disabled=s.used+1>s.limit;
  option.textContent=option.disabled?'Loop（変更には追加1枠必要）':'Loop';
  select.append(option);
  const originalChange=select.onchange;
  select.onchange=event=>{
    if(select.value!=='loop'){originalChange.call(select,event);return;}
    const s=actionSlots();
    if(s.used+1>s.limit){select.value=n.data.protocol;message('Loopへの変更には追加で1枠必要です。');return;}
    structural(()=>{
      n.kind='loop';n.count=3;n.children=[];delete n.data;collapsed.set(n.id,false);
    },'Actionを空のLoopに変更しました。Header／Footerで2枠使用します。',n.id);
    document.querySelector(`[data-node="${n.id}"] .loop-count input`)?.focus();
  };
}
function targetZone(parentId,index,text,boundary=false,inside=false){
  const el=document.createElement('div');el.className='drop-zone'+(boundary?' boundary':'')+(inside?' inside':'');
  el.tabIndex=-1;el.setAttribute('aria-label',text);
  el.dataset.parent=parentId??'';el.dataset.index=index;
  const span=document.createElement('span');span.className='drop-label';span.textContent=text;el.append(span);
  return el;
}
function buildList(nodes,parentId=null){
  const list=document.createElement('div');list.className='tree-list';
  const prefix=parentId?`${labelOf(M.locate(roots(),parentId).node)}内`:'Chain';
  nodes.forEach((n,i)=>{
    const prev=nodes[i-1];
    list.append(targetZone(parentId,i,prev&&M.loop(prev)?`↓ ${labelOf(prev)}の直後（${prefix}）`:
      `${prefix}：${labelOf(n)}の前`,!!prev&&M.loop(prev)));
    list.append(buildNode(n));
  });
  const last=nodes.at(-1);
  const endLabel=parentId?`↳ ${prefix}の末尾`:'↓ Chainの末尾';
  list.append(targetZone(parentId,nodes.length,last&&M.loop(last)?`${endLabel} · ${labelOf(last)}の直後`:endLabel,true,!!parentId));
  // A nested Loop at the end gets two distinct, labelled targets: child end
  // inside its own card, then this parent's end outside that card.
  return list;
}
let cards=new Map();
function buildNode(n){
  const isLoop=M.loop(n),node=isLoop?document.createElement('article'):cards.get(n.id);
  node.classList.add('tree-node');node.dataset.node=n.id;
  if(isLoop)node.className='tree-node loop-card';
  const head=document.createElement('div');head.className='action-head mock-head'+(isLoop?' loop-head':'');
  head.dataset.head=n.id;
  const handle=button('≡',()=>{});handle.className='drag-handle node-handle';
  handle.title='ドラッグで移動 / ↑↓で同階層を移動';handle.setAttribute('aria-label',`${labelOf(n)}を移動`);
  handle.onkeydown=e=>{if(['ArrowUp','ArrowDown'].includes(e.key)){e.preventDefault();siblingMove(n.id,e.key==='ArrowUp'?-1:1);}};
  head.append(handle);
  if(isLoop){
    const toggle=button(`${collapsed.get(n.id)?'▸':'▾'} ${labelOf(n)}`,()=>{
      collapsed.set(n.id,!collapsed.get(n.id));renderActions();
      document.querySelector(`[data-node="${n.id}"] .loop-toggle`)?.focus();
    });
    toggle.className='loop-toggle';toggle.setAttribute('aria-expanded',String(!collapsed.get(n.id)));toggle.setAttribute('aria-controls',`body-${n.id}`);
    head.append(toggle);
    const count=document.createElement('label');count.className='loop-count';count.textContent='回数';
    const input=document.createElement('input');input.type='number';input.min=1;input.max=999;input.step=1;input.value=n.count;
    input.setAttribute('aria-label',`${labelOf(n)}の回数`);
    input.onchange=()=>{
      const value=Number(input.value);
      if(!Number.isInteger(value)||value<1||value>999){input.value=n.count;message('回数は1〜999の整数です。');return;}
      n.count=value;changed();message(`${labelOf(n)}を${value}回に変更しました。`);
    };
    count.append(input,document.createTextNode('回'));head.append(count);
  }else{
    const title=document.createElement('strong');title.textContent=actionLabels[n.data.protocol];head.append(title);
  }
  const summary=document.createElement('span');summary.className='action-summary';
  if(isLoop){let total=0;walk(n.children,c=>{if(!M.loop(c))total++;});summary.textContent=`内部 ${total} Action · 全体 ${M.slots([n])}枠`;}
  else summary.textContent=actionSummary(n.data);
  head.append(summary);
  const ops=document.createElement('div');ops.className='buttons';
  const found=M.locate(roots(),n.id);
  ops.append(button('↑',()=>siblingMove(n.id,-1),found.index===0),
    button('↓',()=>siblingMove(n.id,1),found.index===found.list.length-1),
    button('移動先…',()=>openMove(n.id)));
  if(!isLoop){
    const index=leafRefs.indexOf(n),edit=button(editingAction===index?'完了':'編集',()=>{
      editingAction=editingAction===index?-1:index;renderActions();
      document.querySelector(`[data-node="${n.id}"] .edit-action`)?.focus();
    });
    edit.className='edit-action';edit.setAttribute('aria-expanded',String(editingAction===index));ops.append(edit);
  }
  if(isLoop)ops.append(button('Loop解除',()=>structural(()=>M.unwrap(roots(),n.id),'Loopの囲みを解除しました。内部Actionは保持しています。')));
  ops.append(button('削除',()=>structural(()=>{
    const item=M.locate(roots(),n.id);item.list.splice(item.index,1);
  },isLoop?'Loopと内部Actionを削除しました。「直前の構造操作を戻す」で復元できます。':'Actionを削除しました。')));
  head.append(ops);
  if(isLoop){
    node.append(head);
    const body=document.createElement('div');body.id=`body-${n.id}`;body.hidden=!!collapsed.get(n.id);
    if(!body.hidden){
      const inner=document.createElement('div');inner.className='loop-body';inner.append(buildList(n.children,n.id));
      inner.append(addRow(n.id));body.append(inner);
    }
    node.append(body);
  }else node.querySelector('.action-head').replaceWith(head);
  head.addEventListener('pointerdown',e=>{
    if(e.target.closest('input,select')||e.target.closest('button')&&!e.target.closest('.node-handle,.loop-toggle'))return;
    if(!isLoop&&!e.target.closest('.node-handle'))return;
    beginDrag(e,n.id);
  });
  return node;
}
function renderActions(){
  const editedId=leafRefs[editingAction]?.id;
  syncLeaves();leafRefs=[];walk(roots(),n=>{if(!M.loop(n))leafRefs.push(n);});leafData=leafRefs.map(n=>n.data);
  editingAction=leafRefs.findIndex(n=>n.id===editedId);
  renderOriginalActions();
  cards=new Map(leafRefs.map((n,i)=>[n.id,$('actions').children[i]]));
  leafRefs.forEach(n=>enableLoopChoice(cards.get(n.id),n));
  const tree=buildList(roots());
  $('actions').replaceChildren(tree,addRow(null));
  updateActionSlots();updateWire();paintDrag();
}
function siblingMove(id,direction){
  const f=M.locate(roots(),id);
  if(!f||f.index+direction<0||f.index+direction>=f.list.length)return;
  performMove(id,f.parent?.id??null,f.index+(direction<0?-1:2));
}
function performMove(id,parentId,index){
  return structural(()=>M.move(roots(),id,parentId,index),'配置を変更しました。',id);
}
let movingId=null;
function openMove(id){
  movingId=id;$('moveTarget').replaceChildren();
  function choices(nodes,parentId,path){
    for(let i=0;i<=nodes.length;i++){
      if(M.canMove(roots(),id,parentId,i)){
        const option=document.createElement('option');option.value=JSON.stringify([parentId,i]);
        option.textContent=`${path} / ${i===nodes.length?'末尾':`${labelOf(nodes[i])}の前`}`;$('moveTarget').append(option);
      }
    }
    nodes.filter(M.loop).forEach(n=>{if(n.id!==id)choices(n.children,n.id,`${path} / ${labelOf(n)}`);});
  }
  choices(roots(),null,'Chain');
  $('moveApply').disabled=!$('moveTarget').options.length;$('moveDialog').showModal();
}
$('moveApply').onclick=()=>{const [parent,index]=JSON.parse($('moveTarget').value);$('moveDialog').close();performMove(movingId,parent,index);};
$('moveCancel').onclick=()=>$('moveDialog').close();
$('moveDialog').addEventListener('close',()=>focusNode(movingId));
function paintDrag(){
  document.body.classList.toggle('loop-dragging',!!drag?.active);
  document.querySelectorAll('.drag-source,.drop-active,.hover-opening').forEach(e=>e.classList.remove('drag-source','drop-active','hover-opening'));
  if(!drag?.active)return;
  document.querySelector(`[data-node="${drag.id}"]`)?.classList.add('drag-source');
  document.querySelectorAll('.drop-zone').forEach(el=>el.classList.toggle('invalid',!M.canMove(roots(),drag.id,el.dataset.parent||null,Number(el.dataset.index))));
}
function beginDrag(e,id){
  if(e.button!==0||!e.isPrimary||drag)return;
  // Keep capture on body: hover expansion rebuilds cards, not this element.
  drag={id,pointer:e.pointerId,x:e.clientX,y:e.clientY,startX:e.clientX,startY:e.clientY,active:false,
    target:null,hover:null,hoverAt:0,opened:new Map(),frame:0};
}
function activateDrag(){
  document.body.setPointerCapture(drag.pointer);
  drag.active=true;paintDrag();$('dragStatus').hidden=false;
  drag.frame=requestAnimationFrame(dragFrame);
}
function hitTarget(){
  if(!drag?.active)return;
  const hit=document.elementFromPoint(drag.x,drag.y);
  let zone=hit?.closest('.drop-zone');
  const head=hit?.closest('[data-head]'),node=head&&M.locate(roots(),head.dataset.head)?.node;
  const self=M.locate(roots(),drag.id)?.node;
  const forbidden=node&&(node.id===drag.id||(M.loop(self)&&M.locate(self.children,node.id)));
  const hoverId=node&&M.loop(node)&&collapsed.get(node.id)&&!forbidden?node.id:null;
  if(hoverId!==drag.hover){drag.hover=hoverId;drag.hoverAt=performance.now();}
  document.querySelectorAll('.hover-opening').forEach(e=>e.classList.remove('hover-opening'));
  if(hoverId){
    head.classList.add('hover-opening');
    if(performance.now()-drag.hoverAt>=650){
      drag.opened.set(hoverId,true);collapsed.set(hoverId,false);drag.hover=null;
      renderActions();return hitTarget();
    }
  }
  if(!zone&&head&&!forbidden){
    const rect=head.getBoundingClientRect(),f=M.locate(roots(),node.id);
    if(M.loop(node)&&!collapsed.get(node.id)&&drag.y>rect.top+rect.height*.35)
      zone=document.querySelector(`[data-node="${node.id}"] .loop-body > .tree-list > .drop-zone`);
    else{
      const index=f.index+(!M.loop(node)&&drag.y>rect.top+rect.height/2?1:0);
      zone=[...document.querySelectorAll('.drop-zone')].find(z=>z.dataset.parent===(f.parent?.id??'')&&Number(z.dataset.index)===index);
    }
  }
  const parent=zone?.dataset.parent||null,index=Number(zone?.dataset.index);
  drag.target=zone&&M.canMove(roots(),drag.id,parent,index)?{parent,index}:null;
  document.querySelectorAll('.drop-active').forEach(el=>el.classList.remove('drop-active'));
  if(drag.target)zone.classList.add('drop-active');
  $('dragStatus').textContent=hoverId?'ホバー中… Loopを自動展開します':drag.target?
    `${zone.textContent} に挿入 · Escでキャンセル`:'移動先を選択 · 自分自身／子孫への移動不可 · Escでキャンセル';
}
function dragFrame(){
  if(!drag?.active)return;
  const speed=drag.y<75?-10:drag.y>innerHeight-85?10:0;
  if(speed)window.scrollBy(0,speed);
  hitTarget();if(drag?.active)drag.frame=requestAnimationFrame(dragFrame);
}
function finishDrag(commit){
  if(!drag)return;
  const state=drag;drag=null;cancelAnimationFrame(state.frame);
  if(document.body.hasPointerCapture(state.pointer))document.body.releasePointerCapture(state.pointer);
  if(state.active)suppressClickUntil=performance.now()+400;
  $('dragStatus').hidden=true;
  if(commit&&state.active&&state.target){
    performMove(state.id,state.target.parent,state.target.index);
  }else{
    state.opened.forEach((value,id)=>collapsed.set(id,value));
    if(state.active){renderActions();message('ドラッグをキャンセルしました。配置は変更していません。');focusNode(state.id);}
  }
  paintDrag();
}
function cancelDrag(){finishDrag(false);}
window.addEventListener('pointermove',e=>{
  if(!drag||e.pointerId!==drag.pointer)return;
  drag.x=e.clientX;drag.y=e.clientY;
  if(!drag.active&&Math.hypot(drag.x-drag.startX,drag.y-drag.startY)>5)activateDrag();
  if(drag.active){e.preventDefault();hitTarget();}
},{passive:false});
window.addEventListener('pointerup',e=>{if(drag&&e.pointerId===drag.pointer){if(drag.active)hitTarget();finishDrag(true);}});
window.addEventListener('pointercancel',cancelDrag);
document.body.addEventListener('lostpointercapture',()=>{if(drag)cancelDrag();});
window.addEventListener('blur',cancelDrag);
window.addEventListener('keydown',e=>{if(e.key==='Escape'&&drag){e.preventDefault();cancelDrag();}});
document.addEventListener('click',e=>{if(performance.now()<suppressClickUntil){e.preventDefault();e.stopImmediatePropagation();}},true);
$('theme').onchange=()=>document.documentElement.dataset.theme=$('theme').value;
$('event').onchange=()=>{syncLeaves();cancelDrag();selected=Number($('event').value);leafRefs=[];leafData=[];editingAction=-1;renderActions();};
$('reset').onclick=resetSample;
$('undo').onclick=()=>{
  if(!history.length)return;cancelDrag();const snapshot=JSON.parse(history.pop());
  snapshot.forEach((nodes,i)=>chains[i]=nodes);leafRefs=[];leafData=[];editingAction=-1;
  $('undo').disabled=!history.length;renderActions();message('直前の構造操作を戻しました（その後のフィールド編集も当時の状態に戻ります）。');
};
for(const [id,value]of [['expand',false],['collapse',true]])$(id).onclick=()=>{walk(roots(),n=>{if(M.loop(n))collapsed.set(n.id,value);});renderActions();};
names.forEach((name,i)=>$('inputs').append(button(name,()=>selectGroup(i))));
resetSample();
