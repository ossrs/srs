// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"fmt"
	"math/rand"
	"os"
	"srs-proxy/logger"
	"strings"
	"time"

	"srs-proxy/sync"
)

type SRSServer struct {
	// The server IP.
	IP string
	// The server device ID, configured by user.
	DeviceID string
	// The server id of SRS, store in file, may not change, mandatory.
	ServerID string
	// The service id of SRS, always change when restarted, mandatory.
	ServiceID string
	// The process id of SRS, always change when restarted, mandatory.
	PID string
	// The RTMP listen endpoints.
	RTMP []string
	// The HTTP Stream listen endpoints.
	HTTP []string
	// The HTTP API listen endpoints.
	API []string
	// The SRT server listen endpoints.
	SRT []string
	// The RTC server listen endpoints.
	RTC []string
	// Last update time.
	UpdatedAt time.Time
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

type SRSLoadBalancer struct {
	// All available SRS servers, key is server ID.
	servers sync.Map[string, *SRSServer]
	// The picked server to servce client by specified stream URL, key is stream url.
	picked sync.Map[string, *SRSServer]
}

var srsLoadBalancer = &SRSLoadBalancer{}

func (v *SRSLoadBalancer) Initialize(ctx context.Context) {
	if envDefaultBackendIP() != "" && envDefaultBackendPort() != "" {
		server := NewSRSServer(func(srs *SRSServer) {
			srs.IP = envDefaultBackendIP()
			srs.RTMP = []string{envDefaultBackendPort()}
			srs.ServerID = fmt.Sprintf("default-%v", logger.GenerateContextID())
			srs.ServiceID = logger.GenerateContextID()
			srs.PID = fmt.Sprintf("%v", os.Getpid())
			srs.UpdatedAt = time.Now()
		})
		v.Update(server)
		logger.Df(ctx, "Initialize default SRS media server, %+v", server)
	}
}

func (v *SRSLoadBalancer) Update(server *SRSServer) {
	v.servers.Store(server.ID(), server)
}

func (v *SRSLoadBalancer) Pick(streamURL string) (*SRSServer, error) {
	// Always proxy to the same server for the same stream URL.
	if server, ok := v.picked.Load(streamURL); ok {
		return server, nil
	}

	// Gather all servers, alive in 60s ago.
	var servers []*SRSServer
	v.servers.Range(func(key string, server *SRSServer) bool {
		if time.Since(server.UpdatedAt) < 60*time.Second {
			servers = append(servers, server)
		}
		return true
	})

	// If no servers available, use all possible servers.
	if len(servers) == 0 {
		v.servers.Range(func(key string, server *SRSServer) bool {
			servers = append(servers, server)
			return true
		})
	}

	// No server found, failed.
	if len(servers) == 0 {
		return nil, fmt.Errorf("no server available for %v", streamURL)
	}

	// Pick a server randomly from servers.
	server := servers[rand.Intn(len(servers))]
	v.picked.Store(streamURL, server)
	return server, nil
}
