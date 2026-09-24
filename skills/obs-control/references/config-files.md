# OBS Studio configuration files (OBS 31/32)

Everything OBS remembers lives in plain INI and JSON files. OBS reads them at startup (and on
profile/collection switch) and rewrites the *current* profile and collection while running. Read this
when you need to prepare a machine before OBS starts, inspect what a websocket call changed, or debug
why a setting did not stick. Source: `frontend/OBSApp.cpp`, `frontend/widgets/OBSBasic_Profiles.cpp`,
`frontend/widgets/OBSBasic_SceneCollections.cpp`, `frontend/widgets/OBSBasic_Service.cpp`.

## Contents

- [Where the config directory is](#where-the-config-directory-is)
- [Layout](#layout)
- [global.ini and user.ini](#globalini-and-userini)
- [Profile: basic.ini](#profile-basicini)
- [Profile: service.json](#profile-servicejson)
- [Profile: streamEncoder.json and recordEncoder.json](#profile-streamencoderjson-and-recordencoderjson)
- [Scene collection JSON](#scene-collection-json)
- [obs-websocket config.json](#obs-websocket-configjson)
- [Crash sentinel and logs](#crash-sentinel-and-logs)
- [Editing files offline vs. using the websocket](#editing-files-offline-vs-using-the-websocket)

## Where the config directory is

| platform | path |
| --- | --- |
| macOS | `~/Library/Application Support/obs-studio` |
| Linux | `~/.config/obs-studio` (`$XDG_CONFIG_HOME/obs-studio`); Flatpak: `~/.var/app/com.obsproject.Studio/config/obs-studio` |
| Windows | `%APPDATA%\obs-studio` |
| portable mode (`--portable`, Windows/Linux builds with a `portable_mode` file) | `<install>/config/obs-studio` |

`global.ini [Locations]` can redirect `Configuration`, `SceneCollections`, `Profiles`,
`PluginManagerSettings` to another base dir; OBS appends `obs-studio/basic/{profiles,scenes}` to it.
`obsws.py probe` prints the resolved paths.

## Layout

```
obs-studio/
  global.ini                    machine-level: locations, renderer, updater, InstallGUID
  user.ini                      user-level: [Basic] current Profile/SceneCollection, window/dock state
  basic/profiles/<Dir>/basic.ini        profile settings (output, video, audio, stream flags)
  basic/profiles/<Dir>/service.json     stream destination
  basic/profiles/<Dir>/streamEncoder.json   advanced-mode stream encoder settings
  basic/profiles/<Dir>/recordEncoder.json   advanced-mode record encoder settings (when set)
  basic/scenes/<File>.json      one scene collection (scenes, sources, transitions, audio devices)
  basic/scenes/<File>.json.bak  previous save
  plugin_config/obs-websocket/config.json
  plugin_config/rtmp-services/services.json   downloaded catalog of known services (rtmp_common)
  logs/YYYY-MM-DD HH-MM-SS.txt  one log per run
  .sentinel/run_<uuid>          exists while OBS runs; leftover = unclean shutdown
  crashes/                      crash logs
```

`<Dir>`/`<File>` come from the display name: whitespace -> `_`, other non-alphanumerics removed,
`(N)` suffix when taken (`SRS Test` -> `SRS_Test`). OBS matches profiles/collections by the name *inside*
the file, so renaming files does nothing; edit `[General] Name` or `"name"` instead.

## global.ini and user.ini

```ini
# user.ini
[General]
ConfirmOnExit=true          ; quit prompts only while an output is active
FirstRun=true               ; false on a fresh install until the auto-config wizard has run/been dismissed
[Basic]
Profile=SRS Test            ; display name of the current profile
ProfileDir=SRS_Test         ; its directory
SceneCollection=SRS Test    ; display name
SceneCollectionFile=SRS_Test ; file name without .json
ConfigOnNewProfile=false    ; last "run wizard for new profile" choice in the GUI
[BasicWindow]
SysTrayEnabled=true
SysTrayWhenStarted=false
PreviewEnabled=true
```

Command line `--profile`/`--collection` override `[Basic]` for that run only when the name exists;
unknown names fall back silently to the configured ones (`OBSBasic::InitBasicConfig`).
OBS < 31 kept `[Basic]` and `[BasicWindow]` in `global.ini`.

## Profile: basic.ini

Keys that matter for publishing (defaults from `frontend/OBSApp.cpp` / `OBSBasic::InitBasicConfigDefaults`):

```ini
[General]
Name=SRS Test

[Output]
Mode=Simple                 ; Simple | Advanced
Reconnect=true              ; auto-reconnect the stream output
RetryDelay=2
MaxRetries=25
DelayEnable=false           ; stream delay
BindIP=default
IPFamily=IPv4+IPv6
NewSocketLoopEnable=false   ; Windows RTMP low-latency loop
LowLatencyEnable=false

[Stream1]                   ; multitrack video ("Enhanced Broadcasting"), leave off for SRS
EnableMultitrackVideo=false
IgnoreRecommended=false

[SimpleOutput]
VBitrate=2500               ; kbps, read at every stream start
ABitrate=160
StreamEncoder=x264          ; x264 | x264_lowcpu | apple_h264 | apple_hevc | nvenc | nvenc_hevc | nvenc_av1 | qsv | qsv_av1 | amd | amd_hevc | amd_av1
StreamAudioEncoder=aac      ; aac | opus  (WHIP needs opus)
Preset=veryfast             ; x264 preset
NVENCPreset2=p5
UseAdvanced=false           ; enable custom encoder settings string
x264Settings=               ; e.g. "keyint=60"
RecQuality=Stream
FilePath=~/Movies
RecFormat2=hybrid_mov

[AdvOut]                    ; used when Mode=Advanced
Encoder=obs_x264            ; encoder id: obs_x264, com.apple.videotoolbox.videoencoder.ave.avc, jim_nvenc, obs_qsv11_v2, ...
AudioEncoder=CoreAudio_AAC  ; CoreAudio_AAC | ffmpeg_aac | ffmpeg_opus
TrackIndex=1
ApplyServiceSettings=true
UseRescale=false
RescaleRes=1280x720
RecType=Standard
RecEncoder=none

[Video]
BaseCX=1920                 ; canvas
BaseCY=1080
OutputCX=1280               ; scaled output
OutputCY=720
FPSType=0                   ; 0 common (FPSCommon), 1 integer (FPSInt), 2 fraction (FPSNum/FPSDen)
FPSCommon=30
FPSNum=30
FPSDen=1
ScaleType=bicubic
ColorFormat=NV12
ColorSpace=709
ColorRange=Partial

[Audio]
SampleRate=48000
ChannelSetup=Stereo
```

Settings are read when the output handler is built (profile activation) and at output start. In
practice: bitrate/preset/resolution changes apply to the next stream start; `Mode`, `StreamEncoder`,
`StreamAudioEncoder`, `AdvOut/Encoder` need a profile re-activation (switch away and back) or a restart.
`obs_srs_test.py setup` does the switch automatically.

## Profile: service.json

```json
{"type":"rtmp_custom","settings":{"server":"rtmp://localhost/live","key":"livestream","use_auth":false}}
{"type":"rtmp_custom","settings":{"server":"srt://127.0.0.1:10080?streamid=#!::r=live/livestream,m=publish","key":""}}
{"type":"whip_custom","settings":{"server":"http://localhost:1985/rtc/v1/whip/?app=live&stream=livestream","bearer_token":""}}
{"type":"rtmp_common","settings":{"service":"Twitch","server":"auto","key":"live_..."}}
```

`rtmp_custom` derives the protocol from the URL prefix (`rtmps://` RTMPS, `srt://` SRT, `rist://` RIST,
else RTMP; `plugins/rtmp-services/rtmp-custom.c`). The frontend picks the output type from the protocol
(`frontend/utility/BasicOutputHandler.cpp`): RTMP/RTMPS -> `rtmp_output`, HLS -> `ffmpeg_hls_muxer`,
SRT/RIST -> `ffmpeg_mpegts_muxer`; `whip_custom` asks for `whip_output`. For SRT the `key` is used as
the SRT passphrase, so keep it empty for SRS unless `passphrase` is configured there. Written on every
`SetStreamServiceSettings` and when the Settings dialog is saved.

## Profile: streamEncoder.json and recordEncoder.json

Advanced mode only (`Mode=Advanced`). Plain encoder settings object for `AdvOut/Encoder`:

```json
{"bitrate":2500,"keyint_sec":2,"preset":"veryfast","profile":"high","rate_control":"CBR","tune":"zerolatency","bf":0}
```

Key names come from the encoder plugin: x264 (`plugins/obs-x264/obs-x264.c`): `bitrate`, `rate_control`
(CBR|ABR|VBR|CRF), `crf`, `keyint_sec`, `preset`, `profile`, `tune`, `x264opts`, `bf`, `repeat_headers`.
Apple VideoToolbox (`plugins/mac-videotoolbox/encoder.c`): `bitrate`, `rate_control` (CBR|ABR|CRF),
`keyint_sec`, `profile`, `limit_bitrate`, `max_bitrate`, `bframes`. NVENC: `bitrate`, `preset2`, `tune`,
`profile`, `rate_control`, `bf`, `lookahead`. Simple mode ignores these files and builds settings from
`[SimpleOutput]`.

## Scene collection JSON

Top level (`basic/scenes/SRS_Test.json`):

```json
{
  "name": "SRS Test",
  "current_scene": "SRS Scene",
  "current_program_scene": "SRS Scene",
  "scene_order": [{"name": "Scene"}, {"name": "SRS Scene"}],
  "sources": [ ...every source, including scenes (id "scene") and groups... ],
  "groups": [],
  "transitions": [], "current_transition": "Fade", "transition_duration": 300,
  "quick_transitions": [...],
  "AuxAudioDevice1": { "id": "coreaudio_input_capture", "name": "Mic/Aux", ... },
  "DesktopAudioDevice1": { ... },
  "canvases": [], "saved_projectors": [], "modules": {...}, "version": 2
}
```

Each `sources[]` entry: `name`, `uuid`, `id` (unversioned kind), `versioned_id` (e.g. `color_source_v3`),
`settings` (kind-specific), `mixers` (audio track bitmask, 255 = all), `volume`, `muted`, `enabled`,
`monitoring_type`, `hotkeys`, `private_settings`. A scene's `settings.items[]` holds the scene items:
`name`, `source_uuid`, `visible`, `locked`, `pos {x,y}`, `scale {x,y}`, `rot`, `align`, `bounds_type`,
`bounds {x,y}`, `bounds_align`, `crop_*`, `id`, `blend_method`, `blend_type`. Because items reference
sources by uuid and every source needs a uuid, hand-writing a collection is error-prone: create it through
the websocket once and copy the file, or edit `settings` values in place.

OBS saves the current collection a moment after each change (`SaveProjectDeferred`) and on exit, always
writing a `.bak` first. Edit collection files only while OBS is not running or while another collection is
current.

## obs-websocket config.json

```json
{"first_load": false, "server_enabled": true, "server_port": 4455, "alerts_enabled": false,
 "auth_required": true, "server_password": "xxxxxxxxxxxxxxxx"}
```

Read once in `obs_module_load` (`plugins/obs-websocket/src/Config.cpp`). `first_load=true` makes OBS
generate a password and rewrite the file. The file is rewritten when the Tools > WebSocket Server
Settings dialog is saved, so GUI edits win over file edits made while OBS runs. Command line overrides
for one run: `--websocket_port N`, `--websocket_password X` (also forces auth on), `--websocket_ipv4_only`,
`--websocket_debug`; they do not enable the server, only `server_enabled` does.
OBS 27 and older used `global.ini [OBSWebSocket]`; OBS migrates it.

## Crash sentinel and logs

- `.sentinel/run_<uuid>` is created at startup and deleted on clean exit. A leftover file when OBS is not
  running means the last run crashed or was killed; the next start shows a modal "unclean shutdown"
  dialog (Safe Mode or Normal) that blocks everything until clicked. Delete stale files before an
  automated launch (`obsws.py launch` does). Safe Mode disables third-party plugins, scripting **and
  the websocket server**.
- Logs: newest file in `logs/`. Useful lines: `[obs-websocket] ... WebSocket server started`,
  `Loaded Modules:`, `Available Encoders:`, `[rtmp stream: 'simple_stream'] Connecting to RTMP URL`,
  `Connection to ... successful`, `[obs-webrtc] [whip_output: ...] PeerConnection state is now: Connected|Failed`,
  `[ffmpeg muxer: ...]` for SRT/RIST, `Output 'simple_stream': stopping`, `==== Streaming Start/Stop ====`.
  Starting OBS with `--verbose` adds debug lines; `--unfiltered_log` disables repeat suppression.

## Editing files offline vs. using the websocket

Prefer the websocket while OBS runs: files are the persistence layer, not the control surface, and OBS
overwrites the current profile and collection from memory. Edit files when OBS is stopped to:

- pre-seed a machine (CI, fresh VM): write `plugin_config/obs-websocket/config.json` with
  `server_enabled=true` and a known password, a profile directory with `basic.ini`+`service.json`, and set
  `user.ini [Basic]` (or pass `--profile`/`--collection` on launch);
- change something the websocket cannot (e.g. `[Output] Mode`, `[AdvOut] Encoder` without a profile
  switch, audio `SampleRate`, `[General] Name`);
- remove a scene collection (no websocket request exists).

Minimal offline profile that publishes to SRS on launch with `--startstreaming`:

```
basic/profiles/SRS_CI/basic.ini
    [General]
    Name=SRS CI
    [Output]
    Mode=Simple
    [SimpleOutput]
    VBitrate=2000
    ABitrate=160
    StreamEncoder=x264
    StreamAudioEncoder=aac
    Preset=veryfast
    [Video]
    BaseCX=1280
    BaseCY=720
    OutputCX=1280
    OutputCY=720
    FPSType=0
    FPSCommon=30
basic/profiles/SRS_CI/service.json
    {"type":"rtmp_custom","settings":{"server":"rtmp://srs-host/live","key":"ci","use_auth":false}}
```

Then `obsws.py launch --profile "SRS CI" --collection "<existing collection>" --startstreaming`.
