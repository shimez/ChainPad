const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');
const path = require('node:path');
const context = vm.createContext({TextEncoder});
vm.runInContext(fs.readFileSync(path.join(__dirname,'../web/presets.js'),'utf8')+'\nglobalThis.presets = Presets;',context);
const P=context.presets, usb={usbMidi:true,usbKeyboard:true}, ble={usbMidi:false,usbKeyboard:false};
const config={schemaVersion:2,network:{ssid:'test',password:'secret',oscHost:'192.168.1.2',oscPort:9000},chains:Array.from({length:28},(_,input)=>({input,actions:[]})),encoderRotation:{mode:'actionChain',rotationValue:{rangeSteps:20,initialPosition:0,boundary:'stop',outputs:[]}}};
config.chains[0].actions=[{protocol:'wait',delayMs:123},{protocol:'midi',transport:'both',delayMs:0,message:'noteOn',channel:1,number:48,value:127}];
config.chains[1].actions=[{protocol:'keyboard',transport:'usb',delayMs:0,message:'keyUp',usage:104,modifiers:0}];
config.chains[27].actions=[{protocol:'osc',transport:'wifi',delayMs:0,address:'/test',type:'string',value:'hello'}];
const normalized=v=>JSON.parse(JSON.stringify(v));
const full=P.full(config);
const paired=P.full(config);
paired.chains[0].actions=[{protocol:'midi',transport:'both',delayMs:0,message:'noteOnOff',channel:1,number:60,value:100},
  {protocol:'keyboard',transport:'usb',delayMs:0,message:'keyDownUp',usage:104,modifiers:2}];
assert.deepEqual(normalized(P.read(paired,'full',usb).config.chains[0]),normalized(paired.chains[0]));
const pairedBle=P.read(paired,'full',ble);
assert.equal(pairedBle.converted,3); // Includes the original USB keyUp in Release.
assert.equal(pairedBle.config.chains[0].actions.length,2);
assert.ok(pairedBle.config.chains[0].actions.every(a=>a.transport==='ble'));
paired.chains[0].actions[0].value=0;assert.throws(()=>P.read(paired,'full',usb));
paired.chains[0].actions[0].value=100;
paired.chains[0].actions[0].holdMs=25;
paired.chains[0].actions[1].holdMs=50;
paired.chains[0].actions.push({protocol:'midi',delayMs:0,message:'allNotesOnOff',value:90,holdMs:86400000});
assert.deepEqual(normalized(P.read(paired,'full',usb).config.chains[0]),normalized(paired.chains[0]));
for(const bad of [-1,86400001,1.5,null,'20',true]){
  const invalid=normalized(paired);invalid.chains[0].actions[2].holdMs=bad;
  assert.throws(()=>P.read(invalid,'full',usb));
}
for(const [address,value] of [['/'+'a'.repeat(191),'b'.repeat(128)],['/'+'あ'.repeat(63)+'ab','あ'.repeat(42)+'ab'],['/'+'😀'.repeat(47)+'abc','😀'.repeat(32)]]){
  const file=P.full(config);file.chains[27].actions[0].address=address;file.chains[27].actions[0].value=value;
  assert.doesNotThrow(()=>P.read(file,'full',usb));
  file.chains[27].actions[0].address+='x';assert.throws(()=>P.read(file,'full',usb),/192 bytes/);
  file.chains[27].actions[0].address=address;file.chains[27].actions[0].value+='x';assert.throws(()=>P.read(file,'full',usb),/128 bytes/);
}
const bulkFull=P.full(config);
bulkFull.chains[2].actions=[{protocol:'midi',message:'allNotesOn',delayMs:0,value:88},{protocol:'midi',message:'allNotesOff',delayMs:0}];
assert.deepEqual(normalized(P.read(bulkFull,'full',ble).config.chains[2]),normalized(bulkFull.chains[2]));
bulkFull.chains[2].actions[0].value=0;assert.throws(()=>P.read(bulkFull,'full',ble));
const portable={...config,network:{oscHost:config.network.oscHost,oscPort:config.network.oscPort}};
assert.deepEqual(normalized(P.read(full,'full',usb).config),portable);
assert.deepEqual(normalized(full.network),portable.network);
const oldFile=normalized(full);oldFile.network.ssid='other';oldFile.network.password='other-secret';
assert.deepEqual(normalized(P.read(oldFile,'full',usb).config),portable);
assert.equal(config.network.password,'secret');
const converted=P.read(full,'full',ble);
assert.equal(converted.converted,2);
assert.equal(converted.config.chains[0].actions[1].transport,'ble');
assert.equal(full.chains[0].actions[1].transport,'both');
const key=P.key(config,0);
assert.equal(key.chains.length,2);
assert.deepEqual(normalized(P.read(key,'key',usb).chains),config.chains.slice(0,2));
assert.equal(P.read(key,'key',ble).converted,2);
assert.throws(()=>P.read(key,'full',usb));
assert.throws(()=>P.read(full,'key',usb));
assert.throws(()=>P.key(config,13));
assert.throws(()=>P.read({...full,version:99},'full',usb));
function invalid(change){const copy=normalized(full);change(copy);assert.throws(()=>P.read(copy,'full',usb));}
invalid(v=>v.chains[1].input=0);
invalid(v=>v.chains.pop());
invalid(v=>v.chains[0].actions[0].delayMs=-1);
invalid(v=>v.chains[0].actions[0].delayMs=86400001);
invalid(v=>v.chains[0].actions[0].delayMs=1.5);
invalid(v=>v.chains[0].actions[1].value=0);
invalid(v=>v.chains[0].actions[1].delayMs=10);
invalid(v=>v.chains[0].actions=Array(17).fill({protocol:'wait',delayMs:1}));
for(const [press,release] of [[16,0],[14,2],[8,8],[1,15],[0,16]]){
  const shared=P.full(config);
  shared.chains[0].actions=Array(press).fill({protocol:'wait',delayMs:1});
  shared.chains[1].actions=Array(release).fill({protocol:'wait',delayMs:1});
  const restored=P.read(shared,'full',usb).config;
  assert.equal(restored.chains[0].actions.length,press);
  assert.equal(P.read(P.key(restored,0),'key',usb).chains[1].actions.length,release);
}
invalid(v=>{v.chains[0].actions=Array(10).fill({protocol:'wait',delayMs:1});v.chains[1].actions=Array(7).fill({protocol:'wait',delayMs:1});});
invalid(v=>v.chains[26].actions=Array(9).fill({protocol:'wait',delayMs:1}));
const badKey=P.key(config,0);badKey.chains[0].actions=Array(10).fill({protocol:'wait',delayMs:1});badKey.chains[1].actions=Array(7).fill({protocol:'wait',delayMs:1});assert.throws(()=>P.read(badKey,'key',usb));
invalid(v=>v.chains[1].actions[0].transport='both');
invalid(v=>v.network.oscHost='256.1.2.3');
invalid(v=>v.network.oscPort=0);
// Action order and file source are not mutated; full export owns an independent copy.
assert.deepEqual(normalized(full.chains[0].actions.map(a=>a.protocol)),['wait','midi']);
full.network.oscHost='10.0.0.1';assert.equal(config.network.oscHost,'192.168.1.2');
// Exercise the actual UI import handler, including unsaved Wi-Fi field values.
const fields={ssid:{value:'current-edit'},password:{value:'current-secret'},oscHost:{value:'10.0.0.9'},oscPort:{value:8000}};
const ui=vm.createContext({Presets:P,config:normalized(config),capabilities:usb,saving:false,selected:0,$:id=>fields[id],selectInput:()=>{},changed:()=>{},renderActions:()=>{},rotationSummary:()=>{},message:()=>{}});
const html=fs.readFileSync(path.join(__dirname,'../web/index.html'),'utf8');
vm.runInContext(html.split('\n').find(line=>line.startsWith('function applyImport(')),ui);
for(const file of [P.full(config),oldFile]){
  ui.raw=file;vm.runInContext("applyImport(raw,{kind:'full'})",ui);
  assert.deepEqual(normalized(ui.config.network),{ssid:'current-edit',password:'current-secret',...portable.network});
  assert.equal(fields.ssid.value,'current-edit');assert.equal(fields.password.value,'current-secret');
  assert.equal(fields.oscHost.value,portable.network.oscHost);
  assert.deepEqual(normalized(ui.config.chains),config.chains);
}
console.log('PASS Key/Full round-trip, Wi-Fi exclusion and UI import preservation, Wait order, encoder/OSC preservation, BLE adaptation, invalid imports, no source mutation');
assert.equal(full.version,2);assert.equal(key.version,1);
assert.throws(()=>P.read({...P.full(config),version:1},'full',usb),/Unsupported Version/);
assert.throws(()=>P.read({...P.full(config),schemaVersion:1},'full',usb),/Unsupported schemaVersion/);
const rotationFile=P.full(config);
const cc={protocol:'midi',message:'cc',transport:'both',channel:1,number:7,start:127,end:0};
rotationFile.encoderRotation.rotationValue.outputs=[cc,{...cc,transport:'usb'},
  {protocol:'osc',transport:'wifi',address:'/x',type:'float',start:0.1,end:1e-100}];
let parsed=P.read(rotationFile,'full',ble);
assert.equal(parsed.converted,4); // Two existing Actions plus two Outputs.
assert.equal(parsed.duplicates.length,1);
assert.equal(parsed.config.encoderRotation.rotationValue.outputs[2].start,Math.fround(0.1));
assert.equal(parsed.config.encoderRotation.rotationValue.outputs[2].end,0);
parsed.config.currentPosition=100;parsed.config.encoderRotation.currentPosition=100;
const clean=P.full(parsed.config);
assert(!JSON.stringify(clean).includes('currentPosition'));
assert.equal(P.read(P.key(parsed.config,0),'key',ble).chains.length,2);
for(const v of [0,65536,1.5,'20',null]){
  const f=normalized(rotationFile);f.encoderRotation.rotationValue.rangeSteps=v;assert.throws(()=>P.read(f,'full',usb));
}
for(const field of ['channel','number','start','end']){
  const f=normalized(rotationFile);f.encoderRotation.rotationValue.outputs[0][field]=1.5;assert.throws(()=>P.read(f,'full',usb));
}
const many=normalized(rotationFile);many.encoderRotation.rotationValue.outputs=Array(16).fill(cc);
assert.equal(P.read(many,'full',usb).config.encoderRotation.rotationValue.outputs.length,16);
many.encoderRotation.rotationValue.outputs.push(cc);assert.throws(()=>P.read(many,'full',usb));
const invalidInactive=normalized(rotationFile);invalidInactive.encoderRotation.rotationValue.outputs[2].start=Infinity;
assert.throws(()=>P.read(invalidInactive,'full',usb));
const absent=normalized(rotationFile);delete absent.encoderRotation;assert.throws(()=>P.read(absent,'full',usb));
console.log('PASS Rotation v2 / Key v1 / legacy rejection / inactive validation / normalized duplicates / float32 / runtime exclusion / provisional 16');
