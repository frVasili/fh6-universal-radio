# FH6 Universal Radio — personal fork 1.1.10-p1

Based on [g0ldyy/fh6-universal-radio](https://github.com/g0ldyy/fh6-universal-radio), GPLv3.
Original credits and dashboard links are retained. This is a local fork, not an upstream release.

## Changes

- Album artwork uses the vendored CPU BC7 encoder on its background thread. It no longer launches texconv from the game's process or creates a separate GPU encoding workload.
- Artwork uploads no longer wait for a GPU fence on the rendering thread. Upload buffers, allocators, command lists, and destination textures stay alive until their fence completes. Pending uploads are capped at four; a stalled GPU defers covers rather than blocking rendering. Initial texture discovery still uses upstream readback logic.
- Reuses the most recently completed cover when its URL and dimensions match, and cancels stale conversion work after skips. Image memory is released on early exits.
- YouTube uses an explicitly selected managed QuickJS-NG runtime, logs extractor warnings, and has bounded network retries. This installation also updates yt-dlp. An empty playlist result from a live worker no longer triggers a duplicate in-game process spawn.
- Jellyfin accepts raw UUIDs, complete web links, and `details?id=…&serverId=…` fragments, including existing saved stations. It uses the playlist items API and trims the server URL. Empty casts fail rather than pretending to play. Current-track restart is implemented.
- New **smart** race-start mode: restart at 0–30 seconds inclusive; advance after 30 seconds. If a source cannot restart, keep the recent song instead of skipping it. Existing Next/Restart/Ignore/Off options remain.
- Spotify restart performs a real seek instead of sending Previous. On Proton it uses the included loopback-only Python/MPRIS helper and targets the Spotify desktop app. It does not require account credentials. If Spotify is unavailable or cannot seek, playback is left alone.

## Installed configuration

The installer preserves existing configuration and changes only `playback.race_start_playback` to `smart`. Your account data stays in the game folder; it is not included in this repository or release archive.

Spotify's Linux helper is a user service named `fh6-radio-media.service`, listening only on `127.0.0.1:8421`. It requires Python 3 and python-dbus, and an open Spotify desktop app. Requests need the `X-FH6-Media` header; browser Origin requests are refused. The normal dashboard remains on port 8420.

## Build and install

Use llvm-mingw (tested with 20260908), CMake, and the upstream dependency script:

```sh
bash scripts/get-deps.sh
PATH=/path/to/llvm-mingw/bin:$PATH JOBS=6 bash scripts/build.sh
python scripts/stage-fork.py /path/to/runtime-tools
python scripts/install-fork.py /path/to/ForzaHorizon6 --dry-run
python scripts/install-fork.py /path/to/ForzaHorizon6
bash scripts/install-linux-media-service.sh /path/to/ForzaHorizon6
```

The runtime folder must contain official `yt-dlp.exe` and QuickJS-NG v0.16.2 `qjs-windows-x86_64.exe` renamed `qjs.exe`:

- [yt-dlp releases](https://github.com/yt-dlp/yt-dlp/releases)
- [QuickJS-NG v0.16.2](https://github.com/quickjs-ng/quickjs/releases/tag/v0.16.2)
- [Why YouTube needs a JavaScript runtime](https://github.com/yt-dlp/yt-dlp/wiki/EJS)

The BC7 encoder is pinned to revision `b9438627eef73a1157e84201b6fa6eb2ffd6d9f0` of [bc7enc_rdo](https://github.com/richgel999/bc7enc_rdo); its license is included under `vendor/bc7enc`.

Close FH6 before installing or rolling back. The installer refuses to run while it sees the game running. It completes a backup of all overwritten files before replacing any of them.

## Rollback

The installation prints the backup directory inside `fh6-radio/backups`. To restore it:

```sh
systemctl --user disable --now fh6-radio-media.service
python scripts/install-fork.py /path/to/ForzaHorizon6 --rollback /path/to/backup
```

If removing the helper permanently, remove `~/.config/systemd/user/fh6-radio-media.service` and run `systemctl --user daemon-reload`. If a previous unit existed, the service installer saved it as `.service.bak`.

## Verification and limits

- Windows DLL and worker build successfully with llvm-mingw.
- Actual YouTube source → external worker → yt-dlp/ffmpeg → PCM ring tested under Wine with `https://www.youtube.com/watch?v=ERLhfP48iiw`; decoded nonzero audio before and after restart.
- Actual Jellyfin source with the user's existing saved link/authentication decoded nonzero audio before and after restarting the same track. Tests drain audio in memory, without speaker playback.
- Race policy tests cover 0, 29.999, 30.000, and 30.001 seconds. Playlist-link normalization and configuration preservation tests pass.
- CPU artwork tests cover all four game texture sizes, non-multiple-of-four dimensions, and cancellation. Independent Pillow DDS decoding validates cover colors and transparency. Synthetic image encoding took approximately 0.5–3.5 ms on this computer; this is not a game frame-time measurement.
- Linux helper tests check that restart calls SetPosition for Spotify, never Previous, and refuses unsupported/missing tracks. The installed service health and request guard were verified. Actual race-triggered Spotify behavior still needs an in-game check.
- New dashboard smart-mode save test passes. The broader upstream suite has pre-existing failures: untouched upstream reports 11 failed / 12 passed tests; this fork reports the same 11 failed / 13 passed tests, with the new test passing. Failures include stale layout expectations and uninitialized translations.
- In-game GPU behavior and frame pacing have not yet been measured. The two identified song-change stalls have been removed in code, but an actual driving test is still needed to confirm the perceived hitch is gone.
- YouTube login/age/region restrictions still apply. This does not bypass them, and a public-video test cannot guarantee every playlist works.
