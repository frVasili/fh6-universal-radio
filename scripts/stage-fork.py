#!/usr/bin/env python3
"""After build.sh, stage official yt-dlp.exe and qjs.exe from a supplied folder."""
from pathlib import Path
import hashlib
import shutil
import sys
root = Path(__file__).resolve().parents[1]
cache = Path(sys.argv[1])
dist = root / 'dist'
if not (dist / 'version.dll').is_file(): raise SystemExit('Run scripts/build.sh first')
qjs_sha = '7b27412de844403545bd151fbe49191b4d5b91a9e15b5db7c863fea54639a82b'
if hashlib.sha256((cache / 'qjs.exe').read_bytes()).hexdigest() != qjs_sha:
    raise SystemExit('QuickJS must be the official v0.16.2 Windows x86_64 executable')
(dest := dist / 'fh6-radio/bin').mkdir(parents=True, exist_ok=True)
for name in ['yt-dlp.exe', 'qjs.exe']:
    shutil.copy2(cache / name, dest / name)
shutil.copy2(root / 'scripts/linux-media-bridge.py', dist / 'fh6-radio/linux-media-bridge.py')
shutil.copy2(root / 'LICENSE', dist / 'LICENSE')
shutil.copy2(root / 'FORK.md', dist / 'FORK.md')
(dist / 'fh6-radio/FORK-VERSION.txt').write_text('FH6 Universal Radio personal fork 1.1.10-p4\n')
manifest = ''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.relative_to(dist)}\n'
                   for p in sorted(dist.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS')
(dist / 'SHA256SUMS').write_text(manifest)
