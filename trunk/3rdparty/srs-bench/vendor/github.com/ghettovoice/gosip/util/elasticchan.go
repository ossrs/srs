// Forked from github.com/StefanKopieczek/gossip by @StefanKopieczek
package util

import (
	"fmt"
	"sync"

	"github.com/ghettovoice/gosip/log"
)

// The buffer size of the primitive input and output chans.
const c_ELASTIC_CHANSIZE = 3

// A dynamic channel that does not block on send, but has an unlimited buffer capacity.
// ElasticChan uses a dynamic slice to buffer signals received on the input channel until
// the output channel is ready to process them.
type ElasticChan struct {
	In      chan interface{}
	Out     chan interface{}
	buffer  []interface{}
	stopped bool
	done    chan struct{}

	log   log.Logger
	logMu sync.RWMutex
}

// Initialise the Elastic channel, and start the management goroutine.
func (c *ElasticChan) Init() {
	c.In = make(chan interface{}, c_ELASTIC_CHANSIZE)
	c.Out = make(chan interface{}, c_ELASTIC_CHANSIZE)
	c.buffer = make([]interface{}, 0)
	c.done = make(chan struct{})
}

func (c *ElasticChan) Run() {
	go c.manage()
}

func (c *ElasticChan) Stop() {
	select {
	case <-c.done:
		return
	default:
	}

	logger := c.Log()

	if logger != nil {
		logger.Trace("stopping elastic chan...")
	}

	close(c.In)
	<-c.done

	if logger != nil {
		logger.Trace("elastic chan stopped")
	}
}

func (c *ElasticChan) Log() log.Logger {
	c.logMu.RLock()
	defer c.logMu.RUnlock()

	return c.log

}

func (c *ElasticChan) SetLog(logger log.Logger) {
	c.logMu.Lock()

	c.log = logger.
		WithPrefix("util.ElasticChan").
		WithFields(log.Fields{
			"elastic_chan_ptr": fmt.Sprintf("%p", c),
		})

	c.logMu.Unlock()
}

// Poll for input from one end of the channel and add it to the buffer.
// Also poll sending buffered signals out over the output chan.
// TODO: add cancel chan
func (c *ElasticChan) manage() {
	defer close(c.done)

loop:
	for {
		logger := c.Log()

		if len(c.buffer) > 0 {
			// The buffer has something in it, so try to send as well as
			// receive.
			// (Receive first in order to minimize blocked Send() calls).
			select {
			case in, ok := <-c.In:
				if !ok {
					if logger != nil {
						logger.Trace("elastic chan will dispose")
					}

					break loop
				}
				c.Log().Tracef("ElasticChan %p gets '%v'", c, in)
				c.buffer = append(c.buffer, in)
			case c.Out <- c.buffer[0]:
				c.Log().Tracef("ElasticChan %p sends '%v'", c, c.buffer[0])
				c.buffer = c.buffer[1:]
			}
		} else {
			// The buffer is empty, so there's nothing to send.
			// Just wait to receive.
			in, ok := <-c.In
			if !ok {
				if logger != nil {
					logger.Trace("elastic chan will dispose")
				}

				break loop
			}
			c.Log().Tracef("ElasticChan %p gets '%v'", c, in)
			c.buffer = append(c.buffer, in)
		}
	}

	c.dispose()
}

func (c *ElasticChan) dispose() {
	logger := c.Log()

	if logger != nil {
		logger.Trace("elastic chan disposing...")
	}

	for len(c.buffer) > 0 {
		select {
		case c.Out <- c.buffer[0]:
			c.buffer = c.buffer[1:]
		default:
		}
	}

	if logger != nil {
		logger.Trace("elastic chan disposed")
	}
}
