# Launching, stopping and steering OBS from the command line

Source: `frontend/obs-main.cpp` (argument parsing), `frontend/OBSApp.cpp` (crash handling),
`plugins/obs-websocket/src/Config.cpp` (websocket flags). `scripts/obsws.py launch|quit|probe` wrap this.

## Contents

- [Binary locations](#binary-locations)
- [Command line flags](#command-line-flags)
- [Startup sequence and what can block it](#startup-sequence-and-what-can-block-it)
- [Stopping OBS](#stopping-obs)
- [Headless and CI notes](#headless-and-ci-notes)
- [Version notes](#version-notes)

## Binary locations

| platform | binary | notes |
| --- | --- | --- |
| macOS | `/Applications/OBS.app/Contents/MacOS/OBS` (may be a symlink to another volume) | run the binary directly for arguments; `open -na OBS --args ...` also works but only for a new instance. Version: `Info.plist CFBundleShortVersionString` |
| Windows | `C:\Program Files\obs-studio\bin\64bit\obs64.exe` | **must** be started with the working directory set to `bin\64bit`, otherwise it cannot find its data |
| Linux | `obs` on PATH; Flatpak `flatpak run com.obsproject.Studio -- <args>`; Snap `snap run obs-studio` | `obs --version` prints `OBS Studio - 32.x.y` and exits |

Plugins live next to the binary: `OBS.app/Contents/PlugIns/obs-websocket.plugin`,
`obs-studio\obs-plugins\64bit\obs-websocket.dll`, `/usr/lib/x86_64-linux-gnu/obs-plugins/obs-websocket.so`
(or `/usr/lib/obs-plugins/`). The OBS log lists `Loaded Modules:` including `obs-websocket`.

## Command line flags

From `--help` (OBS 32):

| flag | effect |
| --- | --- |
| `--profile <name>` | start with this profile (display name; ignored if unknown) |
| `--collection <name>` | start with this scene collection |
| `--scene <name>` | start with this scene as program (and preview) |
| `--startstreaming` / `--startrecording` / `--startreplaybuffer` / `--startvirtualcam` | begin the output right after the collection loads (not in Safe Mode) |
| `--studio-mode` | enable studio mode |
| `--minimize-to-tray` | start minimised to the tray (still renders and streams) |
| `--portable`, `-p` | portable mode (config next to the binary; Windows/Linux builds that allow it) |
| `--multi`, `-m` | do not warn about a second running instance (also: crash sentinel not tied to this launch) |
| `--safe-mode` | disables third-party plugins, scripting and **the websocket server**; never use for automation |
| `--only-bundled-plugins` | load first-party plugins only |
| `--verbose`, `--unfiltered_log` | more logging |
| `--disable-updater` | no update check dialog (Windows/macOS) |
| `--disable-missing-files-check` | skip the "missing files" dialog for broken source paths |
| `--always-on-top`, `--allow-opengl`, `--steam` | misc |
| `--version`, `-V`, `--help`, `-h` | print and exit |
| `--websocket_port <n>` | obs-websocket: override port for this run |
| `--websocket_password <pw>` | override password and force auth on for this run |
| `--websocket_ipv4_only`, `--websocket_debug` | bind IPv4 only; debug log |

The websocket flags do not *enable* the server; `server_enabled` in `config.json` does.
Profile/collection names with spaces need normal shell quoting. `--scene` applies after the collection
is loaded and is cleared afterwards.

Automation defaults used by `obsws.py launch`: `--disable-updater --disable-missing-files-check` plus
whatever you pass (`--profile`, `--collection`, `--scene`, `--startstreaming`, `--minimize-to-tray`).

## Startup sequence and what can block it

1. Parse arguments, open the log, load `global.ini`/`user.ini`.
2. Crash check: if `.sentinel/run_*` files exist from a previous run, a **modal dialog** ("unclean
   shutdown", Safe Mode vs Normal) appears before anything else. Nothing responds until it is clicked.
   Remove stale sentinel files first (only when OBS is not running).
3. First run ever: the auto-configuration wizard may open (`user.ini [General] FirstRun`); skipping it
   is one click. Pre-seeding `user.ini` with `FirstRun=true` avoids it.
4. Load modules (websocket server starts listening here but answers 207 NotReady), profile
   (`--profile` or `[Basic] Profile`), then the scene collection; `--scene` and `--startstreaming` apply
   after the collection loaded. `==== Startup complete ====` appears in the log, then requests work.
5. Other dialogs that can appear later and block websocket requests that need the UI thread: missing
   files (disabled by the flag), update available (disabled by the flag), macOS permission prompts for
   camera/microphone/screen recording (first use of such a source), "another instance is running"
   (use `--multi` or quit the other instance), the exit confirmation while an output is active.

Typical readiness time on a laptop: 3-8 s. Poll the handshake plus `GetVersion` rather than sleeping.

## Stopping OBS

- Graceful: macOS `osascript -e 'tell application id "com.obsproject.obs-studio" to quit'`; Windows
  `taskkill /IM obs64.exe` (WM_CLOSE); Linux `kill -TERM <pid>` (OBS installs SIGINT/SIGTERM handlers
  that run the normal shutdown). Graceful exit saves the collection, removes the sentinel and writes
  `==== Shutting down ====` to the log.
- `user.ini [General] ConfirmOnExit=true` only prompts while streaming/recording, so stop outputs first
  (`StopStream`, `StopRecord`) and wait for `outputActive=false`.
- `kill -9` leaves the sentinel and shows the crash dialog next time; clear the sentinel or accept it.
- There is no websocket request to quit OBS. `ExitStarted` is emitted when a shutdown begins.

## Headless and CI notes

- OBS needs a display/GPU context: macOS and Windows sessions must be logged in (no SSH-only start of a
  GUI app on macOS without a user session); Linux works under Xvfb or a virtual Wayland session with
  software OpenGL (`LIBGL_ALWAYS_SOFTWARE=1`, `QT_QPA_PLATFORM=xcb`). `--minimize-to-tray` hides the window.
- Several OBS processes on one machine share one config dir (`user.ini` current profile/collection,
  websocket `config.json`, sentinels, logs) and fight over it. OBS has no config-dir flag; it asks the OS
  (`os_get_config_path`: `NSSearchPathForDirectoriesInDomains` on macOS, `$XDG_CONFIG_HOME` on Linux,
  `SHGetFolderPath` on Windows), and `--portable` only moves it next to the binary. Setting
  `CFFIXED_USER_HOME=<dir>` (macOS) or `XDG_CONFIG_HOME=<dir>/.config` (Linux) per process gives each one a
  private config dir; `obsws.py launch --instance ID` does that plus `--multi` (on macOS
  `CheckIfAlreadyRunning` counts `NSRunningApplication`s by bundle id, so every extra process would
  otherwise open the "already running" dialog). On Windows use one portable OBS copy per test.
- Pre-seed config files before the first launch (see `config-files.md`): websocket `config.json` with
  a fixed password, a profile with the SRS destination, and `--startstreaming` for wizard-free runs.
- Screenshots for visual checks: `SaveSourceScreenshot` (works minimised) via `obsws.py screenshot`.
- Logs are the best diagnostic; copy the newest `logs/*.txt` into test artifacts.

## Version notes

- OBS 28+: obs-websocket 5 bundled (protocol v5, port 4455). OBS 27: obs-websocket 4.9 (protocol v4,
  port 4444, `request-type` messages) unless the 5.x plugin was installed separately.
- OBS 31+: `user.ini` split from `global.ini`; profiles/collections matched by display name via caches.
- obs-websocket 5.6 (OBS 32.0): `canvasUuid` fields on scene/scene-item requests; 5.7 (OBS 32.2): `GetCanvasList`.
- OBS 30+: WHIP output (`obs-webrtc`) and `whip_custom` service; SRT/RIST via `rtmp_custom` since 28.
