// Pure setting model, independent of Engine capability and editor state.
const CallModel = (() => {
  const inputs = 28;
  const invalid = (code, input, index) => Object.assign(new Error(code), {code, input, index});
  function validateAction(a, input, index) {
    if (Object.keys(a).length !== 3 || a.delayMs !== 0 ||
        !Number.isInteger(a.targetInput) || a.targetInput < 0 || a.targetInput >= inputs)
      throw invalid('InvalidCall', input, index);
  }

  // JSON.parse owns syntax, escaping, duplicate-key resolution and nesting.
  // A parallel view replaces decimal/exponent NUMBER tokens with null. Whole
  // quoted tokens are consumed unchanged. Only actual chains[].actions[]
  // Call fields are compared, not arbitrary occurrences of "targetInput".
  function parseFile(text) {
    const doc = JSON.parse(text);
    let lexical;
    if (!Array.isArray(doc?.chains)) return doc;
    doc.chains.forEach((chain, c) => {
      if (!Array.isArray(chain?.actions)) return;
      chain.actions.forEach((a, i) => {
        if (a?.protocol !== 'callChain') return;
        validateAction(a, chain.input, i);
        lexical ??= JSON.parse(text.replace(/"(?:\\.|[^"\\])*"|-?\d+(?:\.\d+)?(?:[eE][+-]?\d+)?/g,
          token => token[0] !== '"' && /[.eE]/.test(token) ? 'null' : token));
        const source = lexical.chains[c].actions[i];
        if (source.targetInput === null || source.delayMs === null)
          throw invalid('InvalidCall', chain.input, i);
      });
    });
    return doc;
  }

  // Input to this pass has already had its ordinary Actions and local Loop
  // markers checked by the existing chain codec. Include inactive inputs.
  function validateGraph(chains) {
    const rows = new Array(inputs);
    for (const {input, actions} of chains) {
      if (!Number.isInteger(input) || input < 0 || input >= inputs) throw invalid('InvalidInput',input);
      if (rows[input]) throw invalid('Duplicate',input);
      if (actions.length > (input < 26 ? 16 : 8) ||
          (input < 26 && actions.length + (rows[input ^ 1]?.length ?? 0) > 16)) throw invalid('Slots',input);
      actions.forEach((a,i) => { if (a.protocol === 'callChain') validateAction(a,input,i); });
      rows[input] = actions;
    }
    for (let id=0; id<inputs; ++id) if (!rows[id]) throw invalid('Missing',id);
    const color = new Uint8Array(inputs), path = [];
    function visit(input) {
      color[input] = 1;
      rows[input].forEach((a,index) => {
        if (a.protocol !== 'callChain' || color[a.targetInput] === 2) return;
        path.push({input,index,target:a.targetInput});
        if (color[a.targetInput] === 1) {
          const error = invalid('Cycle', input, index);
          error.edges = path.slice(path.findIndex(e => e.input === a.targetInput));
          throw error;
        }
        visit(a.targetInput);
        path.pop();
      });
      color[input] = 2;
    }
    for (let i=0; i<inputs; ++i) if (!color[i]) visit(i);
  }
  return {parseFile, validateAction, validateGraph};
})();
if (typeof module !== 'undefined') module.exports = CallModel;
