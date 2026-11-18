// Tree operations over canonical flat Actions. IDs belong to the editor only.
(function(root){
  const loop=n=>n.kind==='loop';
  const slots=nodes=>nodes.reduce((sum,n)=>sum+(loop(n)?2+slots(n.children):1),0);
  function parse(actions,identify=(_,i)=>String(i)){
    const roots=[],stack=[roots];
    actions.forEach((a,i)=>{
      if(a.protocol==='loopEnd'){
        if(stack.length===1)throw Error('Loop Footerに対応するHeaderがありません。');
        stack.pop();return;
      }
      const node={id:identify(a,i),kind:a.protocol==='loopStart'?'loop':'action',data:a};
      stack.at(-1).push(node);
      if(loop(node)){
        if(!Number.isInteger(a.count)||a.count<1||a.count>999)throw Error('Loop回数は1〜999です。');
        node.children=[];stack.push(node.children);
      }
    });
    if(stack.length!==1)throw Error('Loop Headerに対応するFooterがありません。');
    return roots;
  }
  function flatten(nodes){
    return nodes.flatMap(n=>loop(n)?[n.data,...flatten(n.children),{protocol:'loopEnd',delayMs:0}]:[n.data]);
  }
  function locate(nodes,id,parent=null){
    for(let index=0;index<nodes.length;index++){
      const node=nodes[index];
      if(node.id===id)return {node,list:nodes,index,parent};
      if(loop(node)){const found=locate(node.children,id,node);if(found)return found;}
    }
    return null;
  }
  function destination(nodes,id){return id===null?nodes:locate(nodes,id)?.node.children;}
  function canMove(nodes,id,parent,index){
    const source=locate(nodes,id),target=destination(nodes,parent);
    if(!source||!target||!Number.isInteger(index)||index<0||index>target.length)return false;
    if(id===parent||(loop(source.node)&&locate(source.node.children,parent)))return false;
    return !(source.list===target&&(index===source.index||index===source.index+1));
  }
  function move(nodes,id,parent,index){
    if(!canMove(nodes,id,parent,index))return false;
    const source=locate(nodes,id),target=destination(nodes,parent);
    source.list.splice(source.index,1);
    if(source.list===target&&index>source.index)--index;
    target.splice(index,0,source.node);return true;
  }
  function unwrap(nodes,id){
    const found=locate(nodes,id);if(!found||!loop(found.node))return false;
    found.list.splice(found.index,1,...found.node.children);return true;
  }
  const api={loop,slots,parse,flatten,locate,destination,canMove,move,unwrap};
  if(typeof module!=='undefined')module.exports=api;else root.LoopModel=api;
})(globalThis);
