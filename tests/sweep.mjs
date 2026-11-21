import fs from 'fs'; import vm from 'vm'; import { genSong } from './gen.mjs';
const songs = [genSong(), genSong(44100, 7, {bpm:128, padPan:0.3, vocRev:0.6, bassCut:1200, kickDec:12})]; const code=fs.readFileSync(new URL('../src/worker.js', import.meta.url),'utf8');
const sets = JSON.parse(fs.readFileSync(process.argv[2],'utf8'));
const E=a=>{ let s=0; for(let i=0;i<a.L.length;i++) s+=a.L[i]**2+a.R[i]**2; return s; };
const sdr=(ref,est)=>{ let s=0,e=0; for(let i=0;i<ref.L.length;i++){ s+=ref.L[i]**2+ref.R[i]**2; e+=(ref.L[i]-est.L[i])**2+(ref.R[i]-est.R[i])**2; } return 10*Math.log10(s/(e+1e-12)); };
// indice di musical noise: % di energia in punti tempo-frequenza isolati (> 4x la media degli 8 vicini)
function mni(ctx, a){ const B=1025, F=Math.ceil(a.L.length/512)+1; const Lr=new Float64Array(B),Li=new Float64Array(B),Rr=new Float64Array(B),Ri=new Float64Array(B);
  const rows=[new Float32Array(B),new Float32Array(B),new Float32Array(B)]; let iso=0, tot=0;
  const fill=(f,r)=>{ ctx.spectra(a.L,a.R,f,Lr,Li,Rr,Ri); for(let k=0;k<B;k++) r[k]=Math.sqrt((Lr[k]**2+Li[k]**2+Rr[k]**2+Ri[k]**2)/2); };
  fill(0,rows[1]); fill(1,rows[2]);
  for(let f=1;f<F-1;f++){ const t=rows[0]; rows[0]=rows[1]; rows[1]=rows[2]; rows[2]=t; fill(f+1,rows[2]);
    for(let k=1;k<B-1;k++){ const m=rows[1][k]; let nb=0; for(const r of rows) nb+=r[k-1]+r[k]+r[k+1]; nb=(nb-m)/8; const e=m*m; tot+=e; if(m>2*nb) iso+=e; } }
  return 100*iso/(tot||1); }
const names=['drums','vocals','bass','other'], syn={drums:'drums',vocals:'vocals',bass:'bass',other:'chords'};
for(const ps of sets){ const all=[]; let diag=''; const t=Date.now(); for(const song of songs){
  const ctx = { console, Math, Float32Array, Float64Array, Uint8Array, Int8Array, Uint32Array, Array, Object, Set, Number, String, JSON, Error, SF_PARAMS: ps, postMessage: (m)=>{ if(m.type==='result') ctx.__res=m; } };
  vm.createContext(ctx); vm.runInContext(code, ctx);
  ctx.onmessage({data:{type:'analyze', mode:'sep', sr:song.sr, len:song.n, src:{mix:{L:song.mix.L.slice(), R:song.mix.R.slice()}}}});
  const sd=names.map(n=>sdr(song.stems[n], ctx.synth(syn[n],0,song.n)));
  if(process.env.MNI){ const ev=ctx.synth('vocals',0,song.n), ec=ctx.synth('chords',0,song.n); sd.push(mni(ctx,ev)-mni(ctx,song.stems.vocals), mni(ctx,ec)-mni(ctx,song.stems.other)); }
  all.push(sd); if(process.env.DIAG && song===songs[0]){ for(const j of names){ const o=ctx.__setMix(song.stems[j]); const ej=E(song.stems[j]); diag+='\n   '+j.padEnd(7)+names.map(i=>(100*E(ctx.synth(syn[i],0,song.n))/ej).toFixed(0).padStart(5)).join(''); ctx.__setMix(o);} for(const j of ['pads','ep']){ const o=ctx.__setMix(song.extra[j]); const ej=E(song.extra[j]); diag+='\n   '+j.padEnd(7)+names.map(i=>(100*E(ctx.synth(syn[i],0,song.n))/ej).toFixed(0).padStart(5)).join(''); ctx.__setMix(o);} }
  } const sd=all[0].map((_,i)=>(all[0][i]+all[1][i])/2);
  console.log(JSON.stringify(ps).padEnd(70), 'SDR', sd.map(x=>x.toFixed(2).padStart(6)).join(''), ' media', (sd.slice(0,4).reduce((a,b)=>a+b)/4).toFixed(2), ' [b1 '+all[0].map(x=>x.toFixed(1)).join('/')+' b2 '+all[1].map(x=>x.toFixed(1)).join('/')+']', `(${((Date.now()-t)/1000).toFixed(0)}s)`, diag);
}
