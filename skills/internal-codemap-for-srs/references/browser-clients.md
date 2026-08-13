# Browser Publisher and Player Code Map

Use this reference after `skills/internal-codemap-for-srs/SKILL.md` routes a task to browser publishers and players. The listed module directory defines the trusted navigation scope for WHIP, WHEP, HTTP-FLV, and HLS browser clients.

## Browser Client Code

`trunk/research/players/` — Browser research and demonstration clients for publishing and playback.

- `whip.html` — WHIP publisher page: media capture, publishing controls, codec selection, bearer token, session display, and teardown.
- `whep.html` — WHEP player page: playback controls, audio/video selection, codec selection, bearer token, session display, and teardown.
- `rtc_publisher.html` and `rtc_player.html` — Older WebRTC publisher and player pages that translate the stream URL to WHIP or WHEP and use the shared SDK.
- `srs_player.html` — HTML5 live player for HTTP-FLV and HLS; also handles HTTP-TS, MPEG-DASH, MP4, AAC, and MP3 playback.
- `js/srs.sdk.js` — Shared WebRTC SDK implementing WHIP publishing and WHEP playback, including SDP exchange, media tracks, authorization, session resources, and teardown.
- `js/srs.page.js` — Shared page initialization and default URL construction for WHIP, WHEP, HTTP-FLV, and the browser demos.
- `js/srs.utility.js`, `js/winlin.utility.js`, and `js/srs.log.js` — Shared URL parsing, query-string, utility, and logging helpers used by the pages.
- `js/mpegts-1.7.3.min.js` and `js/hls-1.4.14.min.js` — Bundled third-party playback runtimes used by the HTTP-FLV and HLS player. Inspect these vendored files only when the task specifically concerns the dependency itself.

For other pages or assets in this module, list filenames only within `trunk/research/players/`, choose the smallest relevant set, then read or search only those files.

## Boundaries

- Use this map for browser-side publisher and player behavior.
- Use `references/cpp-server.md` for the corresponding server-side WebRTC, HTTP-FLV, or HLS implementation.
- Use `references/go-server.md` when the browser client is exercising the next-generation Go proxy.
- Add `references/testing.md` when reproduction or verification requires server, protocol, E2E, or benchmark tests.
