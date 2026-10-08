// Preview is browser-only. Integer arithmetic mirrors rotation.cpp, including
// rounding the whole weighted result (ties away from zero), not the delta.
const RotationPreview = (() => {
  function map(output, position, steps) {
    if (!Number.isInteger(steps) || steps < 1 || steps > 65535 || !Number.isInteger(position) || position < 0 || position > steps) throw Error('Preview Positionは0〜Range Stepsの整数で指定してください。');
    const floating=output.protocol==='osc'&&output.type==='float';
    const start=floating?Math.fround(output.start):output.start, end=floating?Math.fround(output.end):output.end;
    if(position===0)return start;
    if(position===steps)return end;
    if(floating)return Math.fround(start+(end-start)*(position/steps));
    const n=BigInt(steps), numerator=BigInt(start)*(n-BigInt(position))+BigInt(end)*BigInt(position);
    const magnitude=numerator<0n?-numerator:numerator;
    let rounded=magnitude/n;if(2n*(magnitude%n)>=n)++rounded;
    return Number(numerator<0n?-rounded:rounded);
  }
  function stringify(value,floating=false){
    if(floating&&typeof value==='number'&&Object.is(value,-0))return '-0.0';
    if(Array.isArray(value))return '['+value.map(v=>stringify(v)??'null').join(',')+']';
    if(value&&typeof value==='object')return '{'+Object.keys(value).filter(k=>value[k]!==undefined).map(k=>JSON.stringify(k)+':'+stringify(value[k],value.protocol==='osc'&&value.type==='float'&&['start','end','value'].includes(k))).join(',')+'}';
    return JSON.stringify(value);
  }
  // Device pages are served over HTTP, where structuredClone may be unavailable.
  function clone(value){
    if(Array.isArray(value))return value.map(clone);
    if(value&&typeof value==='object')return Object.fromEntries(Object.entries(value).map(([k,v])=>[k,clone(v)]));
    return value;
  }
  return {map,stringify,clone,format:v=>Object.is(v,-0)?'-0':String(v)};
})();

// This module is loaded before the main UI script; handlers run after Config load.
const RotationEditor = (() => {
  let previewPosition=0;
  const node=(tag,text)=>{const n=document.createElement(tag);if(text!==undefined)n.textContent=text;return n;};
  function field(label,value,write,{options,min,max,float=false}={}) {
    const wrap=node('label',label), input=node(options?'select':'input');
    if(options)for(const [value,text]of options){const option=node('option',text);option.value=value;input.append(option);}
    else {input.type=typeof value==='string'?'text':'number';if(min!==undefined)input.min=min;if(max!==undefined)input.max=max;input.step=float?'any':'1';}
    input.value=RotationPreview.format(value);
    input.onchange=()=>{write(options||input.type==='text'?input.value:input.value===''?NaN:Number(input.value));changed();refresh();};
    wrap.append(input);return wrap;
  }
  function normalized(){return Presets.read(Presets.full(editedConfig()),'full',capabilities).config.encoderRotation;}
  function refresh(){
    if(!config)return;
    const r=config.encoderRotation.rotationValue;
    $('rotationPreviewPosition').max=r.rangeSteps;
    $('rotationSummary').textContent=`編集中: ${config.encoderRotation.mode} · ${r.outputs.length} / 16 Outputs（検証用暫定容量）`;
    $('rotationAdd').disabled=saving||r.outputs.length>=16;
    $('rotationInactive').hidden=config.encoderRotation.mode==='rotationValue';
    $('rotationChainInactive').hidden=selected<26||config.encoderRotation.mode!=='rotationValue';
    $('trigger').disabled=saving||(selected>=26&&config.encoderRotation.mode==='rotationValue');
    const values=$('rotationPreviewValues');values.replaceChildren();
    try {
      const rotation=normalized(), axis=rotation.rotationValue;
      $('rotationValidation').textContent='';
      const duplicates=Presets.duplicateDestinations(rotation);
      $('rotationDuplicates').textContent=duplicates.length?`重複Output: ${duplicates.join(', ')}。OSC送信先は共通です。重複は保存できますが、送信順・到着順・最終値は保証されません。`:'';
      RotationPreview.map({protocol:'midi',start:0,end:0},previewPosition,axis.rangeSteps);
      axis.outputs.forEach((o,i)=>values.append(node('li',`Output ${i+1}: ${RotationPreview.format(RotationPreview.map(o,previewPosition,axis.rangeSteps))}`)));
      if(!axis.outputs.length)values.append(node('li','Outputsは0件です。Rotation ValueモードではPositionのみ更新されます。'));
    }catch(e){$('rotationValidation').textContent=e.message;$('rotationDuplicates').textContent='';}
  }
  function render(){
    if(!config)return;
    const rotation=config.encoderRotation,r=rotation.rotationValue;
    const axis=$('rotationAxis');axis.replaceChildren();
    axis.append(field('Encoder Mode',rotation.mode,v=>{rotation.mode=v;render();},{options:[['actionChain','Action Chain'],['rotationValue','Rotation Value']]}),
      field('Range Steps',r.rangeSteps,v=>{r.rangeSteps=v;render();},{min:1,max:65535}),
      field('Initial Position',r.initialPosition,v=>r.initialPosition=v,{min:0,max:r.rangeSteps}),
      field('Boundary',r.boundary,v=>r.boundary=v,{options:[['stop','Stop'],['wrap','Wrap']]}));
    const list=$('rotationOutputs');list.replaceChildren();
    r.outputs.forEach((o,i)=>{
      const card=node('article');card.className='panel';card.append(node('h3',`Output ${i+1}`));
      const fields=node('div');fields.className='fields';
      const kind=o.protocol==='midi'?'midi':o.type;
      fields.append(field('Output Type',kind,v=>{r.outputs[i]=v==='midi'?{protocol:'midi',message:'cc',transport:capabilities.defaultMidiTransport,channel:1,number:7,start:0,end:127}:{protocol:'osc',transport:'wifi',type:v,address:o.address||'/avatar/parameters/Value',start:0,end:v==='float'?1:127};render();},{options:[['int','OSC Int'],['float','OSC Float'],['midi','MIDI CC']]}));
      if(o.protocol==='osc') {
        const address=field('OSC Address（最大192 UTF-8 bytes）',o.address,v=>{o.address=v;render();});
        address.append(node('small',`${new TextEncoder().encode(o.address).length} / 192 bytes`));fields.append(address);
      } else {
        const allowed=capabilities.midiTransports;
        const transport=field('Transport',o.transport,v=>o.transport=v,{options:allowed.map(t=>[t,t==='both'?'Both (USB + BLE)':t.toUpperCase()])});
        if(!capabilities.usbMidi){transport.lastChild.disabled=true;transport.append(node('small','この機種はBLEのみ対応（固定）'));}
        fields.append(transport,field('Channel',o.channel,v=>o.channel=v,{min:1,max:16}),field('CC',o.number,v=>o.number=v,{min:0,max:127}));
      }
      const limits=kind==='midi'?{min:0,max:127}:kind==='int'?{min:-2147483648,max:2147483647}:{float:true,min:-3.4028234663852886e38,max:3.4028234663852886e38};
      fields.append(field('Start',o.start,v=>o.start=v,limits),field('End',o.end,v=>o.end=v,limits));
      card.append(fields,button('Outputを削除',()=>{r.outputs.splice(i,1);changed();render();}));list.append(card);
    });
    refresh();
  }
  function init(){
    $('rotationAdd').disabled=true;
    $('trigger').closest('.panel').append($('rotationChainInactive'));
    $('rotationAdd').onclick=()=>{if(saving||config.encoderRotation.rotationValue.outputs.length>=16)return;config.encoderRotation.rotationValue.outputs.push({protocol:'osc',transport:'wifi',address:'/avatar/parameters/Value',type:'int',start:0,end:127});changed();render();};
    $('rotationPreviewPosition').oninput=()=>{previewPosition=$('rotationPreviewPosition').value===''?NaN:Number($('rotationPreviewPosition').value);refresh();};
  }
  return {render,refresh,init};
})();
