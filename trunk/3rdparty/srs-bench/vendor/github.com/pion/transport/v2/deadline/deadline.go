// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package deadline provides deadline timer used to implement
// net.Conn compatible connection
package deadline

import (
	"context"
	"sync"
	"time"
)

// Deadline signals updatable deadline timer.
// Also, it implements context.Context.
type Deadline struct {
	exceeded chan struct{}
	stop     chan struct{}
	stopped  chan bool
	deadline time.Time
	mu       sync.RWMutex
}

// New creates new deadline timer.
func New() *Deadline {
	d := &Deadline{
		exceeded: make(chan struct{}),
		stop:     make(chan struct{}),
		stopped:  make(chan bool, 1),
	}
	d.stopped <- true
	return d
}

// Set new deadline. Zero value means no deadline.
func (d *Deadline) Set(t time.Time) {
	d.mu.Lock()
	defer d.mu.Unlock()

	d.deadline = t

	close(d.stop)

	select {
	case <-d.exceeded:
		d.exceeded = make(chan struct{})
	default:
		stopped := <-d.stopped
		if !stopped {
			d.exceeded = make(chan struct{})
		}
	}
	d.stop = make(chan struct{})
	d.stopped = make(chan bool, 1)

	if t.IsZero() {
		d.stopped <- true
		return
	}

	if dur := time.Until(t); dur > 0 {
		exceeded := d.exceeded
		stopped := d.stopped
		go func() {
			timer := time.NewTimer(dur)
			select {
			case <-timer.C:
				close(exceeded)
				stopped <- false
			case <-d.stop:
				if !timer.Stop() {
					<-timer.C
				}
				stopped <- true
			}
		}()
		return
	}

	close(d.exceeded)
	d.stopped <- false
}

// Done receives deadline signal.
func (d *Deadline) Done() <-chan struct{} {
	d.mu.RLock()
	defer d.mu.RUnlock()
	return d.exceeded
}

// Err returns context.DeadlineExceeded if the deadline is exceeded.
// Otherwise, it returns nil.
func (d *Deadline) Err() error {
	d.mu.RLock()
	defer d.mu.RUnlock()
	select {
	case <-d.exceeded:
		return context.DeadlineExceeded
	default:
		return nil
	}
}

// Deadline returns current deadline.
func (d *Deadline) Deadline() (time.Time, bool) {
	d.mu.RLock()
	defer d.mu.RUnlock()
	if d.deadline.IsZero() {
		return d.deadline, false
	}
	return d.deadline, true
}

// Value returns nil.
func (d *Deadline) Value(interface{}) interface{} {
	return nil
}
