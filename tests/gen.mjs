// Brano di test multitraccia (stem veri noti): 100 BPM, Re minore, Dm–Bb–F–C, 16 battute
export function genSong(sr=44100, seed=1, o={}){
  const padPan=o.padPan??0.55, vocRev=o.vocRev??0.35, bassCut=o.bassCut??600, kickDec=o.kickDec??7;
  let s=seed; const rnd=()=>{ s=(s*16807)%2147483647; return s/2147483647*2-1; };
  const bpm=o.bpm??100, beat=60/bpm, bars=16, n=Math.ceil((bars*4*beat+2)*sr);
  const st=()=>({L:new Float32Array(n),R:new Float32Array(n)});
  const S={drums:st(), vocals:st(), bass:st(), other:st()}; const X={pads:st(), ep:st()};
  const add=(dst,t0,sig,pan=0,g=1)=>{ const i0=Math.round(t0*sr), gl=g*Math.cos((pan+1)*Math.PI/4)*Math.SQRT2, gr=g*Math.sin((pan+1)*Math.PI/4)*Math.SQRT2;
    for(let i=0;i<sig.length&&i0+i<n;i++){ if(i0+i<0) continue; dst.L[i0+i]+=sig[i]*gl; dst.R[i0+i]+=sig[i]*gr; } };
  const hz=m=>440*2**((m-69)/12);
  const biquadBP=(x,f,q)=>{ const w=2*Math.PI*f/sr, al=Math.sin(w)/(2*q), b0=al, b2=-al, a0=1+al, a1=-2*Math.cos(w), a2=1-al; const y=new Float32Array(x.length); let x1=0,x2=0,y1=0,y2=0;
    for(let i=0;i<x.length;i++){ const v=(b0*x[i]+b2*x2-a1*y1-a2*y2)/a0; x2=x1;x1=x[i];y2=y1;y1=v;y[i]=v;} return y; };
  const lp=(x,f)=>{ const a=Math.exp(-2*Math.PI*f/sr); let y=0; for(let i=0;i<x.length;i++){ y=(1-a)*x[i]+a*y; x[i]=y; } return x; };
  const saw=(f,d,det=0)=>{ const m=Math.round(d*sr), x=new Float32Array(m); let ph=Math.random(); const fr=f*2**(det/1200)/sr; for(let i=0;i<m;i++){ ph+=fr; ph-=Math.floor(ph); x[i]=2*ph-1; } return x; };
  // batteria
  const kick=()=>{ const m=Math.round(0.45*sr), x=new Float32Array(m); let ph=0; for(let i=0;i<m;i++){ const t=i/sr, f=45+90*Math.exp(-t*30); ph+=2*Math.PI*f/sr; x[i]=Math.sin(ph)*Math.exp(-t*kickDec)*0.9; } return x; };
  const snare=()=>{ const m=Math.round(0.3*sr), nz=new Float32Array(m); for(let i=0;i<m;i++) nz[i]=rnd()*Math.exp(-i/sr*18); const b=biquadBP(nz,2000,0.8); for(let i=0;i<m;i++) b[i]=b[i]*1.6+Math.sin(2*Math.PI*185*i/sr)*Math.exp(-i/sr*25)*0.45; return b; };
  const hat=(open)=>{ const m=Math.round((open?0.35:0.06)*sr), x=new Float32Array(m); let p=0; for(let i=0;i<m;i++){ const r=rnd(); x[i]=(r-p)*0.25*Math.exp(-i/sr*(open?9:60)); p=r; } return biquadBP(x,9000,0.7).map(v=>v*2.2); };
  // accordi Dm Bb F C
  const prog=[[62,65,69],[58,62,65],[60,65,69],[60,64,67]], roots=[38,34,41,36];
  const reverb=(x,dec=0.5)=>{ const L=new Float32Array(x.length+sr), R=new Float32Array(x.length+sr); const dl=[1557,1617,1491,1422], dr=[1277,1356,1188,1116];
    for(const [out,ds] of [[L,dl],[R,dr]]) for(const d of ds){ const buf=new Float32Array(out.length); for(let i=0;i<out.length;i++){ buf[i]=(i<x.length?x[i]:0)+(i>=d?buf[i-d]*dec:0); } for(let i=0;i<out.length;i++) out[i]+=buf[i]*0.12; } return {L,R}; };
  const addRev=(dst,t0,sig,g)=>{ const r=reverb(sig); const i0=Math.round(t0*sr); for(let i=0;i<r.L.length&&i0+i<n;i++){ dst.L[i0+i]+=r.L[i]*g; dst.R[i0+i]+=r.R[i]*g; } };
  const melody=[[69,0,1],[67,1,0.5],[65,1.5,0.5],[67,2,1.5],[62,4,1],[65,5,1],[69,6,1.5],[72,8,1],[70,9,1],[69,10,1],[65,11,1],[67,12,2.5]];
  const vowels=[[730,1090,2440],[270,2290,3010],[300,870,2240],[530,1840,2480]];
  for(let bar=0;bar<bars;bar++){ const t0=bar*4*beat, c=bar%4, full=bar>=2;
    // pad stereo (largo) + piano elettrico al centro
    for(const m of prog[c]){ const a=lp(saw(hz(m),4*beat,6),1800), b=lp(saw(hz(m),4*beat,-6),1800); const env=i=>Math.min(1,i/(0.05*sr))*Math.min(1,(a.length-i)/(0.05*sr));
      for(let i=0;i<a.length;i++){ a[i]*=0.05*env(i); b[i]*=0.05*env(i);} for(const D of [S.other,X.pads]){ add(D,t0,a,-padPan); add(D,t0+0.01,b,padPan); addRev(D,t0,a.map((v,i)=>v+b[i]),0.5); } }
    for(let k=0;k<4;k++){ if(!full) break; for(const m of prog[c]){ const d=beat*0.95, mm=Math.round(d*sr), x=new Float32Array(mm); for(let i=0;i<mm;i++){ const t=i/sr; x[i]=(Math.sin(2*Math.PI*hz(m+12)*t)+0.3*Math.sin(4*Math.PI*hz(m+12)*t))*Math.exp(-t*3.5)*0.07; } add(S.other,t0+k*beat,x,0.05); add(X.ep,t0+k*beat,x,0.05); } }
    if(full){ for(let e=0;e<8;e++){ const t=t0+e*beat/2, d=beat/2*0.9, x=lp(lp(saw(hz(roots[c]+(e%4===3?12:0)),d),bassCut),bassCut); for(let i=0;i<x.length;i++) x[i]*=0.45*Math.min(1,i/80)*Math.min(1,(x.length-i)/300); add(S.bass,t,x,0); }
      add(S.drums,t0,kick()); add(S.drums,t0+2*beat,kick()); add(S.drums,t0+2.5*beat,kick(),0,0.8);
      add(S.drums,t0+beat,snare(),0.05); add(S.drums,t0+3*beat,snare(),0.05); }
    for(let e=0;e<8;e++) add(S.drums,t0+e*beat/2,hat(e===7),0.3,e%2?0.6:0.9);
    if((bar>=4&&bar<8)||(bar>=10&&bar<14)){ const half=(bar%2)*4;
      for(const [m,b,d] of melody){ if(b<half||b>=half+4) continue; const tt=t0+(b-half)*beat, mm=Math.round(d*beat*sr); let ph=0; const x=new Float32Array(mm), vw=vowels[Math.floor(m+b)%4];
        for(let i=0;i<mm;i++){ const t=i/sr, f=hz(m)*(1+0.012*Math.sin(2*Math.PI*5.3*t)*Math.min(1,t*3)); ph+=f/sr; ph-=Math.floor(ph); x[i]=(2*ph-1); }
        let y=new Float32Array(mm); for(const [fq,a] of [[vw[0],1],[vw[1],0.6],[vw[2],0.3]]){ const bp=biquadBP(x,fq,9); for(let i=0;i<mm;i++) y[i]+=bp[i]*a; }
        for(let i=0;i<mm;i++){ y[i]*=0.55*Math.min(1,i/(0.03*sr))*Math.min(1,(mm-i)/(0.06*sr)); }
        const cons=new Float32Array(Math.round(0.04*sr)); for(let i=0;i<cons.length;i++) cons[i]=rnd()*0.15*Math.exp(-i/sr*80); const cb=biquadBP(cons,5000,1);
        add(S.vocals,tt-0.03,cb); add(S.vocals,tt,y); addRev(S.vocals,tt,y,vocRev); } }
  }
  const mix={L:new Float32Array(n),R:new Float32Array(n)}; for(const k in S) for(let i=0;i<n;i++){ mix.L[i]+=S[k].L[i]; mix.R[i]+=S[k].R[i]; }
  let pk=0; for(let i=0;i<n;i++) pk=Math.max(pk,Math.abs(mix.L[i]),Math.abs(mix.R[i])); const g=0.89/pk;
  for(const o of [mix,...Object.values(S),...Object.values(X)]) for(let i=0;i<n;i++){ o.L[i]*=g; o.R[i]*=g; }
  return {sr, n, mix, stems:S, extra:X, bpm, key:'Dm'};
}
