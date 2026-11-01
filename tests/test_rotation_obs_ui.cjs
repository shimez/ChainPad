const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
class Element{
  constructor(tag){this.tag=tag;this.children=[];this.textContent='';this.className='';this.style={setProperty(){}};}
  append(...items){this.children.push(...items);}
  replaceChildren(...items){this.children=items;}
  get lastChild(){return this.children.at(-1);}
  setAttribute(){}
  setCustomValidity(value){this.validationMessage=value;}
  focus(){}
}
const nodes={},config=JSON.parse(fs.readFileSync('web/builtin/ChainPad-Configuration-Default.json','utf8'));
config.encoderRotation={mode:'rotationValue',rotationValue:{rangeSteps:20,initialPosition:0,boundary:'stop',outputs:[{protocol:'osc',transport:'wifi',type:'int',address:'/original',start:0,end:127}]}};
let cache={state:'unavailable',complete:false,scenes:[]},listener,requests=0;
const ctx=vm.createContext({TextEncoder,config,selected:26,saving:false,names:[],capabilities:{usbMidi:true,midiTransports:['usb','ble','both'],defaultMidiTransport:'usb',obsActions:true},
  document:{createElement:tag=>new Element(tag)},$:id=>nodes[id]??=new Element('div'),editedConfig:()=>config,cancelActionDrag(){},
  changed(){vm.runInContext('RotationEditor.refresh()',ctx);},
  obsScenes:{select(){cache={state:'unavailable',complete:false,scenes:[]};},snapshot:()=>cache,busy:()=>false,subscribe(fn){listener=fn;},refresh(){requests++;}}});
const html=fs.readFileSync('web/index.html','utf8');
vm.runInContext(fs.readFileSync('web/presets.js','utf8')+'\n'+fs.readFileSync('web/obs-actions.js','utf8')+'\n'+fs.readFileSync('web/rotation-ui.js','utf8')+'\n'+['function control(','function button('].map(prefix=>html.split('\n').find(l=>l.startsWith(prefix))).join('\n'),ctx);
const run=code=>vm.runInContext(code,ctx);
const all=(n=nodes.rotationOutputs)=>[n,...n.children.flatMap(c=>all(c))];
const field=(label,card=nodes.rotationOutputs)=>all(card).find(n=>n.tag==='label'&&n.textContent===label)?.lastChild;
// Source-name controls append advisory text after the input.
const input=(label,card)=>all(card).find(n=>n.tag==='label'&&n.textContent===label).children.find(n=>n.tag==='input');
const choose=(label,value)=>{const f=field(label);f.value=String(value);f.onchange();};
run('RotationEditor.render()');assert.equal(requests,0);
choose('Action Type','obs');const o=config.encoderRotation.rotationValue.outputs[0];
assert.equal(o.protocol,'obs');assert.equal(o.start,-60);assert.equal(o.end,0);assert(!('address' in o));
assert.equal(field('Start（dB）').min,-100);assert.equal(field('End（dB）').max,26);
const name=input('対象の音声ソース',nodes.rotationOutputs);name.value='手入力';name.oninput();
choose('Start（dB）',-40);choose('End（dB）',0);
assert.equal(nodes.rotationPreviewValues.children[0].textContent,'Action 1: -40 dB');
assert.equal(requests,0); // Preview never sends to OBS.
const buttons=()=>all().filter(n=>n.tag==='button');
buttons().find(n=>n.textContent==='音声ソース一覧から選択').onclick();assert.equal(requests,1);
cache={state:'ready',complete:true,scenes:['Mic','Desktop']};listener();
const list=all().find(n=>n.tag==='select'&&n.size===6);list.selectedIndex=1;list.value='Desktop';list.ondblclick();assert.equal(o.inputName,'Desktop');
const original=JSON.stringify(config.encoderRotation);run('RotationEditor.render()');assert.equal(JSON.stringify(config.encoderRotation),original);
assert.equal(run("Presets.read(Presets.full(config),'full',capabilities).config.encoderRotation.rotationValue.outputs[0].inputName"),'Desktop');
choose('End（dB）',27);assert(nodes.rotationValidation.textContent);choose('End（dB）',0);assert.equal(nodes.rotationValidation.textContent,'');
ctx.capabilities.obsActions=false;run('RotationEditor.render()');assert.equal(buttons().find(n=>n.textContent==='音声ソース一覧から選択').disabled,true);
assert.equal(JSON.stringify(config.encoderRotation),original);
choose('Action Type','midi');assert.equal(config.encoderRotation.rotationValue.outputs[0].protocol,'midi');assert(!('inputName' in config.encoderRotation.rotationValue.outputs[0]));
console.log('PASS Rotation OBS UI: type switch, dB fields, manual source, picker/double-click, no preview sends, validation and unsupported preservation');
