#!/usr/bin/env bash
set -euo pipefail
game_dir=${1:?Usage: install-linux-media-service.sh GAME_DIR}
helper=$(realpath "$game_dir/fh6-radio/linux-media-bridge.py")
[ -f "$helper" ] || { echo 'Install the fork first.' >&2; exit 1; }
python3 -c 'import dbus'
unit_dir="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
mkdir -p "$unit_dir"
unit="$unit_dir/fh6-radio-media.service"
[ ! -e "$unit" ] || cp -p "$unit" "$unit.bak"
# Quote the path for systemd, including literal percent specifiers.
helper_escaped=${helper//\\/\\\\}
helper_escaped=${helper_escaped//\"/\\\"}
helper_escaped=${helper_escaped//%/%%}
cat > "$unit" <<UNIT
[Unit]
Description=FH6 Spotify seek support for Proton
After=graphical-session.target
PartOf=graphical-session.target

[Service]
Type=simple
ExecStart=/usr/bin/python3 "$helper_escaped"
Restart=on-failure
RestartSec=3
NoNewPrivileges=true

[Install]
WantedBy=default.target
UNIT
systemctl --user daemon-reload
systemctl --user enable --now fh6-radio-media.service
