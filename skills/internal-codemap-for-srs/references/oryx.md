# Oryx Code Map

Use this reference after `skills/internal-codemap-for-srs/SKILL.md` routes a task to Oryx.

## Contents

- [Repository Boundary](#repository-boundary)
- [Architecture and Startup](#architecture-and-startup)
- [Go Platform Backend](#go-platform-backend)
- [React Dashboard](#react-dashboard)
- [Runtime, Packaging, and Deployment](#runtime-packaging-and-deployment)
- [Release Service and Automation](#release-service-and-automation)
- [Tests and Verification](#tests-and-verification)
- [Change Routing](#change-routing)

## Repository Boundary

- Authoritative repository: `https://github.com/ossrs/oryx`
- Configured local checkout: `~/git/oryx`
- Check this path directly before using the route. If it does not exist, stop and ask the user to clone `https://github.com/ossrs/oryx` into `~/git/oryx`; do not clone it automatically or search for another checkout.
- Keep the SRS repository as the current working directory. Use `git -C ~/git/oryx ...` for Git operations and use paths beginning with `~/git/oryx` for file operations. Never replace this configured path with its expanded absolute path.
- Before editing, inspect the branch, commit, and worktree state of both repositories. Do not overwrite unrelated changes:

```bash
git status --short --branch
git rev-parse HEAD
git -C ~/git/oryx status --short --branch
git -C ~/git/oryx rev-parse HEAD
```

- Route Oryx documentation through `skills/internal-docs-for-srs/SKILL.md`. Use this reference for source, configuration, packaging, and verification navigation.
- Exclude `platform/vendor/`, `test/vendor/`, generated `ui/build/`, compiled binaries, runtime data under `platform/containers/data/`, and symlinked runtime directories such as `platform/record`, `platform/upload`, and `platform/vlive` unless the task explicitly concerns generated or persistent state.

## Architecture and Startup

Oryx is a single-node application assembled from a Go platform backend, a React dashboard, SRS, Redis, NGINX, FFmpeg, and helper tools.

The release-container startup path is:

1. Root `Dockerfile` builds and installs the Go platform, React UI, release service, bootstraps, and bundled SRS tree.
2. `platform/bootstrap` starts Redis through `platform/auto/start_redis`, starts SRS through `platform/auto/start_srs`, then launches the Go `platform` binary.
3. `platform/main.go` loads environment and persistent configuration, initializes Redis and global state, starts feature workers, and runs the HTTP/HTTPS service.
4. `platform/service.go` registers the core management, authentication, proxy, static UI, HTTPS, stream, and feature HTTP handlers.

Use `mgmt/bootstrap` only for the outer host-managed deployment that starts and monitors an Oryx Docker container. It is not the in-container platform entrypoint.

## Go Platform Backend

The trusted backend scope is the selected files under `~/git/oryx/platform/`. List filenames in this directory first, then choose the smallest files below. Do not search the whole directory by default.

### Core Lifecycle and Shared Infrastructure

- `main.go` — Process entrypoint, signal handling, environment defaults, Redis and persistent-state initialization, worker lifecycle, and HTTP-service startup.
- `service.go` — Core HTTP/HTTPS servers and management endpoints: initialization, login and tokens, health and versions, OpenAI settings, limits, HLS modes, certificates, stream listing and kickoff, SRS API/media proxying, and UI/static serving.
- `utils.go` — Shared configuration and environment accessors, Redis client, HTTP and URL helpers, media and HLS artifact models, FFprobe models, FFmpeg process helpers, file handling, and common task utilities.
- `srs-hooks.go` — SRS callback endpoints, publish-secret verification, stream and HLS events, and generated SRS/NGINX configuration interactions.
- `callback.go` — User-configured outbound callbacks for stream, recording, and OCR events.
- `cert.go` — Self-signed and Let's Encrypt certificate management, certificate reload, and SSL file/config updates.
- `candidate.go` — WebRTC candidate hostname and address resolution.
- `crontab.go` — Periodic maintenance, version refresh, cache refresh, and certificate renewal scheduling.
- `fastcache.go` — Frequently polled HLS mode state cached from Redis.
- `openai.go` — Model-specific OpenAI request capability helpers.
- `report.go` — Current release-version reporting values.
- `srs-errors.go` — Oryx error-code definitions.
- `version.go` — Platform version constant; inspect release and changelog workflows before changing it.

### Media and AI Features

- `dvr-local-disk.go` — Current local recording workflow, recording rules and filters, HLS collection, MP4 post-processing, artifact listing, and task lifecycle.
- `dvr-tencent-cos.go` — Legacy Tencent COS recording integration.
- `dvr-tencent-vod.go` — Legacy Tencent VoD recording integration.
- `forward.go` — Managed FFmpeg restreaming configuration and tasks for external platforms.
- `virtual-live-stream.go` — Virtual-live source selection, upload/server/URL inputs, FFmpeg loop publishing, limits, and task lifecycle.
- `camera-live-stream.go` — RTSP or other camera-source probing and FFmpeg forwarding, including optional generated audio.
- `trancode.go` — Live FFmpeg transcoding configuration and tasks. Preserve the repository's historical filename spelling when routing changes.
- `transcript.go` — Live transcription queues and tasks, ASR, subtitle fixing, overlay/WebVTT HLS generation, configuration, and status endpoints.
- `ocr.go` — Live-stream image extraction, AI recognition, callback and cleanup queues, configuration, tasks, and status endpoints.
- `live-room.go` — Live-room persistence, tokens, assistant settings, and room CRUD/publish endpoints.
- `ai-talk.go` — Voice/text assistant stages, users, subscribers, ASR, chat, TTS, conversation lifecycle, and HTTP endpoints.
- `dubbing.go` — Video-dubbing projects and tasks, ASR segments, translation, TTS, rephrasing, merging, playback, and export.

Pair backend changes with `service.go`, `utils.go`, or `srs-hooks.go` only when the feature actually crosses those shared boundaries. Do not read all shared files preemptively.

## React Dashboard

The trusted dashboard scope is `~/git/oryx/ui/src/`. Select the smallest page and its directly imported components.

### Application Shell

- `App.js` — Browser routing, authentication gate, environment loading, and top-level layouts.
- `pages/Navigator.js` — Main navigation.
- `pages/Scenario.js` — Scenario-tab router.
- `pages/Settings.js` — System pages for HLS modes, OpenAPI, callbacks, streams, OpenAI, limits, authentication, and HTTPS.
- `pages/Login.js`, `pages/Logout.js`, and `pages/Setup.js` — Administrator initialization and authentication UI.
- `utils.js` — UI token, locale, API-error, and common browser helpers.
- `i18n.js` and `resources/locale.json` — Internationalization initialization and translations. Update both language variants represented in the locale data when adding user-visible strings.

### Scenario Pages

- `pages/ScenarioLive.js` and `pages/ScenarioSrt.js` — Publishing and playback instructions and URLs.
- `pages/ScenarioForward.js` — Restreaming UI.
- `pages/ScenarioRecord.js` — Current local recording UI.
- `pages/ScenarioRecordCos.js` and `pages/ScenarioRecordVod.js` — Legacy Tencent recording UIs.
- `pages/ScenarioVLive.js` — Virtual-live UI.
- `pages/ScenarioCamera.js` — Camera streaming UI.
- `pages/ScenarioTranscode.js` — Live transcoding UI.
- `pages/ScenarioTranscript.js` — Transcription UI.
- `pages/ScenarioLiveRoom.js` — Live-room and assistant configuration UI.
- `pages/ScenarioDubbing.js` — Dubbing project, task, segment editor, and export UI.
- `pages/ScenarioOCR.js` — OCR configuration and status UI.
- `pages/ScenarioTutorials.js`, `pages/ScenarioOthers.js`, and `pages/ScenarioSource.js` — Tutorials, secondary/deprecated scenarios, and source guidance.
- `pages/Popouts.js` — Standalone/popout assistant and live-room surfaces.

### Shared Components

- `components/UrlGenerator.js` — Dashboard publish/play URL construction from environment and secret data.
- `components/VideoSourceSelector.js` and `components/FileUploader.js` — Shared upload, server-file, online-download, and stream-URL source selection.
- `components/AITalk.js` and `components/AIDictation.js` — Browser audio/text assistant clients and trace/status UI.
- `components/OpenAISettings.js` — Shared OpenAI service configuration fields.
- `components/DvrStatus.js` — Recording and legacy DVR status hooks.
- `components/LanguageSwitch.js` — Locale selection.
- `components/SrsErrorBoundary.js` — API and rendering error presentation.
- `components/SrsQRCode.js`, `components/SetupCamSecret.js`, and `components/SrsEnvContext.js` — Shared QR, publish-secret, and environment state.

## Runtime, Packaging, and Deployment

- Root `Makefile` — Builds the backend, tests, release service, and UI; installs the assembled tree under `/usr/local/oryx` by default.
- Root `Dockerfile` — Multi-stage Oryx image build, SRS/FFmpeg/UI integration, UPX compression, youtube-dl packaging, exposed ports, `/data` link, and final entrypoint.
- `platform/Makefile` and `ui/Makefile` — Backend binary and bilingual dashboard builds.
- `platform/bootstrap` and `platform/auto/` — In-container lifecycle for Redis, SRS, the Go backend, shutdown, and Redis persistence.
- `platform/containers/conf/` — Bundled Redis, SRS, NGINX, Prometheus, and development/release templates.
- `platform/containers/www/` — Bundled player, console, and tool pages. Treat copied third-party or SRS web assets as vendored unless the task specifically owns them.
- `mgmt/bootstrap` — Host-side Docker wrapper used by installed deployments.
- `usr/lib/systemd/system/oryx.service` — Systemd unit for installer deployments.
- `scripts/setup-ubuntu/` — Generic Ubuntu package build, installation, uninstallation, and init integration.
- `scripts/setup-aapanel/` and `scripts/setup-bt/` — aaPanel and Baota plugin packaging, Python control layer, installer, and web UI.
- `scripts/setup-droplet/` and `scripts/setup-lighthouse/` — Cloud image provisioning.
- `scripts/lightsail.sh` — AWS Lightsail bootstrap.
- `scripts/nginx-hls-cdn/` — Optional HLS CDN images and NGINX templates.
- `focal/Dockerfile` and `auto/focal.sh` — Oryx Focal base-image build and publication.

For a packaging change, follow the artifact from its source through the root `Makefile`, root `Dockerfile` or installer, and the exact consuming bootstrap. Do not update every deployment variant unless the requested behavior is shared by all of them.

## Release Service and Automation

- `releases/main.go`, `releases/version.go`, and `releases/releases.js` — Small version-discovery service and generated release values.
- `scripts/version.sh` — Reads the platform version used by packaging and workflows.
- `.github/workflows/pullrequest.yml` — Primary build, unit, image, installer, and integration verification for pull requests.
- `.github/workflows/test.yml` and `.github/workflows/test-online.yml` — Development-image and disposable online-environment tests.
- `.github/workflows/release.yml` and `.github/workflows/api-release.yml` — Oryx image, installer/plugin, release, and version-service publication.
- `.github/workflows/focal.yml`, `.github/workflows/nginx-hls-cdn.yml`, and `.github/workflows/mirrors.yml` — Base image, HLS CDN, and mirror publication.

Treat publication, cloud provisioning, DNS, certificate issuance, registry login, and release creation as external side effects. Do not run them during ordinary verification.

## Tests and Verification

### Test Ownership

- `platform/utils_test.go` — Focused Go unit tests for shared URL and FFmpeg-log helpers.
- `ui/src/**/*.test.js` — React component tests. Add the test beside the changed component when possible.
- `test/main_test.go` — Integration harness, endpoints, secrets, service initialization, FFmpeg/ffprobe discovery, media fixture, and common helpers.
- `test/system_test.go` — Readiness, versions, login, environment, candidate selection, secrets, and stream kickoff.
- `test/api_test.go` — Management APIs, HTTPS, HLS modes, publish secret, SRS API proxy, authentication, and CORS.
- `test/media_test.go` — RTMP/SRT publishing and HLS/HTTP-FLV playback.
- `test/scenario_test.go` — Virtual live, recording, post-processing, forwarding, transcoding, and callbacks.
- `test/camera_test.go` — Camera URL, video-only audio generation, and duration behavior.
- `test/liveroom_test.go` — Live-room CRUD, publishing, and authorization.
- `test/openai_test.go` — Transcription and live-room assistant behavior; some cases require configured external OpenAI services.

### Smallest Verification

Always start with static checks in the changed repository:

```bash
git -C ~/git/oryx diff --check
git -C ~/git/oryx diff --stat
```

Choose only the relevant next step:

- Go platform change: `(cd ~/git/oryx/platform && go test -mod=vendor ./...)`
- React component or page change: `(cd ~/git/oryx/ui && npm test -- --runInBand)` after dependencies are available; also run `npm run lint` for JavaScript changes.
- Release-service change: `(cd ~/git/oryx/releases && go test ./...)`
- Shell change: `bash -n <changed-script>` for each changed Bash script.
- Installer Python change: run a syntax check for the changed Python files, then use only the matching disposable installer workflow when integration verification is required.
- Docker or assembled-runtime change: build the Oryx image, then run the smallest matching `test/` case against a disposable container.

The root CI-equivalent build is `make -j && make test -j` from `~/git/oryx`, but it is broader and requires Node dependencies, Go dependencies, FFmpeg/ffprobe, and the media fixture expected by the test harness. Use it only after focused checks or for release-level changes.

Integration tests can initialize passwords, write persistent `/data`, start media publishers, replace containers, request real certificates, call OpenAI, or contact cloud services depending on flags. Run them only against an explicitly disposable target, redact all secrets, and select a specific test with `-run` before widening scope. Use `DEVELOPER.md` and `.github/workflows/pullrequest.yml` for the version-matched command and required setup.

## Change Routing

Choose the smallest source, UI, and test slice:

| Change | Primary backend | Primary UI | Verification |
|---|---|---|---|
| Startup, environment, persistence, lifecycle | `main.go`, then `utils.go` only if shared config changes | `App.js` or `Settings.js` only when exposed | `platform/utils_test.go`, `test/system_test.go` |
| Authentication, management API, HTTPS, streams | `service.go`, `cert.go` as needed | `Settings.js`, login/setup pages | `test/api_test.go`, `test/system_test.go` |
| Publish secret, SRS hooks, HLS events | `srs-hooks.go` | `ScenarioLive.js`, `SetupCamSecret.js`, `UrlGenerator.js` | `test/api_test.go`, `test/media_test.go` |
| Recording | `dvr-local-disk.go` | `ScenarioRecord.js`, `DvrStatus.js` | recording cases in `test/scenario_test.go` |
| Restreaming | `forward.go` | `ScenarioForward.js` | forwarding cases in `test/scenario_test.go` |
| Virtual live | `virtual-live-stream.go` | `ScenarioVLive.js`, source-selector components | virtual-live cases in `test/scenario_test.go` |
| Camera ingest | `camera-live-stream.go` | `ScenarioCamera.js` | `test/camera_test.go` |
| Transcoding | `trancode.go` | `ScenarioTranscode.js` | transcoding cases in `test/scenario_test.go` |
| Transcription | `transcript.go` | `ScenarioTranscript.js` | transcription cases in `test/openai_test.go` |
| Live rooms and voice assistant | `live-room.go`, then `ai-talk.go` when conversation processing changes | `ScenarioLiveRoom.js`, `AITalk.js` or `AIDictation.js` | `test/liveroom_test.go`, focused assistant cases in `test/openai_test.go` |
| Dubbing | `dubbing.go` | `ScenarioDubbing.js` | add focused backend/UI tests; no dedicated black-box file exists in the current map |
| OCR | `ocr.go`, plus `callback.go` only for outbound delivery | `ScenarioOCR.js` | add focused backend/UI tests; callback coverage may use `test/scenario_test.go` |
| Dashboard URL generation | backend environment endpoint only if required | `UrlGenerator.js` and consuming scenario page | colocated React test plus relevant media/system case |
| Docker/runtime assembly | root `Dockerfile`, root `Makefile`, exact bootstrap/config | none unless behavior is visible | image build plus focused `test/` case |
| Installer or cloud image | exact `scripts/setup-*` directory or `focal/` | plugin-local UI only | syntax checks plus matching disposable CI workflow |
| Version or release | `platform/version.go`, `releases/`, `scripts/version.sh`, exact workflow | none | release Go tests and static workflow inspection |

If an Oryx task reveals a defect in the underlying standalone SRS server, stop at the product boundary and add only the responsible SRS map from `SKILL.md`; do not search SRS broadly from this reference.
