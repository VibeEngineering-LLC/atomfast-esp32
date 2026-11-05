const assert=require('assert'),fs=require('fs'),path=require('path');
const web=path.join(__dirname,'..','..','web');
const X=require(process.env.HX_MODULE||path.join(web,'axes.js'));

let passed=0,total=0;
function check(name,fn){
  total++;
  try{
    fn();
    passed++;
    console.log('ok  '+name);
  }catch(e){
    console.log('FAIL '+name+': '+e.message);
  }
}

const rows=[15.8,17.3,19.1,18.0].map((v,i)=>({ts:i,cps:v}));
const stale={count:{mode:'manual',min:12.5,max:26}};

check('fitAll: ручная ось -> авто',()=>{
  const cfg=X.fitAll(stale);
  const r=X.resolve(cfg.count,X.autoRange(rows,['cps'],false));
  assert.strictEqual(r.mode,'auto','expected auto, got '+r.mode);
  assert(Math.abs(r.min-(15.8-0.33))<1e-9,r.min+'..'+r.max);
  assert(Math.abs(r.max-(19.1+0.33))<1e-9,r.min+'..'+r.max);
});

check('fitAll: zero сохраняется, cfg не мутируется',()=>{
  const src={count:{mode:'manual',min:1,max:2,zero:true},temp:{mode:'manual',min:3,max:4}};
  assert.deepStrictEqual(X.fitAll(src),{count:{mode:'auto',zero:true},temp:{mode:'auto'}});
  assert.strictEqual(src.count.mode,'manual');
  assert.deepStrictEqual(X.fitAll({}),{});
  assert.deepStrictEqual(X.fitAll(null),{});
});

check('ручной масштаб после смены периода возможен',()=>{
  const cfg=X.setManual(X.fitAll(stale),'count',10,30);
  assert.strictEqual(X.resolve(cfg.count,null).mode,'manual');
});

check('проводка',()=>{
  const textIn=fs.readFileSync(path.join(web,'chart-input.js'),'utf8');
  const idx=textIn.indexOf("getElementById('segWin').addEventListener");
  assert(idx>=0,'segWin addEventListener not found in chart-input.js');
  const seg=textIn.slice(idx);
  assert(/rgClear\(false\);\s*axCfg = AFAX\.fitAll\(axCfg\)/.test(seg),'fitAll call missing in chart-input.js segWin handler');

  const textDraw=fs.readFileSync(path.join(web,'chart-draw.js'),'utf8');
  assert(/axRangeKey !== rk\) \{ axCfg = AFAX\.fitAll\(axCfg\)/.test(textDraw),'fitAll call missing in chart-draw.js');

  const textHist=fs.readFileSync(path.join(web,'history.html'),'utf8');
  assert(textHist.includes('AFAX.fitAll(axCfg)'),'AFAX.fitAll(axCfg) missing in history.html');
  assert(textHist.includes('function fitAll'),'function fitAll missing in history.html');
});

console.log('TESTS: '+passed+'/'+total);
process.exit(passed===total?0:1);
