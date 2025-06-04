// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package nack

import (
	"time"

	"github.com/pion/interceptor"
	"github.com/pion/logging"
)

// GeneratorOption can be used to configure GeneratorInterceptor
type GeneratorOption func(r *GeneratorInterceptor) error

// GeneratorSize sets the size of the interceptor.
// Size must be one of: 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768
func GeneratorSize(size uint16) GeneratorOption {
	return func(r *GeneratorInterceptor) error {
		r.size = size
		return nil
	}
}

// GeneratorSkipLastN sets the number of packets (n-1 packets before the last received packets) to ignore when generating
// nack requests.
func GeneratorSkipLastN(skipLastN uint16) GeneratorOption {
	return func(r *GeneratorInterceptor) error {
		r.skipLastN = skipLastN
		return nil
	}
}

// GeneratorMaxNacksPerPacket sets the maximum number of NACKs sent per missing packet, e.g. if set to 2, a missing
// packet will only be NACKed at most twice. If set to 0 (default), max number of NACKs is unlimited
func GeneratorMaxNacksPerPacket(maxNacks uint16) GeneratorOption {
	return func(r *GeneratorInterceptor) error {
		r.maxNacksPerPacket = maxNacks
		return nil
	}
}

// GeneratorLog sets a logger for the interceptor
func GeneratorLog(log logging.LeveledLogger) GeneratorOption {
	return func(r *GeneratorInterceptor) error {
		r.log = log
		return nil
	}
}

// GeneratorInterval sets the nack send interval for the interceptor
func GeneratorInterval(interval time.Duration) GeneratorOption {
	return func(r *GeneratorInterceptor) error {
		r.interval = interval
		return nil
	}
}

// GeneratorStreamsFilter sets filter for generator streams
func GeneratorStreamsFilter(filter func(info *interceptor.StreamInfo) bool) GeneratorOption {
	return func(r *GeneratorInterceptor) error {
		r.streamsFilter = filter
		return nil
	}
}
