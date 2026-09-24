# obs-websocket 5.x protocol, as needed for controlling OBS

Source of truth: `docs/generated/protocol.md` in the obs-websocket repository (5.7.x) and
`src/requesthandler/*.cpp`. obs-websocket ships inside OBS Studio 28+; the version bundled with OBS
32.2 is 5.7.4, with OBS 32.0 it was 5.6.3 (same RPC version 1, canvas requests missing).

`scripts/obsws.py` implements everything in this file. Read this when you need a request that the
script does not wrap, or when a request fails and you need the status code meaning.

## Contents

- [Transport and handshake](#transport-and-handshake)
- [Message envelopes](#message-envelopes)
- [Request status codes](#request-status-codes)
- [Requests used for test automation](#requests-used-for-test-automation)
  - [General](#general) · [Config: profiles, collections, video, service](#config-profiles-collections-video-service)
  - [Scenes](#scenes) · [Inputs](#inputs) · [Scene items](#scene-items) · [Stream and outputs](#stream-and-outputs)
  - [Record, screenshots, UI](#record-screenshots-ui)
- [Events worth subscribing to](#events-worth-subscribing-to)
- [Behaviour learned from the source](#behaviour-learned-from-the-source)

## Transport and handshake

- Plain WebSocket on `ws://host:4455/` (port from `plugin_config/obs-websocket/config.json`, default 4455;
  binds all interfaces, IPv6 dual-stack unless `--websocket_ipv4_only`). No TLS.
- Request subprotocol `obswebsocket.json` (text frames). `obswebsocket.msgpack` also exists.
- Server sends `Hello` (op 0) immediately. Client answers with exactly one `Identify` (op 1). Anything else
  before `Identified` (op 2) closes the socket with 4007 NotIdentified.
- Authentication (when `Hello.d.authentication` exists):
  `secret = base64(sha256(password + salt))`, `authentication = base64(sha256(secret + challenge))`.
  Wrong password: close code 4009 AuthenticationFailed.
- `Identify.d.eventSubscriptions` is a bitmask; pass 0 for request-only sessions so events do not interleave
  with responses. `All` normal categories = 0x0FFF; high-volume ones must be added explicitly:
  InputVolumeMeters 1<<16, InputActiveStateChanged 1<<17, InputShowStateChanged 1<<18,
  SceneItemTransformChanged 1<<19. Category bits: General 1<<0, Config 1<<1, Scenes 1<<2, Inputs 1<<3,
  Transitions 1<<4, Filters 1<<5, Outputs 1<<6, SceneItems 1<<7, MediaInputs 1<<8, Vendors 1<<9, Ui 1<<10,
  Canvases 1<<11.
- The server accepts TCP connections before OBS finished loading; requests then fail with 207 NotReady.
  Retry the whole handshake until `GetVersion` succeeds (obsws.py `launch --wait` does this).

## Message envelopes

```json
{"op": 6, "d": {"requestType": "SetCurrentProgramScene", "requestId": "<uuid>", "requestData": {"sceneName": "Scene 12"}}}
{"op": 7, "d": {"requestType": "...", "requestId": "<uuid>", "requestStatus": {"result": true, "code": 100}, "responseData": {...}}}
{"op": 8, "d": {"requestId": "<uuid>", "haltOnFailure": false, "executionType": 0, "requests": [{"requestType": "...", "requestData": {...}}, ...]}}
{"op": 9, "d": {"requestId": "<uuid>", "results": [ {requestType, requestStatus, responseData}, ... ]}}
{"op": 5, "d": {"eventType": "StreamStateChanged", "eventIntent": 64, "eventData": {...}}}
```

Batch `executionType`: -1 None, 0 SerialRealtime (default), 1 SerialFrame (one request per rendered frame),
2 Parallel. `Sleep` (`sleepMillis` <= 50000 or `sleepFrames` <= 10000) only works inside a batch.
`requestStatus.comment` carries the human-readable reason on failure.

## Request status codes

| code | name | typical cause |
| --- | --- | --- |
| 100 | Success | |
| 203 | UnknownRequestType | typo, or request newer than the bundled obs-websocket |
| 207 | NotReady | OBS still loading; retry |
| 300/301 | MissingRequestField / MissingRequestData | |
| 400-404 | InvalidRequestField / Type / OutOfRange / Empty | e.g. `parameterValue` must be a string |
| 500 | OutputRunning | streaming: cannot change service, video settings, profile |
| 501 | OutputNotRunning | StopStream while idle |
| 600 | ResourceNotFound | wrong scene/input/profile name (names are case-sensitive) |
| 601 | ResourceAlreadyExists | input name taken (input names are global, not per scene) |
| 604 | InvalidResourceState | |
| 605 | InvalidInputKind | kind missing on this platform/build; check `GetInputKindList` |
| 700 | ResourceCreationFailed | bad `streamServiceType` etc. |
| 702 | RequestProcessingFailed | |

## Requests used for test automation

Field names are exact. `?` marks optional fields. Most "by name" fields have a `...Uuid` twin.

### General

- `GetVersion` -> `obsVersion`, `obsWebSocketVersion`, `rpcVersion`, `availableRequests[]`, `platform`
  (`macos`/`windows`/`ubuntu`), `supportedImageFormats[]`.
- `GetStats` -> `cpuUsage`, `memoryUsage`, `activeFps`, `averageFrameRenderTime`, `renderSkippedFrames`,
  `outputSkippedFrames`, `outputTotalFrames`.
- `Sleep {sleepMillis}` (batch only). `CallVendorRequest {vendorName, requestType, ?requestData}` for plugins.
- `TriggerHotkeyByName {hotkeyName}`, `GetHotkeyList`.

### Config: profiles, collections, video, service

- `GetProfileList` -> `currentProfileName`, `profiles[]`.
- `CreateProfile {profileName}` creates a *clean* profile (OBS defaults, no wizard) and switches to it.
  The switch is queued on the UI thread: poll `GetProfileList` until `currentProfileName` matches before
  writing parameters. Duplicate name -> 601.
- `SetCurrentProfile {profileName}` (synchronous). `RemoveProfile {profileName}`: silent for a non-current
  profile, but for the **current** profile OBS opens a modal "confirm removal" dialog that blocks all further
  requests (`OBSBasic::on_actionRemoveProfile_triggered`). Always `SetCurrentProfile` to another one first.
- `GetProfileParameter {parameterCategory, parameterName}` -> `parameterValue`, `defaultParameterValue`.
- `SetProfileParameter {parameterCategory, parameterName, parameterValue}` writes `basic.ini`
  `[category] name=value` and saves. Value must be a string ("2000", "true"); `null` deletes the key.
  Only *reads* at output start take effect immediately (bitrates, presets); output mode and encoder ids are
  fixed when the profile is activated, so switch to another profile and back (or restart) after changing
  `Output/Mode`, `SimpleOutput/StreamEncoder`, `SimpleOutput/StreamAudioEncoder`, `AdvOut/Encoder`.
- `GetVideoSettings` / `SetVideoSettings {?baseWidth,?baseHeight,?outputWidth,?outputHeight,?fpsNumerator,?fpsDenominator}`
  pairs required; applies immediately via `obs_frontend_reset_video()`; refused (500) while any output runs.
- `GetSceneCollectionList` -> `currentSceneCollectionName`, `sceneCollections[]`.
- `CreateSceneCollection {sceneCollectionName}` creates a new collection containing one scene named
  `Scene` (localized) plus the default Mic/Aux input, switches to it and blocks until loaded.
  `SetCurrentSceneCollection {sceneCollectionName}` also blocks. There is **no RemoveSceneCollection**;
  delete the JSON in `basic/scenes/` while that collection is not current (OBS lists it until restart).
- `GetStreamServiceSettings` -> `streamServiceType`, `streamServiceSettings`.
- `SetStreamServiceSettings {streamServiceType, streamServiceSettings}`; refused while streaming.
  Same type -> settings are merged over the current ones; different type -> a new service is created.
  Saved to `service.json` immediately.
  - RTMP/RTMPS/SRT/RIST: `"rtmp_custom"` with `{"server": "rtmp://host/app", "key": "stream", "use_auth": false}`
    (`"username"`, `"password"` when `use_auth`). The URL scheme selects the protocol and output:
    `rtmp(s)://` -> `rtmp_output`, `srt://`/`rist://` -> `ffmpeg_mpegts_muxer` (key is appended as
    stream id / passphrase; for SRS put everything in the `streamid=` query and leave `key` empty).
  - WHIP (WebRTC): `"whip_custom"` with `{"server": "http://host:1985/rtc/v1/whip/?app=live&stream=livestream", "bearer_token": ""}`.
    WHIP only allows Opus audio and H264/HEVC/AV1 video, B-frames off (the service forces `bf=0`).
- `GetRecordDirectory` / `SetRecordDirectory {recordDirectory}`.
- `GetPersistentData` / `SetPersistentData {realm: "OBS_WEBSOCKET_DATA_REALM_GLOBAL"|"..._PROFILE", slotName, slotValue}`.

### Scenes

- `GetSceneList` -> `currentProgramSceneName`, `currentPreviewSceneName`, `scenes[{sceneName, sceneUuid, sceneIndex}]`.
- `CreateScene {sceneName}` -> `sceneUuid`. `RemoveScene {sceneName}`. `SetSceneName {sceneName, newSceneName}`.
- `GetCurrentProgramScene`, `SetCurrentProgramScene {sceneName}`; preview variants need studio mode.
- `GetGroupList`, `GetSceneSceneTransitionOverride`, `SetSceneSceneTransitionOverride`.

### Inputs

- `GetInputKindList {?unversioned}` -> `inputKinds[]` (use the **versioned** ids in CreateInput, e.g.
  `color_source_v3`, `text_ft2_source_v2`, `slideshow_v2`; unversioned ones like `ffmpeg_source`,
  `image_source`, `browser_source` have no suffix).
- `GetInputList {?inputKind}` -> `inputs[{inputName, inputUuid, inputKind, unversionedInputKind}]`.
- `GetInputDefaultSettings {inputKind}` -> `defaultInputSettings` (the authoritative key names).
- `CreateInput {sceneName, inputName, inputKind, ?inputSettings, ?sceneItemEnabled}` -> `inputUuid`, `sceneItemId`.
  Creates the source **and** a scene item in that scene. Input names are global across scenes and
  collections-in-memory; reuse an existing input with `CreateSceneItem` instead.
- `GetInputSettings {inputName}` -> `inputSettings` (non-default keys only), `inputKind`.
- `SetInputSettings {inputName, inputSettings, ?overlay=true}`; `overlay=false` resets to defaults first.
- `RemoveInput {inputName}`, `SetInputName {inputName, newInputName}`.
- Audio: `GetInputMute`/`SetInputMute {inputName, inputMuted}`, `SetInputVolume {inputName, inputVolumeMul|inputVolumeDb}`,
  `SetInputAudioTracks {inputName, inputAudioTracks: {"1": true, ...}}`, `SetInputAudioMonitorType`.
- `GetSpecialInputs` -> `desktop1`, `desktop2`, `mic1`..`mic4` (names or null).
- `GetInputPropertiesListPropertyItems {inputName, propertyName}` enumerates device lists (cameras, audio devices).
- Media: `GetMediaInputStatus {inputName}` -> `mediaState`, `mediaDuration`, `mediaCursor`;
  `TriggerMediaInputAction {inputName, mediaAction: "OBS_WEBSOCKET_MEDIA_INPUT_ACTION_RESTART"|PLAY|PAUSE|STOP|NEXT|PREVIOUS}`.

### Scene items

- `GetSceneItemList {sceneName}` -> `sceneItems[{sceneItemId, sourceName, sceneItemIndex, sceneItemEnabled, sceneItemTransform, ...}]`.
- `GetSceneItemId {sceneName, sourceName, ?searchOffset}` -> `sceneItemId` (600 if absent).
- `CreateSceneItem {sceneName, sourceName, ?sceneItemEnabled}` -> `sceneItemId`. `RemoveSceneItem {sceneName, sceneItemId}`.
- `SetSceneItemTransform {sceneName, sceneItemId, sceneItemTransform}` with any of `positionX`, `positionY`,
  `rotation`, `scaleX`, `scaleY`, `alignment`, `boundsType`, `boundsAlignment`, `boundsWidth`, `boundsHeight`,
  `cropLeft/Right/Top/Bottom`. `alignment` bits: LEFT 1, RIGHT 2, TOP 4, BOTTOM 8 (0 = centre; 5 = top-left).
  `boundsType`: `OBS_BOUNDS_NONE`, `OBS_BOUNDS_STRETCH`, `OBS_BOUNDS_SCALE_INNER`, `OBS_BOUNDS_SCALE_OUTER`,
  `OBS_BOUNDS_SCALE_TO_WIDTH`, `OBS_BOUNDS_SCALE_TO_HEIGHT`, `OBS_BOUNDS_MAX_ONLY`. To fit a source to the
  canvas: alignment 5, position 0/0, `OBS_BOUNDS_SCALE_INNER` with bounds = canvas size.
- `SetSceneItemEnabled {sceneName, sceneItemId, sceneItemEnabled}`, `SetSceneItemIndex`, `SetSceneItemLocked`,
  `SetSceneItemBlendMode`, `DuplicateSceneItem`.

### Stream and outputs

- `GetStreamStatus` -> `outputActive`, `outputReconnecting`, `outputTimecode`, `outputDuration` (ms),
  `outputCongestion` (0..1), `outputBytes`, `outputSkippedFrames`, `outputTotalFrames`.
- `StartStream` (500 if active; returns before the connection is up: poll status or wait for the
  `StreamStateChanged` event with `outputState` `OBS_WEBSOCKET_OUTPUT_STARTED`), `StopStream`, `ToggleStream`.
- `SendStreamCaption {captionText}` (CEA-608).
- `GetOutputList` -> `outputs[{outputName, outputKind, outputActive, outputFlags, outputWidth, outputHeight}]`.
  The stream output (`simple_stream`/`adv_stream`) is only listed while it exists; use the Stream requests.
- `GetOutputStatus {outputName}`, `StartOutput`, `StopOutput`, `GetOutputSettings`, `SetOutputSettings`.
- Virtual camera / replay buffer: `StartVirtualCam`, `StopVirtualCam`, `GetVirtualCamStatus`, `StartReplayBuffer`, `SaveReplayBuffer`.

### Record, screenshots, UI

- `GetRecordStatus` -> `outputActive`, `outputPaused`, `outputTimecode`, `outputDuration`, `outputBytes`.
  `StartRecord`, `StopRecord` -> `outputPath`, `PauseRecord`, `ResumeRecord`, `SplitRecordFile`, `CreateRecordChapter`.
- `GetSourceScreenshot {sourceName, imageFormat, ?imageWidth, ?imageHeight, ?imageCompressionQuality}` -> `imageData` (data URI).
  `SaveSourceScreenshot {..., imageFilePath}` writes the file. Use the current program scene name as
  `sourceName` to capture what is being streamed.
- `GetStudioModeEnabled`, `SetStudioModeEnabled {studioModeEnabled}`, `TriggerStudioModeTransition`.
- `GetSceneTransitionList`, `SetCurrentSceneTransition {transitionName}`, `SetCurrentSceneTransitionDuration {transitionDuration}`.
- Filters: `GetSourceFilterKindList`, `CreateSourceFilter {sourceName, filterName, filterKind, ?filterSettings}`,
  `SetSourceFilterSettings`, `SetSourceFilterEnabled`, `RemoveSourceFilter`.
- `GetMonitorList`, `OpenVideoMixProjector`, `OpenInputPropertiesDialog {inputName}` (opens GUI dialogs; avoid in automation).

## Events worth subscribing to

- `StreamStateChanged {outputActive, outputState}` with states `OBS_WEBSOCKET_OUTPUT_STARTING|STARTED|STOPPING|STOPPED|RECONNECTING|RECONNECTED`.
  A RECONNECTING right after STARTED means the server refused/dropped the connection.
- `RecordStateChanged`, `RecordFileChanged`, `VirtualcamStateChanged`.
- `CurrentProfileChanged {profileName}`, `CurrentSceneCollectionChanged {sceneCollectionName}`, `SceneCollectionListChanged`, `ProfileListChanged`.
- `SceneCreated`, `CurrentProgramSceneChanged {sceneName}`, `InputCreated`, `InputSettingsChanged`, `SceneItemCreated`.
- `MediaInputPlaybackStarted` / `MediaInputPlaybackEnded {inputName}`.
- `ExitStarted` (OBS is shutting down).

`obsws.py events --types StreamStateChanged --timeout 60` prints them as JSON lines.

## Behaviour learned from the source

- Profile and collection names map to sanitised file names (`GetFileSafeName`: whitespace -> `_`, other
  non-alphanumerics dropped, `(N)` suffix if taken); OBS matches by the name stored inside the files
  (`basic.ini [General] Name`, collection JSON `"name"`), never by file name. `SRS Test` -> `SRS_Test/`.
- `CreateProfile` copies nothing from the current profile (clean defaults); `obs_frontend_duplicate_profile`
  is not exposed over websocket.
- Activating a profile (`CreateProfile`, `SetCurrentProfile`) runs `OBSBasic::ActivateProfile(..., reset=true)`:
  it saves the outgoing profile, loads the new `basic.ini`, then `UpdateProfileEncoders()` (re-derives the
  simple-mode encoder defaults from the *current service's* supported codecs and falls back to x264 for
  missing encoders) and `ResetProfileData()` (`ResetVideo`, reloads `service.json` via `InitService`,
  `ResetOutputs`). Consequences: set the stream service **before** re-activating the profile, otherwise a
  WHIP service left selected turns `StreamAudioEncoder` back into `opus`; and a profile switch is the
  cheapest way to make `Output/Mode` or encoder-id changes take effect without restarting OBS.
- `SetStreamServiceSettings` merges when the type is unchanged, so stale keys (e.g. an old `key`) survive
  unless you set them explicitly.
- `StartStream` fails with 500 when already active and is asynchronous; a failed connection shows as
  `outputActive=false` again within a few seconds (with `Output/Reconnect=true` it keeps retrying and
  `outputReconnecting=true`).
- obs-websocket closes idle clients never; the client must close. Closing without waiting for the peer
  close frame is logged by OBS as code 1006 but is harmless.
- Requests run on the websocket thread and marshal to the UI thread where required; a modal dialog in OBS
  (crash-recovery prompt, missing-files dialog, auto-config wizard) blocks those requests. Launch with
  `--disable-missing-files-check` and clear stale `.sentinel/run_*` files before starting.
