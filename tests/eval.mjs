import fs from 'fs'; import vm from 'vm'; import { genSong } from './gen.mjs';
const workerPath = process.argv[2] || new URL('../src/worker.js', import.meta.url).pathname;
const song = genSong();
const ctx = { console, Math, Float32Array, Float64Array, Uint8Array, Int8Array, Uint32Array, Array, Object, Set, Number, String, JSON, Error,
  SF_PARAMS: process.env.SFP? JSON.parse(process.env.SFP): null, postMessage: (m)=>{ if(m.type==='result') ctx.__res=m; if(m.type==='error') console.log('ERR',m.msg); } };
vm.createContext(ctx); vm.runInContext(fs.readFileSync(workerPath,'utf8'), ctx);
const t0=Date.now();
ctx.onmessage({data:{type:'analyze', mode:'sep', sr:song.sr, len:song.n, src:{mix:{L:song.mix.L.slice(), R:song.mix.R.slice()}}}});
const tA=(Date.now()-t0)/1000;
const r=ctx.__res; console.log(`analisi ${tA.toFixed(1)}s  bpm ${r.bpm} key ${r.key.name}  prog ${r.progression.slice(0,8).join(' ')}`);
const sdr=(ref,est)=>{ let s=0,e=0; for(let i=0;i<ref.L.length;i++){ s+=ref.L[i]**2+ref.R[i]**2; e+=(ref.L[i]-est.L[i])**2+(ref.R[i]-est.R[i])**2; } return 10*Math.log10(s/(e+1e-12)); };
const map={drums:'drums', vocals:'vocals', bass:'bass', other:'chords'}; const out={};
for(const [gt,nm] of Object.entries(map)){ const t=Date.now(); const est=ctx.synth(nm,0,song.n); out[gt]=sdr(song.stems[gt],est); out[gt+'_t']=(Date.now()-t)/1000; }
// strumentale = tutto meno voce
const inst={L:new Float32Array(song.n),R:new Float32Array(song.n)}; for(const k of ['drums','bass','other']) for(let i=0;i<song.n;i++){ inst.L[i]+=song.stems[k].L[i]; inst.R[i]+=song.stems[k].R[i]; }
out.instrum=sdr(inst, ctx.synth('inst',0,song.n));
console.log('SDR dB  batteria %s  voce %s  basso %s  accordi %s  strumentale %s', ...['drums','vocals','bass','other','instrum'].map(k=>out[k].toFixed(2).padStart(6)));
console.log('tempo sintesi stem (s):', ['drums','vocals','bass','other'].map(k=>out[k+'_t'].toFixed(1)).join(' '));
if(process.argv[3]){ fs.writeFileSync(process.argv[3], JSON.stringify(out)); }
