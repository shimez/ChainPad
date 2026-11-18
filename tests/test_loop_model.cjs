const assert=require('node:assert/strict');
const fs=require('node:fs'),vm=require('node:vm');
const M=require('../web/loop-model.js');
const context=vm.createContext({TextEncoder});
vm.runInContext(fs.readFileSync('web/presets.js','utf8')+'\nglobalThis.P=Presets;',context);
const P=context.P,caps={usbMidi:true,usbKeyboard:true};
const start=count=>({protocol:'loopStart',delayMs:0,count});
const end=()=>({protocol:'loopEnd',delayMs:0});
const wait={protocol:'wait',delayMs:10};
const wire=[start(3),wait,start(2),wait,end(),end(),wait];
const tree=M.parse(wire);
assert.deepEqual(M.flatten(tree),wire);
assert.equal(M.slots(tree),7);
assert.equal(M.canMove(tree,'0','2',0),false);
assert.equal(M.move(tree,'2',null,1),true);
assert.deepEqual(M.flatten(tree),[start(3),wait,end(),start(2),wait,end(),wait]);
assert.equal(M.unwrap(tree,'0'),true);
assert.deepEqual(M.flatten(tree),[wait,start(2),wait,end(),wait]);
assert.throws(()=>M.parse([end()]));
assert.throws(()=>M.parse([start(1)]));
const preset=actions=>({format:'chainpad-key-preset',version:1,chains:[{input:0,actions},{input:1,actions:[]}]});
assert.deepEqual(JSON.parse(JSON.stringify(P.read(preset(wire),'key',caps).chains[0].actions)),wire);
for(const actions of [[end()],[start(1)],[start(0),end()],[start(1000),end()],
  [{...start(3),id:'ui'},end()],[{...start(3),count:true},end()],[start(1),{...end(),count:1}]])
  assert.throws(()=>P.read(preset(actions),'key',caps));
assert.doesNotThrow(()=>P.read(preset([...Array.from({length:8},()=>start(999)),...Array.from({length:8},end)]),'key',caps));
const over=preset([start(1),end()]);over.chains[1].actions=Array(15).fill(wait);
assert.throws(()=>P.read(over,'key',caps));
console.log('PASS Loop tree: roundtrip / subtree moves / cycle rejection / unwrap; Preset: nesting / strict markers / shared slots');
