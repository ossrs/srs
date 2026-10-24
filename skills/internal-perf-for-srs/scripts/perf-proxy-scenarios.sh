#!/bin/bash
# The scenarios measured through the Go proxy, sourced by the scripts that
# take -target proxy. They are the protocols the proxy relays without
# conversion: RTMP, HTTP-FLV, HTTP-TS, HLS, SRT and WebRTC play, the RTMP
# and WHIP publishers, and many-streams. The proxy has no RTSP or DASH route,
# and a cross-protocol bridge costs the origin, not the proxy; WHEP play is
# whip-whep, since rtmp-whep makes the origin transcode AAC to Opus.
# perf-skill-test.sh checks this list against the registry.
proxy_scenarios=(rtmp-rtmp rtmp-flv rtmp-ts rtmp-hls srt-srt whip-whep rtmp-pub whip-pub many-streams)

# proxy_origins prints how many SRS origins a proxy scenario runs on by
# default: three for every scenario, so the proxy runs out before an origin.
proxy_origins() {
    echo 3
}

# is_proxy_scenario succeeds when the scenario is in the list.
is_proxy_scenario() {
    local s
    for s in "${proxy_scenarios[@]}"; do
        [[ "$s" == "$1" ]] && return 0
    done
    return 1
}
