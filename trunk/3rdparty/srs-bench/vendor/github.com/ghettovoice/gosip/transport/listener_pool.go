package transport

import (
	"crypto/tls"
	"errors"
	"fmt"
	"net"
	"strings"
	"sync"

	"github.com/ghettovoice/gosip/log"
)

type ListenerKey string

func (key ListenerKey) String() string {
	return string(key)
}

type ListenerPool interface {
	log.Loggable

	Done() <-chan struct{}
	String() string
	Put(key ListenerKey, listener net.Listener) error
	Get(key ListenerKey) (net.Listener, error)
	All() []net.Listener
	Drop(key ListenerKey) error
	DropAll() error
	Length() int
}

type ListenerHandler interface {
	log.Loggable

	Cancel()
	Done() <-chan struct{}
	String() string
	Key() ListenerKey
	Listener() net.Listener
	Serve()
	// TODO implement later, runtime replace of the net.Listener in handler
	// Update(ls net.Listener)
}

type listenerPool struct {
	hwg   sync.WaitGroup
	mu    sync.RWMutex
	store map[ListenerKey]ListenerHandler

	output chan<- Connection
	errs   chan<- error
	cancel <-chan struct{}

	done   chan struct{}
	hconns chan Connection
	herrs  chan error

	log log.Logger
}

func NewListenerPool(
	output chan<- Connection,
	errs chan<- error,
	cancel <-chan struct{},
	logger log.Logger,
) ListenerPool {
	pool := &listenerPool{
		store: make(map[ListenerKey]ListenerHandler),

		output: output,
		errs:   errs,
		cancel: cancel,

		done:   make(chan struct{}),
		hconns: make(chan Connection),
		herrs:  make(chan error),
	}
	pool.log = logger.
		WithPrefix("transport.ListenerPool").
		WithFields(log.Fields{
			"listener_pool_ptr": fmt.Sprintf("%p", pool),
		})

	go func() {
		<-pool.cancel
		pool.dispose()
	}()
	go pool.serveHandlers()

	return pool
}

func (pool *listenerPool) String() string {
	if pool == nil {
		return "<nil>"
	}

	return fmt.Sprintf("transport.ListenerPool<%s>", pool.Log().Fields())
}

func (pool *listenerPool) Log() log.Logger {
	return pool.log
}

// Done returns channel that resolves when pool gracefully completes it work.
func (pool *listenerPool) Done() <-chan struct{} {
	return pool.done
}

func (pool *listenerPool) Put(key ListenerKey, listener net.Listener) error {
	select {
	case <-pool.cancel:
		return &PoolError{
			fmt.Errorf("listener pool closed"),
			"put listener",
			pool.String(),
		}
	default:
	}
	if key == "" {
		return &PoolError{
			fmt.Errorf("empty listener key"),
			"put listener",
			pool.String(),
		}
	}

	pool.mu.Lock()
	defer pool.mu.Unlock()

	return pool.put(key, listener)
}

func (pool *listenerPool) Get(key ListenerKey) (net.Listener, error) {
	pool.mu.RLock()
	defer pool.mu.RUnlock()

	return pool.getListener(key)
}

func (pool *listenerPool) Drop(key ListenerKey) error {
	pool.mu.Lock()
	defer pool.mu.Unlock()

	return pool.drop(key)
}

func (pool *listenerPool) DropAll() error {
	pool.mu.Lock()
	for key := range pool.store {
		if err := pool.drop(key); err != nil {
			pool.Log().Errorf("drop listener %s failed: %s", key, err)
		}
	}
	pool.mu.Unlock()

	return nil
}

func (pool *listenerPool) All() []net.Listener {
	pool.mu.RLock()
	listns := make([]net.Listener, 0)
	for _, handler := range pool.store {
		listns = append(listns, handler.Listener())
	}
	pool.mu.RUnlock()

	return listns
}

func (pool *listenerPool) Length() int {
	pool.mu.RLock()
	defer pool.mu.RUnlock()

	return len(pool.store)
}

func (pool *listenerPool) dispose() {
	// clean pool
	pool.DropAll()
	pool.hwg.Wait()

	// stop serveHandlers goroutine
	close(pool.hconns)
	close(pool.herrs)

	close(pool.done)
}

func (pool *listenerPool) serveHandlers() {
	pool.Log().Debug("start serve listener handlers")
	defer pool.Log().Debug("stop serve listener handlers")

	for {
		logger := pool.Log()

		select {
		case conn, ok := <-pool.hconns:
			if !ok {
				return
			}
			if conn == nil {
				continue
			}

			logger = log.AddFieldsFrom(logger, conn)
			logger.Trace("passing up connection")

			select {
			case <-pool.cancel:
				return
			case pool.output <- conn:
				logger.Trace("connection passed up")
			}
		case err, ok := <-pool.herrs:
			if !ok {
				return
			}
			if err == nil {
				continue
			}

			var lerr *ListenerHandlerError
			if errors.As(err, &lerr) {
				pool.mu.RLock()
				handler, gerr := pool.get(lerr.Key)
				pool.mu.RUnlock()
				if gerr == nil {
					logger = logger.WithFields(handler.Log().Fields())

					if lerr.Network() {
						// listener broken or closed, should be dropped
						logger.Debugf("listener network error: %s; drop it and go further", lerr)

						if err := pool.Drop(handler.Key()); err != nil {
							logger.Error(err)
						}
					} else {
						// other
						logger.Tracef("listener error: %s; pass the error up", lerr)
					}
				} else {
					// ignore, handler already dropped out
					logger.Tracef("ignore error from already dropped out listener %s: %s", lerr.Key, lerr)

					continue
				}
			} else {
				// all other possible errors
				logger.Tracef("ignore non listener error: %s", err)

				continue
			}

			select {
			case <-pool.cancel:
				return
			case pool.errs <- err:
				logger.Trace("error passed up")
			}
		}
	}
}

func (pool *listenerPool) put(key ListenerKey, listener net.Listener) error {
	if _, err := pool.get(key); err == nil {
		return &PoolError{
			fmt.Errorf("key %s already exists in the pool", key),
			"put listener",
			pool.String(),
		}
	}

	// wrap to handler
	handler := NewListenerHandler(key, listener, pool.hconns, pool.herrs, pool.Log())

	pool.Log().WithFields(handler.Log().Fields()).Trace("put listener to the pool")

	// update store
	pool.store[handler.Key()] = handler

	// start serving
	pool.hwg.Add(1)
	go handler.Serve()
	go func() {
		<-handler.Done()
		pool.hwg.Done()
	}()

	return nil
}

func (pool *listenerPool) drop(key ListenerKey) error {
	// check existence in pool
	handler, err := pool.get(key)
	if err != nil {
		return err
	}

	handler.Cancel()

	pool.Log().WithFields(handler.Log().Fields()).Trace("drop listener from the pool")

	// modify store
	delete(pool.store, key)

	return nil
}

func (pool *listenerPool) get(key ListenerKey) (ListenerHandler, error) {
	if handler, ok := pool.store[key]; ok {
		return handler, nil
	}

	return nil, &PoolError{
		fmt.Errorf("listenr %s not found in the pool", key),
		"get listener",
		pool.String(),
	}
}

func (pool *listenerPool) getListener(key ListenerKey) (net.Listener, error) {
	if handler, err := pool.get(key); err == nil {
		return handler.Listener(), nil
	} else {
		return nil, err
	}
}

type listenerHandler struct {
	key      ListenerKey
	listener net.Listener

	output chan<- Connection
	errs   chan<- error

	cancelOnce sync.Once
	canceled   chan struct{}
	done       chan struct{}

	log log.Logger
}

func NewListenerHandler(
	key ListenerKey,
	listener net.Listener,
	output chan<- Connection,
	errs chan<- error,
	logger log.Logger,
) ListenerHandler {
	handler := &listenerHandler{
		key:      key,
		listener: listener,

		output: output,
		errs:   errs,

		canceled: make(chan struct{}),
		done:     make(chan struct{}),
	}

	handler.log = logger.
		WithPrefix("transport.ListenerHandler").
		WithFields(log.Fields{
			"listener_handler_ptr": fmt.Sprintf("%p", handler),
			"listener_ptr":         fmt.Sprintf("%p", listener),
			"listener_key":         key,
		})

	return handler
}

func (handler *listenerHandler) String() string {
	if handler == nil {
		return "<nil>"
	}

	return fmt.Sprintf("transport.ListenerHandler<%s>", handler.Log().Fields())
}

func (handler *listenerHandler) Log() log.Logger {
	return handler.log
}

func (handler *listenerHandler) Key() ListenerKey {
	return handler.key
}

func (handler *listenerHandler) Listener() net.Listener {
	return handler.listener
}

func (handler *listenerHandler) Serve() {
	defer close(handler.done)

	handler.Log().Debug("begin serve listener")
	defer handler.Log().Debugf("stop serve listener")

	wg := &sync.WaitGroup{}
	wg.Add(1)
	go handler.acceptConnections(wg)

	wg.Wait()
}

func (handler *listenerHandler) acceptConnections(wg *sync.WaitGroup) {
	defer func() {
		handler.Listener().Close()
		wg.Done()
	}()

	handler.Log().Debug("begin accept connections")
	defer handler.Log().Debug("stop accept connections")

	for {
		// wait for the new connection
		baseConn, err := handler.Listener().Accept()
		if err != nil {
			//// if we get timeout error just go further and try accept on the next iteration
			//var netErr net.Error
			//if errors.As(err, &netErr) {
			//	if netErr.Timeout() || netErr.Temporary() {
			//		handler.Log().Warnf("listener timeout or temporary unavailable, sleep by %s", netErrRetryTime)
			//
			//		time.Sleep(netErrRetryTime)
			//
			//		continue
			//	}
			//}

			// broken or closed listener
			// pass up error and exit
			err = &ListenerHandlerError{
				err,
				handler.Key(),
				fmt.Sprintf("%p", handler),
				listenerNetwork(handler.Listener()),
				handler.Listener().Addr().String(),
			}

			select {
			case <-handler.canceled:
			case handler.errs <- err:
			}

			return
		}

		var network string
		switch bc := baseConn.(type) {
		case *tls.Conn:
			network = "tls"
		case *wsConn:
			if _, ok := bc.Conn.(*tls.Conn); ok {
				network = "wss"
			} else {
				network = "ws"
			}
		default:
			network = strings.ToLower(baseConn.RemoteAddr().Network())
		}

		key := ConnectionKey(network + ":" + baseConn.RemoteAddr().String())
		handler.output <- NewConnection(baseConn, key, network, handler.Log())
	}
}

// Cancel stops serving.
// blocked until Serve completes
func (handler *listenerHandler) Cancel() {
	handler.cancelOnce.Do(func() {
		close(handler.canceled)
		handler.Listener().Close()

		handler.Log().Debug("listener handler canceled")
	})
}

// Done returns channel that resolves when handler gracefully completes it work.
func (handler *listenerHandler) Done() <-chan struct{} {
	return handler.done
}

func listenerNetwork(ls net.Listener) string {
	if val, ok := ls.(interface{ Network() string }); ok {
		return val.Network()
	}

	switch ls.(type) {
	case *net.TCPListener:
		return "tcp"
	case *net.UnixListener:
		return "unix"
	default:
		return ""
	}
}
