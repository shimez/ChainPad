// Stored credentials never enter browser state. Only a pending replacement does.
const WifiPassword = (() => {
  function create(get, changed = () => {}) {
    const input = get('password'), clear = get('clearPassword'), status = get('passwordStatus');
    let configured = false;
    function render() {
      input.disabled = clear.checked;
      status.textContent = clear.checked ? '保存時にパスワードを削除します。' :
        configured ? 'パスワード設定済み。未入力なら現在の値を保持します。' : 'パスワード未設定。未入力なら未設定のままです。';
    }
    clear.onchange = () => { if (clear.checked) input.value = ''; render(); changed(); };
    input.oninput = () => { changed(); };
    function load(value) { configured = value === true; input.value = ''; clear.checked = false; render(); }
    function update(network) {
      const result = {...network};
      delete result.password; delete result.passwordConfigured;
      if (clear.checked) result.password = '';
      else if (input.value !== '') result.password = input.value;
      return result;
    }
    function saved(network) {
      if (Object.prototype.hasOwnProperty.call(network, 'password')) configured = network.password !== '';
      delete network.password;
      load(configured);
      return configured;
    }
    return {load, update, saved};
  }
  return {create};
})();
