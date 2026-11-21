#!/usr/bin/env python3
"""Costruisce Sample Forge in dist/:
   - sample-forge.html      app completa in un solo file (funziona offline)
   - sample-pack-app.html   la stessa app col nome "Sample Pack Creator", per il sito MBStudio91
   - SampleForge-Mac.zip    app per Mac (finestra dedicata su Chrome/Edge/Brave, altrimenti browser predefinito)
Uso: python3 build.py"""
import pathlib, zipfile, stat, re

ROOT = pathlib.Path(__file__).resolve().parent
SRC, MAC, DIST = ROOT / 'src', ROOT / 'mac', ROOT / 'dist'


def build_app():
    t = (SRC / 'template.html').read_text(encoding='utf-8')
    w = (SRC / 'worker.js').read_text(encoding='utf-8')
    assert '</script' not in w, 'il worker non può contenere </script'
    assert t.count('/*WORKER*/') == 1
    return t.replace('/*WORKER*/', w)


def site_variant(s):
    """Versione per il sito: stesso programma, nome Sample Pack Creator, avvisi MVSep adatti a una pagina online."""
    def rep(a, b, n=1):
        nonlocal s
        assert s.count(a) == n, (a, s.count(a))
        s = s.replace(a, b)
    rep('<title>Sample Forge · MBStudio91</title>', '<title>Sample Pack Creator · MBStudio91</title>')
    rep('<span class="mbs-badge">Sample Forge</span>', '<span class="mbs-badge">Sample Pack Creator</span>')
    rep('<span>MBStudio91 · Sample Forge · uso personale</span>', '<span>MBStudio91 · Sample Pack Creator · uso personale</span>')
    rep("'MBStudio91_SampleForge_'", "'MBStudio91_SamplePack_'", 2)
    rep('`SAMPLE FORGE · MBStudio91`', '`SAMPLE PACK CREATOR · MBStudio91`')
    rep("Serve internet; funziona nell'app per Mac.", "Serve internet e che MVSep accetti richieste da questa pagina; altrimenti usa l'app per Mac.")
    rep("in questa versione online le connessioni verso altri siti sono bloccate, quindi MVSep automatico funziona solo nell\\u2019app per Mac. ",
        "se compare \\u201cnon riesco a collegarmi\\u201d, MVSep non accetta richieste da questa pagina: usa l\\u2019app per Mac oppure carica gli stem a mano. ")
    return s


def mac_zip(html):
    version = re.search(r'<key>CFBundleShortVersionString</key><string>([^<]+)', (MAC / 'Info.plist').read_text()).group(1)
    out = DIST / 'SampleForge-Mac.zip'
    base = 'Sample Forge.app/Contents/'
    def add(z, name, data, mode=0o644):
        info = zipfile.ZipInfo(base + name, date_time=(2026, 1, 1, 0, 0, 0))
        info.external_attr = (stat.S_IFREG | mode) << 16
        info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, data)
    with zipfile.ZipFile(out, 'w') as z:
        add(z, 'Info.plist', (MAC / 'Info.plist').read_bytes())
        add(z, 'MacOS/SampleForge', (MAC / 'SampleForge-launcher.sh').read_bytes(), 0o755)
        add(z, 'Resources/AppIcon.icns', (MAC / 'AppIcon.icns').read_bytes())
        add(z, 'Resources/index.html', html.encode('utf-8'))
    return out, version


if __name__ == '__main__':
    DIST.mkdir(exist_ok=True)
    app = build_app()
    (DIST / 'sample-forge.html').write_text(app, encoding='utf-8')
    (DIST / 'sample-pack-app.html').write_text(site_variant(app), encoding='utf-8')
    z, v = mac_zip(app)
    for f in sorted(DIST.iterdir()):
        print(f'{f.name:28s} {f.stat().st_size:>9,d} byte')
    print('versione app Mac:', v)
