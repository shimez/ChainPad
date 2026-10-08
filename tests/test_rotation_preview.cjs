const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const context=vm.createContext({TextEncoder});
vm.runInContext(fs.readFileSync('web/presets.js','utf8')+'\n'+fs.readFileSync('web/rotation-ui.js','utf8')+'\nthis.preview=RotationPreview;this.presets=Presets;',context);
const {preview,presets}=context;
const int=(start,end)=>({protocol:'osc',transport:'wifi',type:'int',address:'/test',start,end});
assert.equal(preview.map(int(-2,1),1,2),-1);
assert.equal(preview.map(int(-1,0),1,2),-1);
assert.equal(preview.map(int(0,1),1,2),1);
for(const n of [1,2,20,65535])for(const [a,b]of [[-2147483648,2147483647],[2147483647,-2147483648],[-100,0],[0,127],[42,42]]){
  assert.equal(preview.map(int(a,b),0,n),a);assert.equal(preview.map(int(a,b),n,n),b);
  for(let p=0;p<=n;p++){
    // All possible Phase 1 weighted numerators fit exactly in Number (under 2^48).
    const weighted=a*(n-p)+b*p, expected=Math.sign(weighted)*Math.floor(Math.abs(weighted)/n+.5);
    assert.equal(preview.map(int(a,b),p,n),expected===0?0:expected);
  }
}
const float={...int(-1e-60,3.4028234663852886e38),type:'float'};
assert(Object.is(preview.map(float,0,20),-0));
assert.equal(preview.map(float,20,20),Math.fround(float.end));
const encoded=preview.stringify({protocol:'osc',type:'float',start:-0,end:0,initialPosition:-0,c:undefined});
const parsed=JSON.parse(encoded);
assert(encoded.includes('"start":-0.0')); // Float parser path; integer fields must remain integer tokens.
assert(Object.is(parsed.start,-0));assert(!Object.is(parsed.initialPosition,-0));assert(!('c'in parsed));
const source={outputs:[{start:-0,end:NaN}]},copy=preview.clone(source);
assert(Object.is(copy.outputs[0].start,-0));assert(Number.isNaN(copy.outputs[0].end));
copy.outputs[0].start=1;assert(Object.is(source.outputs[0].start,-0));
for(const position of [-1,1.5,NaN,Infinity,21])assert.throws(()=>preview.map(int(0,1),position,20));
console.log('PASS Preview integer full-range sweeps / negative midpoint / endpoints / float32 signed zero / lossless wire -0 / invalid Position');
if(process.argv.includes('--firmware')){
  const vectors=JSON.parse(fs.readFileSync('.pio/host-tests/rotation-preview-vectors.json','utf8'));
  const buffer=new ArrayBuffer(4),view=new DataView(buffer);
  const fromBits=b=>{view.setUint32(0,b);return view.getFloat32(0);};
  for(const [a,b,p,n,bits]of vectors){const value=preview.map({...float,start:fromBits(a),end:fromBits(b)},p,n);view.setFloat32(0,value);assert.equal(view.getUint32(0),bits);}
  console.log(`PASS ${vectors.length} Firmware-produced float32 Preview bit-pattern comparisons`);
}
