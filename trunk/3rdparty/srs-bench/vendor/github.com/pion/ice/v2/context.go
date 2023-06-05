// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import (
	"context"
	"time"
)

func (a *Agent) context() context.Context {
	return agentContext(a.done)
}

type agentContext chan struct{}

// Done implements context.Context
func (a agentContext) Done() <-chan struct{} {
	return (chan struct{})(a)
}

// Err implements context.Context
func (a agentContext) Err() error {
	select {
	case <-(chan struct{})(a):
		return ErrRunCanceled
	default:
		return nil
	}
}

// Deadline implements context.Context
func (a agentContext) Deadline() (deadline time.Time, ok bool) {
	return time.Time{}, false
}

// Value implements context.Context
func (a agentContext) Value(interface{}) interface{} {
	return nil
}
