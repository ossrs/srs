// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package nack

import "github.com/pion/logging"

// ResponderOption can be used to configure ResponderInterceptor
type ResponderOption func(s *ResponderInterceptor) error

// ResponderSize sets the size of the interceptor.
// Size must be one of: 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768
func ResponderSize(size uint16) ResponderOption {
	return func(r *ResponderInterceptor) error {
		r.size = size
		return nil
	}
}

// ResponderLog sets a logger for the interceptor
func ResponderLog(log logging.LeveledLogger) ResponderOption {
	return func(r *ResponderInterceptor) error {
		r.log = log
		return nil
	}
}

// DisableCopy bypasses copy of underlying packets. It should be used when
// you are not re-using underlying buffers of packets that have been written
func DisableCopy() ResponderOption {
	return func(s *ResponderInterceptor) error {
		s.packetFactory = &noOpPacketFactory{}
		return nil
	}
}
