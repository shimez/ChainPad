// Sources: https://docs.vrchat.com/docs/osc-as-input-controller
// VRChat Wiki contributors, https://wiki.vrchat.com/index.php?title=OSC&oldid=71908
// Wiki-derived descriptions adapted into Japanese, CC BY-SA 4.0.
const OscAddresses = (() => {
  const entries=[];
  // Descriptions refined by ChainPad hardware tests; retain uncertain conditions.
  const testedNotes={
    '/input/Voice':'マイクの動作設定によって変わります。「プッシュトゥトーク」では1でマイクON、0でOFFになります。Pressに1、Releaseに0を設定してください。',
    '/input/AFKToggle':'1を送信した後、0を送信するとAFK状態が切り替わります。Pressに1、Releaseに0を設定すると、キーを離したときに切り替わります。',
    '/input/PanicButton':'1でセーフモードを有効にします。操作後は0へ戻してください。0ではセーフモードは解除されません。解除はVRChatのUIで行います。',
    '/input/LookLeft':'1で左へ旋回し、0で停止します。VRでは「Comfort Turning（回転のスナップ）」がOFFの場合に動作します。',
    '/input/LookRight':'1で右へ旋回し、0で停止します。VRでは「Comfort Turning（回転のスナップ）」がOFFの場合に動作します。',
    '/input/ComfortLeft':'VRで左へスナップターンします。「Comfort Turning（回転のスナップ）」をONにしてください。Pressに1、Releaseに0を設定します。',
    '/input/ComfortRight':'VRで右へスナップターンします。「Comfort Turning（回転のスナップ）」をONにしてください。Pressに1、Releaseに0を設定します。',
    '/input/LookHorizontal':'-1〜1で左右の視点を操作します。操作終了時は0へ戻してください。Desktopで動作確認済みです。',
    '/input/LookVertical':'-1〜1で上下の視点を操作します。操作終了時は0へ戻してください。Desktopで動作確認済みです。WikiではDocs未掲載・OSCQuery掲載と記載。',
    '/input/ToggleSitStand':'座位／立位を切り替える入力としてWikiに掲載されていますが、今回の実機テストでは動作を確認できていません。',
    '/input/MoveHoldFB':'保持物を前後に動かす入力として掲載されていますが、今回の実機テストでは動作を確認できていません。',
    '/usercamera/Close':'true／falseのどちらでも、送信するたびにカメラを閉じます。Pressにtrueを設定し、Release側は空にしてください。',
    '/usercamera/Capture':'true／falseのどちらでも、送信するたびに撮影します。Pressにtrueを設定し、Release側は空にしてください。',
    '/usercamera/CaptureDelayed':'true／falseのどちらでも、送信するたびにタイマー撮影を開始します。Pressにtrueを設定し、Release側は空にしてください。',
    '/usercamera/FlySpeed':'0〜1で飛行速度を調整します。0で最小速度0.1、1で最大速度15.0。中間値は非線形に変換されるため、送信値と表示速度は一致しません。',
    '/usercamera/Lightness':'グリーンスクリーンの明度を0〜50で指定します。50を超える値は50に制限されます。',
    '/usercamera/OrientationIsLandscape':'trueで横向き、falseで縦向きに設定します。',
    '/usercamera/TriggerTakesPhotos':'飛行モード中のトリガーによる撮影を、trueで有効、falseで無効にします。',
    '/usercamera/DollyPathsStayVisible':'Dolly再生中のパス表示設定として掲載されていますが、今回の実機テストでは動作を確認できていません。',
    '/usercamera/Lock':'カメラ固定の設定です。固定される対象・動作条件は実機で確認中です。',
    '/input/ShowDebugInfo7':'PhysBone・Contactの可視化。ワールド作者、またはWorld Debuggingが有効なワールドで利用する項目ですが、今回の実機テストでは動作を確認できていません。'
  };
  const add=(group,type,address,label,note)=>entries.push(Object.freeze({group,type,address,label,note:testedNotes[address]??note}));
  const release='1 = 押下 / 0 = 解放。Release側にも同じAddressを設定し、0へ戻してください。';
  const input=(group,name,label,note='')=>add(group,'int','/input/'+name,label,release+' '+note);
  for(const [name,label,note] of [
    ['Voice','ボイス操作',''],
    ['Jump','ジャンプ','ワールドがジャンプを許可している場合に動作します。'],
    ['Run','走る','ワールドが走行を許可している場合に動作します。'],
    ['QuickMenuToggleLeft','クイックメニュー（左）','0→1で切り替えます。'],
    ['QuickMenuToggleRight','クイックメニュー（右）','0→1で切り替えます。'],
    ['PanicButton','セーフモード',''],
    ['ToggleSitStand','座位／立位切り替え','WikiではDocs未掲載・OSCQuery掲載と記載。'],
    ['AFKToggle','AFKモード切り替え','WikiではDocs未掲載・OSCQuery掲載と記載。']])input('基本操作',name,label,note);
  for(const [name,label] of [['MoveForward','前進'],['MoveBackward','後退'],['MoveLeft','左へ移動'],['MoveRight','右へ移動'],['LookLeft','左を向く'],['LookRight','右を向く'],['ComfortLeft','左へスナップターン'],['ComfortRight','右へスナップターン']])input('移動・視点',name,label,name.startsWith('Comfort')?'VRのみ。':'');
  for(const [name,label] of [['Vertical','前後移動（軸）'],['Horizontal','左右移動（軸）'],['LookHorizontal','左右視点（軸）'],['LookVertical','上下視点（軸）']])add('移動・視点','float','/input/'+name,label,'-1〜1 / 0 = 停止。操作終了時は0へ戻してください。'+(name==='LookVertical'?' WikiではDocs未掲載・OSCQuery掲載と記載。':''));
  for(const side of ['Right','Left'])for(const [name,label] of [['Use','使用'],['Grab','つかむ'],['Drop','離す']])input('アイテム',name+side,label+'（'+(side==='Right'?'右手':'左手')+'）','VR向け入力。');
  for(const [name,label] of [['MoveHoldFB','保持物を前後移動'],['SpinHoldCwCcw','保持物を左右回転'],['SpinHoldUD','保持物を上下回転'],['SpinHoldLR','保持物を横方向に回転'],['UseAxisRight','右手の使用（軸）'],['GrabAxisRight','右手のつかみ（軸）']])add('アイテム','float','/input/'+name,label,'-1〜1 / 0 = 停止。'+(name.includes('Axis')?' Docsでは動作未確認、WikiではOSCQuery未掲載と記載。':''));
  add('カメラ','int','/usercamera/Mode','カメラモード','0: OFF / 1: Photo / 2: Stream / 3: Emoji / 4: Multilayer / 5: Print / 6: Drone。');
   for(const [name,label] of [['Close','カメラを閉じる'],['Capture','写真を撮る'],['CaptureDelayed','タイマー撮影']])add('カメラ','bool','/usercamera/'+name,label,'');
  for(const [name,label] of [['ShowUIInCamera','UIを写す'],['LocalPlayer','自分を写す'],['RemotePlayer','他プレイヤーを写す'],['Environment','背景を写す'],['GreenScreen','グリーンスクリーン'],['Lock','カメラを固定'],['SmoothMovement','滑らかな移動'],['LookAtMe','自分を向く'],['AutoLevelRoll','ロール自動水平'],['AutoLevelPitch','ピッチ自動水平'],['Flying','フライング'],['TriggerTakesPhotos','トリガーで撮影'],['DollyPathsStayVisible','Dollyパスを表示'],['AudioFromCamera','カメラ位置から音声'],['ShowFocus','フォーカス表示'],['Streaming','Spout出力'],['RollWhileFlying','飛行中のロール'],['OrientationIsLandscape','横向き撮影']])add('カメラ設定','bool','/usercamera/'+name,label,'状態設定：true = 有効 / false = 無効。受信ごとに反転する操作ではありません。');
  for(const [name,label,min,max] of [['Zoom','ズーム',20,150],['Exposure','露出',-10,4],['FocalDistance','焦点距離',0,10],['Aperture','絞り',1.4,32],['Hue','グリーンスクリーン色相',0,360],['Saturation','グリーンスクリーン彩度',0,100],['Lightness','グリーンスクリーン明度',0,50],['LookAtMeXOffset','自分を向く・Xオフセット',-25,25],['LookAtMeYOffset','自分を向く・Yオフセット',-25,25],['FlySpeed','飛行速度',0,1],['TurnSpeed','旋回速度',0.1,5],['SmoothingStrength','平滑化の強さ',0.1,10],['PhotoRate','Dolly撮影レート',0.1,2],['Duration','Dollyの長さ',0.1,60]])add('カメラ調整','float','/usercamera/'+name,label,'Wiki記載範囲：'+min+'〜'+max+'。');
  add('Dolly','bool','/dolly/Play','Dolly再生／停止','true = 再生 / false = 停止。遅延再生のスキップ・キャンセルにも使用。');
  add('Dolly','float','/dolly/PlayDelayed','Dolly遅延再生','再生開始までの遅延を秒で指定します。');
  for(const [name,label] of [['Import','Dollyパスを読み込む'],['Export','Dollyパスを書き出す'],['ExportLocal','Dollyパスをローカル座標で書き出す']])add('Dolly','string','/dolly/'+name,label,'VRChat側のJSONファイルパスを指定。詳細なパス規則はDolly仕様で確認してください。');
  add('アバター','float','/avatar/eyeheight','アバターの目の高さ','単位m。公式サポート範囲0.1〜100。ワールドでスケール変更が禁止されている場合は無効。');
  add('アバター','int','/avatar/parameters/VRCEmote','標準エモート','1〜16。標準Animatorで使われる名前ですが、全アバター共通の必須パラメーターではありません。');
  add('チャット','bool','/chatbox/typing','入力中インジケーター','true = 表示 / false = 非表示。');
  add('トラッキング','float','/tracking/eye/EyesClosedAmount','両目の閉じ具合','0 = 全開 / 1 = 全閉。継続入力がない場合は10秒後に通常のまばたきへ戻ります。');
  ['UIの可視化','アセットバンドル情報','ビルド・FPS','出力ログ','ユーザー統計','ネットワークグラフ','同期オブジェクト一覧','PhysBone・Contact可視化','同期オブジェクト情報','プレイヤー情報'].forEach((label,i)=>input('デバッグ','ShowDebugInfo'+i,label,'World Debug Views。'+([0,6,7,8,9].includes(i)?'ワールド作者、またはWorld Debuggingが有効なワールドで利用。':'')+(i===0?' WikiではDocs未掲載・OSCQuery掲載と記載。':'')));
  Object.freeze(entries);
  const find=address=>entries.find(e=>e.address===address);
  function search(query='',group='すべて'){
    const q=query.trim().toLowerCase();
    return entries.filter(e=>(group==='すべて'||e.group===group)&&`${e.label} ${e.address} ${e.note}`.toLowerCase().includes(q));
  }
  function apply(action,entry){
    if(!entries.includes(entry))return;
    if(action.type!==entry.type)action.value=entry.type==='bool'?false:entry.type==='string'?'':0;
    action.address=entry.address;action.type=entry.type;
  }
  function open(onSelect,returnFocus){
    const dialog=document.createElement('dialog');dialog.className='osc-picker';dialog.setAttribute('aria-labelledby','osc-picker-title');
    const make=(tag,text,className)=>{const e=document.createElement(tag);if(text)e.textContent=text;if(className)e.className=className;return e;};
    const heading=make('div',null,'osc-picker-heading'),title=make('h2','VRChat OSC Address');title.id='osc-picker-title';
    const close=make('button','✕');close.type='button';close.setAttribute('aria-label','一覧を閉じる');close.onclick=()=>dialog.close();heading.append(title,close);
    const body=make('div',null,'osc-picker-body'),query=make('input');query.type='search';query.placeholder='検索：ジャンプ、Voice、/usercamera/…';query.setAttribute('aria-label','OSC Addressを検索');
    const filters=make('div',null,'osc-picker-filters'),count=make('p',null,'hint');count.setAttribute('role','status');const results=make('div',null,'osc-picker-results');let group='すべて',selection=null;
    function render(){const matches=search(query.value,group);count.textContent=matches.length+'件';results.replaceChildren();results.scrollTop=0;for(const entry of matches){const b=make('button',null,'osc-picker-result');b.type='button';b.append(make('strong',entry.label),make('span',entry.type,'tag'),make('code',entry.address),make('small',entry.note));b.onclick=()=>{selection=entry;dialog.close();};results.append(b);}if(!matches.length)results.append(make('p','一致する候補がありません。独自のAddressは元の入力欄へ直接入力できます。','hint'));}
    for(const name of ['すべて',...new Set(entries.map(e=>e.group))]){const b=make('button',name);b.type='button';b.setAttribute('aria-pressed',String(name===group));b.onclick=()=>{group=name;for(const c of filters.children)c.setAttribute('aria-pressed',String(c===b));render();};filters.append(b);}
    query.oninput=render;body.append(query,filters,count,results);
    const foot=make('div',null,'osc-picker-foot');foot.append(make('p','AddressとTypeを反映します。同じ型ならValueを維持し、型が変わる場合は0／false／空文字にします。選択後はValueを設定してください。'));
    const sources=make('p','出典：');for(const [label,url] of [['VRChat Docs','https://docs.vrchat.com/docs/osc-as-input-controller'],['VRChat Wiki contributors（参照版71908）','https://wiki.vrchat.com/index.php?title=OSC&oldid=71908'],['CC BY-SA 4.0','https://creativecommons.org/licenses/by-sa/4.0/']]){const a=make('a',label);a.href=url;a.target='_blank';a.rel='noopener noreferrer';sources.append(a,document.createTextNode(' · '));}foot.append(sources,make('p','Wikiの説明を日本語に要約・再構成。実機での全候補の動作は未検証です。読み取り専用・複数引数の項目は含みません。アバター固有のAddressは自由入力できます。'));
    dialog.append(heading,body,foot);dialog.addEventListener('close',()=>{dialog.remove();if(selection)onSelect(selection);else if(returnFocus&&returnFocus.isConnected)returnFocus.focus();},{once:true});document.body.append(dialog);render();dialog.showModal();query.focus();
  }
  return {entries,find,search,apply,open};
})();
