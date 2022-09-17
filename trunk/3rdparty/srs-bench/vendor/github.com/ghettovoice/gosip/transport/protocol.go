package transport

import (
	"fmt"
	"strings"
	"time"

	"github.com/ghettovoice/gosip/log"
	"github.com/ghettovoice/gosip/sip"
)

const (
	//netErrRetryTime = 5 * time.Second
	sockTTL = time.Hour
)

// Protocol implements network specific features.
type Protocol interface {
	Done() <-chan struct{}
	Network() string
	Reliable() bool
	Streamed() bool
	Listen(target *Target, options ...ListenOption) error
	Send(target *Target, msg sip.Message) error
	String() string
}

type ProtocolFactory func(
	network string,
	output chan<- sip.Message,
	errs chan<- error,
	cancel <-chan struct{},
	msgMapper sip.MessageMapper,
	logger log.Logger,
) (Protocol, error)

type protocol struct {
	network  string
	reliable bool
	streamed bool

	log log.Logger
}

func (pr *protocol) Log() log.Logger {
	return pr.log
}

func (pr *protocol) String() string {
	if pr == nil {
		return "<nil>"
	}

	fields := pr.Log().Fields().WithFields(log.Fields{
		"network": pr.network,
	})

	return fmt.Sprintf("transport.Protocol<%s>", fields)
}

func (pr *protocol) Network() string {
	return strings.ToUpper(pr.network)
}

func (pr *protocol) Reliable() bool {
	return pr.reliable
}

func (pr *protocol) Streamed() bool {
	return pr.streamed
}
