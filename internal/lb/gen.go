// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package lb

//go:generate go tool counterfeiter -o lbfakes/fake_origin_load_balancer.go . OriginLoadBalancer
//go:generate go tool counterfeiter -o lbfakes/fake_origin_service.go . OriginService
//go:generate go tool counterfeiter -o lbfakes/fake_hls_service.go . HLSService
//go:generate go tool counterfeiter -o lbfakes/fake_rtc_service.go . RTCService
