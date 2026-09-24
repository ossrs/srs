# Sources (inputs) and encoders for OBS test scenes

Read this when composing a scene through `CreateInput`/`SetInputSettings` or when choosing encoder
settings for a test. Kind ids and setting keys come from the plugin sources under `plugins/` and were
confirmed with `GetInputDefaultSettings` on OBS 32.2. Always confirm availability at runtime with
`obsws.py call GetInputKindList` (kinds differ per platform and build).

## Contents

- [Input kinds by platform](#input-kinds-by-platform)
- [Settings for synthetic test sources](#settings-for-synthetic-test-sources)
- [Capture sources](#capture-sources)
- [Audio](#audio)
- [Scene item placement](#scene-item-placement)
- [Encoders](#encoders)
- [Choosing a test composition](#choosing-a-test-composition)

## Input kinds by platform

| purpose | macOS | Windows | Linux |
| --- | --- | --- | --- |
| solid colour | `color_source_v3` | same | same |
| text | `text_ft2_source_v2` (FreeType) | `text_gdiplus_v3` (also FreeType if enabled) | `text_ft2_source_v2` |
| image | `image_source`, `slideshow_v2` | same | same |
| video file / URL | `ffmpeg_source` (Media Source), `vlc_source` (if VLC installed) | same | same |
| web page | `browser_source` (CEF; absent in some Linux distro builds) | same | often missing |
| camera | `macos-avcapture` (`av_capture_input_v2` legacy) | `dshow_input` | `v4l2_input`, `pipewire-camera-source` |
| screen | `screen_capture` (ScreenCaptureKit), `display_capture`, `window_capture` | `monitor_capture`, `window_capture`, `game_capture` | `pipewire-desktop-capture-source`, `xshm_input`, `xcomposite_input` |
| mic / desktop audio | `coreaudio_input_capture`, `coreaudio_output_capture`, `sck_audio_capture` | `wasapi_input_capture`, `wasapi_output_capture`, `wasapi_process_output_capture` | `pulse_input_capture`, `pulse_output_capture`, `pipewire-audio-*` |
| other | `syphon-input` | `decklink-input`, `text_gdiplus_v3` | `jack_output_capture` |

Unversioned kinds (`color_source`, `text_ft2_source`) are accepted by `GetInputList` filters but
`CreateInput` wants the versioned id when one exists.

## Settings for synthetic test sources

Colours are packed integers `0xAABBGGRR` (alpha, blue, green, red): opaque white `4294967295`,
opaque red `4278190335` (0xFF0000FF). `rgb_to_obs_color()` in `obs_srs_test.py` converts `#RRGGBB`.

### color_source_v3 (`plugins/image-source/color-source.c`)

```json
{"color": 4280821791, "width": 1280, "height": 720}
```
Width/height define the source size; make them the canvas size for a full background.

### text_ft2_source_v2 (`plugins/text-freetype2/text-freetype2.c`)

```json
{"text": "SRS test", "font": {"face": "Helvetica", "size": 64, "flags": 0, "style": ""},
 "color1": 4294967295, "color2": 4294967295, "outline": true, "drop_shadow": false,
 "word_wrap": false, "custom_width": 0, "antialiasing": true,
 "from_file": false, "text_file": "", "log_mode": false, "log_lines": 6}
```
`from_file=true` re-reads `text_file` when it changes: a cheap way to show a live timestamp written by a
script. Default face: Helvetica (macOS), Arial (Windows), Sans (Linux). Windows `text_gdiplus_v3` uses
`text`, `font{face,size,flags,style}`, `color`, `opacity`, `outline`, `read_from_file`, `file`, `chatlog`.

### ffmpeg_source, "Media Source" (`plugins/obs-ffmpeg/obs-ffmpeg-source.c`)

```json
{"is_local_file": true, "local_file": "/abs/path/clip.mp4", "looping": true,
 "clear_on_media_end": false, "restart_on_activate": false, "hw_decode": false,
 "speed_percent": 100, "buffering_mb": 2, "close_when_inactive": false}
```
Network input instead: `{"is_local_file": false, "input": "rtmp://host/live/other", "input_format": "",
"reconnect_delay_sec": 10, "buffering_mb": 2}` (also `srt://`, `http://...flv`, `udp://`), which turns OBS
into a relay client of SRS: publish A, pull A into a media source, publish B.
Audio from a media source goes to mixer track 1 by default (`mixers` 255), so the stream gets audio even
without a microphone. Generate a moving clip with audio:
`ffmpeg -f lavfi -i testsrc2=size=1280x720:rate=30 -f lavfi -i sine=frequency=440:sample_rate=48000 -t 20 -c:v libx264 -preset veryfast -pix_fmt yuv420p -c:a aac -shortest test.mp4`
(`obs_srs_test.py` does this when `media`/`media:auto` is requested and ffmpeg is on PATH).
Control playback: `TriggerMediaInputAction {inputName, mediaAction: "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_RESTART"}`,
`GetMediaInputStatus` -> `mediaState` (`OBS_MEDIA_STATE_PLAYING`, `..._ENDED`, `..._ERROR`).

### image_source

```json
{"file": "/abs/path/image.png", "unload": false, "linear_alpha": false}
```

### browser_source (obs-browser)

```json
{"url": "https://example.com/overlay", "width": 1280, "height": 720, "fps_custom": true, "fps": 30,
 "reroute_audio": false, "shutdown": false, "restart_when_active": false,
 "css": "body { background-color: rgba(0, 0, 0, 0); margin: 0px auto; overflow: hidden; }"}
```
Local page: `{"is_local_file": true, "local_file": "/abs/path/page.html", ...}`. A page with a JS clock
gives cheap continuous motion; a WHEP/HTTP-FLV player page (SRS ships `players/whep.html`,
`players/flv.html`) lets OBS *play* an SRS stream inside a scene.

### slideshow_v2

`{"files": [{"value": "/path/a.png"}, {"value": "/path/b.png"}], "slide_time": 8000, "transition": "fade", "loop": true}`.

## Capture sources

- macOS `macos-avcapture`: `device` (unique id), `preset`/`use_preset`, `frame_rate`, `resolution`,
  `input_format`, `color_space`. Enumerate devices with
  `GetInputPropertiesListPropertyItems {inputName, propertyName: "device"}` on an existing input.
  Camera and screen recording need the OS permission dialogs answered once by a human; `screen_capture`
  settings: `type` (0 display, 1 window, 2 application), `display_uuid`, `window`, `show_cursor`, `hide_obs`.
- Windows `dshow_input`: `video_device_id`, `res_type`, `resolution`, `frame_interval`, `video_format`;
  `monitor_capture`: `monitor_id`, `method`, `capture_cursor`; `game_capture`: `capture_mode`, `window`.
- Linux `v4l2_input`: `device_id` (`/dev/video0`), `input`, `pixelformat`, `resolution`, `framerate`;
  `pipewire-desktop-capture-source` requires a portal dialog (not headless).

Capture sources are real hardware: prefer synthetic sources for CI and use capture only when the test is
about camera/screen paths.

## Audio

- A new scene collection contains `Mic/Aux` (`coreaudio_input_capture` / `wasapi_input_capture` /
  `pulse_input_capture`, device `default`) and, on Windows/Linux, `Desktop Audio`. macOS has no desktop
  audio capture by default (`sck_audio_capture` exists on macOS 13+). `GetSpecialInputs` lists them.
- Mute/unmute: `SetInputMute {inputName, inputMuted}`; volume: `SetInputVolume {inputName, inputVolumeDb: -6}`.
- Streams carry mixer track 1 in simple mode (`AdvOut/TrackIndex` in advanced mode). A source's tracks:
  `SetInputAudioTracks {inputName, inputAudioTracks: {"1": true, "2": false, ...}}`.
- Audio encoder ids: `CoreAudio_AAC` (macOS/Windows with Apple codecs), `ffmpeg_aac`, `ffmpeg_opus`,
  `libfdk_aac` (if built). Simple mode picks by `SimpleOutput/StreamAudioEncoder` = `aac` | `opus`.
  Output sample rate is `Audio/SampleRate` (48000 default; the AAC encoder may resample to 44100 on macOS).

## Scene item placement

Canvas coordinates originate top-left. `SetSceneItemTransform` fields:

- fill the canvas keeping aspect: `{"positionX":0,"positionY":0,"alignment":5,"boundsType":"OBS_BOUNDS_SCALE_INNER","boundsAlignment":0,"boundsWidth":W,"boundsHeight":H}`
- stretch to canvas: same with `OBS_BOUNDS_STRETCH`
- pin text top-left with margin: `{"positionX":32,"positionY":32,"alignment":5}`
- bottom-right corner: `{"positionX":W-32,"positionY":H-32,"alignment":10}` (RIGHT 2 | BOTTOM 8)
- picture-in-picture at 25 %: `{"positionX":W-16,"positionY":H-16,"alignment":10,"boundsType":"OBS_BOUNDS_SCALE_INNER","boundsWidth":W/4,"boundsHeight":H/4}`

Z-order: later `CreateInput`/`CreateSceneItem` calls land on top; `SetSceneItemIndex` reorders
(index 0 = bottom). `SetSceneItemEnabled false` hides without removing.

## Encoders

Video encoder ids (from the OBS log `Available Encoders:`):

| id | where | notes |
| --- | --- | --- |
| `obs_x264` | everywhere | CPU; `preset` ultrafast..veryslow, `tune zerolatency` for low latency, `profile high`, `keyint_sec 2` |
| `com.apple.videotoolbox.videoencoder.ave.avc` / `.ave.hevc` | macOS | hardware H264/HEVC; `rate_control` CBR/ABR, `bframes` |
| `jim_nvenc` / `jim_hevc_nvenc` / `jim_av1_nvenc` (`ffmpeg_nvenc` fallback) | NVIDIA | `preset2` p1..p7, `tune hq/ll/ull` |
| `obs_qsv11_v2`, `obs_qsv11_hevc` | Intel | |
| `h264_texture_amf`, `h265_texture_amf`, `av1_texture_amf` | AMD | |
| `ffmpeg_aom_av1`, `ffmpeg_svt_av1` | software AV1 | slow |

Simple mode names map to ids in `frontend/utility/SimpleOutput.cpp`: `x264` -> `obs_x264`,
`apple_h264` -> VideoToolbox H264, `nvenc` -> `jim_nvenc`, `qsv` -> `obs_qsv11_v2`, `amd` -> AMF.
Simple-mode x264 uses `rate_control=CBR`, `bitrate=VBitrate`, `preset=Preset`, `keyint_sec=0` (auto,
250 frames) unless `UseAdvanced=true` with `x264Settings="keyint=60"`. For SRS tests where GOP matters
(HLS segmenting, WebRTC join time) use advanced mode with `streamEncoder.json {"keyint_sec": 2}` or the
simple-mode `x264Settings` string.

Codec support per protocol in OBS: RTMP H264/HEVC/AV1 (enhanced RTMP) + AAC; SRT/RIST H264/HEVC/AV1 +
AAC/Opus in MPEG-TS; WHIP H264/HEVC/AV1 + Opus only. SRS accepts H264 (+ HEVC over RTMP/SRT/WebRTC in
recent versions) and AAC; WebRTC audio is Opus and is transcoded to AAC for RTMP consumers.

## Choosing a test composition

- **Bitrate / encoder / network tests**: colour background + `ffmpeg_source` test clip (motion, audio) + text label.
  Static scenes encode to almost nothing and hide bitrate problems.
- **Latency measurements**: browser clock or text-from-file timestamp; compare with the player.
- **Reconnect tests**: kick the publisher via SRS API (`DELETE /api/v1/clients/<cid>`) and watch
  `StreamStateChanged` RECONNECTING/RECONNECTED with `Output/Reconnect=true`.
- **Codec matrix**: switch `SimpleOutput/StreamEncoder` (needs profile re-activation) or use advanced mode
  with `AdvOut/Encoder` + `streamEncoder.json`; check `video.codec` in the SRS API.
- **Multiple streams**: OBS has one stream output per instance; run more instances with `--multi`,
  separate `--profile` names and `--websocket_port` values, or use the media source relay trick above.
