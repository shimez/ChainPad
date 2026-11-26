const assert = require('node:assert/strict');
const M = require('../web/call-model.js'), cases = require('./call-model-cases.json');
const call = targetInput => ({protocol:'callChain', delayMs:0, targetInput});
const empty = () => Array.from({length:28}, (_,input) => ({input,actions:[]}));
for (const [raw,expected] of cases.wire) {
  const text = `{"chains":[{"input":0,"actions":[${raw}]}]}`;
  if (expected === null) assert.throws(() => M.parseFile(text), {code:'InvalidCall', input:0, index:0});
  else assert.equal(JSON.stringify(M.parseFile(text).chains[0].actions[0].targetInput), String(expected));
}
const fake = '{"protocol":"callChain","targetInput":1e0}';
const mixed = `{"other":{"targetInput":1e0},"chains":[{"input":0,"actions":[
  {"protocol":"osc","value":${JSON.stringify(fake)},"targetInput":1.0},
  {"protocol":"callChain","delayMs":0,"targetInput":2}]}]}`;
assert.deepEqual(M.parseFile(mixed), JSON.parse(mixed));
assert.throws(() => M.parseFile(mixed.replace('"targetInput":2','"targetInput":2e0')), {code:'InvalidCall',index:1});
for (const {edges,cycle} of cases.graphs) {
  const rows = empty();
  for (const [from,to] of edges) rows[from].actions.push(call(to));
  if (!cycle.length) M.validateGraph(rows.reverse());
  else assert.throws(() => M.validateGraph(rows.reverse()), e =>
    e.code === 'Cycle' && (assert.deepEqual(e.edges.map(x=>[x.input,x.index,x.target]),cycle), true));
}
const rows = empty();
{
  const draft=empty(), wait={protocol:'wait',delayMs:125};
  draft[0].actions=[{protocol:'loopStart',delayMs:0,count:2},wait,{protocol:'loopEnd',delayMs:0}];
  draft[1].actions=[call(0)];
  draft[2].actions=[call(1)];
  const before=structuredClone(draft);
  for(const [id,depth] of [[0,1],[1,2],[2,3]])
    assert.throws(()=>M.checkReplacement(draft,0,1,call(id)),e=>e.code==='Cycle'&&e.edges.length===depth&&e.edges[0].index===1);
  M.checkReplacement(draft,0,1,call(27)); // Empty target is valid.
  assert.deepEqual(draft,before);
  assert.equal(draft[0].actions[1],wait);
}
for (let i=0;i<27;++i) rows[i].actions.push(call(i+1));
M.validateGraph(rows);
rows[27].actions.push(call(0));
assert.throws(() => M.validateGraph(rows), e => e.code === 'Cycle' && e.edges.length === 28);
assert.throws(() => M.validateGraph(rows.slice(1)), {code:'Missing'});
assert.throws(() => M.validateGraph(rows.concat(rows[0])), {code:'Duplicate'});
const nested=empty(), start={protocol:'loopStart',delayMs:0,count:2}, end={protocol:'loopEnd',delayMs:0};
nested[0].actions=[...Array(7).fill(start),call(2),...Array(7).fill(end)];
nested[2].actions=[...Array(8).fill(start),...Array(8).fill(end)];
for (const chain of nested) require('../web/loop-model.js').parse(chain.actions);
M.validateGraph(nested);
nested[1].actions=[call(2),call(2)];
assert.throws(()=>M.validateGraph(nested),{code:'Slots'});
nested[1].actions=[]; nested[26].actions=Array(9).fill(call(2));
assert.throws(()=>M.validateGraph(nested),{code:'Slots'});
console.log('PASS Call model: shared wire/graph cases, structural lexical checks, 27 calls, cycle diagnostics');
