import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('installer', Path(__file__).parents[1] / 'scripts/install-fork.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)

class ConfigTest(unittest.TestCase):
    def test_install_preserves_config_and_backs_up_previous_dll(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            game, dist = root / 'game', root / 'dist'
            (game / 'fh6-radio').mkdir(parents=True)
            (dist / 'fh6-radio/bin').mkdir(parents=True)
            (game / 'forzahorizon6.exe').touch()
            (game / 'version.dll').write_bytes(b'old')
            (dist / 'version.dll').write_bytes(b'new')
            (dist / 'fh6-radio/bin/qjs.exe').touch()
            (dist / 'fh6-radio/config.toml').write_text('[playback]\nrace_start_playback="smart"\n')
            before = '[jellyfin]\napi_key="example"\n[playback]\nrace_start_playback="ignore" # preserve\nsong_restart_seconds=17\n'
            config = game / 'fh6-radio/config.toml'
            config.write_text(before)
            with patch.object(installer, '__file__', str(root / 'scripts/install-fork.py')), patch.object(installer, 'game_is_running', return_value=False), patch('sys.argv', ['install-fork.py', str(game)]):
                installer.main()
            self.assertEqual(config.read_text(), before)
            self.assertEqual((game / 'version.dll').read_bytes(), b'new')
            backup = next((game / 'fh6-radio/backups').iterdir())
            self.assertEqual((backup / 'version.dll').read_bytes(), b'old')
            self.assertEqual((backup / 'fh6-radio/config.toml').read_text(), before)

if __name__ == '__main__': unittest.main()
