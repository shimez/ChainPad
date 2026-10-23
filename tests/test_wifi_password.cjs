// Run the production editor and page handlers with an API fixture. Stored
// passwords are deliberately absent from GET; only explicit replacements travel.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const helper=fs.readFileSync('web/wifi-password.js','utf8');
const html=fs.readFileSync('web/index.html','utf8');
const line=prefix=>html.split('\n').find(l=>l.startsWith(prefix));
const plain=v=>JSON.parse(JSON.stringify(v));
const tick=()=>new Promise(resolve=>setImmediate(resolve));
function nodes(){const all={};return {all,get:id=>all[id]??={value:'',checked:false,disabled:false,textContent:'',classList:{toggle(){}}}};}
async function configurator(){
  const {all,get}=nodes();let fail=false;const requests=[];
  const stored=JSON.parse(fs.readFileSync('web/builtin/ChainPad-Configuration-Default.json','utf8'));
  stored.network={ssid:'original',passwordConfigured:true,oscHost:'10.0.0.1',oscPort:9000};
  const ctx=vm.createContext({TextEncoder,selected:0,dirty:false,saving:false,config:undefined,capabilities:{},$:get,
    RotationPreview:{clone:structuredClone,stringify:JSON.stringify},selectInput(){},rotationSummary(){},cancelActionDrag(){},
    changed(){ctx.dirty=true;},renderActions(){},message(){},document:{querySelectorAll:()=>Object.values(all)},
    fetch:async(path,options)=>{
      if(path==='/api/config')return new Response(JSON.stringify(stored));
      if(path==='/api/capabilities')return new Response('{"usbMidi":true,"usbKeyboard":true}');
      if(path==='/api/status')return new Response('{"storageState":"ready","configOutputsAllowed":true}');
      const body=options.body?JSON.parse(options.body):null;requests.push([path,body]);
      if(fail&&path==='/api/config/begin')return new Response('{"message":"write failed"}',{status:400});
      return new Response('{"token":7,"message":"saved"}');
    }});
  vm.runInContext(helper+'\nconst passwordEditor=WifiPassword.create($);\n'+fs.readFileSync('web/presets.js','utf8')+'\n'+
    line('async function api(')+'\n'+line('async function load(')+'\n'+line('function applyImport(')+'\n'+line('function editedConfig(')+'\n'+
    html.slice(html.indexOf('async function saveConfig('),html.indexOf("$('save').onclick")),ctx);
  await ctx.load();assert.equal(get('password').value,'');assert.match(get('passwordStatus').textContent,/設定済み/);
  assert(!Object.hasOwn(ctx.config.network,'password'));
  const submitted=()=>requests.filter(([p])=>p==='/api/config/begin').at(-1)[1].network;
  get('ssid').value='edited-ssid';get('oscPort').value='9001';
  await ctx.saveConfig();assert(!Object.hasOwn(submitted(),'password'));assert.equal(submitted().ssid,'edited-ssid');
  // Import preserves all three password editing states, while exported data
  // remains portable and contains neither SSID nor replacement credentials.
  for(const mode of ['keep','replace','clear']){
    vm.runInContext('passwordEditor.load(true)',ctx);
    if(mode==='replace')get('password').value='new-only-in-input';
    if(mode==='clear'){get('clearPassword').checked=true;get('clearPassword').onchange();}
    assert.equal(get('password').disabled,mode==='clear');
    ctx.raw=vm.runInContext('Presets.full(config)',ctx);ctx.raw.network.password='untrusted-import';
    vm.runInContext("applyImport(raw,{kind:'full'})",ctx);
    assert.equal(get('password').value,mode==='replace'?'new-only-in-input':'');
    assert.equal(get('clearPassword').checked,mode==='clear');
    assert(!Object.hasOwn(ctx.config.network,'password'));
    const exported=plain(vm.runInContext('Presets.full(editedConfig())',ctx));
    assert.deepEqual(Object.keys(exported.network).sort(),['oscHost','oscPort']);
    fail=true;await assert.rejects(ctx.saveConfig(),/write failed/);assert.equal(ctx.saving,false);
    assert.equal(get('password').value,mode==='replace'?'new-only-in-input':'');
    assert.equal(get('clearPassword').checked,mode==='clear');
    fail=false;await ctx.saveConfig();
    if(mode==='keep')assert(!Object.hasOwn(submitted(),'password'));
    else assert.equal(submitted().password,mode==='clear'?'':'new-only-in-input');
    assert.equal(get('password').value,'');assert.equal(get('clearPassword').checked,false);assert.equal(get('password').disabled,false);
    assert(!Object.hasOwn(ctx.config.network,'password'));
    await ctx.saveConfig();assert(!Object.hasOwn(submitted(),'password')); // Success must not resend a replacement.
  }
}
async function wifi(){
  const page=fs.readFileSync('web/wifi.html','utf8'),script=page.match(/<script>\s*([\s\S]*?)<\/script>/)[1];
  for(const mode of ['keep','replace','clear']){
    const {get}=nodes();let fail=false;const requests=[];
    const ctx=vm.createContext({document:{getElementById:get},fetch:async(path,options)=>{
      if(options.method==='GET')return new Response('{"ssid":"original","passwordConfigured":true}');
      requests.push(JSON.parse(options.body));
      return new Response(JSON.stringify({message:fail?'write failed':'saved'}),{status:fail?400:200});
    }});
    vm.runInContext(helper+'\n'+script,ctx);await tick();assert.equal(get('password').value,'');
    get('ssid').value='edited';
    if(mode==='replace')get('password').value='new-value';
    if(mode==='clear'){get('clearPassword').checked=true;get('clearPassword').onchange();}
    assert.equal(get('password').disabled,mode==='clear');
    fail=true;await get('wifi').onsubmit({preventDefault(){}});
    assert.equal(get('save').disabled,false);assert.equal(get('password').value,mode==='replace'?'new-value':'');
    fail=false;await get('wifi').onsubmit({preventDefault(){}});
    assert.deepEqual(requests[0],requests[1]);assert.equal(requests[1].ssid,'edited');
    if(mode==='keep')assert(!Object.hasOwn(requests[1],'password'));
    else assert.equal(requests[1].password,mode==='replace'?'new-value':'');
    assert.equal(get('password').value,'');assert.equal(get('clearPassword').checked,false);assert(get('save').disabled);
  }
}
(async()=>{await configurator();await wifi();console.log('PASS Wi-Fi password UI: public GET load, keep/replace/clear, failure retry, successful secret cleanup, repeat save, Preset import edit-state preservation/export exclusion');})().catch(e=>{console.error(e);process.exitCode=1;});
