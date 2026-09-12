import importlib.util
from pathlib import Path
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location('bridge', Path(__file__).parents[1] / 'scripts/linux-media-bridge.py')
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)

class RestartTest(unittest.TestCase):
    def test_seeks_current_spotify_track_to_zero(self):
        bus, props, player = Mock(), Mock(), Mock()
        props.Get.side_effect = [True, {'mpris:trackid': '/org/spotify/track/123'}]
        with patch.object(bridge.dbus, 'Interface', side_effect=[props, player]):
            self.assertTrue(bridge.restart_spotify(bus))
        bus.get_object.assert_called_once_with('org.mpris.MediaPlayer2.spotify', '/org/mpris/MediaPlayer2', introspect=False)
        player.SetPosition.assert_called_once_with('/org/spotify/track/123', 0, timeout=2)
        player.Previous.assert_not_called()

    def test_no_seek_support_leaves_track_alone(self):
        props = Mock()
        props.Get.return_value = False
        with patch.object(bridge.dbus, 'Interface', return_value=props):
            self.assertFalse(bridge.restart_spotify(Mock()))
        props.SetPosition.assert_not_called()

    def test_missing_track_leaves_player_alone(self):
        props = Mock()
        props.Get.side_effect = [True, {}]
        with patch.object(bridge.dbus, 'Interface', return_value=props):
            self.assertFalse(bridge.restart_spotify(Mock()))
        props.SetPosition.assert_not_called()

class SmartSkipTest(unittest.TestCase):
    def test_restart_threshold_boundaries(self):
        for seconds in (1, 30, 59):
            for position in (0, seconds * 1000000, seconds * 1000000 + 1):
                with self.subTest(seconds=seconds, position=position):
                    props, player = Mock(), Mock()
                    props.Get.side_effect = [position, True]
                    with patch.object(bridge.dbus, 'Interface', side_effect=[props, player]), patch.object(bridge, 'restart_spotify', return_value=True) as restart:
                        result = bridge.smart_skip_spotify(Mock(), seconds)
                    self.assertTrue(result['ok'])
                    if position <= seconds * 1000000:
                        restart.assert_called_once()
                        player.Next.assert_not_called()
                    else:
                        restart.assert_not_called()
                        player.Next.assert_called_once_with(timeout=2)

    def test_unavailable_restart_does_not_skip(self):
        props = Mock()
        props.Get.return_value = 500000
        with patch.object(bridge.dbus, 'Interface', return_value=props), patch.object(bridge, 'restart_spotify', return_value=False):
            self.assertFalse(bridge.smart_skip_spotify(Mock(), 30)['ok'])
        props.Next.assert_not_called()

    def test_out_of_range_rejected(self):
        for seconds in (0, 60):
            with self.assertRaises(ValueError):
                bridge.smart_skip_spotify(Mock(), seconds)

if __name__ == '__main__': unittest.main()
