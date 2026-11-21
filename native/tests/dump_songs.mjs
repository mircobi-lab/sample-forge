// Scrive i brani di prova del banco (stem veri noti) come float32 grezzi (mix_L ... other_R) e mix.wav 16 bit.
// Uso: node tests/dump_songs.mjs <cartella di uscita>
import fs from 'fs'; import path from 'path';
import { genSong } from '../../tests/gen.mjs';
const out = process.argv[2] || 'testdata';
const songs = { song1: genSong(), song2: genSong(44100, 7, { bpm: 128, padPan: 0.3, vocRev: 0.6, bassCut: 1200, kickDec: 12 }) };
for (const [name, s] of Object.entries(songs)) {
  const dir = path.join(out, name); fs.mkdirSync(dir, { recursive: true });
  const w = (k, a) => fs.writeFileSync(path.join(dir, k + '.f32'), Buffer.from(a.buffer, a.byteOffset, a.byteLength));
  w('mix_L', s.mix.L); w('mix_R', s.mix.R);
  for (const k of ['drums', 'vocals', 'bass', 'other']) { w(k + '_L', s.stems[k].L); w(k + '_R', s.stems[k].R); }
  fs.writeFileSync(path.join(dir, 'info.txt'), `${s.sr} ${s.n}\n`);
  const n = s.n, buf = Buffer.alloc(44 + n * 4);
  buf.write('RIFF', 0); buf.writeUInt32LE(36 + n * 4, 4); buf.write('WAVEfmt ', 8); buf.writeUInt32LE(16, 16); buf.writeUInt16LE(1, 20); buf.writeUInt16LE(2, 22);
  buf.writeUInt32LE(s.sr, 24); buf.writeUInt32LE(s.sr * 4, 28); buf.writeUInt16LE(4, 32); buf.writeUInt16LE(16, 34); buf.write('data', 36); buf.writeUInt32LE(n * 4, 40);
  for (let i = 0; i < n; i++) for (const [c, a] of [[0, s.mix.L], [1, s.mix.R]]) buf.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(a[i] * 32767))), 44 + i * 4 + c * 2);
  fs.writeFileSync(path.join(dir, 'mix.wav'), buf);
  console.log(name, n, 'campioni');
}
