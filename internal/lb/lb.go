// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package lb

import (
	"context"
	"fmt"
	"strings"
	"time"
)

const HLSAliveDuration = 120 * time.Second

// DefaultRTCSessionTTL is how long a WebRTC session lives after the client's last packet, unless
// PROXY_RTC_SESSION_TTL sets it.
const DefaultRTCSessionTTL = 120 * time.Second

func parseOriginServerTTL(value string) (time.Duration, error) {
	return parseTTL("PROXY_ORIGIN_SERVER_TTL", value, 300*time.Second)
}

// ParseRTCSessionTTL parses PROXY_RTC_SESSION_TTL, a positive Go duration, DefaultRTCSessionTTL when empty.
func ParseRTCSessionTTL(value string) (time.Duration, error) {
	return parseTTL("PROXY_RTC_SESSION_TTL", value, DefaultRTCSessionTTL)
}

func parseTTL(name, value string, defaultTTL time.Duration) (time.Duration, error) {
	if value == "" {
		return defaultTTL, nil
	}

	ttl, err := time.ParseDuration(value)
	if err != nil {
		return 0, fmt.Errorf("invalid %v %q: %w", name, value, err)
	}
	if ttl <= 0 {
		return 0, fmt.Errorf("invalid %v %q: must be positive", name, value)
	}
	return ttl, nil
}

type OriginServer struct {
	IP string `json:"ip,omitempty"`
	// Configured by the user in SRS.
	DeviceID string `json:"device_id,omitempty"`
	// SRS stores it in a file, so it may stay the same after a restart.
	ServerID string `json:"server_id,omitempty"`
	// SRS makes a new one on each restart.
	ServiceID string   `json:"service_id,omitempty"`
	PID       string   `json:"pid,omitempty"`
	RTMP      []string `json:"rtmp,omitempty"`
	// The HTTP stream endpoints, not the HTTP API.
	HTTP      []string  `json:"http,omitempty"`
	API       []string  `json:"api,omitempty"`
	SRT       []string  `json:"srt,omitempty"`
	RTC       []string  `json:"rtc,omitempty"`
	UpdatedAt time.Time `json:"update_at,omitempty"`
}

func (v *OriginServer) ID() string {
	return fmt.Sprintf("%v-%v-%v", v.ServerID, v.ServiceID, v.PID)
}

func (v *OriginServer) String() string {
	return fmt.Sprintf("%v", v)
}

func (v *OriginServer) Format(f fmt.State, c rune) {
	switch c {
	case 'v', 's':
		if f.Flag('+') {
			var sb strings.Builder
			sb.WriteString(fmt.Sprintf("pid=%v, server=%v, service=%v", v.PID, v.ServerID, v.ServiceID))
			if v.DeviceID != "" {
				sb.WriteString(fmt.Sprintf(", device=%v", v.DeviceID))
			}
			if len(v.RTMP) > 0 {
				sb.WriteString(fmt.Sprintf(", rtmp=[%v]", strings.Join(v.RTMP, ",")))
			}
			if len(v.HTTP) > 0 {
				sb.WriteString(fmt.Sprintf(", http=[%v]", strings.Join(v.HTTP, ",")))
			}
			if len(v.API) > 0 {
				sb.WriteString(fmt.Sprintf(", api=[%v]", strings.Join(v.API, ",")))
			}
			if len(v.SRT) > 0 {
				sb.WriteString(fmt.Sprintf(", srt=[%v]", strings.Join(v.SRT, ",")))
			}
			if len(v.RTC) > 0 {
				sb.WriteString(fmt.Sprintf(", rtc=[%v]", strings.Join(v.RTC, ",")))
			}
			sb.WriteString(fmt.Sprintf(", update=%v", v.UpdatedAt.Format("2006-01-02 15:04:05.999")))
			fmt.Fprintf(f, "SRS ip=%v, id=%v, %v", v.IP, v.ID(), sb.String())
		} else {
			fmt.Fprintf(f, "SRS ip=%v, id=%v", v.IP, v.ID())
		}
	default:
		fmt.Fprintf(f, "%v, fmt=%%%c", v, c)
	}
}

func NewOriginServer(opts ...func(*OriginServer)) *OriginServer {
	v := &OriginServer{}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

type HLSPlayStream interface {
	// The SRS Proxy Backend HLS ID.
	GetSPBHID() string
	Initialize(ctx context.Context) HLSPlayStream
}

type RTCConnection interface {
	GetUfrag() string
}

type OriginService interface {
	// Called for each registration or heartbeat of an origin server.
	Update(ctx context.Context, server *OriginServer) error
	Pick(ctx context.Context, streamURL string) (*OriginServer, error)
}

// Players fetch the M3U8 by stream URL and the TS files by SPBHID.
type HLSService interface {
	LoadOrStoreHLS(ctx context.Context, streamURL string, value HLSPlayStream) (HLSPlayStream, error)
	LoadHLSBySPBHID(ctx context.Context, spbhid string) (HLSPlayStream, error)
}

type RTCService interface {
	StoreWebRTC(ctx context.Context, streamURL string, value RTCConnection) error
	LoadWebRTCByUfrag(ctx context.Context, ufrag string) (RTCConnection, error)
	// Removes the entries that still hold value, so a newer session of the stream keeps its own.
	DeleteWebRTC(ctx context.Context, streamURL string, value RTCConnection) error
}

type OriginLoadBalancer interface {
	OriginService
	HLSService
	RTCService
	Initialize(ctx context.Context) error
}
