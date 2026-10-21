// Exercise the production renderActions/control handlers with a minimal DOM adapter.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync('web/index.html','utf8');
class Element{
  constructor(tag){this.tag=tag;this.children=[];this.textContent='';this.className='';this.classList={add:()=>{},remove:()=>{}};}
  append(...items){this.children.push(...items);}
  replaceChildren(...items){this.children=items;}
  get lastChild(){return this.children.at(-1);}
  setAttribute(){}
  querySelector(){return {focus(){}};}
}
const nodes={},plain=x=>JSON.parse(JSON.stringify(x));
const config=JSON.parse(fs.readFileSync('web/builtin/ChainPad-Configuration-Default.json','utf8'));
for(const chain of config.chains)chain.actions=[];
const ctx=vm.createContext({TextEncoder,config,selected:0,saving:false,keyboardLayout:'jis',
  usageNames:{104:'F13',30:'1'},capabilities:{usbKeyboard:true,usbMidi:true,keyboardTransports:['usb','ble']},
  document:{createElement:tag=>new Element(tag),createTextNode:text=>({textContent:text,children:[]})},
  $:id=>nodes[id]??=(new Element('div')),waitWarning(){},changed(){},RotationEditor:{refresh(){}}});
const line=prefix=>html.split('\n').find(l=>l.startsWith(prefix));
vm.runInContext(fs.readFileSync('web/presets.js','utf8')+'\n'+fs.readFileSync('web/action-ui.js','utf8')+'\n'+
   ['function control(','function current(','function button(','function defaults('].map(line).join('\n')+'\n'+
  html.slice(html.indexOf('const actionLabels='),html.indexOf('function rotationSummary()'))+'\neditingAction=0;',ctx);
const P=vm.runInContext('Presets',ctx);
function all(node=nodes.actions){return [node,...node.children.flatMap(n=>all(n))];}
function field(label){return all().find(n=>n.tag==='label'&&n.textContent===label)?.children.find(n=>['select','input'].includes(n.tag));}
function choose(label,value){const f=field(label);assert(f,`Missing ${label}`);f.value=String(value);f.onchange();}
function show(a,input=0){for(const c of config.chains)c.actions=[];config.chains[input].actions=[a];ctx.selected=input;ctx.renderActions();}
const action=(message,usage=104)=>({protocol:'keyboard',transport:'usb',delayMs:0,message,usage,modifiers:0});
const order=[233,234,226,205,181,182,183];
for(const input of Array.from({length:28},(_,i)=>i)){
  for(const message of ['keyDown','keyUp','keyDownUp','consumer','releaseAll']){
    const a=action(message,message==='consumer'?226:104);
    if(message==='keyDownUp'){a.holdMs=57;a.modifiers=2;}
    if(message==='releaseAll'){a.usage=30;a.modifiers=4;} // Existing valid payload must remain untouched on render.
    const before=JSON.stringify(a);show(a,input);
    assert.equal(JSON.stringify(a),before);
    assert.equal(field('種類').value,message==='consumer'?'consumer':message==='releaseAll'?'releaseAll':'keyboard');
    assert.equal(!!field('押下時間 (ms)'),message==='keyDownUp');
    assert.equal(all().some(n=>n.className==='modifiers'),['keyDown','keyDownUp'].includes(message));
    assert.equal(all().some(n=>n.tag==='label'&&n.textContent.startsWith('キー（')),!['consumer','releaseAll'].includes(message));
    if(message==='consumer'){
      assert.deepEqual(field('操作').children.map(n=>Number(n.value)),order);
      assert.deepEqual(field('操作').children.map(n=>n.textContent),order.map(u=>P.consumerNames[u]));
      assert(all().some(n=>String(n.textContent).includes('このイベントで1回')));
      for(const layout of ['ascii','jis']){ctx.keyboardLayout=layout;ctx.renderActions();assert.equal(JSON.stringify(a),before);}
      assert(ctx.actionSummary(a).includes('スピーカーミュート · 押下→解除'));
    }
    if(message==='releaseAll')assert.equal(ctx.actionSummary(a),'HID · USB · 全解除');
    choose('種類',field('種類').value);assert.equal(JSON.stringify(a),before);
  }
}
// Real change handlers reset only type-specific fields; no new JSON classification field.
const a=action('keyDownUp',30);a.modifiers=3;a.holdMs=99;show(a);
choose('種類','consumer');assert.deepEqual(a,action('consumer',233));
choose('操作',226);assert.equal(a.usage,226);
choose('種類','consumer');assert.equal(a.usage,226);
choose('種類','keyboard');assert.deepEqual(a,action('keyDownUp'));
choose('押下時間 (ms)',123);choose('押し方','keyDown');assert(!('holdMs' in a));
choose('種類','releaseAll');assert.deepEqual(a,action('releaseAll'));
assert(!field('操作'));assert(!field('押下時間 (ms)'));
assert(P.read(P.full(config),'full',ctx.capabilities)); // Release All's required fields remain codec-compatible.
choose('種類','consumer');assert.deepEqual(a,action('consumer',233));
// Transport constraints and persisted Consumer values for all supported boards/usages.
for(const board of ['s3','c5','c6']){
  const usb=board==='s3';ctx.capabilities={usbKeyboard:usb,usbMidi:usb,keyboardTransports:usb?['usb','ble']:['ble']};
  for(const usage of order){
    const source=action('consumer',usage);show(source);
    const full=P.read(P.full(config),'full',ctx.capabilities).config;
    const imported=plain(full.chains[0].actions[0]);assert.deepEqual(imported,{...source,transport:usb?'usb':'ble'});
    show(imported);assert.equal(field('Transport').disabled,!usb);
    const before=JSON.stringify(imported);ctx.renderActions();assert.equal(JSON.stringify(imported),before);
    const key=P.read(P.key(config,0),'key',ctx.capabilities);
    assert.deepEqual(plain(key.chains[0].actions[0]),imported);
    assert.deepEqual(plain(P.read(P.full(config),'full',ctx.capabilities).config.chains[0].actions[0]),imported);
    assert(!('holdMs' in imported));assert(!('kind' in imported));
  }
}
console.log('PASS HID UI: existing JSON unchanged, all 28 events, field visibility, kind/operation changes, explicit Usage order, summaries, JIS/US, Full/Key round-trip, S3/C5/C6 transports');
for(let input=0;input<28;input++){
  show(action('keyDownUp'),input);choose('Action','stopOtherChains');
  const expected={protocol:'stopOtherChains',delayMs:0};
  assert.deepEqual(plain(config.chains[input].actions[0]),expected);
  assert.equal(field('Action').children.find(n=>n.value==='stopOtherChains').textContent,'他のChainを中断');
  assert.equal(all().filter(n=>n.tag==='label').length,1); // No Transport, wait, usage or target.
  assert(all().some(n=>String(n.textContent).includes('実行済みのMIDI・HID・OSC操作は取り消されません')));
  assert(ctx.actionSummary(expected).includes('このChainは継続'));
  all().find(n=>n.className==='edit-action').onclick();
  assert(!field('Action'));
  all().find(n=>n.className==='edit-action').onclick();
  assert.equal(field('Action').value,'stopOtherChains');
  const restored=P.read(P.full(config),'full',ctx.capabilities).config;
  show(plain(restored.chains[input].actions[0]),input);
  assert.deepEqual(plain(config.chains[input].actions[0]),expected);
  choose('Action','keyboard');assert.equal(field('種類').value,'keyboard');
}
console.log('PASS Stop UI: all 28 events, selection, no parameters, summary, close/re-edit, Full round-trip, switch back');
