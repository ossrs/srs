// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

import (
	"container/list"
	"sync"
)

// Operation is a function.
type operation func()

// Operations is a task executor.
type operations struct {
	mu     sync.Mutex
	busyCh chan struct{}
	ops    *list.List

	updateNegotiationNeededFlagOnEmptyChain *atomicBool
	onNegotiationNeeded                     func()
	isClosed                                bool
}

func newOperations(
	updateNegotiationNeededFlagOnEmptyChain *atomicBool,
	onNegotiationNeeded func(),
) *operations {
	return &operations{
		ops:                                     list.New(),
		updateNegotiationNeededFlagOnEmptyChain: updateNegotiationNeededFlagOnEmptyChain,
		onNegotiationNeeded:                     onNegotiationNeeded,
	}
}

// Enqueue adds a new action to be executed. If there are no actions scheduled,
// the execution will start immediately in a new goroutine. If the queue has been
// closed, the operation will be dropped. The queue is only deliberately closed
// by a user.
func (o *operations) Enqueue(op operation) {
	o.mu.Lock()
	defer o.mu.Unlock()
	_ = o.tryEnqueue(op)
}

// tryEnqueue attempts to enqueue the given operation. It returns false
// if the op is invalid or the queue is closed. mu must be locked by
// tryEnqueue's caller.
func (o *operations) tryEnqueue(op operation) bool {
	if op == nil {
		return false
	}

	if o.isClosed {
		return false
	}
	o.ops.PushBack(op)

	if o.busyCh == nil {
		o.busyCh = make(chan struct{})
		go o.start()
	}

	return true
}

// IsEmpty checks if there are tasks in the queue.
func (o *operations) IsEmpty() bool {
	o.mu.Lock()
	defer o.mu.Unlock()

	return o.ops.Len() == 0
}

// Done blocks until all currently enqueued operations are finished executing.
// For more complex synchronization, use Enqueue directly.
func (o *operations) Done() {
	var wg sync.WaitGroup
	wg.Add(1)
	o.mu.Lock()
	enqueued := o.tryEnqueue(func() {
		wg.Done()
	})
	o.mu.Unlock()
	if !enqueued {
		return
	}
	wg.Wait()
}

// GracefulClose waits for the operations queue to be cleared and forbids
// new operations from being enqueued.
func (o *operations) GracefulClose() {
	o.mu.Lock()
	if o.isClosed {
		o.mu.Unlock()

		return
	}
	// do not enqueue anymore ops from here on
	// o.isClosed=true will also not allow a new busyCh
	// to be created.
	o.isClosed = true

	busyCh := o.busyCh
	o.mu.Unlock()
	if busyCh == nil {
		return
	}
	<-busyCh
}

func (o *operations) pop() func() {
	o.mu.Lock()
	defer o.mu.Unlock()
	if o.ops.Len() == 0 {
		return nil
	}

	e := o.ops.Front()
	o.ops.Remove(e)
	if op, ok := e.Value.(operation); ok {
		return op
	}

	return nil
}

func (o *operations) start() {
	defer func() {
		o.mu.Lock()
		defer o.mu.Unlock()
		// this wil lbe the most recent busy chan
		close(o.busyCh)

		if o.ops.Len() == 0 || o.isClosed {
			o.busyCh = nil

			return
		}

		// either a new operation was enqueued while we
		// were busy, or an operation panicked
		o.busyCh = make(chan struct{})
		go o.start()
	}()

	fn := o.pop()
	for fn != nil {
		fn()
		fn = o.pop()
	}
	if !o.updateNegotiationNeededFlagOnEmptyChain.get() {
		return
	}
	o.updateNegotiationNeededFlagOnEmptyChain.set(false)
	o.onNegotiationNeeded()
}
