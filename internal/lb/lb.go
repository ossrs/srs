// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package lb

import (
	"context"
	"fmt"
	"strings"
	"time"
)

// If server heartbeat in this duration, it's alive.
const ServerAliveDuration = 300 * time.Second

// If HLS streaming update in this duration, it's alive.
const HLSAliveDuration = 120 * time.Second

// If WebRTC streaming update in this duration, it's alive.
const RTCAliveDuration = 120 * time.Second

// SRSServer represents a backend origin server.
type SRSServer struct {
	// The server IP.
	IP string `json:"ip,omitempty"`
	// The server device ID, configured by user.
	DeviceID string `json:"device_id,omitempty"`
	// The server id of SRS, store in file, may not change, mandatory.
	ServerID string `json:"server_id,omitempty"`
	// The service id of SRS, always change when restarted, mandatory.
	ServiceID string `json:"service_id,omitempty"`
	// The process id of SRS, always change when restarted, mandatory.
	PID string `json:"pid,omitempty"`
	// The RTMP listen endpoints.
	RTMP []string `json:"rtmp,omitempty"`
	// The HTTP Stream listen endpoints.
	HTTP []string `json:"http,omitempty"`
	// The HTTP API listen endpoints.
	API []string `json:"api,omitempty"`
	// The SRT server listen endpoints.
	SRT []string `json:"srt,omitempty"`
	// The RTC server listen endpoints.
	RTC []string `json:"rtc,omitempty"`
	// Last update time.
	UpdatedAt time.Time `json:"update_at,omitempty"`
}

func (v *SRSServer) ID() string {
	return fmt.Sprintf("%v-%v-%v", v.ServerID, v.ServiceID, v.PID)
}

func (v *SRSServer) String() string {
	return fmt.Sprintf("%v", v)
}

func (v *SRSServer) Format(f fmt.State, c rune) {
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

func NewSRSServer(opts ...func(*SRSServer)) *SRSServer {
	v := &SRSServer{}
	for _, opt := range opts {
		opt(v)
	}
	return v
}

// HLSPlayStream is the interface for HLS streaming sessions.
type HLSPlayStream interface {
	// GetSPBHID returns the SRS Proxy Backend HLS ID.
	GetSPBHID() string
	// Initialize initializes the HLS play stream with context.
	Initialize(ctx context.Context) HLSPlayStream
}

// RTCConnection is the interface for WebRTC streaming connections.
type RTCConnection interface {
	// GetUfrag returns the ICE username fragment.
	GetUfrag() string
}

// SRSLoadBalancer is the interface to load balance the SRS servers.
type SRSLoadBalancer interface {
	// Initialize the load balancer.
	Initialize(ctx context.Context) error
	// Update the backend server.
	Update(ctx context.Context, server *SRSServer) error
	// Pick a backend server for the specified stream URL.
	Pick(ctx context.Context, streamURL string) (*SRSServer, error)
	// Load or store the HLS streaming for the specified stream URL.
	LoadOrStoreHLS(ctx context.Context, streamURL string, value HLSPlayStream) (HLSPlayStream, error)
	// Load the HLS streaming by SPBHID, the SRS Proxy Backend HLS ID.
	LoadHLSBySPBHID(ctx context.Context, spbhid string) (HLSPlayStream, error)
	// Store the WebRTC streaming for the specified stream URL.
	StoreWebRTC(ctx context.Context, streamURL string, value RTCConnection) error
	// Load the WebRTC streaming by ufrag, the ICE username.
	LoadWebRTCByUfrag(ctx context.Context, ufrag string) (RTCConnection, error)
}

// SrsLoadBalancer is the global SRS load balancer instance.
var SrsLoadBalancer SRSLoadBalancer
