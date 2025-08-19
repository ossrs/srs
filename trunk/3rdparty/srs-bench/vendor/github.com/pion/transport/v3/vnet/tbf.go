// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package vnet

import (
	"math"
	"sync"
	"time"

	"github.com/pion/logging"
)

const (
	// Bit is a single bit
	Bit = 1
	// KBit is a kilobit
	KBit = 1000 * Bit
	// MBit is a Megabit
	MBit = 1000 * KBit
)

// TokenBucketFilter implements a token bucket rate limit algorithm.
type TokenBucketFilter struct {
	NIC
	currentTokensInBucket float64
	c                     chan Chunk
	queue                 *chunkQueue
	queueSize             int // in bytes

	mutex             sync.Mutex
	rate              int
	maxBurst          int
	minRefillDuration time.Duration

	wg   sync.WaitGroup
	done chan struct{}

	log logging.LeveledLogger
}

// TBFOption is the option type to configure a TokenBucketFilter
type TBFOption func(*TokenBucketFilter) TBFOption

// TBFQueueSizeInBytes sets the max number of bytes waiting in the queue. Can
// only be set in constructor before using the TBF.
func TBFQueueSizeInBytes(bytes int) TBFOption {
	return func(t *TokenBucketFilter) TBFOption {
		prev := t.queueSize
		t.queueSize = bytes
		return TBFQueueSizeInBytes(prev)
	}
}

// TBFRate sets the bit rate of a TokenBucketFilter
func TBFRate(rate int) TBFOption {
	return func(t *TokenBucketFilter) TBFOption {
		t.mutex.Lock()
		defer t.mutex.Unlock()
		previous := t.rate
		t.rate = rate
		return TBFRate(previous)
	}
}

// TBFMaxBurst sets the bucket size of the token bucket filter. This is the
// maximum size that can instantly leave the filter, if the bucket is full.
func TBFMaxBurst(size int) TBFOption {
	return func(t *TokenBucketFilter) TBFOption {
		t.mutex.Lock()
		defer t.mutex.Unlock()
		previous := t.maxBurst
		t.maxBurst = size
		return TBFMaxBurst(previous)
	}
}

// Set updates a setting on the token bucket filter
func (t *TokenBucketFilter) Set(opts ...TBFOption) (previous TBFOption) {
	for _, opt := range opts {
		previous = opt(t)
	}
	return previous
}

// NewTokenBucketFilter creates and starts a new TokenBucketFilter
func NewTokenBucketFilter(n NIC, opts ...TBFOption) (*TokenBucketFilter, error) {
	tbf := &TokenBucketFilter{
		NIC:                   n,
		currentTokensInBucket: 0,
		c:                     make(chan Chunk),
		queue:                 nil,
		queueSize:             50000,
		mutex:                 sync.Mutex{},
		rate:                  1 * MBit,
		maxBurst:              8 * KBit,
		minRefillDuration:     100 * time.Millisecond,
		wg:                    sync.WaitGroup{},
		done:                  make(chan struct{}),
		log:                   logging.NewDefaultLoggerFactory().NewLogger("tbf"),
	}
	tbf.Set(opts...)
	tbf.queue = newChunkQueue(0, tbf.queueSize)
	tbf.wg.Add(1)
	go tbf.run()
	return tbf, nil
}

func (t *TokenBucketFilter) onInboundChunk(c Chunk) {
	t.c <- c
}

func (t *TokenBucketFilter) run() {
	defer t.wg.Done()

	t.refillTokens(t.minRefillDuration)
	lastRefill := time.Now()

	for {
		select {
		case <-t.done:
			t.drainQueue()
			return
		case chunk := <-t.c:
			if time.Since(lastRefill) > t.minRefillDuration {
				t.refillTokens(time.Since(lastRefill))
				lastRefill = time.Now()
			}
			t.queue.push(chunk)
			t.drainQueue()
		}
	}
}

func (t *TokenBucketFilter) refillTokens(dt time.Duration) {
	m := 1000.0 / float64(dt.Milliseconds())
	add := (float64(t.rate) / m) / 8.0
	t.mutex.Lock()
	defer t.mutex.Unlock()
	t.currentTokensInBucket = math.Min(float64(t.maxBurst), t.currentTokensInBucket+add)
	t.log.Tracef("add=(%v / %v) / 8 = %v, currentTokensInBucket=%v, maxBurst=%v", t.rate, m, add, t.currentTokensInBucket, t.maxBurst)
}

func (t *TokenBucketFilter) drainQueue() {
	for {
		next := t.queue.peek()
		if next == nil {
			break
		}
		tokens := float64(len(next.UserData()))
		if t.currentTokensInBucket < tokens {
			t.log.Tracef("currentTokensInBucket=%v, tokens=%v, stop drain", t.currentTokensInBucket, tokens)
			break
		}
		t.log.Tracef("currentTokensInBucket=%v, tokens=%v, pop chunk", t.currentTokensInBucket, tokens)
		t.queue.pop()
		t.NIC.onInboundChunk(next)
		t.currentTokensInBucket -= tokens
	}
}

// Close closes and stops the token bucket filter queue
func (t *TokenBucketFilter) Close() error {
	close(t.done)
	t.wg.Wait()
	return nil
}
