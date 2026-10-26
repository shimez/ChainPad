const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const flush=()=>new Promise(resolve=>setImmediate(resolve));
async function main(){
  const calls=[],document={hidden:false};let now=0,reply={state:'loading',complete:false,scenes:[],generation:1},fail=false,hold;
  const ctx=vm.createContext({TextEncoder,document,Date:{now:()=>now},fetch:async(path,options)=>{
    calls.push({path,...options});const data=reply;if(hold)await hold;if(fail)throw Error();
    return {ok:true,json:async()=>data};
  }});
  vm.runInContext(fs.readFileSync('web/obs-actions.js','utf8'),ctx);
  const manager=vm.runInContext('ObsActionsUI.create()',ctx);
  const ready=generation=>({connectionState:'Ready',generation});
  manager.init({obsActions:false});manager.tick(ready(1));await flush();assert.equal(calls.length,0);
  manager.init({obsActions:true});await flush();assert.equal(calls.length,0);
  manager.tick({connectionState:'Disconnected',generation:1});await flush();assert.equal(calls.length,0);
  manager.tick(ready(1));await flush();assert.equal(calls.length,0);
  await manager.refresh();assert.equal(calls[0].method,'POST');
  reply={state:'ready',complete:true,scenes:['ゲーム'],generation:1,ageMs:0};
  manager.tick(ready(1));await flush();assert.equal(calls.at(-1).method,'GET');assert(manager.snapshot().complete);
  const count=calls.length;
  for(let i=0;i<10;i++)manager.tick(ready(1));await flush();assert.equal(calls.length,count);
  now=60001;manager.tick(ready(1));assert(!manager.snapshot().complete);assert.equal(calls.length,count);
  reply={...reply,generation:2};manager.tick(ready(2));await flush();assert.equal(calls.length,count);assert(!manager.snapshot().complete);
  await manager.refresh();assert(manager.snapshot().complete);
  fail=true;await manager.refresh();assert.equal(manager.snapshot().state,'error');assert(!manager.snapshot().complete);
  assert.equal(manager.snapshot().scenes[0],'ゲーム');fail=false;
  let release;hold=new Promise(resolve=>release=resolve);
  const pending=manager.refresh();const n=calls.length;await manager.refresh();assert.equal(calls.length,n);
  manager.tick({connectionState:'Disconnected',generation:3});release();await pending;hold=null;
  assert(!manager.snapshot().complete); // Late pre-disconnect reply cannot restore authority.
  reply={...reply,generation:3};manager.tick(ready(3));await flush();assert.equal(calls.length,n);
  await manager.refresh();assert(manager.snapshot().complete);
  hold=new Promise(resolve=>release=resolve);
  const oldRequest=manager.refresh();const beforeReconnect=calls.length;
  reply={...reply,generation:4};manager.tick(ready(4));assert.equal(calls.length,beforeReconnect);
  hold=null;release();await oldRequest;await flush();
  assert.equal(calls.length,beforeReconnect);assert(!manager.snapshot().complete);
  await manager.refresh();assert.equal(manager.snapshot().generation,4);assert(manager.snapshot().complete);
  const hiddenCount=calls.length;document.hidden=true;await manager.refresh();manager.tick(ready(4));assert.equal(calls.length,hiddenCount);
  document.hidden=false;manager.failed();assert(!manager.snapshot().complete);
  reply={state:'loading',complete:false,scenes:[],generation:4};manager.tick(ready(4));await flush();assert.equal(calls.length,hiddenCount);
  await manager.refresh();
  now+=20001;const beforeTimeout=calls.length;manager.tick(ready(4));assert.equal(manager.snapshot().state,'error');assert.equal(calls.length,beforeTimeout);
  console.log('PASS scene manager: manual-only discovery across startup/Ready/reconnect, bounded result polling, expiry, late response isolation, failures and hidden-page pause');
}
main().catch(e=>{console.error(e);process.exitCode=1;});
