package nack

import (
	"time"

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
