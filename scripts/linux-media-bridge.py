#!/usr/bin/env python3
"""Optional loopback-only Spotify seek bridge for Wine/Proton (python-dbus).

Run alongside the game. Only controls Spotify; other players are untouched.
The custom header prevents web pages from invoking actions through CORS.
"""
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import dbus
from urllib.parse import urlsplit, parse_qs


def restart_spotify(bus):
    obj = bus.get_object('org.mpris.MediaPlayer2.spotify', '/org/mpris/MediaPlayer2', introspect=False)
    props = dbus.Interface(obj, 'org.freedesktop.DBus.Properties')
    player = 'org.mpris.MediaPlayer2.Player'
    if not props.Get(player, 'CanSeek', timeout=1):
        return False
    metadata = props.Get(player, 'Metadata', timeout=1)
    track_id = metadata.get('mpris:trackid')
    if not track_id or str(track_id).endswith('/NoTrack'):
        return False
    dbus.Interface(obj, player).SetPosition(dbus.ObjectPath(track_id), dbus.Int64(0), timeout=2)
    return True


def smart_skip_spotify(bus, seconds):
    if not 1 <= seconds <= 59:
        raise ValueError("Song restart must be between 1 and 59 seconds")
    obj = bus.get_object('org.mpris.MediaPlayer2.spotify', '/org/mpris/MediaPlayer2', introspect=False)
    props = dbus.Interface(obj, 'org.freedesktop.DBus.Properties')
    player_name = 'org.mpris.MediaPlayer2.Player'
    # Query at execution time so scrubbing/reconnecting cannot leave the
    # decision dependent on the DLL's buffered-PCM position estimate.
    position = int(props.Get(player_name, 'Position', timeout=1))
    if position < 0:
        return {'ok': False, 'action': 'unknown_position'}
    if position <= seconds * 1000000:
        ok = restart_spotify(bus)
        return {'ok': ok, 'action': 'restart', 'position_ms': position // 1000}
    if not props.Get(player_name, 'CanGoNext', timeout=1):
        return {'ok': False, 'action': 'next_unavailable'}
    dbus.Interface(obj, player_name).Next(timeout=2)
    return {'ok': True, 'action': 'next', 'position_ms': position // 1000}


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.headers.get('X-FH6-Media') != '1' or self.headers.get('Origin'):
            self.send_error(403)
            return
        url = urlsplit(self.path)
        if url.path not in ('/spotify/restart', '/spotify/smart', '/status'):
            self.send_error(404)
            return
        try:
            if url.path == '/spotify/smart':
                seconds = int(parse_qs(url.query).get('seconds', ['30'])[0])
                result = smart_skip_spotify(dbus.SessionBus(), seconds)
            else:
                result = {'ok': url.path == '/status' or restart_spotify(dbus.SessionBus())}
        except (ValueError, dbus.DBusException):
            result = {'ok': False}
        ok = result['ok']
        payload = json.dumps(result).encode()
        self.send_response(200 if ok else 503)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *_):
        pass


if __name__ == '__main__':
    HTTPServer(('127.0.0.1', 8421), Handler).serve_forever()
