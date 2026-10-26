// OBS settings are independent of Config/Presets. Never retain a stored secret.
const ObsSettingsUI = (() => {
  function create(get, externalBusy = () => false) {
    let supported = false, loaded = false, pending = false, edited = false;
    let clear = false, savedEnabled = false, configured = false, storage = '';
    let epoch = 0;
    const fields = ['obsEnabled','obsIp','obsPort','obsPassword'];
    function connection(text, tone) {
      get('obsConnection').textContent = 'Connection: ' + text;
      get('obsConnection').className = 'obs-summary-status obs-state-' + tone;
    }
    function note(text, error = false) {
      get('obsMessage').textContent = text;
      get('obsMessage').classList.toggle('error', error);
    }
    function refresh() {
      const locked = pending || externalBusy();
      const writable = loaded && ['missing','ready','corrupt','unsupportedVersion'].includes(storage);
      get('obsFields').disabled = locked || !writable;
      get('obsPassword').disabled = clear;
      get('obsClear').checked = clear;
      get('obsSave').disabled = !supported || !writable || locked;
      get('obsReconnect').disabled = !supported || !loaded || !savedEnabled || !get('obsEnabled').checked || locked;
      get('obsPasswordStatus').textContent = clear ? 'Password: 削除予定（Save OBSで確定）' :
        !['ready','missing'].includes(storage) ? 'Password: 不明。新しい値の入力または明示的な削除が必要です。' :
        `Password: ${configured ? '設定済み' : '未設定'}${get('obsPassword').value ? ' — 変更予定' : ''}`;
      get('obsStorage').textContent = !loaded ? '設定未取得' :
        ['corrupt','unsupportedVersion'].includes(storage) ? `保存データ: ${storage}。保存時に置換確認を行います。` :
        storage === 'ioError' ? '保存データを読み込めません。ページの再読み込みまたは本体の再起動を行ってください。' :
        `保存データ: ${storage}${edited ? ' — 未保存の変更あり' : ''}`;
    }
    async function request(path, method = 'GET', body) {
      const response = await fetch('/api/' + path, {
        method, cache: 'no-store', headers: body ? {'Content-Type':'application/json'} : {},
        body: body ? JSON.stringify(body) : undefined
      });
      const data = await response.json();
      return {response, data};
    }
    function errorText(data, status) {
      // M2 errors are fixed strings. Never display request bodies or a thrown fetch payload.
      const code = data?.error?.code;
      return `${data?.error?.message || 'OBS操作に失敗しました。'}${code ? ' (' + code + ')' : ''} [HTTP ${status}]`;
    }
    function snapshot(data, fill) {
      storage = data.storageState;
      loaded = true;
      const settings = data.settingsAvailable === true ? data.settings : null;
      savedEnabled = settings?.enabled === true;
      configured = settings?.passwordConfigured === true;
      if (fill) {
        get('obsEnabled').checked = savedEnabled;
        get('obsIp').value = settings?.ip || '';
        get('obsPort').value = settings?.port ?? 4455;
        get('obsPassword').value = '';
        clear = false; edited = false;
      }
    }
    async function reload() {
      if (!supported || pending || externalBusy()) return;
      if (edited && !confirm('OBSの未保存の編集を破棄して再取得しますか？')) return;
      pending = true; refresh();
      try {
        const {response, data} = await request('obs');
        if (!response.ok || !data.storageState) { note(errorText(data, response.status), true); return; }
        snapshot(data, true); note('OBS設定を読み込みました。');
      } catch { note('OBS設定を取得できません。ページを再読み込みしてください。', true); }
      finally { pending = false; refresh(); }
    }
    async function save() {
      if (get('obsSave').disabled || pending || externalBusy()) return;
      const ip = get('obsIp').value.trim(), portText = get('obsPort').value;
      if (!/^\d{1,3}(\.\d{1,3}){3}$/.test(ip) || ip.split('.').some(n => Number(n) > 255)) {
        note('IP AddressにIPv4アドレスを入力してください。', true); return;
      }
      const port = Number(portText);
      if (!/^\d+$/.test(portText) || !Number.isInteger(port) || port < 1 || port > 65535) {
        note('Portは1〜65535の整数で入力してください。', true); return;
      }
      const body = {enabled:get('obsEnabled').checked, ip, port};
      if (clear) body.password = '';
      else if (get('obsPassword').value !== '') body.password = get('obsPassword').value;
      if (['corrupt','unsupportedVersion'].includes(storage)) {
        if (!Object.prototype.hasOwnProperty.call(body, 'password')) {
          note('保存済みパスワードは不明です。新しい値を入力するか「保存時にパスワードをクリア」を指定してください。', true); return;
        }
        if (!confirm('読み込めないOBS保存データを、画面上の設定で置き換えますか？')) return;
        body.replaceInvalid = true;
      }
      ++epoch; pending = true; refresh();
      try {
        const {response, data} = await request('obs', 'PUT', body);
        if (data.saved === true) {
          // 503 can mean persisted but apply failed; never invite a blind resave.
          get('obsPassword').value = ''; clear = false; edited = false;
          if (data.storageState) snapshot(data, true);
          else { loaded = false; savedEnabled = false; }
          if (response.ok && data.ok === true && data.applyState === 'accepted')
            note('OBS設定を保存しました（反映受付済み）。接続結果はConnectionで確認してください。');
          else note('OBS設定は保存されましたが、反映または応答に問題があります。' + errorText(data, response.status) + ' ページを再読み込みし、必要に応じてReconnectしてください。', true);
          connection('更新待ち…', 'pending');
        } else {
          if (data.storageState) snapshot(data, false);
          note(errorText(data, response.status), true);
        }
      } catch { note('保存結果を確認できません。編集内容は保持しています。再保存の前にページを再読み込みして設定を確認してください。', true); }
      finally { ++epoch; delete body.password; pending = false; refresh(); }
    }
    async function reconnect() {
      if (get('obsReconnect').disabled || pending || externalBusy()) return;
      ++epoch; pending = true; refresh();
      try {
        const {response, data} = await request('obs/reconnect', 'POST');
        if (data.storageState) snapshot(data, false);
        if (response.status === 202 && data.ok === true && data.applyState === 'accepted') {
          note('保存済み設定で再接続要求を受け付けました。接続結果はConnectionで確認してください。');
          connection('更新待ち…', 'pending');
        } else note(errorText(data, response.status), true);
      } catch { note('再接続要求の結果を確認できません。Connectionの更新を確認してください。', true); }
      finally { ++epoch; pending = false; refresh(); }
    }
    function statusFailed() {
      connection('取得できません（現在の状態は不明）', 'disconnected');
      get('obsDiagnostics').classList.toggle('error', false);
      get('obsDiagnostics').textContent = '';
    }
    function updateStatus(status) {
      if (!status) { statusFailed(); return; }
      const names = {Disabled:'Disabled',Disconnected:'Disconnected',Connecting:'Connecting',Authenticating:'Authenticating',Ready:'Connected',Error:'Connection Error'};
      const tone = status.connectionState === 'Error' ? 'error' : status.connectionState === 'Ready' ? 'ready' :
        ['Connecting','Authenticating'].includes(status.connectionState) ? 'pending' : 'disconnected';
      connection(names[status.connectionState] || '不明', tone);
      get('obsDiagnostics').classList.toggle('error', status.connectionState === 'Error');
      const parts = [];
      if (status.connectionState === 'Error' && (status.lastError === 4009 || status.lastClose === 4009)) parts.push('認証エラー: OBSのパスワードを確認してください。');
      if (status.lastError != null) parts.push(`過去の診断 lastError: ${status.lastError}`);
      if (status.lastClose != null) parts.push(`過去の診断 lastClose: ${status.lastClose}`);
      if (status.blockedReason) parts.push(`停止理由: ${status.blockedReason}`);
      if (status.applyState) parts.push(`反映受付: ${status.applyState}`);
      get('obsDiagnostics').textContent = parts.join(' / ');
    }
    for (const id of fields) get(id).oninput = () => { edited = true; refresh(); };
    get('obsClear').onchange = () => {
      if (pending || externalBusy()) { refresh(); return; }
      clear = get('obsClear').checked; edited = true; refresh();
    };
    get('obsSave').onclick = save;
    get('obsReconnect').onclick = reconnect;
    async function init() {
      try {
        const {response, data} = await request('capabilities');
        supported = response.ok && data.obsWebSocket === true && data.obsSettingsApi === true;
        get('obsSection').hidden = !supported;
        if (supported) await reload();
      } catch { get('obsSection').hidden = true; }
    }
    refresh();
    return {init, reload, refresh, updateStatus, statusFailed, busy:()=>pending, dirty:()=>edited, epoch:()=>epoch};
  }
  return {create};
})();
