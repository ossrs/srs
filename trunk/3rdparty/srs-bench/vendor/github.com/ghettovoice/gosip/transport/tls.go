package transport

import (
	"crypto/tls"
	"crypto/x509"
	"fmt"
	"net"

	"github.com/ghettovoice/gosip/log"
	"github.com/ghettovoice/gosip/sip"
)

type tlsProtocol struct {
	tcpProtocol
}

func NewTlsProtocol(
	output chan<- sip.Message,
	errs chan<- error,
	cancel <-chan struct{},
	msgMapper sip.MessageMapper,
	logger log.Logger,
) Protocol {
	p := new(tlsProtocol)
	p.network = "tls"
	p.reliable = true
	p.streamed = true
	p.conns = make(chan Connection)
	p.log = logger.
		WithPrefix("transport.Protocol").
		WithFields(log.Fields{
			"protocol_ptr": fmt.Sprintf("%p", p),
		})
	//TODO: add separate errs chan to listen errors from pool for reconnection?
	p.listeners = NewListenerPool(p.conns, errs, cancel, p.Log())
	p.connections = NewConnectionPool(output, errs, cancel, msgMapper, p.Log())
	p.listen = func(addr *net.TCPAddr, options ...ListenOption) (net.Listener, error) {
		if len(options) == 0 {
			return net.ListenTCP("tcp", addr)
		}
		optsHash := ListenOptions{}
		for _, opt := range options {
			opt.ApplyListen(&optsHash)
		}
		cert, err := tls.LoadX509KeyPair(optsHash.TLSConfig.Cert, optsHash.TLSConfig.Key)
		if err != nil {
			return nil, fmt.Errorf("load TLS certficate %s: %w", optsHash.TLSConfig.Cert, err)
		}
		return tls.Listen("tcp", addr.String(), &tls.Config{
			Certificates: []tls.Certificate{cert},
		})
	}
	p.dial = func(addr *net.TCPAddr) (net.Conn, error) {
		return tls.Dial("tcp", addr.String(), &tls.Config{
			VerifyPeerCertificate: func(rawCerts [][]byte, verifiedChains [][]*x509.Certificate) error {
				return nil
			},
		})
	}
	p.resolveAddr = func(addr string) (*net.TCPAddr, error) {
		return net.ResolveTCPAddr("tcp", addr)
	}
	//pipe listener and connection pools
	go p.pipePools()

	return p
}
