const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const {test}=require('node:test');
const source=fs.readFileSync('web/firmware-update.js','utf8');
const html=fs.readFileSync('web/index.html','utf8');
const hardware=chip=>`XIAO ESP32${chip} / ChainOSCPad PCB`;
const metadata=(version='0.9.0',chips=['S3','C6','C5'])=>({version,files:chips.map(c=>({chipFamily:`ESP32-${c}`}))});
const response=data=>({ok:true,json:async()=>data});
function fixture(fetcher=async()=>response(metadata())){
  const nodes={},timers=new Map(),calls=[];let serial=0;
  const node=tag=>({tag,hidden:true,children:[],textContent:'',dataset:{},listeners:{},setAttribute(key,value){this[key]=value;},addEventListener(name,fn){this.listeners[name]=fn;},replaceChildren(...items){this.children=items;},append(item){this.children.push(item);}});
  const get=id=>nodes[id]??=node();
  const ctx=vm.createContext({AbortController,URL,TextDecoder,Uint8Array,document:{hidden:false,createElement:node},$:get,
    fetch:(url,options)=>{calls.push({url,options});return fetcher(url,options);},
    setTimeout:(fn,ms)=>{const id=++serial;timers.set(id,{fn,ms});return id;},clearTimeout:id=>timers.delete(id)});
  vm.runInContext(source+'\nconst firmwareUpdate=FirmwareUpdate.create($("firmwareUpdate"));',ctx);
  const check=s=>{ctx.input=s;return vm.runInContext('firmwareUpdate.check(input)',ctx);};
  return {ctx,get,calls,timers,check};
}
test('maps actual firmware hardware strings to all distributed chip families',async()=>{
  const caps=fs.readFileSync('src/capabilities.h','utf8');
  for(const chip of ['S3','C6','C5']){
    assert(caps.includes(hardware(chip)));
    const f=fixture(async()=>response(metadata('0.9.0',[chip])));
    await f.check({version:'0.8.0',hardware:hardware(chip)});
    assert.equal(f.get('firmwareUpdate').hidden,false);
    const link=f.get('firmwareUpdate').children[1];
    assert.equal(link.href,'https://shimez.github.io/ChainPad/installer/');assert.equal(link.target,'_blank');
    assert.equal(link.rel,'noopener noreferrer');
    await f.check({version:'0.8.0',hardware:hardware(chip)});assert.equal(f.calls.length,1);
    assert.equal(f.calls[0].options.credentials,'omit');assert.equal(f.calls[0].options.cache,'no-store');
    assert.equal(f.timers.size,0);
  }
  for(const name of [hardware('C3'),'unknown','toString']){
    const f=fixture();await f.check({version:'0.8.0',hardware:name});assert.equal(f.calls.length,0);
  }
  const f=fixture(async()=>response(metadata('0.9.0',['C6'])));
  await f.check({version:'0.8.0',hardware:hardware('S3')});assert(f.get('firmwareUpdate').hidden);
});
test('numeric version comparison and malformed metadata',async()=>{
  for(const [current,latest,shown] of [['0.9.0','0.10.0',true],['0.8.0','0.8.1',true],['0.8.0','1.0.0',true],['0.8.0','0.8.0',false],['0.8.0','0.7.9',false],['0.8.0','0.8.0+abcdef0',false],['0.8.0','0.9.0-rc1',false],['bad','0.9.0',false],['0.8.0','<script>',false]]){
    const f=fixture(async()=>response(metadata(latest)));await f.check({version:current,hardware:hardware('S3')});
    assert.equal(!f.get('firmwareUpdate').hidden,shown,`${current} -> ${latest}`);
  }
  for(const data of [null,{}, {version:'0.9.0',files:null},{version:'0.9.0',files:[null,{}]}]){
    const f=fixture(async()=>response(data));await f.check({version:'0.8.0',hardware:hardware('S3')});assert(f.get('firmwareUpdate').hidden);
  }
});
test('offline, HTTP, JSON and timeout failures are silent and not retried',async()=>{
  for(const fetcher of [async()=>{throw Error('offline');},async()=>({ok:false}),async()=>({ok:true,json:async()=>{throw Error('JSON');}})]){
    const f=fixture(fetcher),s={version:'0.8.0',hardware:hardware('S3')};
    await f.check(s);await f.check(s);assert(f.get('firmwareUpdate').hidden);assert.equal(f.calls.length,1);assert.equal(f.timers.size,0);
  }
  const f=fixture((url,{signal})=>new Promise((resolve,reject)=>signal.addEventListener('abort',()=>reject(Error('timeout')))));
  const pending=f.check({version:'0.8.0',hardware:hardware('S3')});
  const timer=[...f.timers.values()][0];assert.equal(timer.ms,5000);timer.fn();await pending;
  assert(f.get('firmwareUpdate').hidden);assert.equal(f.timers.size,0);
});
test('production status waits for device data but never waits for remote lookup',async()=>{
  let resolveDevice,rejectRemote,failures=0;
  const f=fixture(()=>new Promise((resolve,reject)=>rejectRemote=reject));
  Object.assign(f.ctx,{statusTimer:null,api:()=>new Promise(resolve=>resolveDevice=resolve),
    obsEditor:{epoch:()=>0,updateStatus(){},statusFailed(){failures++;}},obsScenes:{tick(){},failed(){failures++;}}});
  vm.runInContext(html.match(/^async function status\(.*$/m)[0],f.ctx);
  const poll=f.ctx.status();assert.equal(f.calls.length,0);
  resolveDevice({version:'0.8.0',hardware:hardware('C5'),wifi:true,ip:'192.168.1.2'});await poll;
  assert.equal(f.calls.length,1);assert.equal(f.ctx.status.running,false);
  assert.equal(f.get('systemVersion').textContent,'0.8.0');assert.equal(f.get('systemIp').textContent,'192.168.1.2');
  assert([...f.timers.values()].some(t=>t.ms===3000));
  rejectRemote(Error('offline'));await new Promise(resolve=>setImmediate(resolve));assert.equal(failures,0);
  const next=f.ctx.status();resolveDevice({version:'0.8.0',hardware:hardware('C5')});await next;
  assert.equal(f.calls.length,1);assert.equal(failures,0);
});
test('device status failure does not consume the one-time update check',async()=>{
  const f=fixture();let unavailable=true;
  Object.assign(f.ctx,{statusTimer:null,api:async()=>{if(unavailable)throw Error('device offline');return {version:'0.8.0',hardware:hardware('S3')};},
    obsEditor:{epoch:()=>0,updateStatus(){},statusFailed(){}},obsScenes:{tick(){},failed(){}}});
  vm.runInContext(html.match(/^async function status\(.*$/m)[0],f.ctx);
  await f.ctx.status();assert.equal(f.calls.length,0);
  unavailable=false;await f.ctx.status();await new Promise(resolve=>setImmediate(resolve));
  assert.equal(f.calls.length,1);assert.equal(f.get('firmwareUpdate').hidden,false);
});
const settle=()=>new Promise(resolve=>setImmediate(resolve));
function historyFixture(mutate=()=>{},fetchHistory){
  const data={schemaVersion:1,firmwareVersion:'0.12.0',firmwareCommit:'a'.repeat(40),
    releases:['0.9.0','0.13.0','0.12.0','0.8.0','0.11.0','0.10.0'].map(version=>({version,features:['機能 <img src=x>'],improvements:[]}))};
  const info={...metadata('0.12.0'),commit:data.firmwareCommit};
  mutate(data,info);
  const bytes=new TextEncoder().encode(JSON.stringify(data));
  info.changes={path:'changes.json',schemaVersion:1,bytes:bytes.length,sha256:'b'.repeat(64),...info.changes};
  const f=fixture(async url=>url.endsWith('build.json')?response(info):fetchHistory?fetchHistory():
    {ok:true,body:new ReadableStream({start(controller){controller.enqueue(bytes);controller.close();}})});
  return f;
}
const device={version:'0.8.0',hardware:hardware('S3')};
test('history filters numeric range, sorts, initially renders three and lazily expands the rest',async()=>{
  const f=historyFixture();await f.check(device);await settle();
  const panel=f.get('firmwareUpdate'),details=panel.children[2].children[0];
  assert.equal(details.tag,'details');assert(!details.open);
  assert.equal(details.children[0].textContent,'追加・改善された内容を見る');
  assert.deepEqual(details.children.slice(1,4).map(n=>n.children[0].children[0].textContent),['v0.12.0 ↗','v0.11.0 ↗','v0.10.0 ↗']);
  assert.equal(details.children[1].children[2].children[0].textContent,'機能 <img src=x>');
  const more=details.children[4];assert.equal(more.children[0].textContent,'ほか1バージョン分を表示');
  assert.equal(more.children.length,1);more.open=true;more.listeners.toggle();more.listeners.toggle();
  assert.equal(more.children.length,2);assert.equal(more.children[1].children[0].children[0].href,'https://github.com/shimez/ChainPad/releases/tag/v0.9.0');
  assert.equal(f.calls.length,2);assert.equal(f.calls[1].options.cache,'no-store');assert.equal(f.timers.size,0);
});
test('history errors preserve the notification and installer, with a Releases fallback',async()=>{
  for(const mutate of [d=>d.schemaVersion=2,d=>d.firmwareVersion='0.11.0',d=>d.firmwareCommit='c'.repeat(40),
    d=>d.releases.push(d.releases[0]),d=>d.releases=d.releases.filter(r=>r.version!=='0.12.0'),
    d=>d.releases[0].features=['x'.repeat(1025)],d=>d.releases[0].improvements=null,
    (d,i)=>i.changes={path:'https://other.invalid/history'},(d,i)=>i.changes={bytes:262145}]){
    const f=historyFixture(mutate);await f.check(device);await settle();
    const panel=f.get('firmwareUpdate');assert.equal(panel.hidden,false);
    assert.equal(panel.children[1].href,'https://shimez.github.io/ChainPad/installer/');
    assert.equal(panel.children[2].children[1].href,'https://github.com/shimez/ChainPad/releases');
    assert.equal(f.timers.size,0);
  }
  for(const fetcher of [()=>{throw Error('offline');},()=>({ok:false}),()=>({ok:true,body:new ReadableStream({start(c){c.enqueue(new Uint8Array(262145));c.close();}})})]){
    const f=historyFixture(()=>{},fetcher);await f.check(device);await settle();
    assert.equal(f.get('firmwareUpdate').children[2].children[0].textContent,'変更履歴を取得できませんでした。 ');
  }
});
test('history timeout is independent and does not block status or configuration tasks',async()=>{
  const f=historyFixture(()=>{},()=>new Promise(()=>{}));await f.check(device);await settle();
  assert.equal(f.get('firmwareUpdate').hidden,false);
  vm.runInContext('configurationSaved=true',f.ctx);assert.equal(f.ctx.configurationSaved,true);
  const timer=[...f.timers.values()][0];assert.equal(timer.ms,5000);timer.fn();await settle();
  assert.equal(f.calls[1].options.signal.aborted,true);assert.equal(f.timers.size,0);
  assert.equal(f.get('firmwareUpdate').children[2].children[1].href,'https://github.com/shimez/ChainPad/releases');
  await f.check(device);assert.equal(f.calls.length,2);
});
test('production device polling continues during history retrieval and after failure',async()=>{
  let failHistory,failures=0,polls=0;
  const f=historyFixture(()=>{},()=>new Promise((_,reject)=>failHistory=reject));
  Object.assign(f.ctx,{statusTimer:null,api:async()=>{polls++;return {...device,wifi:true,ip:'192.168.1.2'};},
    obsEditor:{epoch:()=>0,updateStatus(){},statusFailed(){failures++;}},obsScenes:{tick(){},failed(){failures++;}}});
  vm.runInContext(html.match(/^async function status\(.*$/m)[0],f.ctx);
  await f.ctx.status();await settle();
  assert.equal(f.calls.length,2);assert.equal(f.ctx.status.running,false);
  await f.ctx.status();assert.equal(polls,2);assert.equal(failures,0);
  failHistory(Error('history offline'));await settle();await f.ctx.status();
  assert.equal(polls,3);assert.equal(failures,0);assert.equal(f.calls.length,2);
  assert.equal(f.get('systemIp').textContent,'192.168.1.2');
  assert.equal(f.get('firmwareUpdate').children[1].href,'https://shimez.github.io/ChainPad/installer/');
});
test('no update never requests history; empty categories and legacy metadata are handled',async()=>{
  const f=historyFixture();await f.check({...device,version:'0.12.0'});await settle();assert.equal(f.calls.length,1);
  const empty=historyFixture(d=>{d.releases=[{version:'0.12.0',features:[],improvements:[]}];});
  await empty.check(device);await settle();
  const details=empty.get('firmwareUpdate').children[2].children[0];assert.equal(details.children.length,2);
  assert.equal(details.children[1].children[1].textContent,'追加機能・修正改善の記載はありません。');
  const legacy=fixture();await legacy.check(device);await settle();assert.equal(legacy.calls.length,1);
  assert.equal(legacy.get('firmwareUpdate').children[2].children[1].href,'https://github.com/shimez/ChainPad/releases');
});
