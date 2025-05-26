// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package report

import (
	"time"

	"github.com/pion/logging"
)

// SenderOption can be used to configure SenderInterceptor.
type SenderOption func(r *SenderInterceptor) error

// SenderLog sets a logger for the interceptor.
func SenderLog(log logging.LeveledLogger) SenderOption {
	return func(r *SenderInterceptor) error {
		r.log = log
		return nil
	}
}

// SenderInterval sets send interval for the interceptor.
func SenderInterval(interval time.Duration) SenderOption {
	return func(r *SenderInterceptor) error {
		r.interval = interval
		return nil
	}
}

// SenderNow sets an alternative for the time.Now function.
func SenderNow(f func() time.Time) SenderOption {
	return func(r *SenderInterceptor) error {
		r.now = f
		return nil
	}
}

// SenderTicker sets an alternative for the time.NewTicker function.
func SenderTicker(f TickerFactory) SenderOption {
	return func(r *SenderInterceptor) error {
		r.newTicker = f
		return nil
	}
}

// SenderUseLatestPacket sets the interceptor to always use the latest packet, even
// if it appears to be out-of-order.
func SenderUseLatestPacket() SenderOption {
	return func(r *SenderInterceptor) error {
		r.useLatestPacket = true
		return nil
	}
}

// enableStartTracking is used by tests to synchronize whether the loop() has begun
// and it's safe to start sending ticks to the ticker.
func enableStartTracking(startedCh chan struct{}) SenderOption {
	return func(r *SenderInterceptor) error {
		r.started = startedCh
		return nil
	}
}
