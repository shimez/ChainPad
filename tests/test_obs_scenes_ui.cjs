const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const flush=()=>new Promise(resolve=>setImmediate(resolve));
async function main(){
  const calls=[],document={hidden:false};let reply={state:'loading',complete:false,scenes:[],generation:1},fail=false,hold;
  const ctx=vm.createContext({TextEncoder,document,Date,fetch:async(path,options)=>{
    calls.push({path,...options});if(hold)await hold;if(fail)throw Error();
    return {ok:true,json:async()=>reply};
  }});
  vm.runInContext(fs.readFileSync('web/obs-actions.js','utf8'),ctx);
  const manager=vm.runInContext('ObsActionsUI.create()',ctx);
  manager.init({obsActions:false});await flush();assert.equal(calls.length,0);
  manager.init({obsActions:true});await flush();assert.equal(calls[0].method,'POST');
  reply={state:'ready',complete:true,scenes:['ゲーム'],generation:1,ageMs:0};
  manager.tick({connectionState:'Ready'});await flush();assert(manager.snapshot().complete);
  const count=calls.length;manager.tick({connectionState:'Ready'});await flush();
  assert.equal(calls.length,count+1);assert.equal(calls.at(-1).method,'GET');
  reply={...reply,generation:2,state:'stale',complete:false};
  manager.tick({connectionState:'Ready'});await flush();await flush();assert.equal(calls.at(-1).method,'POST');
  fail=true;await manager.refresh();assert.equal(manager.snapshot().state,'error');assert(!manager.snapshot().complete);
  assert.equal(manager.snapshot().scenes[0],'ゲーム');fail=false;
  let release;hold=new Promise(resolve=>release=resolve);
  const pending=manager.refresh();const n=calls.length;await manager.refresh();assert.equal(calls.length,n);
  release();await pending;hold=null;
  document.hidden=true;await manager.refresh();manager.tick({connectionState:'Ready'});assert.equal(calls.length,n);
  document.hidden=false;manager.failed();assert(!manager.snapshot().complete);
  reply={state:'ready',complete:true,scenes:[],generation:2,ageMs:60000};await manager.refresh();assert(!manager.snapshot().complete);
  console.log('PASS scene manager: initial refresh, Ready/reconnect retry, cache GET, stale age, network failure, single-flight, hidden-page pause, retained names');
}
main().catch(e=>{console.error(e);process.exitCode=1;});
