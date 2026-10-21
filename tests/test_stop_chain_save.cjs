// Production UI save/load handlers over a simulated device API; codec/storage
// semantics are covered separately by the C++ host tests.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync('web/index.html','utf8');
const line=prefix=>html.split('\n').find(l=>l.startsWith(prefix));
async function check(usb){
  const config=JSON.parse(fs.readFileSync('web/builtin/ChainPad-Configuration-Default.json','utf8'));
  config.network={ssid:'local',password:'secret',oscHost:'192.168.1.2',oscPort:9000};
  for(const c of config.chains)c.actions=[{protocol:'stopOtherChains',delayMs:0}];
  const nodes={},get=id=>nodes[id]??={value:config.network[id],disabled:false};
  const caps={usbMidi:usb,usbKeyboard:usb},calls=[],staged={chains:[]};let stored;
  const ctx=vm.createContext({TextEncoder,config,capabilities:caps,selected:0,dirty:true,saving:false,$:get,
    RotationPreview:{clone:structuredClone,stringify:JSON.stringify},
    cancelActionDrag(){},rotationSummary(){},selectInput(){},message(){},
    document:{querySelectorAll:()=>[get('save')]},
    fetch:async(path,options)=>{
      calls.push([path,options.method]);let result={message:'OK'};
      const body=options.body?JSON.parse(options.body):undefined;
      if(path==='/api/config/begin'){staged.network=body.network;staged.schemaVersion=body.schemaVersion;result={token:42};}
      else if(path.startsWith('/api/config/chain?')){
        assert.deepEqual(body.actions,[{protocol:'stopOtherChains',delayMs:0}]);staged.chains.push(body);
      }else if(path.startsWith('/api/config/rotation?'))staged.encoderRotation=body;
      else if(path.startsWith('/api/config/commit?')){assert.equal(staged.chains.length,28);stored=structuredClone(staged);}
      else if(path==='/api/config')result=stored;
      else if(path==='/api/capabilities')result=caps;
      else if(path==='/api/status')result={storageState:'ready',configOutputsAllowed:true};
      else assert.equal(path,'/api/panic');
      return new Response(JSON.stringify(result));
    }});
  const save=html.slice(html.indexOf('async function saveConfig('),html.indexOf("$('save').onclick"));
  vm.runInContext(fs.readFileSync('web/presets.js','utf8')+'\n'+
    ['async function api(','async function load(','function run('].map(line).join('\n')+'\n'+save+'\n'+line("$('panic').onclick"),ctx);
  await ctx.saveConfig();assert(!ctx.dirty&&!ctx.saving&&!get('save').disabled);
  ctx.config=undefined;await ctx.load();
  assert.deepEqual(JSON.parse(JSON.stringify(ctx.config.chains)),config.chains);
  const before=JSON.stringify(ctx.config);
  await get('panic').onclick();
  assert.deepEqual(calls.at(-1),['/api/panic','POST']);
  assert.equal(JSON.stringify(ctx.config),before);
}
(async()=>{await check(true);await check(false);console.log('PASS Stop UI save/load: 28 staged chains, USB/BLE-only, persisted reload, existing Panic POST binding');})().catch(e=>{console.error(e);process.exitCode=1;});
