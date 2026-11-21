// Scrive i brani di prova del banco (stem veri noti) come float32 grezzi: mix_L, mix_R, drums_L ... other_R
import fs from 'fs'; import path from 'path';
import { genSong } from '../../sample-forge/tests/gen.mjs';
const out = process.argv[2] || '/tmp/sfdata';
const songs = { song1: genSong(), song2: genSong(44100, 7, { bpm: 128, padPan: 0.3, vocRev: 0.6, bassCut: 1200, kickDec: 12 }) };
for (const [name, s] of Object.entries(songs)) {
  const dir = path.join(out, name); fs.mkdirSync(dir, { recursive: true });
  const w = (k, a) => fs.writeFileSync(path.join(dir, k + '.f32'), Buffer.from(a.buffer, a.byteOffset, a.byteLength));
  w('mix_L', s.mix.L); w('mix_R', s.mix.R);
  for (const k of ['drums', 'vocals', 'bass', 'other']) { w(k + '_L', s.stems[k].L); w(k + '_R', s.stems[k].R); }
  fs.writeFileSync(path.join(dir, 'info.txt'), `${s.sr} ${s.n}\n`);
  console.log(name, s.n);
}
