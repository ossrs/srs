// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package dtls

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"fmt"
	"io"
	"sync"
	"time"

	"github.com/pion/dtls/v3/pkg/crypto/elliptic"
	"github.com/pion/dtls/v3/pkg/crypto/signaturehash"
	"github.com/pion/dtls/v3/pkg/protocol/alert"
	"github.com/pion/dtls/v3/pkg/protocol/handshake"
	"github.com/pion/logging"
)

// [RFC6347 Section-4.2.4]
//                      +-----------+
//                +---> | PREPARING | <--------------------+
//                |     +-----------+                      |
//                |           |                            |
//                |           | Buffer next flight         |
//                |           |                            |
//                |          \|/                           |
//                |     +-----------+                      |
//                |     |  SENDING  |<------------------+  | Send
//                |     +-----------+                   |  | HelloRequest
//        Receive |           |                         |  |
//           next |           | Send flight             |  | or
//         flight |  +--------+                         |  |
//                |  |        | Set retransmit timer    |  | Receive
//                |  |       \|/                        |  | HelloRequest
//                |  |  +-----------+                   |  | Send
//                +--)--|  WAITING  |-------------------+  | ClientHello
//                |  |  +-----------+   Timer expires   |  |
//                |  |         |                        |  |
//                |  |         +------------------------+  |
//        Receive |  | Send           Read retransmit      |
//           last |  | last                                |
//         flight |  | flight                              |
//                |  |                                     |
//               \|/\|/                                    |
//            +-----------+                                |
//            | FINISHED  | -------------------------------+
//            +-----------+
//                 |  /|\
//                 |   |
//                 +---+
//              Read retransmit
//           Retransmit last flight

type handshakeState uint8

const (
	handshakeErrored handshakeState = iota
	handshakePreparing
	handshakeSending
	handshakeWaiting
	handshakeFinished
)

func (s handshakeState) String() string {
	switch s {
	case handshakeErrored:
		return "Errored"
	case handshakePreparing:
		return "Preparing"
	case handshakeSending:
		return "Sending"
	case handshakeWaiting:
		return "Waiting"
	case handshakeFinished:
		return "Finished"
	default:
		return "Unknown"
	}
}

type handshakeFSM struct {
	currentFlight      flightVal
	flights            []*packet
	retransmit         bool
	retransmitInterval time.Duration
	state              *State
	cache              *handshakeCache
	cfg                *handshakeConfig
	closed             chan struct{}
}

type handshakeConfig struct {
	localPSKCallback             PSKCallback
	localPSKIdentityHint         []byte
	localCipherSuites            []CipherSuite             // Available CipherSuites
	localSignatureSchemes        []signaturehash.Algorithm // Available signature schemes
	extendedMasterSecret         ExtendedMasterSecretType  // Policy for the Extended Master Support extension
	localSRTPProtectionProfiles  []SRTPProtectionProfile   // Available SRTPProtectionProfiles, if empty no SRTP support
	localSRTPMasterKeyIdentifier []byte
	serverName                   string
	supportedProtocols           []string
	clientAuth                   ClientAuthType // If we are a client should we request a client certificate
	localCertificates            []tls.Certificate
	nameToCertificate            map[string]*tls.Certificate
	insecureSkipVerify           bool
	verifyPeerCertificate        func(rawCerts [][]byte, verifiedChains [][]*x509.Certificate) error
	verifyConnection             func(*State) error
	sessionStore                 SessionStore
	rootCAs                      *x509.CertPool
	clientCAs                    *x509.CertPool
	initialRetransmitInterval    time.Duration
	disableRetransmitBackoff     bool
	customCipherSuites           func() []CipherSuite
	ellipticCurves               []elliptic.Curve
	insecureSkipHelloVerify      bool
	connectionIDGenerator        func() []byte
	helloRandomBytesGenerator    func() [handshake.RandomBytesLength]byte

	onFlightState func(flightVal, handshakeState)
	log           logging.LeveledLogger
	keyLogWriter  io.Writer

	localGetCertificate       func(*ClientHelloInfo) (*tls.Certificate, error)
	localGetClientCertificate func(*CertificateRequestInfo) (*tls.Certificate, error)

	initialEpoch uint16

	mu sync.Mutex

	clientHelloMessageHook        func(handshake.MessageClientHello) handshake.Message
	serverHelloMessageHook        func(handshake.MessageServerHello) handshake.Message
	certificateRequestMessageHook func(handshake.MessageCertificateRequest) handshake.Message

	resumeState *State
}

type flightConn interface {
	notify(ctx context.Context, level alert.Level, desc alert.Description) error
	writePackets(context.Context, []*packet) error
	recvHandshake() <-chan recvHandshakeState
	setLocalEpoch(epoch uint16)
	handleQueuedPackets(context.Context) error
	sessionKey() []byte
}

func (c *handshakeConfig) writeKeyLog(label string, clientRandom, secret []byte) {
	if c.keyLogWriter == nil {
		return
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	_, err := c.keyLogWriter.Write([]byte(fmt.Sprintf("%s %x %x\n", label, clientRandom, secret)))
	if err != nil {
		c.log.Debugf("failed to write key log file: %s", err)
	}
}

func srvCliStr(isClient bool) string {
	if isClient {
		return "client"
	}

	return "server"
}

func newHandshakeFSM(
	s *State, cache *handshakeCache, cfg *handshakeConfig,
	initialFlight flightVal,
) *handshakeFSM {
	return &handshakeFSM{
		currentFlight:      initialFlight,
		state:              s,
		cache:              cache,
		cfg:                cfg,
		retransmitInterval: cfg.initialRetransmitInterval,
		closed:             make(chan struct{}),
	}
}

func (s *handshakeFSM) Run(ctx context.Context, conn flightConn, initialState handshakeState) error {
	state := initialState
	defer func() {
		close(s.closed)
	}()
	for {
		s.cfg.log.Tracef("[handshake:%s] %s: %s", srvCliStr(s.state.isClient), s.currentFlight.String(), state.String())
		if s.cfg.onFlightState != nil {
			s.cfg.onFlightState(s.currentFlight, state)
		}
		var err error
		switch state {
		case handshakePreparing:
			state, err = s.prepare(ctx, conn)
		case handshakeSending:
			state, err = s.send(ctx, conn)
		case handshakeWaiting:
			state, err = s.wait(ctx, conn)
		case handshakeFinished:
			state, err = s.finish(ctx, conn)
		default:
			return errInvalidFSMTransition
		}
		if err != nil {
			return err
		}
	}
}

func (s *handshakeFSM) Done() <-chan struct{} {
	return s.closed
}

func (s *handshakeFSM) prepare(ctx context.Context, conn flightConn) (handshakeState, error) {
	s.flights = nil
	// Prepare flights
	var (
		dtlsAlert *alert.Alert
		err       error
		pkts      []*packet
	)
	gen, retransmit, errFlight := s.currentFlight.getFlightGenerator()
	if errFlight != nil {
		err = errFlight
		dtlsAlert = &alert.Alert{Level: alert.Fatal, Description: alert.InternalError}
	} else {
		pkts, dtlsAlert, err = gen(conn, s.state, s.cache, s.cfg)
		s.retransmit = retransmit
	}
	if dtlsAlert != nil {
		if alertErr := conn.notify(ctx, dtlsAlert.Level, dtlsAlert.Description); alertErr != nil {
			if err != nil {
				err = alertErr
			}
		}
	}
	if err != nil {
		return handshakeErrored, err
	}

	s.flights = pkts
	epoch := s.cfg.initialEpoch
	nextEpoch := epoch
	for _, p := range s.flights {
		p.record.Header.Epoch += epoch
		if p.record.Header.Epoch > nextEpoch {
			nextEpoch = p.record.Header.Epoch
		}
		if h, ok := p.record.Content.(*handshake.Handshake); ok {
			h.Header.MessageSequence = uint16(s.state.handshakeSendSequence) //nolint:gosec // G115
			s.state.handshakeSendSequence++
		}
	}
	if epoch != nextEpoch {
		s.cfg.log.Tracef("[handshake:%s] -> changeCipherSpec (epoch: %d)", srvCliStr(s.state.isClient), nextEpoch)
		conn.setLocalEpoch(nextEpoch)
	}

	return handshakeSending, nil
}

func (s *handshakeFSM) send(ctx context.Context, c flightConn) (handshakeState, error) {
	// Send flights
	if err := c.writePackets(ctx, s.flights); err != nil {
		return handshakeErrored, err
	}

	if s.currentFlight.isLastSendFlight() {
		return handshakeFinished, nil
	}

	return handshakeWaiting, nil
}

func (s *handshakeFSM) wait(ctx context.Context, conn flightConn) (handshakeState, error) { //nolint:gocognit,cyclop
	parse, errFlight := s.currentFlight.getFlightParser()
	if errFlight != nil {
		if alertErr := conn.notify(ctx, alert.Fatal, alert.InternalError); alertErr != nil {
			return handshakeErrored, alertErr
		}

		return handshakeErrored, errFlight
	}

	retransmitTimer := time.NewTimer(s.retransmitInterval)
	for {
		select {
		case state := <-conn.recvHandshake():
			if state.isRetransmit {
				close(state.done)

				return handshakeSending, nil
			}

			nextFlight, alert, err := parse(ctx, conn, s.state, s.cache, s.cfg)
			s.retransmitInterval = s.cfg.initialRetransmitInterval
			close(state.done)
			if alert != nil {
				if alertErr := conn.notify(ctx, alert.Level, alert.Description); alertErr != nil {
					if err != nil {
						err = alertErr
					}
				}
			}
			if err != nil {
				return handshakeErrored, err
			}
			if nextFlight == 0 {
				break
			}
			s.cfg.log.Tracef(
				"[handshake:%s] %s -> %s",
				srvCliStr(s.state.isClient),
				s.currentFlight.String(),
				nextFlight.String(),
			)
			if nextFlight.isLastRecvFlight() && s.currentFlight == nextFlight {
				return handshakeFinished, nil
			}
			s.currentFlight = nextFlight

			return handshakePreparing, nil

		case <-retransmitTimer.C:
			if !s.retransmit {
				return handshakeWaiting, nil
			}

			// RFC 4347 4.2.4.1:
			// Implementations SHOULD use an initial timer value of 1 second (the minimum defined in RFC 2988 [RFC2988])
			// and double the value at each retransmission, up to no less than the RFC 2988 maximum of 60 seconds.
			if !s.cfg.disableRetransmitBackoff {
				s.retransmitInterval *= 2
			}
			if s.retransmitInterval > time.Second*60 {
				s.retransmitInterval = time.Second * 60
			}

			return handshakeSending, nil
		case <-ctx.Done():
			s.retransmitInterval = s.cfg.initialRetransmitInterval

			return handshakeErrored, ctx.Err()
		}
	}
}

func (s *handshakeFSM) finish(ctx context.Context, c flightConn) (handshakeState, error) {
	select {
	case state := <-c.recvHandshake():
		close(state.done)
		if s.state.isClient {
			return handshakeFinished, nil
		} else {
			return handshakeSending, nil
		}
	case <-ctx.Done():
		return handshakeErrored, ctx.Err()
	}
}
