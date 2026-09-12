#!/usr/bin/env python3
"""Install this build with a timestamped, file-level rollback backup.
Usage: python scripts/install-fork.py GAME_DIR [--dry-run]
Rollback: python scripts/install-fork.py GAME_DIR --rollback BACKUP_DIR
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil


def game_is_running():
    for path in Path('/proc').glob('[0-9]*/comm'):
        try:
            if 'forzahorizon6' in path.read_text().lower():
                return True
        except OSError:
            pass
    return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('game', type=Path)
    parser.add_argument('--dry-run', action='store_true')
    parser.add_argument('--rollback', type=Path)
    args = parser.parse_args()
    game = args.game.resolve()
    if not (game / 'forzahorizon6.exe').is_file():
        raise SystemExit('Forza Horizon 6 executable not found')
    if game_is_running():
        raise SystemExit('Close FH6 before installing or rolling back')
    if args.rollback:
        backup = args.rollback.resolve()
        manifest = json.loads((backup / 'manifest.json').read_text())
        if manifest['game'] != str(game):
            raise SystemExit('Backup belongs to another installation')
        for item in manifest['files']:
            relative = Path(item['path'])
            if relative.is_absolute() or '..' in relative.parts:
                raise SystemExit('Invalid backup path')
            dest = game / relative
            if item['existed']:
                shutil.copy2(backup / relative, dest)
            elif dest.exists():
                dest.unlink()
        print('Restored files from', backup)
        return
    root = Path(__file__).resolve().parents[1]
    dist = root / 'dist'
    files = {p.relative_to(dist): p for p in dist.rglob('*') if p.is_file()}
    # Keep the installed config byte-for-byte; new fields use their runtime defaults.
    files.pop(Path('README.txt'), None)
    config_rel = Path('fh6-radio/config.toml')
    existing_config = game / config_rel
    config_text = (existing_config if existing_config.exists() else files[config_rel]).read_text()
    if not (dist / 'version.dll').exists() or not (dist / 'fh6-radio/bin/qjs.exe').exists():
        raise SystemExit('Build and stage the runtime helpers first')
    if args.dry_run:
        print(f'Will install {len(files)} files, preserve existing settings')
        return
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    backup = game / 'fh6-radio' / 'backups' / stamp
    backup.mkdir(parents=True, exist_ok=False)
    manifest = {'game': str(game), 'files': []}
    # Complete the backup before the first overwrite.
    for rel in sorted(files):
        dest = game / rel
        manifest['files'].append({'path': str(rel), 'existed': dest.exists()})
        if dest.exists():
            out = backup / rel
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(dest, out)
    (backup / 'manifest.json').write_text(json.dumps(manifest, indent=2))
    for rel, src in files.items():
        dest = game / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        if rel == config_rel:
            dest.write_text(config_text)
        else:
            shutil.copy2(src, dest)
    print('Installed. Rollback backup:', backup)

if __name__ == '__main__':
    main()
