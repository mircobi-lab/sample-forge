'use strict';
/* Sample Forge · MBStudio91 — motore DSP (Web Worker)
   Separazione approssimativa (HPSS + estrazione del centro stereo + bande),
   BPM, tonalità, accordi, onset, scelta e ritaglio dei campioni. */
const N = 2048, HOP = 512, B = N / 2 + 1, MEDH = 17, MEDP = 17;
let SR, LEN, MODE, src, F, mag, cw, feat, grid, fps, keyInfo, cappedSec = 0;
let bandB, bandV, pcOfBin, bandIdx;
const win = new Float64Array(N);
for (let i = 0; i < N; i++) win[i] = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / N);
const re = new Float64Array(N), im = new Float64Array(N);
const fft = makeFFT(N);

function makeFFT(n) {
  const lv = Math.log2(n) | 0, rev = new Uint32Array(n);
  for (let i = 0; i < n; i++) { let r = 0, x = i; for (let j = 0; j < lv; j++) { r = (r << 1) | (x & 1); x >>= 1; } rev[i] = r; }
  const c = new Float64Array(n / 2), s = new Float64Array(n / 2);
  for (let i = 0; i < n / 2; i++) { c[i] = Math.cos(2 * Math.PI * i / n); s[i] = Math.sin(2 * Math.PI * i / n); }
  return function (xr, xi, inv) {
    for (let i = 0; i < n; i++) { const j = rev[i]; if (j > i) { let t = xr[i]; xr[i] = xr[j]; xr[j] = t; t = xi[i]; xi[i] = xi[j]; xi[j] = t; } }
    for (let size = 2; size <= n; size <<= 1) {
      const h = size >> 1, st = n / size;
      for (let i = 0; i < n; i += size) {
        for (let j = i, k = 0; j < i + h; j++, k += st) {
          const wr = c[k], wi = inv ? s[k] : -s[k];
          const ar = xr[j + h], ai = xi[j + h];
          const tr = ar * wr - ai * wi, ti = ar * wi + ai * wr;
          xr[j + h] = xr[j] - tr; xi[j + h] = xi[j] - ti; xr[j] += tr; xi[j] += ti;
        }
      }
    }
    if (inv) for (let i = 0; i < n; i++) { xr[i] /= n; xi[i] /= n; }
  };
}

const progress = (p, msg) => postMessage({ type: 'progress', p, msg });

/* Spettri di L e R di un frame con una sola FFT complessa (z = L + iR) */
function spectra(L, R, f, Lr, Li, Rr, Ri) {
  const s = f * HOP - N / 2;
  for (let i = 0; i < N; i++) {
    const t = s + i;
    if (t >= 0 && t < LEN) { re[i] = L[t] * win[i]; im[i] = R[t] * win[i]; } else { re[i] = 0; im[i] = 0; }
  }
  fft(re, im, false);
  for (let k = 0; k < B; k++) {
    const nk = (N - k) & (N - 1), zr = re[k], zi = im[k], znr = re[nk], zni = im[nk];
    Lr[k] = (zr + znr) / 2; Li[k] = (zi - zni) / 2; Rr[k] = (zi + zni) / 2; Ri[k] = -(zr - znr) / 2;
  }
}

function setupBins() {
  bandB = new Float32Array(B); bandV = new Float32Array(B); pcOfBin = new Int8Array(B); bandIdx = new Uint8Array(B);
  for (let k = 0; k < B; k++) {
    const hz = k * SR / N;
    bandB[k] = hz < 150 ? 1 : hz > 250 ? 0 : 0.5 + 0.5 * Math.cos(Math.PI * (hz - 150) / 100);
    const v = hz < 120 ? 0 : hz < 220 ? 0.5 - 0.5 * Math.cos(Math.PI * (hz - 120) / 100) : hz < 9000 ? 1 : hz < 13000 ? 1 - 0.75 * (hz - 9000) / 4000 : 0.25;
    bandV[k] = Math.min(v, 1 - bandB[k]);
    pcOfBin[k] = (hz >= 130 && hz <= 4200) ? (((Math.round(12 * Math.log2(hz / 440)) + 69) % 12) + 12) % 12 : -1;
    bandIdx[k] = hz < 150 ? 0 : hz < 800 ? 1 : hz < 4000 ? 2 : 3;
  }
}

/* ---------- Separatore v2 ----------
   Maschere di Wiener a 4 sorgenti (batteria / voce / basso / altro) che sommano sempre a 1:
   - HPSS a due scale: mediana temporale corta (Hs) e lunga (Hl) contro mediana in frequenza (Pf).
     La batteria è ciò che è "largo in frequenza", la voce ciò che è armonico ma varia nel tempo
     (vibrato, sillabe), pad e accordi ciò che resta stabile a lungo.
   - Coerenza stereo mediata (tempo + frequenza): niente stime istantanee rumorose, meno "musical noise".
   - Smoothing tempo-frequenza delle maschere, poi rinormalizzazione.
   - Gate sulla voce: dove la voce è assente la sua maschera si chiude, i rientri tornano agli altri stem. */
const P = { q: 1, csharp: 1.5, cSmoothT: 0.55, cSmoothF: 2, medS: 17, medL: 171, medLstep: 4, medP: 17, pMargin: 2, fluctW: 1.0, sustW: 0.4,
  smoothT: 1, smoothF: 1, smoothMode: 'tri', smoothLoHz: 400, sharp: 1.3, vGateLo: 0.02, vGateHi: 0.10, vGateWin: 0.15, kickHz: 130, supLo: 0.15, supHi: 0.6 };
if (typeof SF_PARAMS === 'object' && SF_PARAMS) Object.assign(P, SF_PARAMS);
let msk = null; // Uint8Array(F*B*4) — [batteria, voce, basso, altro], somma 255

function pass1() {
  const { L, R } = src.mix;
  mag = new Float32Array(F * B); cw = new Uint8Array(F * B);
  const Lr = new Float64Array(B), Li = new Float64Array(B), Rr = new Float64Array(B), Ri = new Float64Array(B);
  const tx = new Float64Array(B + 1), ty = new Float64Array(B + 1), tz = new Float64Array(B + 1);
  const Sxy = new Float64Array(B), Sxx = new Float64Array(B), Syy = new Float64Array(B), a = P.cSmoothT, W = P.cSmoothF;
  for (let f = 0; f < F; f++) {
    spectra(L, R, f, Lr, Li, Rr, Ri);
    const base = f * B;
    for (let k = 0; k < B; k++) {
      const a2 = Lr[k] * Lr[k] + Li[k] * Li[k], b2 = Rr[k] * Rr[k] + Ri[k] * Ri[k];
      mag[base + k] = Math.sqrt((a2 + b2) / 2);
      tx[k + 1] = tx[k] + (Lr[k] * Rr[k] + Li[k] * Ri[k]); ty[k + 1] = ty[k] + a2; tz[k + 1] = tz[k] + b2;
    }
    for (let k = 0; k < B; k++) {
      const lo = Math.max(0, k - W), hi = Math.min(B, k + W + 1);
      Sxy[k] = a * Sxy[k] + (1 - a) * (tx[hi] - tx[lo]); Sxx[k] = a * Sxx[k] + (1 - a) * (ty[hi] - ty[lo]); Syy[k] = a * Syy[k] + (1 - a) * (tz[hi] - tz[lo]);
      const den = Sxx[k] + Syy[k];
      const c = den < 1e-18 ? 1 : Math.max(0, 2 * Sxy[k] / den);
      cw[base + k] = Math.round(255 * Math.min(1, c) ** P.csharp);
    }
    if ((f & 255) === 0) progress(0.03 + 0.2 * f / F, 'Analisi spettrale…');
  }
}

function qsel(a, n, k) {
  let l = 0, r = n - 1;
  while (r > l) {
    const pv = a[(l + r) >> 1]; let i = l, j = r;
    while (i <= j) { while (a[i] < pv) i++; while (a[j] > pv) j--; if (i <= j) { const t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; } }
    if (k <= j) r = j; else if (k >= i) l = i; else break;
  }
  return a[k];
}

function buildMasks() {
  msk = new Uint8Array(F * B * 4);
  const bs = new Float32Array(P.medS), bl = new Float32Array(P.medL + 1), bp = new Float32Array(P.medP);
  const hs = P.medS >> 1, hl = P.medL >> 1, hp = P.medP >> 1, q = P.q, binHz = SR / N;
  const aHs = new Float32Array(B), aHl = new Float32Array(B), aPf = new Float32Array(B), mH = new Float32Array(B), comb = new Float32Array(B);
  const kickBins = Math.min(Math.floor((B - 2) / 3), Math.round(P.kickHz / binHz));
  for (let f = 0; f < F; f++) {
    const base = f * B;
    for (let k = 0; k < B; k++) {
      let n = 0;
      for (let g = f - hs; g <= f + hs; g++) bs[n++] = mag[(g < 0 ? 0 : g >= F ? F - 1 : g) * B + k];
      aHs[k] = qsel(bs, n, n >> 1);
      n = 0;
      for (let g = f - hl; g <= f + hl; g += P.medLstep) bl[n++] = mag[(g < 0 ? 0 : g >= F ? F - 1 : g) * B + k];
      aHl[k] = qsel(bl, n, n >> 1);
      n = 0;
      for (let x = k - hp; x <= k + hp; x++) bp[n++] = mag[base + (x < 0 ? 0 : x >= B ? B - 1 : x)];
      aPf[k] = qsel(bp, n, n >> 1);
      const pq = aPf[k] ** q, hm = (P.pMargin * aHs[k]) ** q;
      mH[k] = mag[base + k] * (1 - pq / (pq + hm + 1e-20));
    }
    // cassa contro basso: una nota di basso ha armoniche a 2x e 3x, la coda di una cassa quasi no
    const kickAt = (k) => {
      const h2 = mag[base + 2 * k], h3 = Math.max(mag[base + 3 * k], 0.5 * (mag[base + 3 * k - 1] + mag[base + 3 * k + 1]));
      let t = ((h2 + h3) / (mag[base + k] + 1e-12) - P.supLo) / (P.supHi - P.supLo);
      t = t < 0 ? 0 : t > 1 ? 1 : t * t * (3 - 2 * t); return 1 - t;
    };
    for (let k = 0; k < B; k++) comb[k] = k <= kickBins ? kickAt(Math.max(2, k)) : 0;
    for (let k = 0; k < B; k++) {
      const Hs = aHs[k], Hl = aHl[k], Pf = aPf[k];
      const pq = Pf ** q, hsq = Hs ** q, hlq = Hl ** q, hm = (P.pMargin * Hs) ** q;
      const mp = pq / (pq + hm + 1e-20), mh = 1 - mp;
      const s = Math.min(1, hlq / (hsq + 1e-20));
      const bb = bandB[k];
      let wv = (cw[base + k] / 255) * bandV[k] * (P.fluctW * (1 - s) + P.sustW * s);
      if (wv > 1 - bb) wv = 1 - bb;
      const isBass = 1 - comb[k];
      const o = (base + k) * 4;
      const d = mp + mh * bb * (1 - isBass), v = mh * wv, b = mh * bb * isBass;
      msk[o] = Math.round(255 * d); msk[o + 1] = Math.round(255 * v); msk[o + 2] = Math.round(255 * b); msk[o + 3] = Math.max(0, 255 - msk[o] - msk[o + 1] - msk[o + 2]);
    }
    if ((f & 127) === 0) progress(0.23 + 0.32 * f / F, 'Separazione batteria / voce / basso / accordi…');
  }
}

function smoothMasks() {
  const T = P.smoothT, W = P.smoothF, kLo = Math.round(P.smoothLoHz * N / SR);
  const row = new Float32Array(B * 4), prev = new Float32Array(B * 4), cur = new Float32Array(B * 4), acc = new Float32Array(B * 4);
  // frequenza (in place, riga per riga)
  if (W > 0) for (let f = 0; f < F; f++) {
    const base = f * B * 4;
    for (let i = 0; i < B * 4; i++) row[i] = msk[base + i];
    for (let k = kLo; k < B; k++) for (let s = 0; s < 4; s++) {
      let sum = 0, n = 0; for (let x = Math.max(0, k - W); x <= Math.min(B - 1, k + W); x++) { sum += row[x * 4 + s]; n++; }
      msk[base + k * 4 + s] = Math.round(sum / n);
    }
  }
  // tempo (raggio 1, con copia della riga precedente originale)
  {
    for (let i = 0; i < B * 4; i++) prev[i] = msk[i];
    for (let f = 0; f < F; f++) {
      const base = f * B * 4, nb = Math.min(F - 1, f + 1) * B * 4;
      for (let i = 0; i < B * 4; i++) cur[i] = msk[base + i];
      if (T === 0) for (let i = 0; i < B * 4; i++) acc[i] = cur[i];
      else if (P.smoothMode === 'med') for (let i = 0; i < B * 4; i++) { const a = prev[i], b = cur[i], c = msk[nb + i]; acc[i] = Math.max(Math.min(a, b), Math.min(Math.max(a, b), c)); }
      else if (P.smoothMode === 'tri') for (let i = 0; i < B * 4; i++) acc[i] = 0.25 * prev[i] + 0.5 * cur[i] + 0.25 * msk[nb + i];
      else for (let i = 0; i < B * 4; i++) acc[i] = (prev[i] + cur[i] + msk[nb + i]) / 3;
      for (let i = 0; i < B * 4; i++) prev[i] = cur[i];
      for (let k = 0; k < B; k++) {
        const o = k * 4; let a0 = acc[o] ** P.sharp, a1 = acc[o + 1] ** P.sharp, a2 = acc[o + 2] ** P.sharp, a3 = acc[o + 3] ** P.sharp;
        const sum = a0 + a1 + a2 + a3 || 1;
        const d = Math.round(255 * a0 / sum), v = Math.round(255 * a1 / sum), b = Math.round(255 * a2 / sum);
        msk[base + o] = d; msk[base + o + 1] = v; msk[base + o + 2] = b; msk[base + o + 3] = Math.max(0, 255 - d - v - b);
      }
    }
  }
  progress(0.57, 'Pulizia delle maschere…');
}

function vocalGate() {
  const r = new Float32Array(F);
  for (let f = 0; f < F; f++) {
    let ev = 0, et = 0; const base = f * B;
    for (let k = 0; k < B; k++) { const m = mag[base + k], v = m * msk[(base + k) * 4 + 1] / 255; ev += v * v; et += m * m; }
    r[f] = ev / (et + 1e-20);
  }
  const w = Math.max(1, Math.round(P.vGateWin * fps)), Pr = prefix(r);
  for (let f = 0; f < F; f++) {
    const a = Math.max(0, f - w), b = Math.min(F, f + w + 1), x = (Pr[b] - Pr[a]) / (b - a);
    let g = (x - P.vGateLo) / (P.vGateHi - P.vGateLo); g = g < 0 ? 0 : g > 1 ? 1 : g * g * (3 - 2 * g);
    if (g >= 0.999) continue;
    for (let k = 0; k < B; k++) {
      const o = (f * B + k) * 4, v = msk[o + 1]; if (!v) continue;
      const keep = Math.round(v * g), give = v - keep, rest = msk[o] + msk[o + 2] + msk[o + 3];
      msk[o + 1] = keep;
      if (rest > 0) { const d = Math.round(give * msk[o] / rest), b = Math.round(give * msk[o + 2] / rest); msk[o] += d; msk[o + 2] += b; msk[o + 3] = 255 - msk[o] - msk[o + 1] - msk[o + 2]; }
      else msk[o + 3] += give;
    }
  }
}

/* ---------- Passo 2: descrittori per frame ---------- */
function frameMagsStems(f, o, tmp) {
  const st = ['drums', 'vocals', 'bass', 'other'], m = {};
  for (const name of st) {
    const s = src[name]; spectra(s.L, s.R, f, tmp.Lr, tmp.Li, tmp.Rr, tmp.Ri);
    const a = tmp[name];
    for (let k = 0; k < B; k++) a[k] = Math.sqrt((tmp.Lr[k] ** 2 + tmp.Li[k] ** 2 + tmp.Rr[k] ** 2 + tmp.Ri[k] ** 2) / 2);
    m[name] = a;
  }
  for (let k = 0; k < B; k++) {
    const d = m.drums[k], v = m.vocals[k], b = m.bass[k], x = m.other[k];
    o.t[k] = Math.sqrt(d * d + v * v + b * b + x * x); o.p[k] = d; o.v[k] = v; o.b[k] = b;
    o.i[k] = Math.sqrt(b * b + x * x); o.h[k] = Math.sqrt(b * b + x * x + v * v);
  }
}

function pass2() {
  const z = () => new Float32Array(F);
  feat = { fluxFull: z(), fluxPerc: z(), fluxVoc: z(), fluxBass: z(), eTot: z(), ePerc: z(), eVoc: z(), eBass: z(), eInst: z(),
    pb: new Float32Array(F * 4), chroma: new Float32Array(F * 12), key: new Float64Array(12) };
  const o = { t: new Float32Array(B), p: new Float32Array(B), v: new Float32Array(B), b: new Float32Array(B), i: new Float32Array(B), h: new Float32Array(B) };
  const prev = { t: new Float32Array(B), p: new Float32Array(B), v: new Float32Array(B), b: new Float32Array(B) };
  const tmp = { Lr: new Float64Array(B), Li: new Float64Array(B), Rr: new Float64Array(B), Ri: new Float64Array(B),
    drums: new Float32Array(B), vocals: new Float32Array(B), bass: new Float32Array(B), other: new Float32Array(B) };
  for (let f = 0; f < F; f++) {
    if (MODE === 'sep') {
      const base = f * B;
      for (let k = 0; k < B; k++) {
        const m = mag[base + k], x = (base + k) * 4, d = msk[x] / 255, v = msk[x + 1] / 255, b = msk[x + 2] / 255, c = msk[x + 3] / 255;
        o.t[k] = m; o.p[k] = m * d; o.v[k] = m * v; o.b[k] = m * b; o.i[k] = m * (c + b); o.h[k] = m * (1 - d);
      }
    } else frameMagsStems(f, o, tmp);
    let ff = 0, fp = 0, fv = 0, fb = 0, et = 0, ep = 0, ev = 0, eb = 0, ei = 0;
    const cb = f * 12, pbb = f * 4;
    for (let k = 1; k < B; k++) {
      const lt = Math.log1p(o.t[k]), lp = Math.log1p(o.p[k]), lv = Math.log1p(o.v[k]), lb = Math.log1p(o.b[k]);
      let d = lt - prev.t[k]; if (d > 0) ff += d; prev.t[k] = lt;
      d = lp - prev.p[k]; if (d > 0) fp += d; prev.p[k] = lp;
      d = lv - prev.v[k]; if (d > 0) fv += d; prev.v[k] = lv;
      d = lb - prev.b[k]; if (d > 0) fb += d; prev.b[k] = lb;
      const p2 = o.p[k] * o.p[k];
      et += o.t[k] * o.t[k]; ep += p2; ev += o.v[k] * o.v[k]; eb += o.b[k] * o.b[k]; ei += o.i[k] * o.i[k];
      feat.pb[pbb + bandIdx[k]] += p2;
      const pc = pcOfBin[k];
      if (pc >= 0) { feat.chroma[cb + pc] += o.i[k]; feat.key[pc] += o.h[k]; }
    }
    feat.fluxFull[f] = ff; feat.fluxPerc[f] = fp; feat.fluxVoc[f] = fv; feat.fluxBass[f] = fb;
    feat.eTot[f] = et; feat.ePerc[f] = ep; feat.eVoc[f] = ev; feat.eBass[f] = eb; feat.eInst[f] = ei;
    if ((f & 127) === 0) progress(0.6 + 0.13 * f / F, MODE === 'sep' ? 'Calcolo dei descrittori…' : 'Analisi degli stem…');
  }
}

/* ---------- utilità ---------- */
function percentile(arr, q) { const a = Float32Array.from(arr).sort(); return a.length ? a[Math.min(a.length - 1, Math.floor(a.length * q))] : 0; }
function prefix(x) { const p = new Float64Array(x.length + 1); for (let i = 0; i < x.length; i++) p[i + 1] = p[i] + x[i]; return p; }
function peaks(x, w, meanW, deltaRel) {
  const n = x.length, P = prefix(x), delta = deltaRel * percentile(x, 0.95), out = [];
  let last = -1e9;
  for (let i = 1; i < n - 1; i++) {
    const v = x[i]; if (v <= 0 || v <= x[i - 1]) continue;
    let ok = true;
    for (let j = Math.max(0, i - w); j <= Math.min(n - 1, i + w); j++) if (x[j] > v) { ok = false; break; }
    if (!ok) continue;
    const a = Math.max(0, i - meanW), b = Math.min(n, i + meanW + 1), mean = (P[b] - P[a]) / (b - a);
    if (v >= mean * 1.25 + delta && i - last >= w) { out.push(i); last = i; }
  }
  return out;
}
function interp(x, p) { const i = Math.floor(p); if (i < 0 || i >= x.length - 1) return 0; const fr = p - i; return x[i] * (1 - fr) + x[i + 1] * fr; }

/* ---------- Tempo, fase e battere ---------- */
function onsetEnv() {
  const x = feat.fluxFull, n = x.length, P = prefix(x), w = Math.round(fps * 0.25), env = new Float32Array(n);
  for (let i = 0; i < n; i++) { const a = Math.max(0, i - w), b = Math.min(n, i + w + 1); env[i] = Math.max(0, x[i] - (P[b] - P[a]) / (b - a)); }
  return env;
}
function bestPhase(env, Tf) {
  let best = -1, bp = 0;
  for (let ph = 0; ph < Tf; ph += 0.5) {
    let s = 0; for (let p = ph; p < env.length; p += Tf) s += interp(env, p);
    if (s > best) { best = s; bp = ph; }
  }
  return { score: best, ph: bp };
}
function makeGrid(forcedBpm) {
  const env = onsetEnv(); let bpm, ph;
  const dur = LEN / SR;
  if (forcedBpm) { bpm = forcedBpm; ph = bestPhase(env, fps * 60 / bpm).ph; }
  else {
    const n = env.length; let best = -1, bestLag = 0;
    for (let lag = Math.floor(fps * 60 / 200); lag <= Math.ceil(fps * 60 / 60); lag++) {
      let s = 0; for (let i = 0; i + lag < n; i++) s += env[i] * env[i + lag];
      s /= Math.max(1, n - lag);
      const b = 60 * fps / lag, w = Math.exp(-0.5 * (Math.log2(b / 120) / 0.9) ** 2);
      if (s * w > best) { best = s * w; bestLag = lag; }
    }
    let b0 = best > 0 ? 60 * fps / bestLag : 120;
    while (b0 < 78) b0 *= 2; while (b0 > 165) b0 /= 2;
    let bs = -1; bpm = b0; ph = 0;
    for (let b = b0 - 3.5; b <= b0 + 3.5; b += 0.02) { const r = bestPhase(env, fps * 60 / b); if (r.score > bs) { bs = r.score; bpm = b; ph = r.ph; } }
    const rb = Math.round(bpm);
    if (Math.abs(bpm - rb) < 0.35) { const r = bestPhase(env, fps * 60 / rb); if (r.score >= 0.97 * bs) { bpm = rb; ph = r.ph; } }
    bpm = Math.round(bpm * 100) / 100;
  }
  const T = 60 / bpm; let t0 = ph / fps; if (t0 > 0.75 * T) t0 -= T; const nBeats = Math.max(0, Math.floor((dur - t0) / T));
  // battere: dove cambiano gli accordi (novità armonica) + dove cade la cassa
  const chB = new Float32Array(nBeats * 12), lowB = new Float32Array(nBeats);
  for (let b = 0; b < nBeats; b++) {
    const f0 = Math.max(0, Math.round((t0 + b * T) * fps)), f1 = Math.round((t0 + (b + 1) * T) * fps);
    for (let f = f0; f < f1 && f < F; f++) for (let p = 0; p < 12; p++) chB[b * 12 + p] += feat.chroma[f * 12 + p];
    let low = 0; for (let g = Math.max(0, f0 - 2); g <= Math.min(F - 1, f0 + 2); g++) low = Math.max(low, feat.pb[g * 4]); lowB[b] = Math.sqrt(low);
  }
  const win2 = (b, dir) => { const v = new Float32Array(12); for (let x = 0; x < 2; x++) { const bb = dir > 0 ? b + x : b - 1 - x; if (bb >= 0 && bb < nBeats) for (let p = 0; p < 12; p++) v[p] += chB[bb * 12 + p]; } return v; };
  const cosd = (a, c) => { let n = 0, x = 0, y = 0; for (let p = 0; p < 12; p++) { n += a[p] * c[p]; x += a[p] * a[p]; y += c[p] * c[p]; } return (x > 0 && y > 0) ? 1 - n / Math.sqrt(x * y) : 0; };
  const nov = new Float32Array(nBeats); for (let b = 1; b < nBeats; b++) nov[b] = cosd(win2(b, -1), win2(b, 1));
  const sumC = new Float64Array(4), sumL = new Float64Array(4);
  for (let b = 0; b < nBeats; b++) { sumC[b % 4] += nov[b]; sumL[b % 4] += lowB[b]; }
  const nC = Math.max(...sumC) || 1, nL = Math.max(...sumL) || 1;
  let d = 0, bestD = -1;
  for (let c = 0; c < 4; c++) { const s = sumC[c] / nC + 0.6 * sumL[c] / nL; if (s > bestD) { bestD = s; d = c; } }
  grid = { bpm, T, t0, d, nBeats, nBars: Math.max(0, Math.floor((nBeats - d) / 4)) };
}
const beatSample = b => Math.round((grid.t0 + b * grid.T) * SR);
const barBeat = j => grid.d + 4 * j;
function beatAgg(arr) {
  const out = new Float32Array(grid.nBeats);
  for (let b = 0; b < grid.nBeats; b++) {
    const f0 = Math.max(0, Math.round((grid.t0 + b * grid.T) * fps)), f1 = Math.round((grid.t0 + (b + 1) * grid.T) * fps);
    let s = 0; for (let f = f0; f < f1 && f < F; f++) s += arr[f]; out[b] = s;
  }
  return out;
}

/* ---------- Tonalità ---------- */
const NAMES_S = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const NAMES_F = ['C', 'Db', 'D', 'Eb', 'E', 'F', 'Gb', 'G', 'Ab', 'A', 'Bb', 'B'];
const NAMES_IT = ['Do', 'Do#', 'Re', 'Mi♭', 'Mi', 'Fa', 'Fa#', 'Sol', 'La♭', 'La', 'Si♭', 'Si'];
const keyCorr = new Float32Array(24);
function detectKey() {
  const maj = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88];
  const min = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];
  const x = Array.from(feat.key);
  const corr = (a, b) => { const ma = a.reduce((s, v) => s + v, 0) / 12, mb = b.reduce((s, v) => s + v, 0) / 12; let n = 0, da = 0, db = 0; for (let i = 0; i < 12; i++) { n += (a[i] - ma) * (b[i] - mb); da += (a[i] - ma) ** 2; db += (b[i] - mb) ** 2; } return n / Math.sqrt(da * db + 1e-12); };
  let best = -2, root = 0, minor = false;
  for (let r = 0; r < 12; r++) for (const [prof, m] of [[maj, false], [min, true]]) {
    const rot = x.map((_, i) => x[(i + r) % 12]); const c = corr(rot, prof);
    keyCorr[(m ? 12 : 0) + r] = c;
    if (c > best) { best = c; root = r; minor = m; }
  }
  const flatKeys = minor ? [2, 7, 0, 5, 10, 3] : [5, 10, 3, 8, 1, 6];
  const flats = flatKeys.includes(root);
  const names = flats ? NAMES_F : NAMES_S;
  keyInfo = { root, minor, flats, name: names[root] + (minor ? 'm' : ''), it: NAMES_IT[root] + (minor ? ' minore' : ' maggiore'), conf: best };
}
const chordName = (r, minor) => (keyInfo.flats ? NAMES_F : NAMES_S)[r] + (minor ? 'm' : '');

function __setMix(m) { const o = src.mix; src.mix = m; return o; }
/* ---------- Sintesi (ricostruzione di uno stem su un intervallo) ---------- */
function synth(name, s0, s1) {
  s0 = Math.round(s0); s1 = Math.round(s1);
  if (s0 < 0) { const a = synthR(name, 0, s1); if (!a) return null; const L = new Float32Array(s1 - s0), R = new Float32Array(s1 - s0); L.set(a.L, -s0); R.set(a.R, -s0); return { L, R }; }
  return synthR(name, s0, s1);
}
function synthR(name, s0, s1) {
  s0 = Math.max(0, Math.round(s0)); s1 = Math.min(LEN, Math.round(s1));
  const len = s1 - s0; if (len <= 16) return null;
  const L = new Float32Array(len), R = new Float32Array(len);
  if (MODE === 'stems') {
    const parts = { drums: ['drums'], vocals: ['vocals'], bass: ['bass'], chords: ['other'], harm: ['bass', 'other'], inst: ['drums', 'bass', 'other'], mix: ['drums', 'vocals', 'bass', 'other'] }[name];
    for (const p of parts) { const s = src[p]; for (let i = 0; i < len; i++) { L[i] += s.L[s0 + i]; R[i] += s.R[s0 + i]; } }
    return { L, R };
  }
  if (name === 'mix') { L.set(src.mix.L.subarray(s0, s1)); R.set(src.mix.R.subarray(s0, s1)); return { L, R }; }
  const Lr = new Float64Array(B), Li = new Float64Array(B), Rr = new Float64Array(B), Ri = new Float64Array(B), m = new Float32Array(B);
  const sel = { drums: [0], vocals: [1], bass: [2], chords: [3], harm: [2, 3], inst: [0, 2, 3] }[name];
  const fA = Math.max(0, Math.ceil((s0 - N / 2) / HOP)), fB = Math.min(F - 1, Math.floor((s1 + N / 2) / HOP));
  for (let f = fA; f <= fB; f++) {
    spectra(src.mix.L, src.mix.R, f, Lr, Li, Rr, Ri);
    for (let k = 0; k < B; k++) { const o = (f * B + k) * 4; let v = 0; for (const j of sel) v += msk[o + j]; m[k] = v / 255; }
    for (let k = 0; k < B; k++) {
      const lr = Lr[k] * m[k], li = Li[k] * m[k], rr = Rr[k] * m[k], ri = Ri[k] * m[k];
      re[k] = lr - ri; im[k] = li + rr;
      if (k > 0 && k < N / 2) { re[N - k] = lr + ri; im[N - k] = -li + rr; }
    }
    fft(re, im, true);
    const st = f * HOP - N / 2 - s0;
    for (let i = 0; i < N; i++) { const t = st + i; if (t >= 0 && t < len) { const w = win[i] / 1.5; L[t] += re[i] * w; R[t] += im[i] * w; } }
  }
  return { L, R };
}

function fades(a, inMs, outMs) {
  const ni = Math.min(a.L.length, Math.round(inMs * SR / 1000)), no = Math.min(a.L.length, Math.round(outMs * SR / 1000)), n = a.L.length;
  for (let i = 0; i < ni; i++) { const g = i / ni; a.L[i] *= g; a.R[i] *= g; }
  for (let i = 0; i < no; i++) { const g = i / no; a.L[n - 1 - i] *= g; a.R[n - 1 - i] *= g; }
  return a;
}
function normalize(a) {
  let pk = 0; for (let i = 0; i < a.L.length; i++) { pk = Math.max(pk, Math.abs(a.L[i]), Math.abs(a.R[i])); }
  if (pk < 1e-5) return null;
  const g = 0.891 / pk; for (let i = 0; i < a.L.length; i++) { a.L[i] *= g; a.R[i] *= g; }
  a.rawPeak = pk; return a;
}
function tailLowpass(a, t0, t1, hz) {
  const c = Math.exp(-2 * Math.PI * hz / SR), i0 = Math.round(t0 * SR), i1 = Math.round(t1 * SR);
  for (const ch of [a.L, a.R]) { let y1 = 0, y2 = 0;
    for (let i = 0; i < ch.length; i++) { y1 = (1 - c) * ch[i] + c * y1; y2 = (1 - c) * y1 + c * y2; const w = i <= i0 ? 0 : i >= i1 ? 1 : (i - i0) / (i1 - i0); ch[i] = ch[i] * (1 - w) + y2 * w; } }
}
function slice(a, from, len) { return { L: a.L.slice(from, from + len), R: a.R.slice(from, from + len) }; }
const fmtT = t => { const m = Math.floor(t / 60), s = t - m * 60; return m + ':' + (s < 10 ? '0' : '') + s.toFixed(1); };
function makePad(a, label, sub, start, kind, extra) {
  if (!a) return null; a = normalize(a); if (!a) return null;
  return Object.assign({ label, sub, L: a.L, R: a.R, start, dur: a.L.length / SR, kind }, extra || {});
}

/* ---------- BATTERIA ---------- */
function selectDrums() {
  const on = peaks(feat.fluxPerc, 3, Math.round(fps * 0.4), 0.08);
  const items = on.map((f) => {
    const e = [0, 0, 0, 0]; let s = 0, tot = 0;
    for (let g = f; g < f + 3 && g < F; g++) { for (let c = 0; c < 4; c++) e[c] += feat.pb[g * 4 + c]; s += feat.ePerc[g]; tot += feat.eTot[g]; }
    const sum = e[0] + e[1] + e[2] + e[3] + 1e-12; let late = 0, hi0 = 1e-12;
    for (let g = f; g < f + 3 && g < F; g++) hi0 += feat.pb[g * 4 + 3];
    for (let g = f + 8; g < f + 11 && g < F; g++) late += feat.pb[g * 4 + 3];
    return { f, t: f / fps, sh: e.map(x => x / sum), s, ratio: s / (tot + 1e-12), decay: late / hi0 };
  });
  const maxS = Math.max(1e-12, ...items.map(i => i.s));
  items.forEach((it, i) => {
    const nx = items[i + 1], pv = items[i - 1];
    it.gapN = nx ? nx.t - it.t : 9; it.gapP = pv ? it.t - pv.t : 9; it.nextS = nx ? nx.s : 0;
    const protos = { kick: [0.75, 0.18, 0.05, 0.02], snare: [0.12, 0.38, 0.35, 0.15], hat: [0.02, 0.06, 0.27, 0.65], perc: [0.25, 0.55, 0.15, 0.05] };
    let bd = 9; for (const [k, p] of Object.entries(protos)) { const d = p.reduce((s, v, j) => s + (v - it.sh[j]) ** 2, 0); if (d < bd) { bd = d; it.cls = k; } }
    it.score = Math.sqrt(it.s / maxS) * Math.min(1, it.gapN / 0.18) * Math.min(1, it.gapP / 0.08) * (0.25 + it.ratio);
  });
  const valid = items.filter(i => i.s > 0.02 * maxS);
  const used = new Set();
  const pick = (list, n) => {
    const sorted = list.filter(i => !used.has(i)).sort((a, b) => b.score - a.score), out = [];
    for (const minGap of [4, 0.5]) for (const it of sorted) {
      if (out.length >= n) break;
      if (!out.includes(it) && out.every(o => Math.abs(o.t - it.t) > minGap)) out.push(it);
    }
    out.forEach(o => used.add(o)); return out;
  };
  const kicks = pick(valid.filter(i => i.cls === 'kick'), 2), snares = pick(valid.filter(i => i.cls === 'snare'), 2);
  const hats = pick(valid.filter(i => i.cls === 'hat'), 2), percs = pick(valid.filter(i => i.cls === 'perc'), 2);
  const name = { kick: 'Cassa', snare: 'Rullante', hat: 'Hi-hat', perc: 'Perc' };
  const slots = [kicks[0], kicks[1], snares[0], snares[1], hats[0], hats[1], percs[0], percs[1]];
  const want = ['kick', 'kick', 'snare', 'snare', 'hat', 'hat', 'perc', 'perc'];
  for (let i = 0; i < 8; i++) if (!slots[i]) { const alt = pick(valid, 1)[0]; if (alt) { slots[i] = alt; } }
  const counters = {};
  return slots.map((it, i) => {
    if (!it) return null;
    let len = { kick: 0.5, snare: 0.45, hat: it.decay < 0.12 ? 0.2 : 0.55, perc: 0.4 }[it.cls];
    if (it.nextS > 0.12 * it.s && it.gapN < len) len = Math.max(it.cls === 'hat' ? 0.05 : 0.1, it.gapN - 0.008);
    const pre = 0.04, s0 = Math.round((it.t - pre) * SR), raw = synth('drums', s0, s0 + Math.round((len + pre) * SR));
    if (!raw) return null;
    const look = Math.min(raw.L.length, Math.round(0.12 * SR)); let pk = 0;
    for (let j = 0; j < look; j++) pk = Math.max(pk, Math.abs(raw.L[j]), Math.abs(raw.R[j]));
    let idx = 0; while (idx < look && Math.max(Math.abs(raw.L[idx]), Math.abs(raw.R[idx])) < 0.12 * pk) idx++;
    const st = Math.max(0, idx - Math.round(0.002 * SR));
    const a = fades(slice(raw, st, Math.round(len * SR)), 0.5, it.cls === 'kick' ? 25 : 12);
    if (it.cls === 'kick') tailLowpass(a, 0.05, 0.11, 900);
    let label = name[it.cls];
    if (it.cls === 'hat') label = it.decay < 0.12 ? 'Hi-hat chiuso' : 'Hi-hat aperto';
    counters[label] = (counters[label] || 0) + 1;
    const lab = want[i] === it.cls ? label + (counters[label] > 1 ? ' 2' : '') : label + ' (alt)';
    return makePad(a, lab, fmtT(it.t), (s0 + st) / SR, 'drum', { cls: it.cls });
  });
}

/* ---------- VOCI ---------- */
function selectVox(bV, bT) {
  const pads = [], taken = [];
  const p90 = percentile(bV, 0.9) || 1e-12;
  const free = (a, b) => taken.every(([x, y]) => b <= x || a >= y);
  // chop di 2 movimenti che partono da un attacco della voce
  const von = peaks(feat.fluxVoc, 4, Math.round(fps * 0.5), 0.1);
  const chopLen = Math.min(1.6, 2 * grid.T);
  const chops = von.map(f => {
    let v = 0, t = 0; for (let g = f; g < Math.min(F, f + Math.round(chopLen * fps)); g++) { v += feat.eVoc[g]; t += feat.eTot[g]; }
    return { t: f / fps, score: v * (v / (t + 1e-12)) };
  }).sort((a, b) => b.score - a.score);
  const chosen = [];
  for (const c of chops) { if (chosen.length >= 3) break; if (chosen.every(o => Math.abs(o.t - c.t) > 3)) chosen.push(c); }
  chosen.sort((a, b) => a.t - b.t).forEach((c, i) => {
    const s0 = Math.round((c.t - 0.01) * SR), a = synth('vocals', s0, s0 + Math.round(chopLen * SR));
    taken.push([c.t, c.t + chopLen]);
    pads.push(a ? makePad(fades(a, 3, 30), 'Chop ' + (i + 1), fmtT(c.t), s0 / SR, 'vox') : null);
  });
  while (pads.length < 3) pads.push(null);
  const phrase = (nb, n) => {
    const cand = [];
    for (let j = 0; j + nb <= grid.nBars; j++) {
      let v = 0, t = 0, cov = 0; const b0 = barBeat(j);
      for (let b = b0; b < b0 + 4 * nb; b++) { v += bV[b]; t += bT[b]; if (bV[b] > 0.2 * p90) cov++; }
      cov /= 4 * nb; cand.push({ j, score: v * (v / (t + 1e-12)) * cov * cov });
    }
    cand.sort((a, b) => b.score - a.score);
    const out = [];
    for (const c of cand) {
      if (out.length >= n || c.score <= 0) break;
      const a0 = beatSample(barBeat(c.j)) / SR, a1 = beatSample(barBeat(c.j + nb)) / SR;
      if (out.every(o => o.j + nb <= c.j || c.j + nb <= o.j)) { out.push(c); taken.push([a0, a1]); }
    }
    return out.sort((a, b) => a.j - b.j);
  };
  const lab = nb => nb === 1 ? 'Frase 1 battuta' : 'Frase 2 battute';
  for (const [nb, n] of [[1, 3], [2, 2]]) {
    const ph = phrase(nb, n);
    for (let i = 0; i < n; i++) {
      const c = ph[i]; if (!c) { pads.push(null); continue; }
      const s0 = beatSample(barBeat(c.j)), s1 = s0 + Math.round(nb * 4 * grid.T * SR), a = synth('vocals', s0, s1);
      pads.push(a ? makePad(fades(a, 3, 20), lab(nb) + (n > 1 ? ' ' + (i + 1) : ''), 'battuta ' + (c.j + 1) + ' · ' + fmtT(s0 / SR), s0 / SR, 'vox', { bars: nb }) : null);
    }
  }
  return pads.slice(0, 8);
}

/* ---------- LOOP ---------- */
function selectLoops(A) {
  const { bT, bP, bV, bB, bI } = A, ranges = [];
  const barSum = (arr, j, nb) => { let s = 0; for (let b = barBeat(j); b < barBeat(j + nb); b++) s += arr[b] || 0; return s; };
  const best = (nb, fn, avoid) => {
    while (nb > 1 && nb > grid.nBars) nb >>= 1;
    let bj = -1, bs = -1;
    for (let j = 0; j + nb <= grid.nBars; j++) {
      if (avoid && ranges.some(([a, b]) => !(j + nb <= a || j >= b))) continue;
      const s = fn(j, nb); if (s > bs) { bs = s; bj = j; }
    }
    if (bj < 0 && avoid) return best(nb, fn, false);
    return bj < 0 ? null : { j: bj, nb };
  };
  const mk = (w, srcName, label) => {
    if (!w) return null;
    const s0 = beatSample(barBeat(w.j)), s1 = s0 + Math.round(w.nb * 4 * grid.T * SR), a = synth(srcName, s0, s1);
    if (!a) return null;
    return makePad(fades(a, 1.5, 2), label + ' ' + w.nb + (w.nb === 1 ? ' batt.' : ' batt.'), 'battuta ' + (w.j + 1) + ' · ' + fmtT(s0 / SR), s0 / SR, 'loop', { bars: w.nb, loop: true });
  };
  const rel = (num, den) => (j, nb) => { const n = barSum(num, j, nb), d = barSum(den, j, nb); return n * (n / (d + 1e-12)); };
  const wA = best(4, (j, nb) => barSum(bT, j, nb) + 0.5 * barSum(bP, j, nb)); if (wA) ranges.push([wA.j, wA.j + wA.nb]);
  const wS = best(2, (j, nb) => barSum(bT, j, nb), true); if (wS) ranges.push([wS.j, wS.j + wS.nb]);
  const wD = best(1, (j, nb) => barSum(bT, j, nb), true);
  const instScore = (j, nb) => barSum(bI, j, nb) + barSum(bP, j, nb) - 2 * barSum(bV, j, nb);
  return [
    mk(wA, 'mix', 'Mix'), mk(wS, 'mix', 'Mix'), mk(wD, 'mix', 'Mix'),
    mk(best(2, rel(bP, bT)), 'drums', 'Batteria'),
    mk(best(4, instScore), 'inst', 'Strumentale'),
    mk(best(4, rel(bV, bT)), 'vocals', 'A cappella'),
    mk(best(2, rel(bI, bT)), 'chords', 'Accordi'),
    mk(best(2, rel(bB, bT)), 'bass', 'Basso')
  ];
}

/* ---------- ACCORDI ---------- */
let progression = [];
function selectChords(bI) {
  const nb = grid.nBeats, cb = new Float32Array(nb * 12);
  for (let b = 0; b < nb; b++) {
    const f0 = Math.max(0, Math.round((grid.t0 + b * grid.T) * fps)), f1 = Math.round((grid.t0 + (b + 1) * grid.T) * fps);
    for (let f = f0; f < f1 && f < F; f++) for (let p = 0; p < 12; p++) cb[b * 12 + p] += feat.chroma[f * 12 + p];
  }
  const tpl = []; // 0..11 maggiori, 12..23 minori
  for (const minor of [false, true]) for (let r = 0; r < 12; r++) { const v = new Float32Array(12); v[r] = 1; v[(r + (minor ? 3 : 4)) % 12] = 1; v[(r + 7) % 12] = 1; tpl.push({ v, r, minor }); }
  const k = keyInfo.root, dia = new Set(keyInfo.minor ? [k + 12, (k + 3) % 12, ((k + 5) % 12) + 12, ((k + 7) % 12) + 12, (k + 7) % 12, (k + 8) % 12, (k + 10) % 12]
    : [k, ((k + 2) % 12) + 12, ((k + 4) % 12) + 12, (k + 5) % 12, (k + 7) % 12, ((k + 9) % 12) + 12]);
  const sc = new Float32Array(nb * 24);
  for (let b = 0; b < nb; b++) {
    let n = 0; for (let p = 0; p < 12; p++) n += cb[b * 12 + p] ** 2; n = Math.sqrt(n) || 1;
    for (let c = 0; c < 24; c++) { let s = 0; for (let p = 0; p < 12; p++) s += cb[b * 12 + p] * tpl[c].v[p]; sc[b * 24 + c] = s / (n * Math.sqrt(3)) + (dia.has(c) ? 0.04 : 0); }
  }
  const med = percentile(Array.from(bI).filter(x => x > 0), 0.5);
  const lab = new Int8Array(nb), conf = new Float32Array(nb);
  for (let b = 0; b < nb; b++) {
    if (bI[b] < 0.08 * med) { lab[b] = -1; continue; }
    let bc = 0, bs = -1;
    for (let c = 0; c < 24; c++) { const s = 0.5 * (b > 0 ? sc[(b - 1) * 24 + c] : 0) + sc[b * 24 + c] + 0.5 * (b < nb - 1 ? sc[(b + 1) * 24 + c] : 0); if (s > bs) { bs = s; bc = c; } }
    lab[b] = bc; conf[b] = sc[b * 24 + bc];
  }
  const segs = []; // {c, b0, n, conf}
  for (let b = 0; b < nb;) { let e = b; while (e < nb && lab[e] === lab[b]) e++; if (lab[b] >= 0) { let c = 0; for (let x = b; x < e; x++) c += conf[x]; segs.push({ c: lab[b], b0: b, n: e - b, conf: c / (e - b) }); } b = e; }
  // progressione per battuta
  progression = [];
  for (let j = 0; j < grid.nBars; j++) {
    const cnt = {}; for (let b = barBeat(j); b < barBeat(j + 1); b++) if (lab[b] >= 0) cnt[lab[b]] = (cnt[lab[b]] || 0) + 1;
    const top = Object.entries(cnt).sort((a, b) => b[1] - a[1])[0];
    progression.push(top ? chordName(tpl[+top[0]].r, tpl[+top[0]].minor) : '—');
  }
  const byC = {};
  for (const s of segs) {
    const o = byC[s.c] || (byC[s.c] = { c: s.c, first: s.b0, total: 0, best: null }); o.total += s.n;
    const q = Math.min(s.n, 4) + s.conf; if (!o.best || q > o.bestQ) { o.best = s; o.bestQ = q; }
  }
  let list = Object.values(byC).filter(o => o.best.n >= 2 || o.total >= 2).sort((a, b) => b.total - a.total).slice(0, 8).sort((a, b) => a.first - b.first);
  return Array.from({ length: 8 }, (_, i) => {
    const o = list[i]; if (!o) return null;
    const s = o.best, n = Math.max(2, Math.min(4, s.n)), s0 = beatSample(s.b0), s1 = beatSample(s.b0 + n);
    const a = synth('harm', s0, s1); if (!a) return null;
    const nm = chordName(tpl[o.c].r, tpl[o.c].minor);
    return makePad(fades(a, 4, 50), nm, n + ' mov. · ' + fmtT(s0 / SR), s0 / SR, 'chord', { chord: nm });
  });
}

/* Maggiore o relativa minore? Il profilo di Krumhansl le confonde spesso:
   decide l'accordo di tonica più presente, con peso al primo e all'ultimo accordo. */
function refineKey() {
  const bars = progression.filter(c => c !== '—'); if (bars.length < 2) return;
  let best = -1e9, bk = -1;
  for (let key = 0; key < 24; key++) {
    const r = key % 12, minor = key >= 12, nm = (x, m) => chordName((r + x) % 12, m);
    const dia = minor ? [nm(0, true), nm(3, false), nm(5, true), nm(7, true), nm(7, false), nm(8, false), nm(10, false)]
      : [nm(0, false), nm(2, true), nm(4, true), nm(5, false), nm(7, false), nm(9, true)];
    const tonic = dia[0], frac = bars.filter(c => dia.includes(c)).length / bars.length;
    const sc = 2 * frac + bars.filter(c => c === tonic).length / bars.length + (bars[0] === tonic ? 0.35 : 0) + (bars[bars.length - 1] === tonic ? 0.2 : 0) + 0.3 * keyCorr[key];
    if (sc > best) { best = sc; bk = key; }
  }
  const root = bk % 12, minor = bk >= 12;
  if (root === keyInfo.root && minor === keyInfo.minor) return;
  const flats = (minor ? [2, 7, 0, 5, 10, 3] : [5, 10, 3, 8, 1, 6]).includes(root);
  keyInfo = { root, minor, flats, name: (flats ? NAMES_F : NAMES_S)[root] + (minor ? 'm' : ''), it: NAMES_IT[root] + (minor ? ' minore' : ' maggiore'), conf: keyCorr[bk] };
}
function inKey(midi) {
  const steps = keyInfo.minor ? [0, 2, 3, 5, 7, 8, 10, 11] : [0, 2, 4, 5, 7, 9, 11];
  return steps.includes(((midi - keyInfo.root) % 12 + 12) % 12);
}

/* ---------- BASSO ---------- */
function yin(x, sr) {
  const tMin = Math.floor(sr / 400), tMax = Math.ceil(sr / 28), W = Math.min(x.length - tMax - 1, Math.floor(sr * 0.12));
  if (W < tMax) return null;
  const d = new Float32Array(tMax + 1);
  for (let tau = 1; tau <= tMax; tau++) { let s = 0; for (let j = 0; j < W; j++) { const v = x[j] - x[j + tau]; s += v * v; } d[tau] = s; }
  const cm = new Float32Array(tMax + 1); cm[0] = 1; let run = 0;
  for (let tau = 1; tau <= tMax; tau++) { run += d[tau]; cm[tau] = d[tau] * tau / (run || 1); }
  let tau = -1;
  for (let t = tMin; t <= tMax; t++) if (cm[t] < 0.15) { while (t + 1 <= tMax && cm[t + 1] < cm[t]) t++; tau = t; break; }
  if (tau < 0) { let bv = 1; for (let t = tMin; t <= tMax; t++) if (cm[t] < bv) { bv = cm[t]; tau = t; } if (bv > 0.3) return null; }
  let tt = tau;
  if (tau > 1 && tau < tMax) { const a = cm[tau - 1], b = cm[tau], c = cm[tau + 1], den = a - 2 * b + c; if (Math.abs(den) > 1e-9) tt = tau + 0.5 * (a - c) / den; }
  return { f0: sr / tt, clarity: 1 - cm[tau] };
}
function selectBass() {
  const on = peaks(feat.fluxBass, 4, Math.round(fps * 0.5), 0.1);
  const D = Math.max(1, Math.floor(SR / 4000)), sr2 = SR / D, notes = {};
  const cands = on.map((f, i) => ({ f, t: f / fps, gap: (on[i + 1] !== undefined ? (on[i + 1] - f) / fps : 1) }));
  let eMax = 0; for (const c of cands) { let e = 0; for (let g = c.f; g < Math.min(F, c.f + 8); g++) e += feat.eBass[g]; c.e = e; eMax = Math.max(eMax, e); }
  const top = cands.filter(c => c.e > 0.05 * eMax && c.gap >= 0.16).sort((a, b) => b.e - a.e).slice(0, 60);
  for (const c of top) {
    const len = Math.min(1.0, c.gap - 0.01), s0 = Math.round((c.t - 0.01) * SR), a = synth('bass', s0, s0 + Math.round(len * SR));
    if (!a) continue;
    const off = Math.round(0.03 * SR), n = Math.floor((a.L.length - off) / D), x = new Float32Array(Math.max(0, n));
    for (let i = 0; i < n; i++) { let s = 0; for (let j = 0; j < D; j++) { const k = off + i * D + j; s += a.L[k] + a.R[k]; } x[i] = s / (2 * D); }
    const r = yin(x, sr2); if (!r) continue;
    const midi = Math.round(69 + 12 * Math.log2(r.f0 / 440)); if (midi < 23 || midi > 64) continue;
    const q = r.clarity * Math.sqrt(c.e / eMax) * Math.min(1, len / 0.4);
    if (!notes[midi] || q > notes[midi].q) notes[midi] = { midi, f0: r.f0, q, a, t: c.t, s0 };
  }
  // una nota fuori tonalità a un semitono da una nota in tonalità più forte è quasi sempre un errore di intonazione
  for (const o of Object.values(notes)) if (!inKey(o.midi)) for (const nb of [o.midi - 1, o.midi + 1]) if (notes[nb] && inKey(nb) && notes[nb].q >= 0.25 * o.q) { delete notes[o.midi]; break; }
  const list = Object.values(notes).sort((a, b) => b.q - a.q).slice(0, 8).sort((a, b) => a.midi - b.midi);
  const names = keyInfo.flats ? NAMES_F : NAMES_S;
  return Array.from({ length: 8 }, (_, i) => {
    const o = list[i]; if (!o) return null;
    const nm = names[o.midi % 12] + (Math.floor(o.midi / 12) - 1);
    return makePad(fades(o.a, 2, 40), 'Basso ' + nm, o.f0.toFixed(1) + ' Hz · ' + fmtT(o.t), o.s0 / SR, 'bass', { note: nm, midi: o.midi });
  });
}

/* ---------- regia ---------- */
function sliceAll() {
  progress(0.8, 'Scelgo i campioni migliori…');
  const A = { bT: beatAgg(feat.eTot), bP: beatAgg(feat.ePerc), bV: beatAgg(feat.eVoc), bB: beatAgg(feat.eBass), bI: beatAgg(feat.eInst) };
  const banks = [];
  progress(0.82, 'Ritaglio la batteria…'); banks.push({ id: 'drums', pads: selectDrums() });
  progress(0.87, 'Ritaglio le voci…'); banks.push({ id: 'vox', pads: selectVox(A.bV, A.bT) });
  progress(0.91, 'Preparo i loop…'); banks.push({ id: 'loops', pads: selectLoops(A) });
  progress(0.95, 'Riconosco gli accordi…'); banks.push({ id: 'chords', pads: selectChords(A.bI) });
  refineKey();
  progress(0.97, 'Estraggo le note di basso…'); banks.push({ id: 'bass', pads: selectBass() });
  const tr = [];
  banks.forEach(b => b.pads.forEach(p => { if (p) tr.push(p.L.buffer, p.R.buffer); }));
  postMessage({ type: 'result', sr: SR, dur: LEN / SR, cappedSec, bpm: grid.bpm, beat0: grid.t0 + grid.d * grid.T, T: grid.T, nBars: grid.nBars, key: keyInfo, progression, mode: MODE, banks }, tr);
}

onmessage = (e) => {
  const m = e.data;
  try {
    if (m.type === 'analyze') {
      SR = m.sr; MODE = m.mode; src = m.src; LEN = m.len; cappedSec = m.cappedSec || 0;
      fps = SR / HOP; F = Math.ceil(LEN / HOP) + 1; setupBins();
      progress(0.02, 'Preparazione…');
      if (MODE === 'sep') { pass1(); buildMasks(); smoothMasks(); vocalGate(); }
      pass2();
      if (MODE === 'sep') { mag = null; cw = null; }
      progress(0.75, 'Tempo e tonalità…');
      detectKey(); makeGrid(0);
      sliceAll();
    } else if (m.type === 'reslice') {
      progress(0.75, 'Ricalcolo la griglia…'); makeGrid(m.bpm); sliceAll();
    } else if (m.type === 'synth') {
      const a = synth(m.name, 0, LEN) || { L: new Float32Array(1), R: new Float32Array(1) };
      postMessage({ type: 'synth', id: m.id, L: a.L, R: a.R }, [a.L.buffer, a.R.buffer]);
    }
  } catch (err) { postMessage({ type: 'error', msg: String(err && err.stack || err) }); }
};
