// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package rfc8888

import "time"

// An Option is a function that can be used to configure a SenderInterceptor
type Option func(*SenderInterceptor) error

// SenderTicker sets an alternative for time.Ticker.
func SenderTicker(f TickerFactory) Option {
	return func(i *SenderInterceptor) error {
		i.newTicker = f
		return nil
	}
}

// SenderNow sets an alternative for the time.Now function.
func SenderNow(f func() time.Time) Option {
	return func(i *SenderInterceptor) error {
		i.now = f
		return nil
	}
}

// SendInterval sets the feedback send interval for the interceptor
func SendInterval(interval time.Duration) Option {
	return func(s *SenderInterceptor) error {
		s.interval = interval
		return nil
	}
}
