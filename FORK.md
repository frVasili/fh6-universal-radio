# FH6 Universal Radio — personal fork 1.1.10-p5

Based on [g0ldyy/fh6-universal-radio](https://github.com/g0ldyy/fh6-universal-radio), GPLv3.
Original credits and dashboard links are retained. This is a local fork, not an upstream release.

## p5 startup safety fix

- Constructs the persistent artwork worker on the bridge thread before DX12 hooks are installed. This avoids starting the worker for the first time from a game render submission during the splash-to-world loading transition.

## p4 playback performance changes

- Artwork preparation now uses one joinable worker with latest-request cancellation and a bounded encoded-payload cache. Cached covers can be republished after the game recreates its target texture without changing the displayed metadata.
- Discovery cache expiry and heap rescans use elapsed time rather than call counts, so the 20 ms control loop cannot invalidate or rescan rapidly. Informational logs are buffered; warnings and errors still flush promptly.
- The low-gain DSP path skips clipping arithmetic when the configured gain cannot reach the clipper knee. The existing smart race policy, Spotify metadata ownership, YouTube runtime, Jellyfin links, and helper service are preserved.

The D3D12 queue/resource redesign and asynchronous transport queue remain deferred: the current tests do not validate FH6 texture ownership across queues or raw-source lifetime across source switches.

## Earlier p3 graphics and Spotify changes

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
- In-game GPU behavior and frame pacing have not yet been measured. p3 moves completion off the render thread while preserving the p2 idle bypass. An actual Spotify driving test is still needed to confirm perceived stutter is improved.
- YouTube login/age/region restrictions still apply. This does not bypass them, and a public-video test cannot guarantee every playlist works.
