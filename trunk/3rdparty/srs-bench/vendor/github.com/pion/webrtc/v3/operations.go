// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

import (
	"container/list"
	"sync"
)

// Operation is a function
type operation func()

// Operations is a task executor.
type operations struct {
	mu   sync.Mutex
	busy bool
	ops  *list.List
}

func newOperations() *operations {
	return &operations{
		ops: list.New(),
	}
}

// Enqueue adds a new action to be executed. If there are no actions scheduled,
// the execution will start immediately in a new goroutine.
func (o *operations) Enqueue(op operation) {
	if op == nil {
		return
	}

	o.mu.Lock()
	running := o.busy
	o.ops.PushBack(op)
	o.busy = true
	o.mu.Unlock()

	if !running {
		go o.start()
	}
}

// IsEmpty checks if there are tasks in the queue
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
	o.Enqueue(func() {
		wg.Done()
	})
	wg.Wait()
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
		if o.ops.Len() == 0 {
			o.busy = false
			return
		}
		// either a new operation was enqueued while we
		// were busy, or an operation panicked
		go o.start()
	}()

	fn := o.pop()
	for fn != nil {
		fn()
		fn = o.pop()
	}
}
