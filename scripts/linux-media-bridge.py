#!/usr/bin/env python3
"""Optional loopback-only Spotify seek bridge for Wine/Proton (python-dbus).

Run alongside the game. Only controls Spotify; other players are untouched.
The custom header prevents web pages from invoking actions through CORS.
"""
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
import dbus


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


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.headers.get('X-FH6-Media') != '1' or self.headers.get('Origin'):
            self.send_error(403)
            return
        if self.path not in ('/spotify/restart', '/status'):
            self.send_error(404)
            return
        try:
            ok = self.path == '/status' or restart_spotify(dbus.SessionBus())
        except dbus.DBusException:
            ok = False
        payload = json.dumps({'ok': bool(ok)}).encode()
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
