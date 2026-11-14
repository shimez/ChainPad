const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const ctx=vm.createContext({TextEncoder});
vm.runInContext(fs.readFileSync('web/osc-addresses.js','utf8'),ctx);
const picker=vm.runInContext('OscAddresses',ctx);
assert.equal(new Set(picker.entries.map(e=>e.address)).size,picker.entries.length);
assert.equal(picker.search('jump')[0].address,'/input/Jump');
assert.deepEqual(Array.from(picker.search('撮影','カメラ'),e=>e.address),['/usercamera/Capture','/usercamera/CaptureDelayed']);
assert.equal(picker.search('/USERCAMERA/CAPTURE')[0].type,'bool');
assert.equal(picker.search('not-an-address').length,0);
for(const forbidden of ['/chatbox/input','/usercamera/Pose','/avatar/parameters/MuteSelf','/avatar/parameters/AFK'])assert.equal(picker.find(forbidden),undefined);
const action={protocol:'osc',transport:'wifi',delayMs:0,address:'/custom',type:'int',value:127};
const keys=Object.keys(action);
picker.apply(action,picker.find('/input/Jump'));assert.equal(action.value,127); // Do not silently overwrite same-type user values.
picker.apply(action,picker.find('/usercamera/Capture'));assert.equal(action.value,false);
picker.apply(action,picker.find('/usercamera/Zoom'));assert.equal(action.value,0);
picker.apply(action,picker.find('/dolly/Import'));assert.equal(action.value,'');
assert.deepEqual(Object.keys(action),keys);assert.equal(action.transport,'wifi');assert.equal(action.delayMs,0);
const before=JSON.stringify(action);picker.apply(action,{address:'/injected',type:'int'});assert.equal(JSON.stringify(action),before);
const html=fs.readFileSync('web/index.html','utf8'),build=fs.readFileSync('scripts/build.py','utf8');
assert(html.indexOf('src="osc-addresses.js"')<html.indexOf('src="action-ui.js"'));
assert(build.includes("web/osc-addresses.js"));
vm.runInContext(fs.readFileSync('web/presets.js','utf8'),ctx);
const presets=vm.runInContext('Presets',ctx);
const config={schemaVersion:2,network:{ssid:'',oscHost:'192.168.1.2',oscPort:9000},chains:Array.from({length:28},(_,input)=>({input,actions:[]})),encoderRotation:{mode:'actionChain',rotationValue:{rangeSteps:20,initialPosition:0,boundary:'stop',outputs:[]}}};
for(const entry of picker.entries){
  const a={protocol:'osc',transport:'wifi',delayMs:0,address:'/custom',type:'string',value:'custom'};
  picker.apply(a,entry);config.chains[0].actions=[a];
  for(const kind of ['full','key']){
    const restored=presets.read(kind==='full'?presets.full(config):presets.key(config,0),kind,{usbMidi:false,usbKeyboard:false});
    const chains=kind==='full'?restored.config.chains:restored.chains;
    assert.deepEqual(JSON.parse(JSON.stringify(chains[0].actions[0])),a);
  }
}
console.log(`PASS OSC catalog: ${picker.entries.length} unique candidates, search, type changes, Full/Key round-trip and embedding`);
