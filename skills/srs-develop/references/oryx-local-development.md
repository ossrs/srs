# Oryx Local Development on macOS

Use this reference when a maintainer wants to build, run, debug, and show Oryx changes locally on macOS without running Oryx itself in Docker.

## Scope

This workflow runs Oryx as separate local development processes:

1. Redis from Homebrew.
2. The SRS C++ media server built from the local SRS checkout.
3. The Oryx Go platform backend with `go run .`.
4. The Oryx React dashboard with `npm start`.

Do not use the Oryx Docker image for this workflow. Do not use `oryx/platform/bootstrap` for this workflow; that script is for the assembled runtime and assumes packaged paths such as `/usr/local/srs` and `/data`.

## Repository Layout

Assume the SRS repository is checked out at:

```bash
~/git/srs
```

Assume the Oryx checkout is available at the project-root-relative path:

```bash
~/git/srs/oryx
```

When acting from the AI skill, keep the current working directory unchanged and use paths beginning with `oryx/` or `git -C oryx/` according to the normal Oryx path rules.

## One-Time SRS Build

Build the local SRS server:

```bash
cd ~/git/srs/trunk && ./configure && make
```

After this, the local SRS binary should exist at:

```bash
~/git/srs/trunk/objs/srs
```

## Start Redis

Install Redis once if needed:

```bash
brew install redis
```

Start Redis:

```bash
brew services start redis
redis-cli ping
```

Expected result:

```text
PONG
```

Oryx defaults to Redis at `127.0.0.1:6379`, so no Redis environment variables are needed for the normal local workflow.

## Start Local SRS

Run SRS in its own terminal:

```bash
cd ~/git/srs/oryx/platform && ~/git/srs/trunk/objs/srs -c containers/conf/srs.release-local.conf
```

This local Oryx SRS configuration uses:

- RTMP: `1935`
- SRS HTTP API: `1985`
- HTTP media: `8080`
- WebRTC UDP: `8000`
- SRT UDP: `10080`

No `CANDIDATE` variable is needed for the normal localhost development loop. Only set `CANDIDATE` when intentionally testing WebRTC from another device, another host, or a non-localhost IP/domain.

## Start Oryx Go Backend

Run the Oryx platform backend in another terminal:

```bash
cd ~/git/srs/oryx/platform && go run .
```

The backend listens on the default local ports:

- Oryx management/API HTTP: `2022`
- Platform HTTP: `2024`
- HTTPS: `2443`

For ordinary local debugging, if self-signed certificate generation is not relevant to the change, it is acceptable to run:

```bash
cd ~/git/srs/oryx/platform && AUTO_SELF_SIGNED_CERTIFICATE=off go run .
```

## Start React Dashboard

Install UI dependencies once or when `package.json` changes:

```bash
cd ~/git/srs/oryx/ui && npm install
```

Run the React development server in another terminal:

```bash
cd ~/git/srs/oryx/ui && npm start
```

Open:

```text
http://localhost:3000
```

The React development server proxies Oryx API, SRS API, RTC, player, tool, and media paths to `http://127.0.0.1:2022`, so the browser should use the React dev server at `localhost:3000` during UI development.

## Local Change Loop

Use this loop to show the result of code changes quickly:

- React dashboard change under `oryx/ui/src/`: save the file and let `npm start` hot-reload the browser.
- Go backend change under `oryx/platform/`: stop and restart `go run .`.
- SRS C++ media server change under `trunk/`: rebuild SRS with `cd ~/git/srs/trunk && make`, then restart the local SRS process.
- Oryx configuration/template change under `oryx/platform/containers/conf/`: restart the affected SRS or Go backend process.

Keep Redis running across restarts unless the task specifically needs clean persistent state.

## Quick Health Checks

After all processes are running:

```bash
redis-cli ping
curl http://localhost:2022/terraform/v1/mgmt/versions
curl http://localhost:1985/api/v1/versions
```

## Local Verification Scripts

Every script below self-starts Redis (if unreachable), local SRS, and the Oryx Go backend using this document's same commands and ports, then stops only the processes it started — anything already running before the script was invoked (Redis included) is left alone. None of them start the React dashboard; none needs it.

- `scripts/oryx-api-smoke-test.sh` — Version (no-auth health check), password login, Bearer security-key authentication. Reads the mgmt password from `$MGMT_PASSWORD` or `oryx/platform/containers/data/config/.env`; only prints byte-lengths of tokens/secrets, never their values. Override the target with `ORYX_ENDPOINT` if the Go backend is not on the default `http://localhost:2022`.
- `scripts/oryx-live-streaming-test.sh` — End-to-end check of the "Live" scenario page (`?tab=live`): queries the publish secret from `/terraform/v1/hooks/srs/secret/query` (what the page's `useUrls()` hook calls), then publishes through RTMP, SRT, and WHIP in turn with that secret. For each protocol, confirms the stream shows up as actively published in the SRS HTTP API (`/api/v1/streams/`) and verifies playback via RTMP, HTTP-FLV, and HLS. SRT and WHIP need an ffmpeg built with `--enable-libsrt` and the `whip` muxer, which the default Homebrew formula lacks — the script resolves one from `PATH`, then `~/.local/bin`, then builds one via `scripts/setup-ffmpeg-with-whip.sh` (several minutes on first run, cached afterward).

When verifying a change that touches local Oryx development, or just confirming the local stack is healthy end to end, run every script in this list, in order:

```bash
bash skills/srs-develop/scripts/oryx-api-smoke-test.sh
bash skills/srs-develop/scripts/oryx-live-streaming-test.sh
```

Add new Oryx verification scripts to this same list as they're written, and keep them running in this same sequential order — do not let it fall out of sync with `scripts/`.

Open the dashboard:

```text
http://localhost:3000
```

If the dashboard cannot reach Oryx APIs, check that the Go backend is still running on `2022`. If Oryx cannot query SRS or media paths fail, check that local SRS is still running on `1985` and `8080`.
