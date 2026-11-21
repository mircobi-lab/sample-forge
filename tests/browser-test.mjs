// Prova completa nel browser: brano di prova sintetico (112 BPM, La minore, Am–F–C–G),
// analisi, pad, linea di riproduzione ed export dello zip. Uso: node tests/browser-test.mjs
import { chromium } from 'playwright';
import path from 'path';
import fs from 'fs';

const page_ = path.resolve('dist/sample-forge.html');
const b = await chromium.launch({ args: ['--autoplay-policy=no-user-gesture-required'] });
const p = await b.newPage({ viewport: { width: 1200, height: 1500 }, acceptDownloads: true });
const errs = []; p.on('pageerror', e => errs.push(e.message));
await p.goto('file://' + page_);
await p.click('#bDemo');
await p.waitForFunction(() => window.__SF_ready, null, { timeout: 240000 });
const r = await p.evaluate(() => { const r = __SF.S.res; return { bpm: r.bpm, key: r.key.name, prog: r.progression.join(' '), pads: r.banks.reduce((s, b) => s + b.pads.filter(Boolean).length, 0) }; });
console.log(JSON.stringify(r));
await p.keyboard.press('Digit3'); await p.keyboard.press('KeyA'); await p.waitForTimeout(600);
const ph = await p.evaluate(() => document.querySelector('.pad .ph').style.display);
await p.keyboard.press('Space');
fs.mkdirSync('dist', { recursive: true });
await p.screenshot({ path: 'dist/screenshot.png', fullPage: true });
const dl = p.waitForEvent('download', { timeout: 120000 });
await p.click('#bExport');
const d = await dl; await d.saveAs('dist/pack-di-prova.zip');
await b.close();
const fail = [];
if (Math.abs(r.bpm - 112) > 0.5) fail.push('BPM ' + r.bpm);
if (r.key !== 'Am') fail.push('tonalità ' + r.key);
if (!/^Am F C G/.test(r.prog)) fail.push('accordi ' + r.prog.slice(0, 20));
if (r.pads < 30) fail.push('pad ' + r.pads);
if (ph !== 'block') fail.push('linea di riproduzione assente');
if (errs.length) fail.push('errori JS: ' + errs.join(' | '));
console.log(fail.length ? 'FALLITO: ' + fail.join('; ') : 'OK: ' + d.suggestedFilename());
process.exit(fail.length ? 1 : 0);
