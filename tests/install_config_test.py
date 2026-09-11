import importlib.util
from pathlib import Path
import tomllib
import unittest
spec = importlib.util.spec_from_file_location('installer', Path(__file__).parents[1] / 'scripts/install-fork.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)
class ConfigTest(unittest.TestCase):
    def test_preserves_other_settings_and_credentials(self):
        before = '[jellyfin]\napi_key="example"\n[playback]\nrace_start_playback="next" # old\nvolume_normalization=true\n[spotify]\nenabled=true\n'
        after = tomllib.loads(installer.smart_config(before))
        original = tomllib.loads(before)
        original['playback']['race_start_playback'] = 'smart'
        self.assertEqual(after, original)
    def test_adds_missing_setting_or_section(self):
        for before in ['[playback]\nvolume_normalization=false\n', '[spotify]\nenabled=true\n']:
            after = tomllib.loads(installer.smart_config(before))
            self.assertEqual(after['playback']['race_start_playback'], 'smart')
            self.assertEqual(installer.smart_config(installer.smart_config(before)), installer.smart_config(before))
if __name__ == '__main__': unittest.main()
