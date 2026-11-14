# OBS Live Stream

Live stream a community meeting, or any other live session, from OBS: the desktop, a camera, a microphone, the SRS logo and a caption with the meeting ID.

Drive OBS with the `internal-obs-control` skill on the main OBS, in an interactive session with a run id. That skill owns the commands, the run ledger and the cleanup; this file gives the target setup and what went wrong before.

## What the user gives each session

Ask for these when needed, and never write them into the repository, a commit or a task file:

- **Stream server and key** of the platform. The key is a secret.
- **Meeting ID** for the caption, from any meeting app:
  - Tencent Meeting, such as `#TencentMeeting：123-456-789`.
  - Google Meet, such as `meet.google.com/abc-defg-hij`.
  - Zoom, such as `Zoom: 123 4567 8901`.

The stream goes to a remote platform, never to a local SRS. Check the server and key of the platform:

- **WeChat Channels** (Tencent), used now:
  - Server like `rtmp://push-<xx>.wxlivecdn.com/trtc_<app-id>/`.
  - Key like `live_<id>?txSecret=<secret>&txTime=<hex>`, with both parameters.
  - The key expires: `txTime` is a hex Unix time, and `date -r $((16#<hex>))` must print a time after now.
- **YouTube**:
  - Server `rtmp://a.rtmp.youtube.com/live2`.
  - Key like `xxxx-xxxx-xxxx-xxxx-xxxx`, from YouTube Studio.

If a check fails, do not go live: tell the user which check failed and ask for a new server and key.

## OBS without its UI

- Launch OBS with `--minimize-to-tray`, and drive it only over the websocket.
- Never bring the OBS window to the front: keys meant for the terminal can reach OBS. Once this deleted the live scene, and the stream went out blank.
- Check the picture with screenshots, not the OBS window.

## macOS permissions

- OBS needs **Camera**, **Microphone** and **Screen & System Audio Recording** in System Settings, Privacy & Security.
- OBS reads them only at startup. If the log says a permission is granted but capture still fails, toggle OBS off and on in that list, then relaunch OBS.
- Look for these lines in the newest OBS log:
  ```
  [macOS] Permission for audio device access granted.
  [macOS] Permission for video device access granted.
  [macOS] Permission for screen capture granted.
  ```

## Devices

Device IDs change when a USB port or monitor changes, so read them fresh every session:

- **Camera**: `system_profiler SPCameraDataType` prints the `Unique ID`.
- **Display UUID**: `CGDisplayCreateUUIDFromDisplayID(CGMainDisplayID())`, loaded from `ColorSync.framework`, since CoreGraphics does not export it. Never list display items over the websocket; see `internal-obs-control`.
- **Microphone**: CoreAudio devices with input streams, by name and UID. `system_profiler SPAudioDataType` shows names but not UIDs.

Pick the microphone by its device, never `Default`:

- Use the camera's own mic, such as the webcam mic, not the system input:
  - The meeting app uses the system input, such as a USB desk mic.
  - If OBS uses the same mic as the meeting app, the two apps conflict.
  - A separate mic for OBS avoids that conflict.
- The default input may be a Bluetooth headset, which opens at 8000 Hz with phone-call quality.
- A USB mic or webcam mic opens at 48000 Hz; confirm the rate in the OBS log.
- A USB audio UID ends with its USB location, so moving the device changes it.

## Profile and scene

- Profile, scene collection and scene named `SRS Live <run>`, `SRS Live <run>` and `Desktop <run>`.
- Canvas 1920×1080, output 1280×720, 30 fps.

Sources, bottom to top:

| Source | Kind | Settings | Position on the canvas |
| --- | --- | --- | --- |
| Desktop Capture | `screen_capture` | display UUID, `show_cursor` and `hide_obs` on | fills the canvas, scale inner |
| Mic | `coreaudio_input_capture` | the UID of the camera's mic | audio only |
| SRS Logo | `image_source` | absolute path of `assets/srs-alpha-512x256.png` in this skill | top right, at (1896, 24), scale 0.6 |
| Camera | `macos-avcapture` | camera ID, preset 1280×720; a circle mask filter | bottom right, at (1896, 1056), crop 280 left and right, bounds 400×400 |
| Meeting Text | `text_ft2_source_v2` | the caption, font Arial Unicode MS 50, light red `#FF3B30`, outline, drop shadow | top left, at (50, 50) |

Notes:

- A new collection has no global Mic/Aux, so the Mic source is the only audio.
- The FreeType text source cannot load PingFang SC; Arial Unicode MS has the CJK and full-width characters.
- Caption color:
  - White was hard to read over the desktop, and dark red `#8B0000` was too dark on a dark terminal.
  - Light red `#FF3B30` reads well on both.
  - `color1` and `color2` are ABGR integers: set both to `0xFF303BFF`, which is `4281351167`.
- A larger camera covers the meeting's video gallery; a 400 px circle leaves it visible.
- Round camera:
  - Crop 280 px from the left and the right of the 1280×720 picture, which leaves a 720×720 square.
  - Add a `mask_filter_v2` filter named `Circle Mask <run>`, with type `mask_alpha_filter.effect` and the absolute path of `assets/circle-mask.png` in this skill.
  - The mask is a white circle on a transparent square; its alpha cuts the corners away.
  - It changes live, with no stream restart.
- Everything on the desktop is broadcast. Remind the user to close private windows first.

## Encoder

Advanced output mode, streaming:

| Setting | Value |
| --- | --- |
| Audio | track 1, CoreAudio AAC, 64 kbps |
| Video encoder | x264, CBR, 3000 kbps |
| Keyframe interval | 2 s |
| Preset, profile, tune | fast, baseline, zerolatency |

- 2000 kbps looked unclear for a desktop with text.
- At 3000 kbps OBS sends about 3.1 Mbps with 0 dropped frames and about 4% CPU.
- `streamEncoder.json` has no websocket request; write it while another profile is current, then switch back. See `internal-obs-control`.

## Going live

1. **Check the server and key** as in [What the user gives each session](#what-the-user-gives-each-session).
2. **Check the picture and sound** with a screenshot and the mic meter before going live.
3. **Go live** with the server and key, and the caption set.
4. **Confirm** the stream is active with 0 skipped frames, and ask the user to confirm the platform plays it.
   - If OBS is not active within about 15 s, or keeps reconnecting, the platform refused the server or key: stop the stream and tell the user.

When the user asks to go live, start the stream once the checks pass; do not ask again. Stop and report instead when a check fails, such as a missing permission, a blank screenshot or a silent mic.

## Changes while live

- Position, size, caption text and mic device change live.
- Bitrate, encoder, resolution and fps need a stream restart.
- Use the `internal-obs-control` stream restart: a start sent right after a stop can be ignored, and the stream stays off air until a profile reload.

## Cleanup

- Clear the stream key in OBS before cleanup, because `service.json.bak` keeps a copy.
- Remove only this run's resources with the `internal-obs-control` teardown, or keep them for the next live stream.

## Known problems

- OBS may exit silently right after the profile is created. Relaunch and run the same setup again; it reuses the run's profile.
- The first connection may drop once and reconnect by itself about 2 s later.
