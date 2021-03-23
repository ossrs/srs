// Package closer provides signaling channel for shutdown
package closer

import (
	"context"
)

// Closer allows for each signaling a channel for shutdown
type Closer struct {
	ctx       context.Context
	closeFunc func()
}

// NewCloser creates a new instance of Closer
func NewCloser() *Closer {
	ctx, closeFunc := context.WithCancel(context.Background())
	return &Closer{
		ctx:       ctx,
		closeFunc: closeFunc,
	}
}

// NewCloserWithParent creates a new instance of Closer with a parent context
func NewCloserWithParent(ctx context.Context) *Closer {
	ctx, closeFunc := context.WithCancel(ctx)
	return &Closer{
		ctx:       ctx,
		closeFunc: closeFunc,
	}
}

// Done returns a channel signaling when it is done
func (c *Closer) Done() <-chan struct{} {
	return c.ctx.Done()
}

// Err returns an error of the context
func (c *Closer) Err() error {
	return c.ctx.Err()
}

// Close sends a signal to trigger the ctx done channel
func (c *Closer) Close() {
	c.closeFunc()
}
