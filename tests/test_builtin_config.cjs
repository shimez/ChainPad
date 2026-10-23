const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync('web/index.html','utf8');
const sources=Object.fromEntries(['Default','Empty'].map(name=>[name,JSON.parse(fs.readFileSync(`web/builtin/ChainPad-Configuration-${name}.json`,'utf8'))]));
const script=fs.readFileSync('web/presets.js','utf8')+'\n'+fs.readFileSync('web/builtin-config-data.js','utf8');
const plain=v=>JSON.parse(JSON.stringify(v));
const normalize=c=>{c=plain(c);for(const chain of c.chains)for(const a of chain.actions)if(a.message==='keyDownUp')a.holdMs??=0;return c;};
for(const usb of [true,false]){
  let confirmed=true,rendered=0;
  const config={network:{ssid:'private',password:'secret',oscHost:'10.1.2.3',oscPort:1234},chains:[],encoderRotation:{}};
  const controls={ssid:{value:'unsaved ssid'},password:{value:'unsaved password'},oscHost:{value:'10.9.8.7'},oscPort:{value:'9999'}};
  const originalControls=JSON.stringify(controls),network=config.network;
  const ctx=vm.createContext({TextEncoder,config,capabilities:{usbMidi:usb,usbKeyboard:usb},saving:false,selected:0,dirty:false,
    confirm:()=>confirmed,$:id=>controls[id],cancelActionDrag(){},selectInput(){},changed(){ctx.dirty=true;},renderActions(){rendered++;},rotationSummary(){},message(){},
    fetch(){throw Error('Builtin loading must not send HTTP');}});
  const fn=html.slice(html.indexOf('function loadBuiltin('),html.indexOf("$('loadDefault').onclick"));
  vm.runInContext(script+'\n'+fn,ctx);
  for(const name of ['Default','Empty']){
    const raw=vm.runInContext(`BuiltinConfigs.${name}`,ctx);
    assert(!('network' in raw));
    assert.deepEqual(plain(raw.chains),sources[name].chains);
    assert.deepEqual(plain(raw.encoderRotation),sources[name].encoderRotation);
    ctx.loadBuiltin(name);
    assert.deepEqual(plain(config.chains),sources[name].chains);
    assert.deepEqual(plain(config.encoderRotation),sources[name].encoderRotation);
    assert.equal(config.network,network);assert.equal(JSON.stringify(controls),originalControls);assert(ctx.dirty);
  }
  const before=JSON.stringify(config);confirmed=false;ctx.loadBuiltin('Default');assert.equal(JSON.stringify(config),before);
  confirmed=true;ctx.saving=true;assert.throws(()=>ctx.loadBuiltin('Default'));ctx.saving=false;
  vm.runInContext('BuiltinConfigs.Default.network={invalid:true}',ctx);ctx.loadBuiltin('Default'); // ignored even if malformed
  const saved=JSON.stringify(config);vm.runInContext('BuiltinConfigs.Default.chains[0].actions[0].usage=999',ctx);
  assert.throws(()=>ctx.loadBuiltin('Default'));assert.equal(JSON.stringify(config),saved);
  ctx.config=undefined;assert.throws(()=>ctx.loadBuiltin('Empty'));
  assert(rendered>=3);
}
if(process.argv.includes('--firmware')){
  for(const file of ['builtin-boot.json','builtin-reset.json']){
    const got=JSON.parse(fs.readFileSync('.pio/storage-fault-tests/'+file,'utf8'));
    assert.deepEqual(normalize({chains:got.chains}),normalize({chains:sources.Default.chains}));
    assert.deepEqual(got.encoderRotation,sources.Default.encoderRotation);
    assert.equal(got.network.oscHost,'192.168.1.100');assert.equal(got.network.oscPort,9000);
    assert.equal(got.network.ssid,'');assert(!Object.hasOwn(got.network,'password'));assert.equal(got.network.passwordConfigured,false);
  }
}
for(const match of html.matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/g))new vm.Script(match[1]);
console.log('PASS builtin Config: canonical data, editor-only replacement, network/form retention, confirmation/cancel, validation, save/load guards'+(process.argv.includes('--firmware')?', firmware boot/reset equality':''));
