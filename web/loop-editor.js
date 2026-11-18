/* Hierarchical Action editor. Wire Actions remain in config; UI IDs never do. */
const LoopEditor=(()=>{
  const M=LoopModel,ids=new WeakMap(),collapsed=new Set();
  let sequence=0,tree=[],drag=null,suppressUntil=0,rendering=false;
  const identify=a=>{if(!ids.has(a))ids.set(a,`loop-node-${++sequence}`);return ids.get(a);};
  const label=n=>M.loop(n)?'Loop':actionLabels[n.data.protocol];
  const find=id=>M.locate(tree,id);
  const focus=id=>document.querySelector(`[data-node="${id}"] .node-handle`)?.focus();
  function commit(fn,id){
    if(saving)return false;
    const edited=current()[editingAction];
    if(fn()===false)return false;
    current().splice(0,current().length,...M.flatten(tree));
    editingAction=current().indexOf(edited);changed();renderActions();if(id)focus(id);return true;
  }
  function move(id,parent,index){return commit(()=>M.move(tree,id,parent,index),id);}
  function sibling(id,direction){
    const f=find(id);if(!f||f.index+direction<0||f.index+direction>=f.list.length)return;
    move(id,f.parent?.id??null,f.index+(direction<0?-1:2));
  }
  function add(parent){
    const s=actionSlots();if(saving||s.used>=s.limit)return;
    const data=defaults('osc'),id=identify(data);
    commit(()=>{M.destination(tree,parent).push({id,kind:'action',data});if(parent)collapsed.delete(parent);},id);
    editingAction=current().indexOf(data);renderActions();
    document.querySelector(`[data-node="${id}"] .action-details select`)?.focus();
  }
  function addRow(parent){
    const row=document.createElement('div');row.className='loop-add-row';
    const s=actionSlots(),b=button('＋Add Action',()=>add(parent),saving||s.used>=s.limit);
    if(parent)b.setAttribute('aria-label','Loop内にActionを追加');
    row.append(b);return row;
  }
  function chooseLoop(card,n){
    const select=[...card.querySelectorAll('.action-details .fields > label')]
      .find(el=>el.firstChild?.textContent==='Action')?.querySelector('select');
    if(!select)return;
    const option=document.createElement('option'),s=actionSlots();
    option.value='loop';option.disabled=s.used+1>s.limit;
    option.textContent=option.disabled?'Loop（追加1枠必要）':'Loop';select.append(option);
    const original=select.onchange;
    select.onchange=e=>{
      if(select.value!=='loop'){original.call(select,e);return;}
      const s=actionSlots();if(saving||s.used+1>s.limit){select.value=n.data.protocol;return;}
      commit(()=>{n.kind='loop';n.children=[];n.data={protocol:'loopStart',delayMs:0,count:3};ids.set(n.data,n.id);},n.id);
      document.querySelector(`[data-node="${n.id}"] .loop-count input`)?.focus();
    };
  }
  function zone(parent,index,text,boundary=false,inside=false){
    const el=document.createElement('div');el.className='loop-drop-zone'+(boundary?' boundary':'')+(inside?' inside':'');
    el.tabIndex=-1;el.setAttribute('aria-label',text);
    el.dataset.parent=parent??'';el.dataset.index=index;
    const span=document.createElement('span');span.className='drop-label';span.textContent=text;el.append(span);
    return el;
  }
  function list(nodes,parent,cards){
    const el=document.createElement('div');el.className='loop-tree-list';
    const prefix=parent?`${label(find(parent).node)}内`:'Chain';
    nodes.forEach((n,i)=>{
      const prev=nodes[i-1];
      el.append(zone(parent,i,prev&&M.loop(prev)?`↓ ${label(prev)}の直後（${prefix}）`:
        `${prefix}：${label(n)}の前`,!!prev&&M.loop(prev)),node(n,cards));
    });
    const last=nodes.at(-1),endLabel=parent?`↳ ${prefix}の末尾`:'↓ Chainの末尾';
    el.append(zone(parent,nodes.length,last&&M.loop(last)?`${endLabel} · ${label(last)}の直後`:endLabel,true,!!parent));
    return el;
  }
  function node(n,cards){
    const isLoop=M.loop(n),card=isLoop?document.createElement('article'):cards.get(n.data);
    card.classList.add('tree-node');card.dataset.node=n.id;
    if(isLoop)card.className='tree-node loop-card';
    const head=document.createElement('div');head.className='action-head loop-editor-head';
    head.dataset.head=n.id;
    const handle=button('≡',()=>{});handle.className='drag-handle node-handle';
    handle.setAttribute('aria-label',`${label(n)}を移動`);
    handle.onkeydown=e=>{if(['ArrowUp','ArrowDown'].includes(e.key)){e.preventDefault();sibling(n.id,e.key==='ArrowUp'?-1:1);}};
    head.append(handle);
    if(isLoop){
      const toggle=button(`${collapsed.has(n.id)?'▸':'▾'} Loop`,()=>{
        if(collapsed.has(n.id))collapsed.delete(n.id);else collapsed.add(n.id);
        renderActions();document.querySelector(`[data-node="${n.id}"] .loop-toggle`)?.focus();
      });
      toggle.className='loop-toggle';toggle.setAttribute('aria-expanded',String(!collapsed.has(n.id)));
      toggle.setAttribute('aria-controls',`${n.id}-body`);head.append(toggle);
      const count=control('回数',n.data.count,null,v=>{
        if(!Number.isInteger(v)||v<1||v>999){message('回数は1〜999の整数です。',true);renderActions();return;}
        n.data.count=v;
      },'number',1,999);
      count.className='loop-count';count.lastChild.step=1;head.append(count);
    }else{
      const title=document.createElement('strong');title.textContent=label(n)+(n.data.protocol==='obs'&&!capabilities.obsActions?'（非対応）':'');head.append(title);
    }
    const summary=document.createElement('span');summary.className='action-summary';
    if(isLoop){
      let leaves=0;const walk=nodes=>nodes.forEach(c=>M.loop(c)?walk(c.children):++leaves);walk(n.children);
      summary.textContent=`内部 ${leaves} Action · 全体 ${M.slots([n])}枠`;
    }else{summary.dataset.actionIndex=current().indexOf(n.data);summary.textContent=actionSummary(n.data);}
    head.append(summary);
    const f=find(n.id),ops=document.createElement('div');ops.className='buttons';
    ops.append(button('↑',()=>sibling(n.id,-1),f.index===0),button('↓',()=>sibling(n.id,1),f.index===f.list.length-1),
      button('移動先…',()=>openMove(n.id)));
    if(isLoop)ops.append(button('Loop解除',()=>commit(()=>M.unwrap(tree,n.id))));
    else{
      const index=current().indexOf(n.data),edit=button(editingAction===index?'完了':'編集',()=>{
        editingAction=editingAction===index?-1:index;renderActions();
        document.querySelector(`[data-node="${n.id}"] .edit-action`)?.focus();
      });
      edit.className='edit-action';edit.setAttribute('aria-expanded',String(editingAction===index));
      edit.setAttribute('aria-controls',`action-details-${index}`);ops.append(edit);
    }
    ops.append(button('削除',()=>commit(()=>{const f=find(n.id);f.list.splice(f.index,1);})));head.append(ops);
    if(isLoop){
      card.append(head);
      const body=document.createElement('div');body.id=`${n.id}-body`;body.hidden=collapsed.has(n.id);
      body.className='loop-body';
      if(!body.hidden)body.append(list(n.children,n.id,cards),addRow(n.id));
      card.append(body);
    }else{card.querySelector('.action-head').replaceWith(head);chooseLoop(card,n);}
    head.onpointerdown=e=>{
      if(saving||e.button!==0||!e.isPrimary||drag)return;
      if(e.target.closest('input,select')||e.target.closest('button')&&!e.target.closest('.node-handle,.loop-toggle'))return;
      if(!isLoop&&!e.target.closest('.node-handle'))return;
      drag={id:n.id,pointer:e.pointerId,x:e.clientX,y:e.clientY,startX:e.clientX,startY:e.clientY,active:false,opened:[],frame:0};
    };
    return card;
  }
  function openMove(id){
    const dialog=document.createElement('dialog'),select=document.createElement('select');
    select.setAttribute('aria-label','移動先');
    const choices=(nodes,parent,path)=>{
      for(let i=0;i<=nodes.length;i++)if(M.canMove(tree,id,parent,i)){
        const option=document.createElement('option');option.value=JSON.stringify([parent,i]);
        option.textContent=`${path} / ${i===nodes.length?'末尾':`${i+1}: ${label(nodes[i])}の前`}`;select.append(option);
      }
      nodes.forEach((n,i)=>{if(M.loop(n)&&n.id!==id)choices(n.children,n.id,`${path} / ${i+1}: Loop`);});
    };
    choices(tree,null,'Chain');
    dialog.append(select,button('移動',()=>{
      const [parent,index]=JSON.parse(select.value);dialog.close();move(id,parent,index);
    },!select.options.length),button('キャンセル',()=>dialog.close()));
    dialog.onclose=()=>{dialog.remove();focus(id);};document.body.append(dialog);dialog.showModal();
  }
  function paint(){
    document.body.classList.toggle('loop-dragging',!!drag?.active);
    document.querySelectorAll('.loop-drop-zone').forEach(el=>el.classList.toggle('invalid',
      !!drag?.active&&!M.canMove(tree,drag.id,el.dataset.parent||null,Number(el.dataset.index))));
    document.querySelectorAll('.loop-drag-source,.loop-drop-active').forEach(el=>el.classList.remove('loop-drag-source','loop-drop-active'));
    if(drag?.active)document.querySelector(`[data-node="${drag.id}"]`)?.classList.add('loop-drag-source');
  }
  function hit(){
    if(!drag?.active)return;
    const el=document.elementFromPoint(drag.x,drag.y),head=el?.closest('[data-head]');
    const n=head&&find(head.dataset.head)?.node,source=find(drag.id)?.node;
    const forbidden=n&&(n.id===drag.id||(M.loop(source)&&M.locate(source.children,n.id)));
    const hover=n&&M.loop(n)&&collapsed.has(n.id)&&!forbidden?n.id:null;
    if(hover!==drag.hover){drag.hover=hover;drag.hoverAt=performance.now();}
    if(hover&&performance.now()-drag.hoverAt>=650){
      collapsed.delete(hover);drag.opened.push(hover);drag.hover=null;renderActions();return;
    }
    let target=el?.closest('.loop-drop-zone');
    if(!target&&head&&!forbidden){
      const f=find(n.id),rect=head.getBoundingClientRect();
      const inside=M.loop(n)&&!collapsed.has(n.id)&&drag.y>rect.top+rect.height*.35;
      const parent=inside?n.id:f.parent?.id??null;
      const index=inside?0:f.index+(!M.loop(n)&&drag.y>rect.top+rect.height/2?1:0);
      target=[...document.querySelectorAll('.loop-drop-zone')].find(z=>z.dataset.parent===(parent??'')&&Number(z.dataset.index)===index);
    }
    const parent=target?.dataset.parent||null,index=Number(target?.dataset.index);
    drag.target=target&&M.canMove(tree,drag.id,parent,index)?{parent,index}:null;
    paint();if(drag.target)target.classList.add('loop-drop-active');
  }
  function frame(){
    if(!drag?.active)return;
    if(drag.y<75)scrollBy(0,-10);else if(drag.y>innerHeight-85)scrollBy(0,10);
    hit();if(drag?.active)drag.frame=requestAnimationFrame(frame);
  }
  function finish(commitMove){
    if(!drag)return;
    const state=drag;drag=null;cancelAnimationFrame(state.frame);
    if(document.body.hasPointerCapture(state.pointer))document.body.releasePointerCapture(state.pointer);
    if(state.active)suppressUntil=performance.now()+400;
    if(commitMove&&state.active&&state.target)move(state.id,state.target.parent,state.target.index);
    else{
      state.opened.forEach(id=>collapsed.add(id));
      if(state.active&&!rendering){renderActions();focus(state.id);}
    }
    paint();
  }
  window.addEventListener('pointermove',e=>{
    if(!drag||e.pointerId!==drag.pointer)return;drag.x=e.clientX;drag.y=e.clientY;
    if(!drag.active&&Math.hypot(drag.x-drag.startX,drag.y-drag.startY)>5){
      drag.active=true;document.body.setPointerCapture(drag.pointer);drag.frame=requestAnimationFrame(frame);
    }
    if(drag.active){e.preventDefault();hit();}
  },{passive:false});
  window.addEventListener('pointerup',e=>{if(drag&&e.pointerId===drag.pointer){hit();finish(true);}});
  window.addEventListener('pointercancel',()=>finish(false));
  window.addEventListener('blur',()=>finish(false));
  document.body.addEventListener('lostpointercapture',()=>finish(false));
  window.addEventListener('keydown',e=>{if(e.key==='Escape'&&drag){e.preventDefault();finish(false);}});
  document.addEventListener('click',e=>{if(performance.now()<suppressUntil){e.preventDefault();e.stopImmediatePropagation();}},true);
  function render(flatRenderer){
    rendering=true;
    try{
      tree=M.parse(current(),identify);
      if(drag&&!find(drag.id))finish(false);
      flatRenderer();
      const cards=new Map([...$('actions').children].filter(c=>c.dataset.actionIndex!==undefined)
        .map(c=>[current()[Number(c.dataset.actionIndex)],c]));
      $('actions').replaceChildren(list(tree,null,cards));
      updateActionSlots();paint();
    }finally{rendering=false;}
  }
  return {render,add:()=>add(null),cancel:()=>finish(false)};
})();
