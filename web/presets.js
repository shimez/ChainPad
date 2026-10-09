// Shared file/Action validation for Key Presets and full configurations.
// Runs in the browser; large import files never become firmware HTTP bodies.
const Presets = (() => {
  const clone = value => JSON.parse(JSON.stringify(value));
  const fail = text => { throw Error(text); };
  const object = v => v && typeof v === 'object' && !Array.isArray(v);
  const int = (v, min, max) => Number.isInteger(v) && v >= min && v <= max;
  const text = (v, max) => typeof v === 'string' && !v.includes('\0') && new TextEncoder().encode(v).length <= max;
  function action(raw, capabilities, changes) {
    if (!object(raw)) fail('Actionが不正です。');
    const a = clone(raw);
    if (a.protocol === 'wait') {
      if (!int(a.delayMs, 0, 86400000)) fail('Waitは0〜86400000 msの整数で指定してください。');
      return {protocol:'wait', delayMs:a.delayMs};
    }
    if (a.delayMs !== 0) fail('待機にはWait Actionを使用してください。');
    const pair=(a.protocol==='midi'&&['noteOnOff','allNotesOnOff'].includes(a.message))||(a.protocol==='keyboard'&&a.message==='keyDownUp');
    if (a.holdMs!==undefined && (!pair || !int(a.holdMs,0,86400000))) fail('On/Down→Off/Upの待ち時間は0〜86400000 msの整数で指定してください。');
    if (a.protocol === 'osc') {
      if (!text(a.address,192)) fail('OSC AddressはUTF-8で192 bytes以内（NUL不可）にしてください。');
      if (a.type==='string' && !text(a.value,128)) fail('OSC String値はUTF-8で128 bytes以内（NUL不可）にしてください。');
      if (a.transport !== 'wifi' || !a.address.startsWith('/') || /[\x00-\x20]/.test(a.address)) fail('OSC Address / Transportが不正です。');
      const valid = a.type === 'int' ? int(a.value,-2147483648,2147483647) : a.type === 'float' ? typeof a.value === 'number' && Number.isFinite(a.value) && Math.abs(a.value) <= 3.4028234663852886e38 : a.type === 'bool' ? typeof a.value === 'boolean' : a.type === 'string' && text(a.value,128);
      if (!valid) fail('OSCの型または値が不正です。');
    } else if (a.protocol === 'midi') {
      if (['allNotesOn','allNotesOff','allNotesOnOff'].includes(a.message)) {
        if (a.message!=='allNotesOff' && !int(a.value,1,127)) fail('All NotesのVelocityは1〜127で指定してください。');
        return {protocol:'midi',delayMs:0,message:a.message,...(a.message!=='allNotesOff'?{value:a.value}:{}),...(pair&&a.holdMs!==undefined?{holdMs:a.holdMs}:{})};
      }
      if (!['usb','ble','both'].includes(a.transport) || !['noteOn','noteOff','noteOnOff','cc'].includes(a.message) || !int(a.channel,1,16) || !int(a.number,0,127) || !int(a.value,['noteOn','noteOnOff'].includes(a.message)?1:0,127)) fail('MIDI Actionが不正です。');
      if (!capabilities.usbMidi && a.transport !== 'ble') { a.transport='ble'; changes.count++; }
    } else if (a.protocol === 'keyboard') {
      if (!['usb','ble'].includes(a.transport) || !['keyDown','keyUp','keyDownUp','releaseAll'].includes(a.message) || !int(a.usage,4,115) || !int(a.modifiers,0,255)) fail('Keyboard Actionが不正です。');
      if (!capabilities.usbKeyboard && a.transport !== 'ble') { a.transport='ble'; changes.count++; }
    } else fail('未対応のActionです。');
    return a;
  }
  function chains(raw, count, capabilities, changes) {
    if (!Array.isArray(raw) || raw.length !== count) fail(`Chainは${count}個必要です。`);
    const seen = new Set();
    const result = raw.map(c => {
      if (!object(c) || !int(c.input,0,count-1) || seen.has(c.input) || !Array.isArray(c.actions) || c.actions.length>(c.input<26?16:8)) fail('ChainのID・重複・Action数が不正です。');
      seen.add(c.input);
      return {input:c.input, actions:c.actions.map(a=>action(a,capabilities,changes))};
    });
    // Only locate Event IDs; the order of Actions is never altered.
    const ordered=Array.from({length:count},(_,input)=>result.find(c=>c.input===input));
    for(let i=0;i<Math.min(count,26);i+=2)if(ordered[i].actions.length+ordered[i+1].actions.length>16)fail('Press / Release合計は最大16 Actionsです。');
    return ordered;
  }
  function network(n) {
    if (!object(n) || !int(n.oscPort,1,65535) || !text(n.oscHost,15) || !/^\d{1,3}(\.\d{1,3}){3}$/.test(n.oscHost) || n.oscHost.split('.').some(v=>Number(v)>255)) fail('通信設定が不正です。');
    return {oscHost:n.oscHost,oscPort:n.oscPort};
  }
  function rotation(raw, capabilities, changes) {
    if (!object(raw) || !['actionChain','rotationValue'].includes(raw.mode)) fail('Encoder Rotation Modeが不正です。');
    const r=raw.rotationValue;
    if (!object(r) || !int(r.rangeSteps,1,65535) || !int(r.initialPosition,0,r.rangeSteps) || !['stop','wrap'].includes(r.boundary) || !Array.isArray(r.outputs) || r.outputs.length>16) fail('Rotation Axis / Outputsが不正です（暫定最大16 Outputs）。');
    const outputs=r.outputs.map(o=>{
      if (!object(o)) fail('Rotation Outputが不正です。');
      if (o.protocol==='osc') {
        if (o.transport!=='wifi' || !text(o.address,192) || !o.address.startsWith('/') || /[\x00-\x20]/.test(o.address) || !['int','float'].includes(o.type)) fail('Rotation OSCが不正です。');
        const valid=v=>o.type==='int'?int(v,-2147483648,2147483647):typeof v==='number'&&Number.isFinite(v)&&Math.abs(v)<=3.4028234663852886e38;
        if (!valid(o.start)||!valid(o.end)) fail('Rotation OSC値が不正です。');
        return {protocol:'osc',transport:'wifi',address:o.address,type:o.type,start:o.type==='float'?Math.fround(o.start):o.start,end:o.type==='float'?Math.fround(o.end):o.end};
      }
      if (o.protocol!=='midi'||o.message!=='cc'||!['usb','ble','both'].includes(o.transport)||!int(o.channel,1,16)||!int(o.number,0,127)||!int(o.start,0,127)||!int(o.end,0,127)) fail('Rotation MIDI CCが不正です。');
      let transport=o.transport;
      if(!capabilities.usbMidi&&transport!=='ble'){transport='ble';changes.count++;}
      return {protocol:'midi',message:'cc',transport,channel:o.channel,number:o.number,start:o.start,end:o.end};
    });
    return {mode:raw.mode,rotationValue:{rangeSteps:r.rangeSteps,initialPosition:r.initialPosition,boundary:r.boundary,outputs}};
  }
  function duplicateDestinations(rotation) {
    const seen=new Set(), duplicates=new Set();
    for(const o of rotation.rotationValue.outputs){
      const keys=o.protocol==='osc'?['OSC '+o.address]:(o.transport==='both'?['usb','ble']:[o.transport]).map(t=>`${t} Ch ${o.channel} CC ${o.number}`);
      for(const key of keys){if(seen.has(key))duplicates.add(key);seen.add(key);}
    }
    return [...duplicates];
  }
  function read(raw, kind, capabilities) {
    const format = kind==='key' ? 'chainpad-key-preset' : 'chainpad-configuration';
    if (!object(raw) || raw.format!==format) fail('ファイル種別が一致しません。');
    if(raw.version!==(kind==='key'?1:2)) fail('Unsupported Version: このPresetバージョンは非対応です。');
    const changes={count:0};
    if (kind==='key') return {chains:chains(raw.chains,2,capabilities,changes), converted:changes.count};
    if (raw.schemaVersion!==2) fail('Unsupported schemaVersion: expected 2');
    const result={schemaVersion:2,network:network(raw.network),chains:chains(raw.chains,28,capabilities,changes),encoderRotation:rotation(raw.encoderRotation,capabilities,changes)};
    return {config:result,converted:changes.count,duplicates:duplicateDestinations(result.encoderRotation)};
  }
  function full(config) {
    // Rebuild known v2 settings: never export Runtime or recovery authorization.
    if(config.schemaVersion!==2) fail('Unsupported schemaVersion: expected 2');
    return {format:'chainpad-configuration',version:2,schemaVersion:2,network:network(config.network),chains:clone(config.chains),encoderRotation:rotation(config.encoderRotation,{usbMidi:true},{count:0})};
  }
  function key(config, group) {
    if (!int(group,0,12)) fail('Key PresetはキーまたはEncoder Pushで使用してください。');
    return {format:'chainpad-key-preset',version:1,chains:[0,1].map(input=>({input,actions:clone(config.chains.find(c=>c.input===group*2+input).actions)}))};
  }
  return {read,full,key,duplicateDestinations};
})();
