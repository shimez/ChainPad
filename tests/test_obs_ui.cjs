// Production OBS editor with the same lightweight DOM/VM approach as other UI tests.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const source=fs.readFileSync('web/obs-settings.js','utf8');
const html=fs.readFileSync('web/index.html','utf8');
const good=()=>({ok:true,storageState:'ready',settingsAvailable:true,settings:{enabled:true,ip:'192.0.2.1',port:4455,passwordConfigured:true},applyState:'accepted',error:null});
function fixture(caps={obsWebSocket:true,obsSettingsApi:true}){
  const nodes={},calls=[];let next,confirm=true,busy=false;
  const get=id=>nodes[id]??={value:'',checked:false,disabled:false,hidden:false,textContent:'',classList:{toggle(){}}};
  const ctx=vm.createContext({confirm:()=>confirm,fetch:async(path,options)=>{
    calls.push({path,...options});
    if(next){const fn=next;next=null;return fn(path,options);}
    const body=path.endsWith('capabilities')?caps:good();
    if(options.method==='PUT')body.saved=true;
    return new Response(JSON.stringify(body),{status:path.endsWith('reconnect')?202:200});
  }});
  vm.runInContext(source,ctx);
  const editor=vm.runInContext('ObsSettingsUI',ctx).create(get,()=>busy);
  return {get,calls,editor,ctx,next:fn=>next=fn,confirm:v=>confirm=v,busy:v=>busy=v};
}
const json=(data,status)=>new Response(JSON.stringify(data),{status});
async function main(){
  const f=fixture(),{get,editor,calls}=f;await editor.init();
  assert(!get('obsSection').hidden);assert.equal(get('obsIp').value,'192.0.2.1');assert.equal(get('obsPassword').value,'');
  const save=()=>get('obsSave').onclick();
  await save();assert(!('password' in JSON.parse(calls.at(-1).body)));assert.match(get('obsMessage').textContent,/接続結果/);
  get('obsPassword').value='test-only-replacement';get('obsPassword').oninput();await save();
  assert.equal(JSON.parse(calls.at(-1).body).password,'test-only-replacement');assert.equal(get('obsPassword').value,'');
  const clear=value=>{get('obsClear').checked=value;get('obsClear').onchange();};
  assert(!get('obsClear').checked);
  f.confirm(false);clear(true);assert(get('obsPassword').disabled);assert.match(get('obsPasswordStatus').textContent,/削除予定/);
  clear(false);assert(!get('obsPassword').disabled);await save();assert(!('password' in JSON.parse(calls.at(-1).body)));
  clear(true);await save();assert.equal(JSON.parse(calls.at(-1).body).password,'');assert(!get('obsClear').checked);assert(!get('obsPassword').disabled);
  f.confirm(true);
  get('obsIp').value='300.1.2.3';let count=calls.length;await save();assert.equal(calls.length,count);
  get('obsIp').value='192.0.2.2';get('obsPort').value='4455.5';await save();assert.equal(calls.length,count);
  get('obsPort').value='4455';get('obsPassword').value='retain-on-failure';
  f.next(()=>json({...good(),saved:false,error:{code:'ioError',message:'fixed failure'}},500));await save();
  assert.equal(get('obsIp').value,'192.0.2.2');assert.equal(get('obsPassword').value,'retain-on-failure');assert.match(get('obsMessage').textContent,/ioError/);
  f.next(()=>json({...good(),saved:true,applyState:'failed',error:{code:'applyFailed'}},503));await save();
  assert.equal(get('obsPassword').value,'');assert.match(get('obsMessage').textContent,/保存されましたが/);
  let release;f.next(()=>new Promise(resolve=>release=()=>resolve(json({...good(),saved:true},200))));
  const inflight=save();count=calls.length;await save();await get('obsReconnect').onclick();assert.equal(calls.length,count);assert(get('obsFields').disabled);release();await inflight;
  get('obsIp').value='192.0.2.77';get('obsIp').oninput();
  editor.updateStatus({connectionState:'Ready',lastError:-1,applyState:'accepted'});
  assert.equal(get('obsConnection').textContent,'Connection: Connected');assert.equal(get('obsIp').value,'192.0.2.77');
  editor.statusFailed();assert.match(get('obsConnection').textContent,/不明/);assert.equal(get('obsIp').value,'192.0.2.77');
  editor.updateStatus({connectionState:'Error',lastError:4009});assert.match(get('obsDiagnostics').textContent,/認証エラー/);
  editor.updateStatus({connectionState:'Authenticating'});assert.match(get('obsConnection').textContent,/Authenticating/);
  await get('obsReconnect').onclick();assert.equal(calls.at(-1).body,undefined);assert.match(get('obsMessage').textContent,/要求を受け付け/);assert.equal(get('obsIp').value,'192.0.2.77');
  get('obsEnabled').checked=false;get('obsEnabled').oninput();assert(get('obsReconnect').disabled);assert(!get('obsFields').disabled);
  f.next(()=>{throw Error('offline');});await save();assert.equal(get('obsIp').value,'192.0.2.77');assert.match(get('obsMessage').textContent,/確認できません/);
  f.next(()=>json({...good(),settingsAvailable:false,settings:null,storageState:'corrupt'},200));await editor.reload();
  get('obsIp').value='192.0.2.3';get('obsPort').value='4455';count=calls.length;await save();assert.equal(calls.length,count);
  clear(true);f.confirm(false);await save();assert.equal(calls.length,count);
  f.confirm(true);await save();assert.equal(JSON.parse(calls.at(-1).body).replaceInvalid,true);
  f.busy(true);editor.refresh();assert(get('obsSave').disabled);f.busy(false);
  for(const caps of [{},{obsWebSocket:false,obsSettingsApi:false},{obsWebSocket:true,obsSettingsApi:false}]){
    const n=fixture(caps);await n.editor.init();assert(n.get('obsSection').hidden);assert.equal(n.calls.length,1);
  }
  assert(calls.every(c=>['/api/capabilities','/api/obs','/api/obs/reconnect'].includes(c.path)));
  // Shared status request is single-flight, including the initial Config load.
  let resolve,requests=0;
  const ctx=vm.createContext({fetch:()=>{requests++;return new Promise(r=>resolve=()=>r(json({obs:{connectionState:'Ready'}},200)));}});
  vm.runInContext(html.match(/^async function api\(.*$/m)[0],ctx);
  const a=ctx.api('status'),b=ctx.api('status');assert.equal(requests,1);resolve();await Promise.all([a,b]);
  let timer,delay,updates=0;
  let epoch=0;
  Object.assign(ctx,{$:()=>({replaceChildren(){},append(){}}),obsScenes:{tick(){},failed(){}},obsEditor:{epoch:()=>epoch,updateStatus(){updates++;},statusFailed(){}},
    document:{hidden:false,createElement:()=>({})},clearTimeout:()=>{timer=null;},setTimeout:(fn,ms)=>{timer=fn;delay=ms;return 1;},statusTimer:null});
  vm.runInContext(html.match(/^async function status\(.*$/m)[0],ctx);
  const poll=ctx.status();await ctx.status();assert.equal(requests,2);resolve();await poll;assert.equal(delay,3000);assert.equal(updates,1);
  ctx.document.hidden=true;await ctx.status();assert.equal(requests,2);assert.equal(timer,null);
  ctx.document.hidden=false;const stale=ctx.status();epoch++;resolve();await stale;assert.equal(updates,1);
  const leaving=ctx.status();ctx.status.paused=true;resolve();await leaving;assert.equal(timer,null);
  assert(html.indexOf('id="oscDestination"')<html.indexOf('id="obsSection"'));
  for(const id of ['oscDestination','obsSection'])assert.match(html,new RegExp('<details class="network" id="'+id+'"(?: hidden)?>'));
  assert.match(html,/<summary>OBS WebSocket設定<span id="obsConnection"/);
  console.log('PASS OBS UI: independent API, capabilities, password preserve/change/clear/undo, invalid input, 500/503, replacement confirmation, reconnect, polling single-flight/visibility, edit retention');
}
main().catch(e=>{console.error(e);process.exitCode=1;});
