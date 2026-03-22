// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package lb

import (
	"fmt"
	"os"
	"time"

	"srsx/internal/env"
	"srsx/internal/logger"
)

// NewDefaultSRSForDebugging initialize the default SRS media server, for debugging only.
func NewDefaultSRSForDebugging(environment env.Environment) (*SRSServer, error) {
	if environment.DefaultBackendEnabled() != "on" {
		return nil, nil
	}

	if environment.DefaultBackendIP() == "" {
		return nil, fmt.Errorf("empty default backend ip")
	}
	if environment.DefaultBackendRTMP() == "" {
		return nil, fmt.Errorf("empty default backend rtmp")
	}

	server := NewSRSServer(func(srs *SRSServer) {
		srs.IP = environment.DefaultBackendIP()
		srs.RTMP = []string{environment.DefaultBackendRTMP()}
		srs.ServerID = fmt.Sprintf("default-%v", logger.GenerateContextID())
		srs.ServiceID = logger.GenerateContextID()
		srs.PID = fmt.Sprintf("%v", os.Getpid())
		srs.UpdatedAt = time.Now()
	})

	if environment.DefaultBackendHttp() != "" {
		server.HTTP = []string{environment.DefaultBackendHttp()}
	}
	if environment.DefaultBackendAPI() != "" {
		server.API = []string{environment.DefaultBackendAPI()}
	}
	if environment.DefaultBackendRTC() != "" {
		server.RTC = []string{environment.DefaultBackendRTC()}
	}
	if environment.DefaultBackendSRT() != "" {
		server.SRT = []string{environment.DefaultBackendSRT()}
	}
	return server, nil
}
