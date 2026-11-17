/* Prototype-only tree operations. UI state never appears in the wire array. */
(function(root){
  const loop = n => n.kind === 'loop';
  const slots = nodes => nodes.reduce((sum,n)=>sum+(loop(n)?2+slots(n.children):1),0);
  function locate(nodes,id,parent=null){
    for(let index=0;index<nodes.length;index++){
      const node=nodes[index];
      if(node.id===id)return {node,list:nodes,index,parent};
      if(loop(node)){const found=locate(node.children,id,node);if(found)return found;}
    }
    return null;
  }
  function flatten(nodes){
    return nodes.flatMap(n=>loop(n)?[
      {protocol:'loopStart',delayMs:0,count:n.count},
      ...flatten(n.children),{protocol:'loopEnd',delayMs:0}
    ]:[{...n.data}]);
  }
  function destination(nodes,parentId){
    if(parentId===null)return nodes;
    const parent=locate(nodes,parentId)?.node;
    return parent&&loop(parent)?parent.children:null;
  }
  function canMove(nodes,id,parentId,index){
    const source=locate(nodes,id),target=destination(nodes,parentId);
    if(!source||!target||!Number.isInteger(index)||index<0||index>target.length)return false;
    if(parentId===id||(loop(source.node)&&locate(source.node.children,parentId)))return false;
    return !(source.list===target&&(index===source.index||index===source.index+1));
  }
  function move(nodes,id,parentId,index){
    if(!canMove(nodes,id,parentId,index))return false;
    const source=locate(nodes,id),target=destination(nodes,parentId);
    source.list.splice(source.index,1);
    if(source.list===target&&index>source.index)--index;
    target.splice(index,0,source.node);
    return true;
  }
  function unwrap(nodes,id){
    const found=locate(nodes,id);
    if(!found||!loop(found.node))return false;
    found.list.splice(found.index,1,...found.node.children);
    return true;
  }
  const api={loop,slots,locate,flatten,destination,canMove,move,unwrap};
  if(typeof module!=='undefined')module.exports=api;
  else root.LoopModel=api;
})(globalThis);
