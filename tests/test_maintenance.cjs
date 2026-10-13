const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const script=fs.readFileSync('web/maintenance.js','utf8');
const tick=()=>new Promise(resolve=>setImmediate(resolve));
async function fixture({confirm=true,state='idle',failure=false}={}){
  const nodes={factoryReset:{disabled:false},factoryResetMessage:{textContent:''}},calls=[];
  const ctx=vm.createContext({document:{getElementById:id=>nodes[id]},confirm:()=>confirm,fetch:async(url,options)=>{
    calls.push({url,options});
    if(!options)return {ok:true,json:async()=>({state})};
    return {ok:!failure,json:async()=>({message:'write failed'})};
  }});
  vm.runInContext(script,ctx);await tick();return {nodes,calls};
}
(async()=>{
  const cancel=await fixture({confirm:false});await cancel.nodes.factoryReset.onclick();assert.equal(cancel.calls.length,1);
  const ok=await fixture();await ok.nodes.factoryReset.onclick();await ok.nodes.factoryReset.onclick();
  assert.equal(ok.calls.length,2);assert.equal(ok.calls[1].options.method,'POST');
  assert.deepEqual(JSON.parse(ok.calls[1].options.body),{confirm:'factory-reset'});
  assert.match(ok.nodes.factoryResetMessage.textContent,/消去完了ではありません/);assert(ok.nodes.factoryReset.disabled);
  const fail=await fixture({failure:true});await fail.nodes.factoryReset.onclick();assert(!fail.nodes.factoryReset.disabled);assert.match(fail.nodes.factoryResetMessage.textContent,/受付を確認できません/);
  const done=await fixture({state:'complete'});assert.match(done.nodes.factoryResetMessage.textContent,/前回.*完了/);
  const stopped=await fixture({state:'failed'});assert(!stopped.nodes.factoryReset.disabled);assert.match(stopped.nodes.factoryResetMessage.textContent,/停止/);
  const pending=await fixture({state:'accepted'});assert(pending.nodes.factoryReset.disabled);
  for(const page of ['web/index.html','web/wifi.html']){
    const html=fs.readFileSync(page,'utf8');assert.equal((html.match(/id="factoryReset"/g)||[]).length,1);
    assert(html.includes('maintenance.js')&&html.includes('Erase device'));
    if(page.endsWith('index.html')){
      const section=html.slice(html.indexOf('id="maintenance"'));
      assert(section.includes('id="panic"')&&section.includes('id="restart"'));
      assert(!html.slice(html.indexOf('<div class="toolbar">'),html.indexOf('<div id="message"')).includes('id="panic"'));
    }else assert(!html.includes('id="panic"')&&!html.includes('id="restart"'));
  }
  console.log('PASS Maintenance independent startup / confirmation / cancel / duplicate / failure / completion / placement');
})().catch(e=>{console.error(e);process.exitCode=1;});
