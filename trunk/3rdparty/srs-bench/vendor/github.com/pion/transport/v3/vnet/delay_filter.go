// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package vnet

import (
	"context"
	"time"
)

// DelayFilter delays outgoing packets by the given delay. Run must be called
// before any packets will be forwarded.
type DelayFilter struct {
	NIC
	delay time.Duration
	push  chan struct{}
	queue *chunkQueue
}

type timedChunk struct {
	Chunk
	deadline time.Time
}

// NewDelayFilter creates a new DelayFilter with the given nic and delay.
func NewDelayFilter(nic NIC, delay time.Duration) (*DelayFilter, error) {
	return &DelayFilter{
		NIC:   nic,
		delay: delay,
		push:  make(chan struct{}),
		queue: newChunkQueue(0, 0),
	}, nil
}

func (f *DelayFilter) onInboundChunk(c Chunk) {
	f.queue.push(timedChunk{
		Chunk:    c,
		deadline: time.Now().Add(f.delay),
	})
	f.push <- struct{}{}
}

// Run starts forwarding of packets. Packets will be forwarded if they spent
// >delay time in the internal queue. Must be called before any packet will be
// forwarded.
func (f *DelayFilter) Run(ctx context.Context) {
	timer := time.NewTimer(0)
	for {
		select {
		case <-ctx.Done():
			return
		case <-f.push:
			next := f.queue.peek().(timedChunk) //nolint:forcetypeassert
			if !timer.Stop() {
				<-timer.C
			}
			timer.Reset(time.Until(next.deadline))
		case now := <-timer.C:
			next := f.queue.peek()
			if next == nil {
				timer.Reset(time.Minute)
				continue
			}
			if n, ok := next.(timedChunk); ok && n.deadline.Before(now) {
				f.queue.pop() // ignore result because we already got and casted it from peek
				f.NIC.onInboundChunk(n.Chunk)
			}
			next = f.queue.peek()
			if next == nil {
				timer.Reset(time.Minute)
				continue
			}
			if n, ok := next.(timedChunk); ok {
				timer.Reset(time.Until(n.deadline))
			}
		}
	}
}
