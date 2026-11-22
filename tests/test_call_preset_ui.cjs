const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const html=fs.readFileSync('web/index.html','utf8'),line=start=>html.split('\n').find(l=>l.startsWith(start));
const original=JSON.parse(fs.readFileSync('web/builtin/ChainPad-Configuration-Empty.json','utf8'));
const call=targetInput=>({protocol:'callChain',delayMs:0,targetInput});
const full=()=>structuredClone(original);
const key=actions=>({format:'chainpad-key-preset',version:1,chains:[{input:0,actions},{input:1,actions:[]}]});
const fields={ssid:{value:'unsaved'},oscHost:{value:'127.0.0.1'},oscPort:{value:'9010'},presetFile:{}};
const ui=vm.createContext({TextEncoder,config:full(),capabilities:{},saving:false,dirty:false,selected:0,
  $:id=>fields[id],run:f=>f,selectInput(){},changed(){ui.dirty=true;},renderActions(){},rotationSummary(){},
  message(){},names:Array(14).fill('Key'),passwordEditor:{update:n=>n},RotationPreview:{clone:structuredClone},
  api(){throw Error('unexpected HTTP request');}});
vm.runInContext(fs.readFileSync('web/call-model.js','utf8')+'\n'+fs.readFileSync('web/presets.js','utf8')+'\n'+
  line('function applyImport(')+'\n'+line("$('presetFile').onchange")+'\n'+
  html.slice(html.indexOf('async function saveConfig('),html.indexOf("$('save').onclick")),ui);
async function upload(text,kind='full',group=0) {
  ui.importTarget={kind,group};
  fields.presetFile.files=[{size:Buffer.byteLength(text),text:async()=>text}];
  await fields.presetFile.onchange();
}
async function fails(fn,code,message) {
  const before=JSON.stringify(ui.config),ref=ui.config,dirty=ui.dirty;
  await assert.rejects(fn,e=>e.code===code && message.test(e.message));
  assert.equal(ui.config,ref);assert.equal(JSON.stringify(ui.config),before);assert.equal(ui.dirty,dirty);
}
(async()=>{
  const cyclic=full();cyclic.chains[0].actions=[call(3)];cyclic.chains[3].actions=[{protocol:'wait',delayMs:5},call(0)];
  await fails(()=>upload(JSON.stringify(cyclic)),'Cycle',/KEY1 Press #1 → KEY2 Release #2 → KEY1 Press/);
  const good=full();good.chains[3].actions=[call(27)];
  await fails(()=>upload(JSON.stringify(good)),'Unsupported',/KEY2 Release #1.*未対応/);
  await fails(()=>upload(JSON.stringify(good).replace('"targetInput":27','"targetInput":28')),'InvalidCall',/targetInput/);
  ui.config.chains[1].actions=[call(4)];
  const file=JSON.stringify(key([call(1)]));
  await fails(()=>upload(file,'key',2),'Cycle',/KEY1 Release #1 → KEY3 Press #1 → KEY1 Release/);
  await fails(()=>upload(file.replace('"targetInput":1','"targetInput":1e0'),'key',2),'InvalidCall',/KEY3 Press #1.*targetInput/);
  ui.config.chains[1].actions=[];
  await fails(()=>upload(file,'key',2),'Unsupported',/KEY3 Press #1.*未対応/);
  // Test-only capable peer: pasted Action belongs to input 4 but still calls input 1.
  ui.capabilities.callChainSupported=true;await upload(file,'key',2);
  assert.equal(ui.config.chains[4].actions[0].targetInput,1);
  ui.capabilities.callChainSupported=false;
  await fails(()=>ui.saveConfig(),'Unsupported',/KEY3 Press #1/);
  ui.config.chains[1].actions=[call(4)];
  await fails(()=>ui.saveConfig(),'Cycle',/KEY1 Release #1 → KEY3 Press #1/);
  ui.config.chains[1].actions=[call(28)];
  await fails(()=>ui.saveConfig(),'InvalidCall',/KEY1 Release #1.*targetInput/);
  console.log('PASS file import, key paste and pre-save: graph before capability, absolute targets, atomic failure');
})().catch(e=>{console.error(e);process.exitCode=1;});
