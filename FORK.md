# FH6 Universal Radio — personal fork 1.1.10-p6-smart

## September 21 race-start dispatch correction

- Replaced the blanket 45-second race-action cooldown with 250 ms of stable race-state sampling. A valid quick race start now reaches Smart Skip rather than silently keeping the current position. Hotkey cooldowns no longer block race actions.
- Retain the previous known activity across unreadable samples so a new start after a temporary read failure is not swallowed. Repeated activity and brief glitches do not cause extra starts; the erroneous finish/restart signal remains unused.
- Smart action logs explicitly distinguish restart, next, and Spotify's provider position check. The 59-second saved threshold is unchanged.
- Race-transition, transport queue, and threshold regression tests pass; Windows DLL builds. Live in-game trigger timing still requires a play test.

## Smart skip timestamp and restart correction

- Capture the source's current playback position when the race command is queued, before clearing any PCM. Jellyfin uses consumed PCM (decoded bytes minus queued bytes), so buffered-ahead audio does not count as elapsed playback. Spotify on Proton retains its authoritative desktop-player position query at command execution.
- At or below the configured threshold, restart the current song; above it, advance. Failed restarts preserve the current song rather than falling back to Next.
- While transport changes the track/position, the pump returns without refilling old PCM. Clear the old ring only after a successful action, then resume pumping, so stale audio cannot conceal a restart.
- Queue tests cover delayed execution crossing the threshold, failed restart, and audio buffer cleanup; threshold boundary and race transition tests pass.

## Race finish skip correction

- Removed the `radio_state + 0x80 == -1` restart heuristic. A read-only capture during the September 16 play session showed that field changing from 3 to -1 at race completion while both race activity bytes stayed 1; the old code dispatched a second smart skip.
- Race actions now use only the existing race activity rising edge, retaining the 45-second start debounce and the configured smart restart threshold. The former independent five-second restart shortcut is removed.
- Missing race-state reads establish a fresh baseline on recovery, rather than fabricating a race end/start pair.
- Regression coverage checks start, repeated active samples, the captured finish transition, another race, and missing reads. Windows DLL build passed. Installation and a fresh in-game play test are separate steps.

## Jellyfin album covers

- Uses album or inherited primary artwork when a song has no individual cover, preserving track-specific covers when present.
- Normalizes server URLs and loads dashboard Jellyfin covers directly, including private servers without image CORS support.
- Verified with artwork metadata regression tests and a Windows DLL cross-build; in-game display still needs a play test.

## p6-smart

- Smart skip with a saved 1–59 second Song restart slider (default 30).
- Linux Spotify decisions use the desktop player’s current position and target Spotify directly. The helper remains managed by the user’s Steam launch wrapper.
- Transport operations no longer hold the PCM pump’s registry mutex during external I/O; source replacement remains serialized. Source-internal locks can still delay pumping for other providers.
- In-game race transitions still require a user retest.

## p5-safe scheduling cleanup

- Discovery invalidation and heap-scan retries are elapsed-time based, preventing rapid retries from the 20 ms control loop.
- Logging is bounded and asynchronous; file I/O and timestamp formatting run on the logger thread, while warnings and errors still flush promptly.
- At gain values at or below 0.85, the DSP uses direct S16 scaling because clipping cannot occur.
- Artwork preparation uses one persistent worker, a four-entry/eight-megabyte encoded BC7 cache, latest-request replacement, cancellation checks, and target-texture generations. Cached covers are republished after texture recreation.
- Race and hotkey transport commands run through a bounded ordered manager queue. Requests carry the source name and active generation, so a late command cannot operate on a replacement source. The source remains manager-owned while a command runs.

The unsafe D3D12 queue/resource redesign remains deferred: the hook still uses the validated existing graphics path, because the available headless test does not prove FH6 texture state or multi-queue ownership.

Based on [g0ldyy/fh6-universal-radio](https://github.com/g0ldyy/fh6-universal-radio), GPLv3.
Original credits and dashboard links are retained. This is a local fork, not an upstream release.

## Earlier p3 Spotify and render changes

- Normal frames retain p2's atomic early exit. No artwork queue lock, GPU fence polling, resource allocation, or worker wakeup is added to ordinary render submissions.
- Cover-change GPU waits now run on one sleeping background worker, initialized before the graphics hooks. An atomic reservation permits only one upload at a time. Upload/command/allocator/texture resources are held until the GPU fence completes or the device is removed. A stuck GPU occupies one slot and later covers wait; in-flight resources are not freed on a timeout.
- This replaces p1's per-render-submission mutex/polling design and p2's synchronous cover-upload wait. A GPU copy is still needed to display art; submission and resource creation can still have a small cost.
- Spotify metadata parsing consumes each log chunk in one pass rather than repeatedly moving the remaining text for each line. Raw debug chatter no longer causes disk writes on the audio loop; warnings and errors remain.
- A headless real-D3D12 test under Wine deliberately held the GPU copy behind a fence. Background dispatch returned in approximately 0.001 ms while resources remained alive, then readback matched after completion. This tests lifetime and synchronization, not game frame times. The portable worker test also checks reservation and resource release. Spotify preload/actual-load tests continue to pass.
- Reference: [Microsoft fence-based resource management](https://learn.microsoft.com/en-us/windows/win32/direct3d12/fence-based-resource-management).

## Earlier changes

- Album artwork uses the vendored CPU BC7 encoder on its background thread. It no longer launches texconv from the game's process or creates a separate GPU encoding workload.
- After a reported frame-pacing regression, p2 restores upstream GPU upload synchronization. The p1 asynchronous upload queue and per-submission mutex are removed. Normal render submissions now bypass artwork processing entirely when no cover or texture discovery is pending (two atomic checks, no artwork mutex/COM calls/resource scans). CPU conversion remains. A small GPU copy is still necessary to display each cover; initial texture discovery and cover uploads retain upstream synchronization.
- Reuses the most recently completed cover when its URL and dimensions match, and cancels stale conversion work after skips. Image memory is released on early exits.
- YouTube uses an explicitly selected managed QuickJS-NG runtime, logs extractor warnings, and has bounded network retries. This installation also updates yt-dlp. An empty playlist result from a live worker no longer triggers a duplicate in-game process spawn.
- Jellyfin accepts raw UUIDs, complete web links, and `details?id=…&serverId=…` fragments, including existing saved stations. It uses the playlist items API and trims the server URL. Empty casts fail rather than pretending to play. Current-track restart is implemented.
- New **smart** race-start mode: restart at 0–30 seconds inclusive; advance after 30 seconds. If a source cannot restart, keep the recent song instead of skipping it. Existing Next/Restart/Ignore/Off options remain.
- Spotify restart performs a real seek instead of sending Previous. On Proton it uses the included loopback-only Python/MPRIS helper and targets the Spotify desktop app. It does not require account credentials. If Spotify is unavailable or cannot seek, playback is left alone.

## p2 regression correction

The first in-game p1 test was reported to stutter more frequently. The p2 render changes above reduce normal-frame overhead; their real game effect still needs an A/B test. Logs also confirmed Spotify preload metadata could display about 29 seconds early. p2 caches decoded title/artwork by track identity and promotes it only on the actual `command=Load` event, rather than on a duration estimate. Tests replay preload, actual load, an abandoned preload, and a restart. Decoder/audio-pipe latency can still produce a small subsecond offset.

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
- In-game GPU behavior and frame pacing have not yet been measured. The new p5-safe changes are unit/build verified but still require an actual Spotify driving comparison to confirm perceived stutter is improved.
- YouTube login/age/region restrictions still apply. This does not bypass them, and a public-video test cannot guarantee every playlist works.

### September 22: consecutive online race detection (p7)

Live capture reproduced the missed Smart Skip: radio activity bytes +0x68/+0x69
remained 1 at race finish and at the next online race start. The phase at +0x80
changed from 3 to -1 at finish, then back to 3 at the next start. The old
activity-only detector missed that start entirely; Spotify transport was not
called. Treat the -1 phase as an end/rearm signal, never as a start/restart
request. Keep stable-state filtering and emit diagnostic samples on state
changes. Replay of the captured sequence now produces exactly one end and one
subsequent start. Spotify threshold and transport behavior are unchanged.
