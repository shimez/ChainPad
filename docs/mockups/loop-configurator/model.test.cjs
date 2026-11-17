// Structural safety matters even in a mock: no orphan delimiters or lost leaves.
const assert=require('node:assert/strict'),M=require('./model.js');
const action=id=>({id,kind:'action',data:{protocol:'wait',delayMs:100}});
const loop=(id,children)=>({id,kind:'loop',count:3,children});
const tree=[action('a'),loop('outer',[action('b'),loop('inner',[action('c')])]),action('d')];
function check(){
  const flat=M.flatten(tree);let depth=0;
  for(const a of flat){
    if(a.protocol==='loopStart')depth++;
    if(a.protocol==='loopEnd')depth--;
    assert(depth>=0);
    assert(!('id'in a));assert(!('collapsed'in a));
  }
  assert.equal(depth,0);assert.equal(flat.length,M.slots(tree));
  const ids=[];function walk(nodes){for(const n of nodes){ids.push(n.id);if(M.loop(n))walk(n.children);}}walk(tree);
  assert.equal(new Set(ids).size,ids.length);
  return ids;
}
check();
const before=JSON.stringify(tree);
assert.equal(M.move(tree,'outer','inner',0),false);
assert.equal(M.move(tree,'outer','outer',0),false);
assert.equal(M.move(tree,'a',null,-1),false);
assert.equal(JSON.stringify(tree),before);
assert(M.move(tree,'a','inner',1));check(); // outside -> inside
assert(M.move(tree,'c',null,0));check(); // inside -> outside
const inner=M.locate(tree,'inner').node;
assert(M.move(tree,'inner',null,3));check(); // complete subtree
assert.equal(M.locate(tree,'a').parent,inner);
assert(M.move(tree,'inner','outer',0));check(); // re-nest
assert.equal(M.move(tree,'inner','outer',1),false); // same location, no-op
const count=M.slots(tree);
assert(M.unwrap(tree,'outer'));check();
assert.equal(M.slots(tree),count-2);
assert(M.locate(tree,'a'));assert(M.locate(tree,'b'));
console.log('PASS: boundary balance, subtree identity, cross-level moves, cycles, unwrap and slot counts');
