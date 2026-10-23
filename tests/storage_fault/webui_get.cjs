// Execute the existing api/load functions unchanged. Use real fetch over HTTP
// for an interrupted chunked response as well as a finalized truncated body.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),http=require('node:http');
const html=fs.readFileSync('web/index.html','utf8');
const extract=name=>{const match=html.match(new RegExp('^async function '+name+'\\(.*$','m'));assert(match,`missing ${name}`);return match[0];};
const complete=fs.readFileSync('.pio/storage-fault-tests/get-complete.json','utf8');
const partial=fs.readFileSync('.pio/storage-fault-tests/get-aborted.json','utf8');
const nodes={};const node=id=>nodes[id]??={};
async function check(mode){
  let socket,server;
  if(mode==='disconnect'){
    server=http.createServer((req,res)=>{socket=res.socket;res.writeHead(200,{'Content-Type':'application/json','Transfer-Encoding':'chunked'});res.flushHeaders();res.write(partial);});
    await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  }
  const original=mode==='503-empty'?undefined:{sentinel:'previous editor state'};let parsed=0;
  const ctx=vm.createContext({config:original,capabilities:{},selected:0,dirty:true,$:node,
    selectInput(){},rotationSummary(){},message(){},Presets:{full:c=>c,read(){++parsed;}},
    fetch:async path=>{
      if(path==='/api/capabilities')return new Response('{}');
      if(path==='/api/status')return new Response('{"storageState":"ready","configOutputsAllowed":true}');
      assert.equal(path,'/api/config');
      if(mode==='disconnect'){const response=await fetch(`http://127.0.0.1:${server.address().port}/`);assert.equal(response.status,200);socket.destroy();return response;}
      if(mode==='500')return new Response('{"ok":false,"message":"encode failed"}',{status:500});
      if(mode.startsWith('503'))return new Response('{"ok":false,"message":"Stored settings require reload. Restart required."}',{status:503});
      return new Response(mode==='complete'?complete:partial);
    }});
   vm.runInContext(fs.readFileSync('web/wifi-password.js','utf8')+'\nconst passwordEditor=WifiPassword.create($);\n'+extract('api')+'\n'+extract('load'),ctx);
  try {
    if(mode==='complete'){await ctx.load();assert.equal(ctx.config.chains[0].actions.length,16);assert.equal(ctx.config.encoderRotation.rotationValue.outputs.length,16);assert.equal(parsed,1);}
    else {await assert.rejects(ctx.load());assert.equal(ctx.config,original);assert.equal(parsed,0);}
  } finally {if(server)await new Promise(resolve=>server.close(resolve));}
}
const tick=()=>new Promise(resolve=>setImmediate(resolve));
async function recoveryUi(){
  const controls={},calls=[],messages=[];
  const get=id=>controls[id]??={disabled:false,value:'',classList:{toggle(){}},textContent:''};
  const ctx=vm.createContext({config:undefined,capabilities:{},dirty:false,$:get,confirm:()=>true,
    message:(text)=>messages.push(text),document:{getElementById:get},fetch:async(path,options)=>{
      calls.push([path,options?.method]);
      if(path==='/api/config')return new Response('{"message":"Restart required"}',{status:503});
      if(path==='/api/factory-reset')return new Response(options?.method==='POST'?'{"message":"accepted"}':'{"state":"idle"}');
      return new Response('{"message":"Restarting"}');
    }});
  vm.runInContext(extract('api')+'\n'+extract('load')+'\n'+html.match(/^function run\(.*$/m)[0]+'\n'+html.match(/^\$\('restart'\)\.onclick=.*$/m)[0],ctx);
  await assert.rejects(ctx.load());await ctx.api('status');assert(calls.some(([p])=>p==='/api/status'));
  await get('restart').onclick();assert(calls.some(([p,m])=>p==='/api/restart'&&m==='POST'));
  vm.runInContext(fs.readFileSync('web/maintenance.js','utf8'),ctx);await tick();await get('factoryReset').onclick();
  assert(calls.some(([p,m])=>p==='/api/factory-reset'&&m==='POST'));assert.equal(ctx.config,undefined);
  assert(html.indexOf("$('restart').onclick")<html.indexOf('run(load)()')); // binding precedes failing startup load

  const wifi=fs.readFileSync('web/wifi.html','utf8');assert(wifi.includes('id="wifiRestart"'));
  const script=wifi.match(/<script>\s*([\s\S]*?)<\/script>/)[1];
  const wnodes={save:{disabled:true}};const wget=id=>wnodes[id]??={value:'unchanged',disabled:false,classList:{toggle(){}},textContent:''};
  const wctx=vm.createContext({document:{getElementById:wget},confirm:()=>true,fetch:async(path,options)=>{
    calls.push([path,options?.method]);
    if(path==='/api/wifi')return new Response('{"message":"Stored settings require reload. Restart required."}',{status:503});
    return new Response('{"message":"Restarting","state":"idle"}');
  }});
  vm.runInContext(fs.readFileSync('web/wifi-password.js','utf8'),wctx);vm.runInContext(script,wctx);vm.runInContext(fs.readFileSync('web/maintenance.js','utf8'),wctx);await tick();
  assert.equal(wget('ssid').value,'unchanged');assert.equal(wget('password').value,'unchanged');assert(wget('save').disabled);
  assert.match(wget('message').textContent,/Restart/);await wget('wifiRestart').onclick();await wget('factoryReset').onclick();
  assert.equal(typeof wget('wifiRestart').onclick,'function');
}
async function saveError(failedPath){
  const original=JSON.parse(complete),before=JSON.stringify(original),nodes={},calls=[],messages=[];
  const get=id=>nodes[id]??={value:original.network[id],disabled:false};
  const ctx=vm.createContext({config:original,capabilities:{},dirty:true,saving:false,$:get,
    RotationPreview:{clone:structuredClone,stringify:JSON.stringify},Presets:{full:c=>c,read:c=>({config:c})},
    cancelActionDrag(){},rotationSummary(){},message:text=>messages.push(text),document:{querySelectorAll:()=>[get('save')]},
    fetch:async(path)=>{calls.push(path);const failed=path.startsWith('/api/'+failedPath);
      return new Response(JSON.stringify(failed?{ok:false,message:'Settings saved to storage, but runtime apply failed. Do not resave; Restart required.'}:{token:42,message:'Staged'}),{status:failed?400:200});}
  });
  const save=html.slice(html.indexOf('async function saveConfig('),html.indexOf("$('save').onclick"));
  vm.runInContext(fs.readFileSync('web/wifi-password.js','utf8')+'\nconst passwordEditor=WifiPassword.create($);passwordEditor.load(false);',ctx);
  vm.runInContext(extract('api')+'\n'+html.match(/^function run\(.*$/m)[0]+'\n'+save,ctx);
  await vm.runInContext('run(()=>saveConfig())()',ctx);
  assert.equal(ctx.config,original);assert.equal(JSON.stringify(original),before);assert(ctx.dirty);assert(!ctx.saving&&!get('save').disabled);
  assert(messages.some(s=>s.includes('Restart required')));assert(!calls.includes('/api/config'));
  if(failedPath==='config/begin')assert.equal(calls.length,1);
  else assert(calls.some(p=>p.startsWith('/api/config/commit')));
}
(async()=>{for(const mode of ['500','503','503-empty','truncated','disconnect','complete'])await check(mode);
await recoveryUi();await saveError('config/begin');await saveError('config/commit');
console.log('PASS WebUI H1/H4: 503 fresh/existing editor, Wi-Fi credentials retained, save errors preserve edits, Restart/Factory Reset accessible; chunked disconnect rejected; normal Config accepted');
})().catch(e=>{console.error(e);process.exitCode=1;});
