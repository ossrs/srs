// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package proxy

//go:generate go tool counterfeiter -o proxyfakes/fake_rtmp_proxy_server.go . RTMPProxyServer
//go:generate go tool counterfeiter -o proxyfakes/fake_http_stream_proxy_server.go . HTTPStreamProxyServer
//go:generate go tool counterfeiter -o proxyfakes/fake_http_api_proxy_server.go . HTTPAPIProxyServer
//go:generate go tool counterfeiter -o proxyfakes/fake_web_rtc_proxy_server.go . WebRTCProxyServer
