// Optional local browser fixture: node tests/obs_ui_server.cjs (no device access).
const http=require('node:http'),fs=require('node:fs'),path=require('node:path');
let config=JSON.parse(fs.readFileSync('web/builtin/ChainPad-Configuration-Default.json','utf8'));
config.network.ssid='fixture';config.network.passwordConfigured=false;
let settings={enabled:true,ip:'192.0.2.10',port:4455,passwordConfigured:true},staged;
const calls=[];
http.createServer(async(req,res)=>{
  const url=new URL(req.url,'http://localhost');let body='';for await(const chunk of req)body+=chunk;
  const send=(value,status=200)=>{res.writeHead(status,{'Content-Type':'application/json','Cache-Control':'no-store'});res.end(JSON.stringify(value));};
  if(url.pathname==='/fixture/calls')return send(calls);
  if(url.pathname.startsWith('/api/')){
    calls.push({path:url.pathname,method:req.method}); // Never record credentials/body.
    const unsupported=new URL(req.headers.referer||'http://localhost').searchParams.has('unsupported');
    if(url.pathname==='/api/capabilities')return send({obsActions:!unsupported,obsWebSocket:!unsupported,obsSettingsApi:!unsupported,usbMidi:true,usbKeyboard:true,midiTransports:['usb','ble'],keyboardTransports:['usb','ble'],defaultMidiTransport:'usb',maxActions:16,maxWaitMs:86400000,rotationOutputCapacity:16});
    if(url.pathname.startsWith('/api/obs/scenes'))return send({state:settings.enabled?'ready':'unavailable',complete:settings.enabled,scenes:['ゲーム画面','待機画面','雑談'],generation:1,ageMs:0},url.pathname.endsWith('refresh')?202:200);
    if(url.pathname==='/api/config')return send(config);
    if(url.pathname==='/api/status')return send({name:'ChainPad',hardware:'XIAO ESP32S3',version:'M3 fixture',wifi:true,ip:'192.0.2.20',storageState:'ready',configOutputsAllowed:true,obs:unsupported?undefined:{connectionState:settings.enabled?'Ready':'Disabled',lastError:settings.enabled?-1:null,applyState:'accepted'}});
    if(url.pathname==='/api/obs'||url.pathname==='/api/obs/reconnect'){
      if(unsupported)return send({message:'Not found'},404);
      if(req.method==='PUT'){
        const v=JSON.parse(body);settings={enabled:v.enabled,ip:v.ip,port:v.port,passwordConfigured:('password' in v)?v.password!=='':settings.passwordConfigured};
      }
      return send({ok:true,storageState:'ready',settingsAvailable:true,settings,applyState:'accepted',saved:req.method==='PUT'},url.pathname.endsWith('reconnect')?202:200);
    }
    if(url.pathname==='/api/config/begin'){staged=structuredClone(config);staged.network=JSON.parse(body).network;return send({token:1});}
    if(url.pathname==='/api/config/chain'){const c=JSON.parse(body);staged.chains[c.input]=c;return send({ok:true});}
    if(url.pathname==='/api/config/rotation'){staged.encoderRotation=JSON.parse(body);return send({ok:true});}
    if(url.pathname==='/api/config/commit'){config=staged;return send({ok:true,message:'Saved fixture'});}
    return send({state:'idle',ok:true});
  }
  const file=path.join('web',url.pathname==='/'?'index.html':path.basename(url.pathname));
  if(!fs.existsSync(file)){res.writeHead(404);return res.end();}
  res.writeHead(200,{'Content-Type':file.endsWith('.js')?'text/javascript':file.endsWith('.svg')?'image/svg+xml':'text/html'});res.end(fs.readFileSync(file));
}).listen(Number(process.env.PORT)||8765,'127.0.0.1',()=>console.log('OBS UI fixture ready (unsupported: /?unsupported=1)'));
