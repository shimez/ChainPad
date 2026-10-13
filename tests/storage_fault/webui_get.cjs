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
  const original={sentinel:'previous editor state'};let parsed=0;
  const ctx=vm.createContext({config:original,capabilities:{},selected:0,dirty:true,$:node,
    selectInput(){},rotationSummary(){},message(){},Presets:{full:c=>c,read(){++parsed;}},
    fetch:async path=>{
      if(path==='/api/capabilities')return new Response('{}');
      if(path==='/api/status')return new Response('{"storageState":"ready","configOutputsAllowed":true}');
      assert.equal(path,'/api/config');
      if(mode==='disconnect'){const response=await fetch(`http://127.0.0.1:${server.address().port}/`);assert.equal(response.status,200);socket.destroy();return response;}
      if(mode==='500')return new Response('{"ok":false,"message":"encode failed"}',{status:500});
      return new Response(mode==='complete'?complete:partial);
    }});
  vm.runInContext(extract('api')+'\n'+extract('load'),ctx);
  try {
    if(mode==='complete'){await ctx.load();assert.equal(ctx.config.chains[0].actions.length,16);assert.equal(ctx.config.encoderRotation.rotationValue.outputs.length,16);assert.equal(parsed,1);}
    else {await assert.rejects(ctx.load());assert.equal(ctx.config,original);assert.equal(parsed,0);}
  } finally {if(server)await new Promise(resolve=>server.close(resolve));}
}
(async()=>{for(const mode of ['500','truncated','disconnect','complete'])await check(mode);
console.log('PASS existing WebUI api/load: HTTP500, truncated JSON, real chunked disconnect reject without replacing editor; complete max Config accepted');
})().catch(e=>{console.error(e);process.exitCode=1;});
