// transport package implements SIP transport layer.
package transport

import (
	"errors"
	"fmt"
	"io"
	"net"
	"regexp"
	"strconv"
	"strings"

	"github.com/ghettovoice/gosip/log"
	"github.com/ghettovoice/gosip/sip"
)

const (
	MTU = sip.MTU

	DefaultHost     = sip.DefaultHost
	DefaultProtocol = sip.DefaultProtocol

	DefaultUdpPort = sip.DefaultUdpPort
	DefaultTcpPort = sip.DefaultTcpPort
	DefaultTlsPort = sip.DefaultTlsPort
	DefaultWsPort  = sip.DefaultWsPort
	DefaultWssPort = sip.DefaultWssPort
)

// Target endpoint
type Target struct {
	Host string
	Port *sip.Port
}

func (trg *Target) Addr() string {
	var (
		host string
		port sip.Port
	)

	if strings.TrimSpace(trg.Host) != "" {
		host = trg.Host
	} else {
		host = DefaultHost
	}

	if trg.Port != nil {
		port = *trg.Port
	}

	return fmt.Sprintf("%v:%v", host, port)
}

func (trg *Target) String() string {
	if trg == nil {
		return "<nil>"
	}

	fields := log.Fields{
		"target_addr": trg.Addr(),
	}

	return fmt.Sprintf("transport.Target<%s>", fields)
}

func NewTarget(host string, port int) *Target {
	cport := sip.Port(port)

	return &Target{Host: host, Port: &cport}
}

func NewTargetFromAddr(addr string) (*Target, error) {
	host, port, err := net.SplitHostPort(addr)
	if err != nil {
		return nil, err
	}
	iport, err := strconv.Atoi(port)
	if err != nil {
		return nil, err
	}
	return NewTarget(host, iport), nil
}

// Fills endpoint target with default values.
func FillTargetHostAndPort(network string, target *Target) *Target {
	if strings.TrimSpace(target.Host) == "" {
		target.Host = DefaultHost
	}
	if target.Port == nil {
		p := sip.DefaultPort(network)
		target.Port = &p
	}

	return target
}

// Transport error
type Error interface {
	net.Error
	// Network indicates network level errors
	Network() bool
}

func isNetwork(err error) bool {
	var netErr net.Error
	if errors.As(err, &netErr) {
		return true
	} else {
		return errors.Is(err, io.EOF) || errors.Is(err, io.ErrClosedPipe)
	}
}
func isTimeout(err error) bool {
	var netErr net.Error
	if errors.As(err, &netErr) {
		return netErr.Timeout()
	}
	return false
}
func isTemporary(err error) bool {
	var netErr net.Error
	if errors.As(err, &netErr) {
		return netErr.Temporary()
	}
	return false
}
func isCanceled(err error) bool {
	var cancelErr sip.CancelError
	if errors.As(err, &cancelErr) {
		return cancelErr.Canceled()
	}
	return false
}
func isExpired(err error) bool {
	var expiryErr sip.ExpireError
	if errors.As(err, &expiryErr) {
		return expiryErr.Expired()
	}
	return false
}

// Connection level error.
type ConnectionError struct {
	Err     error
	Op      string
	Net     string
	Source  string
	Dest    string
	ConnPtr string
}

func (err *ConnectionError) Unwrap() error   { return err.Err }
func (err *ConnectionError) Network() bool   { return isNetwork(err.Err) }
func (err *ConnectionError) Timeout() bool   { return isTimeout(err.Err) }
func (err *ConnectionError) Temporary() bool { return isTemporary(err.Err) }
func (err *ConnectionError) Error() string {
	if err == nil {
		return "<nil>"
	}

	fields := log.Fields{
		"network":        "???",
		"connection_ptr": "???",
		"source":         "???",
		"destination":    "???",
	}

	if err.Net != "" {
		fields["network"] = err.Net
	}
	if err.ConnPtr != "" {
		fields["connection_ptr"] = err.ConnPtr
	}
	if err.Source != "" {
		fields["source"] = err.Source
	}
	if err.Dest != "" {
		fields["destination"] = err.Dest
	}

	return fmt.Sprintf("transport.ConnectionError<%s> %s failed: %s", fields, err.Op, err.Err)
}

type ExpireError string

func (err ExpireError) Network() bool   { return false }
func (err ExpireError) Timeout() bool   { return true }
func (err ExpireError) Temporary() bool { return false }
func (err ExpireError) Canceled() bool  { return false }
func (err ExpireError) Expired() bool   { return true }
func (err ExpireError) Error() string   { return "transport.ExpireError: " + string(err) }

// Net Protocol level error
type ProtocolError struct {
	Err      error
	Op       string
	ProtoPtr string
}

func (err *ProtocolError) Unwrap() error   { return err.Err }
func (err *ProtocolError) Network() bool   { return isNetwork(err.Err) }
func (err *ProtocolError) Timeout() bool   { return isTimeout(err.Err) }
func (err *ProtocolError) Temporary() bool { return isTemporary(err.Err) }
func (err *ProtocolError) Error() string {
	if err == nil {
		return "<nil>"
	}

	fields := log.Fields{
		"protocol_ptr": "???",
	}

	if err.ProtoPtr != "" {
		fields["protocol_ptr"] = err.ProtoPtr
	}

	return fmt.Sprintf("transport.ProtocolError<%s> %s failed: %s", fields, err.Op, err.Err)
}

type ConnectionHandlerError struct {
	Err        error
	Key        ConnectionKey
	HandlerPtr string
	Net        string
	LAddr      string
	RAddr      string
}

func (err *ConnectionHandlerError) Unwrap() error   { return err.Err }
func (err *ConnectionHandlerError) Network() bool   { return isNetwork(err.Err) }
func (err *ConnectionHandlerError) Timeout() bool   { return isTimeout(err.Err) }
func (err *ConnectionHandlerError) Temporary() bool { return isTemporary(err.Err) }
func (err *ConnectionHandlerError) Canceled() bool  { return isCanceled(err.Err) }
func (err *ConnectionHandlerError) Expired() bool   { return isExpired(err.Err) }
func (err *ConnectionHandlerError) EOF() bool {
	if err.Err == io.EOF {
		return true
	}
	ok, _ := regexp.MatchString("(?i)eof", err.Err.Error())
	return ok
}
func (err *ConnectionHandlerError) Error() string {
	if err == nil {
		return "<nil>"
	}

	fields := log.Fields{
		"handler_ptr": "???",
		"network":     "???",
		"local_addr":  "???",
		"remote_addr": "???",
	}

	if err.HandlerPtr != "" {
		fields["handler_ptr"] = err.HandlerPtr
	}
	if err.Net != "" {
		fields["network"] = err.Net
	}
	if err.LAddr != "" {
		fields["local_addr"] = err.LAddr
	}
	if err.RAddr != "" {
		fields["remote_addr"] = err.RAddr
	}

	return fmt.Sprintf("transport.ConnectionHandlerError<%s>: %s", fields, err.Err)
}

type ListenerHandlerError struct {
	Err        error
	Key        ListenerKey
	HandlerPtr string
	Net        string
	Addr       string
}

func (err *ListenerHandlerError) Unwrap() error   { return err.Err }
func (err *ListenerHandlerError) Network() bool   { return isNetwork(err.Err) }
func (err *ListenerHandlerError) Timeout() bool   { return isTimeout(err.Err) }
func (err *ListenerHandlerError) Temporary() bool { return isTemporary(err.Err) }
func (err *ListenerHandlerError) Canceled() bool  { return isCanceled(err.Err) }
func (err *ListenerHandlerError) Expired() bool   { return isExpired(err.Err) }
func (err *ListenerHandlerError) Error() string {
	if err == nil {
		return "<nil>"
	}

	fields := log.Fields{
		"handler_ptr": "???",
		"network":     "???",
		"local_addr":  "???",
		"remote_addr": "???",
	}

	if err.HandlerPtr != "" {
		fields["handler_ptr"] = err.HandlerPtr
	}
	if err.Net != "" {
		fields["network"] = err.Net
	}
	if err.Addr != "" {
		fields["local_addr"] = err.Addr
	}

	return fmt.Sprintf("transport.ListenerHandlerError<%s>: %s", fields, err.Err)
}

type PoolError struct {
	Err  error
	Op   string
	Pool string
}

func (err *PoolError) Unwrap() error   { return err.Err }
func (err *PoolError) Network() bool   { return isNetwork(err.Err) }
func (err *PoolError) Timeout() bool   { return isTimeout(err.Err) }
func (err *PoolError) Temporary() bool { return isTemporary(err.Err) }
func (err *PoolError) Error() string {
	if err == nil {
		return "<nil>"
	}

	fields := log.Fields{
		"pool": "???",
	}

	if err.Pool != "" {
		fields["pool"] = err.Pool
	}

	return fmt.Sprintf("transport.PoolError<%s> %s failed: %s", fields, err.Op, err.Err)
}

type UnsupportedProtocolError string

func (err UnsupportedProtocolError) Network() bool   { return false }
func (err UnsupportedProtocolError) Timeout() bool   { return false }
func (err UnsupportedProtocolError) Temporary() bool { return false }
func (err UnsupportedProtocolError) Error() string {
	return "transport.UnsupportedProtocolError: " + string(err)
}

//TLSConfig for TLS and WSS only
type TLSConfig struct {
	Domain string
	Cert   string
	Key    string
	Pass   string
}

func (c TLSConfig) ApplyListen(opts *ListenOptions) {
	opts.TLSConfig.Domain = c.Domain
	opts.TLSConfig.Cert = c.Cert
	opts.TLSConfig.Key = c.Key
	opts.TLSConfig.Pass = c.Pass
}
