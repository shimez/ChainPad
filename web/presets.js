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
    if (a.protocol === 'osc') {
      if (a.transport !== 'wifi' || !text(a.address, 96) || !a.address.startsWith('/') || /[\x00-\x20]/.test(a.address)) fail('OSC Address / Transportが不正です。');
      const valid = a.type === 'int' ? int(a.value,-2147483648,2147483647) : a.type === 'float' ? typeof a.value === 'number' && Number.isFinite(a.value) && Math.abs(a.value) <= 3.4028234663852886e38 : a.type === 'bool' ? typeof a.value === 'boolean' : a.type === 'string' && text(a.value,64);
      if (!valid) fail('OSCの型または値が不正です。');
    } else if (a.protocol === 'midi') {
      if (['allNotesOn','allNotesOff'].includes(a.message)) {
        if (a.message==='allNotesOn' && !int(a.value,1,127)) fail('All NotesのVelocityは1〜127で指定してください。');
        return {protocol:'midi',delayMs:0,message:a.message,...(a.message==='allNotesOn'?{value:a.value}:{})};
      }
      if (!['usb','ble','both'].includes(a.transport) || !['noteOn','noteOff','cc'].includes(a.message) || !int(a.channel,1,16) || !int(a.number,0,127) || !int(a.value,a.message==='noteOn'?1:0,127)) fail('MIDI Actionが不正です。');
      if (!capabilities.usbMidi && a.transport !== 'ble') { a.transport='ble'; changes.count++; }
    } else if (a.protocol === 'keyboard') {
      if (!['usb','ble'].includes(a.transport) || !['keyDown','keyUp','releaseAll'].includes(a.message) || !int(a.usage,4,115) || !int(a.modifiers,0,255)) fail('Keyboard Actionが不正です。');
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
  function read(raw, kind, capabilities) {
    const format = kind==='key' ? 'chainpad-key-preset' : 'chainpad-configuration';
    if (!object(raw) || raw.format!==format || raw.version!==1) fail('ファイル種別またはバージョンが一致しません。');
    const changes={count:0};
    if (kind==='key') return {chains:chains(raw.chains,2,capabilities,changes), converted:changes.count};
    if (raw.schemaVersion!==1) fail('設定のschemaVersionが不正です。');
    const result={schemaVersion:1,network:network(raw.network),chains:chains(raw.chains,28,capabilities,changes)};
    return {config:result,converted:changes.count};
  }
  function full(config) { return {format:'chainpad-configuration',version:1,schemaVersion:config.schemaVersion,network:network(config.network),chains:clone(config.chains)}; }
  function key(config, group) {
    if (!int(group,0,12)) fail('Key PresetはキーまたはEncoder Pushで使用してください。');
    return {format:'chainpad-key-preset',version:1,chains:[0,1].map(input=>({input,actions:clone(config.chains.find(c=>c.input===group*2+input).actions)}))};
  }
  return {read,full,key};
})();
