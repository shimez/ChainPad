const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const {test}=require('node:test'),{createHash,webcrypto}=require('node:crypto');
const source=fs.readFileSync('site/installer/metadata.js','utf8');
const html=fs.readFileSync('site/installer/index.html','utf8');
const tick=()=>new Promise(resolve=>setImmediate(resolve));
function data(){
  const info={version:'0.10.0',commit:'a'.repeat(40),files:['S3','C5','C6'].map(chip=>({chipFamily:`ESP32-${chip}`,path:`firmware/ChainPad-ESP32-${chip}.bin`}))};
  const history={schemaVersion:1,firmwareVersion:info.version,firmwareCommit:info.commit,releases:
    ['0.11.0','0.9.0','0.10.0'].map(version=>({version,features:[version==='0.10.0'?'<img src=x onerror=alert(1)> 日本語':'別の版'],improvements:['操作を改善しました。']}))};
  return {info,history};
}
function fixture({mutate=()=>{},changeInfo=()=>{},buildResponse,historyResponse,payload}={}){
  const {info,history}=data();mutate(history);
  const encoded=payload??Buffer.from(JSON.stringify(history));
  info.changes={path:'changes.json',schemaVersion:1,bytes:encoded.length,sha256:createHash('sha256').update(encoded).digest('hex')};
  changeInfo(info);
  const nodes={},calls=[],timers=new Map();let serial=0,historyResolve;
  const historyStarted=new Promise(resolve=>historyResolve=resolve);
  const node=tag=>({tag,textContent:'',children:[],replaceChildren(...items){this.children=items;},append(item){this.children.push(item);}});
  for(const name of ['build','downloads','historySummary','historyContent'])nodes[name]=node('div');
  const ctx=vm.createContext({AbortController,Uint8Array,TextDecoder,crypto:webcrypto,
    document:{createElement:node,getElementById(id){assert(id in nodes,`must not access install control: ${id}`);return nodes[id];}},
    fetch:(url,options)=>{calls.push({url,options});if(url==='../build.json')return buildResponse?buildResponse(options):Promise.resolve(new Response(JSON.stringify(info)));
      assert.equal(url,'../changes.json');historyResolve();return historyResponse?historyResponse(options):Promise.resolve(new Response(encoded));},
    setTimeout:(fn,ms)=>{const id=++serial;timers.set(id,{fn,ms});return id;},clearTimeout:id=>timers.delete(id)});
  const ready=vm.runInContext(source,ctx);
  return {nodes,calls,timers,ready,historyStarted,encoded};
}
function text(node){return [node.textContent,...node.children.map(text)].join(' ');}
function failed(f){assert.match(text(f.nodes.historyContent),/取得できませんでした/);assert.equal(f.nodes.historyContent.children.at(-1).href,'https://github.com/shimez/ChainPad/releases');}
test('uses build version exactly, safely renders Japanese text, and preserves downloads',async()=>{
  const f=fixture();await f.ready;
  assert.equal(f.nodes.build.textContent,'0.10.0 · aaaaaaa');
  assert.equal(f.nodes.historySummary.textContent,'公開版 v0.10.0 の変更内容');
  assert.match(text(f.nodes.historyContent),/<img src=x onerror=alert\(1\)> 日本語/);
  assert(!text(f.nodes.historyContent).includes('別の版'));
  const link=f.nodes.historyContent.children.at(-1);
  assert.equal(link.href,'https://github.com/shimez/ChainPad/releases/tag/v0.10.0');
  assert.equal(link.rel,'noopener noreferrer');assert.equal(link.target,'_blank');
  assert.equal(f.nodes.downloads.children.length,3);
  assert.equal(f.nodes.downloads.children[0].href,'../firmware/ChainPad-ESP32-S3.bin');
  assert.equal(f.nodes.downloads.children[0].download,'');
  assert.equal(f.calls.length,2);assert(f.calls.every(c=>c.options.cache==='no-store'&&c.options.credentials==='omit'));
  assert.equal(f.timers.size,0);
});
test('legacy metadata does not request history and still offers downloads',async()=>{
  const f=fixture({changeInfo:i=>delete i.changes});await f.ready;
  assert.equal(f.calls.length,1);assert.equal(f.nodes.downloads.children.length,3);
  assert.match(text(f.nodes.historyContent),/この公開版の変更履歴/);assert.equal(f.timers.size,0);
});
test('empty categories are omitted and no changes is distinguished from failure',async()=>{
  const f=fixture({mutate:h=>{h.releases.find(r=>r.version==='0.10.0').features=[];}});await f.ready;
  assert(!text(f.nodes.historyContent).includes('追加機能'));assert.match(text(f.nodes.historyContent),/修正・改善/);
  const empty=fixture({mutate:h=>{h.releases=[{version:'0.10.0',features:[],improvements:[]}];}});await empty.ready;
  assert.match(text(empty.nodes.historyContent),/記載はありません/);
});
test('schema, provenance, duplicate and missing version, and bounds fail only history',async()=>{
  for(const mutate of [h=>h.schemaVersion=2,h=>h.firmwareVersion='0.11.0',h=>h.firmwareCommit='b'.repeat(40),
    h=>h.releases.push(h.releases[0]),h=>h.releases=h.releases.filter(r=>r.version!=='0.10.0'),
    h=>h.releases[0].features=null,h=>h.releases[0].features=['a'.repeat(1025)],
    h=>h.releases[0].features=['\u0000'],h=>h.releases[0].features=Array(65).fill('x'),
    h=>h.releases[0].version='01.0.0',h=>h.releases=Array(257).fill(h.releases[0])]){
    const f=fixture({mutate});await f.ready;failed(f);assert.equal(f.nodes.build.textContent,'0.10.0 · aaaaaaa');assert.equal(f.nodes.downloads.children.length,3);assert.equal(f.timers.size,0);
  }
});
test('malformed references do not fetch arbitrary paths or act as legacy',async()=>{
  for(const changeInfo of [i=>i.changes=null,i=>i.changes.path='../other',i=>i.changes.schemaVersion=2,
    i=>i.changes.bytes=262145,i=>i.changes.bytes=true,i=>i.changes.sha256='bad']){
    const f=fixture({changeInfo});await f.ready;failed(f);assert.equal(f.calls.length,1);
  }
});
test('HTTP, network, invalid JSON/UTF8, length and checksum errors are isolated',async()=>{
  for(const options of [
    {historyResponse:async()=>new Response('',{status:404})},
    {historyResponse:async()=>{throw Error('offline');}},
    {payload:Buffer.from('{')},{payload:Buffer.from([255])},
    {changeInfo:i=>i.changes.bytes++},{changeInfo:i=>i.changes.bytes--},
    {changeInfo:i=>i.changes.sha256='0'.repeat(64)},
    {historyResponse:async()=>new Response(new Uint8Array(262145))}
  ]){const f=fixture(options);await f.ready;failed(f);assert.equal(f.nodes.downloads.children.length,3);assert.equal(f.timers.size,0);}
});
test('build failures skip history; unsafe metadata cannot create download links',async()=>{
  for(const options of [{buildResponse:async()=>new Response('',{status:500})},
    {buildResponse:async()=>new Response('{')},{buildResponse:async()=>{throw Error('offline');}},
    {changeInfo:i=>i.files[0].path='https://other.invalid/file'},
    {changeInfo:i=>i.version='<script>'},{changeInfo:i=>i.commit='bad'},
    {buildResponse:async()=>new Response(new Uint8Array(65537))}]){
    const f=fixture(options);await f.ready;assert.match(f.nodes.build.textContent,/取得できません/);
    assert.equal(f.calls.length,1);assert.equal(f.nodes.downloads.children.length,0);assert.equal(f.timers.size,0);
  }
});
test('build and history timeouts complete even if fetch ignores abort; late success cannot overwrite fallback',async()=>{
  const build=fixture({buildResponse:()=>new Promise(()=>{})});
  const bt=[...build.timers.values()][0];assert.equal(bt.ms,5000);bt.fn();await build.ready;
  assert.equal(build.calls[0].options.signal.aborted,true);assert.equal(build.timers.size,0);
  let resolve;
  const f=fixture({historyResponse:()=>new Promise(r=>resolve=r)});await f.historyStarted;
  assert.equal(f.nodes.downloads.children.length,3);assert.equal(f.nodes.build.textContent,'0.10.0 · aaaaaaa');
  const timer=[...f.timers.values()][0];assert.equal(timer.ms,5000);timer.fn();await f.ready;failed(f);
  assert.equal(f.calls[1].options.signal.aborted,true);
  resolve(new Response(f.encoded));await tick();await tick();failed(f);assert.equal(f.timers.size,0);
});
test('body-stall timeout is bounded too',async()=>{
  const f=fixture({historyResponse:async()=>new Response(new ReadableStream({start(){}}))});await f.historyStarted;
  [...f.timers.values()][0].fn();await f.ready;failed(f);
});
test('markup retains ESP Web Tools and its activation button, with initially collapsed history',()=>{
  assert(html.includes('<script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js?module"></script>'));
  assert(html.includes('<esp-web-install-button manifest="../manifest.json"><button slot="activate">ChainPadをインストール</button>'));
  assert(html.includes('<details class="release-history"><summary id="historySummary">'));
  assert(html.includes('<script src="metadata.js"></script>'));
  assert(!source.includes('esp-web-install-button'));assert(!source.includes('manifest.json'));
});
