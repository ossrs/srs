package dtls

import (
	"context"
	"crypto/tls"
	"crypto/x509"
	"errors"
	"sync"
	"time"

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

var errInvalidFSMTransition = errors.New("invalid state machine transition")

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
	currentFlight flightVal
	flights       []*packet
	retransmit    bool
	state         *State
	cache         *handshakeCache
	cfg           *handshakeConfig
	closed        chan struct{}
}

type handshakeConfig struct {
	localPSKCallback            PSKCallback
	localPSKIdentityHint        []byte
	localCipherSuites           []cipherSuite            // Available CipherSuites
	localSignatureSchemes       []signatureHashAlgorithm // Available signature schemes
	extendedMasterSecret        ExtendedMasterSecretType // Policy for the Extended Master Support extension
	localSRTPProtectionProfiles []SRTPProtectionProfile  // Available SRTPProtectionProfiles, if empty no SRTP support
	serverName                  string
	clientAuth                  ClientAuthType // If we are a client should we request a client certificate
	localCertificates           []tls.Certificate
	nameToCertificate           map[string]*tls.Certificate
	insecureSkipVerify          bool
	verifyPeerCertificate       func(rawCerts [][]byte, verifiedChains [][]*x509.Certificate) error
	rootCAs                     *x509.CertPool
	clientCAs                   *x509.CertPool
	retransmitInterval          time.Duration

	onFlightState func(flightVal, handshakeState)
	log           logging.LeveledLogger

	initialEpoch uint16

	mu sync.Mutex
}

type flightConn interface {
	notify(ctx context.Context, level alertLevel, desc alertDescription) error
	writePackets(context.Context, []*packet) error
	recvHandshake() <-chan chan struct{}
	setLocalEpoch(epoch uint16)
	handleQueuedPackets(context.Context) error
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
		currentFlight: initialFlight,
		state:         s,
		cache:         cache,
		cfg:           cfg,
		closed:        make(chan struct{}),
	}
}

func (s *handshakeFSM) Run(ctx context.Context, c flightConn, initialState handshakeState) error {
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
			state, err = s.prepare(ctx, c)
		case handshakeSending:
			state, err = s.send(ctx, c)
		case handshakeWaiting:
			state, err = s.wait(ctx, c)
		case handshakeFinished:
			state, err = s.finish(ctx, c)
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

func (s *handshakeFSM) prepare(ctx context.Context, c flightConn) (handshakeState, error) {
	s.flights = nil
	// Prepare flights
	var (
		a    *alert
		err  error
		pkts []*packet
	)
	gen, retransmit, errFlight := s.currentFlight.getFlightGenerator()
	if errFlight != nil {
		err = errFlight
		a = &alert{alertLevelFatal, alertInternalError}
	} else {
		pkts, a, err = gen(c, s.state, s.cache, s.cfg)
		s.retransmit = retransmit
	}
	if a != nil {
		if alertErr := c.notify(ctx, a.alertLevel, a.alertDescription); alertErr != nil {
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
		p.record.recordLayerHeader.epoch += epoch
		if p.record.recordLayerHeader.epoch > nextEpoch {
			nextEpoch = p.record.recordLayerHeader.epoch
		}
		if h, ok := p.record.content.(*handshake); ok {
			h.handshakeHeader.messageSequence = uint16(s.state.handshakeSendSequence)
			s.state.handshakeSendSequence++
		}
	}
	if epoch != nextEpoch {
		s.cfg.log.Tracef("[handshake:%s] -> changeCipherSpec (epoch: %d)", srvCliStr(s.state.isClient), nextEpoch)
		c.setLocalEpoch(nextEpoch)
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

func (s *handshakeFSM) wait(ctx context.Context, c flightConn) (handshakeState, error) { //nolint:gocognit
	parse, errFlight := s.currentFlight.getFlightParser()
	if errFlight != nil {
		if alertErr := c.notify(ctx, alertLevelFatal, alertInternalError); alertErr != nil {
			if errFlight != nil {
				return handshakeErrored, alertErr
			}
		}
		return handshakeErrored, errFlight
	}

	retransmitTimer := time.NewTimer(s.cfg.retransmitInterval)
	for {
		select {
		case done := <-c.recvHandshake():
			nextFlight, alert, err := parse(ctx, c, s.state, s.cache, s.cfg)
			close(done)
			if alert != nil {
				if alertErr := c.notify(ctx, alert.alertLevel, alert.alertDescription); alertErr != nil {
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
			s.cfg.log.Tracef("[handshake:%s] %s -> %s", srvCliStr(s.state.isClient), s.currentFlight.String(), nextFlight.String())
			if nextFlight.isLastRecvFlight() && s.currentFlight == nextFlight {
				return handshakeFinished, nil
			}
			s.currentFlight = nextFlight
			return handshakePreparing, nil

		case <-retransmitTimer.C:
			if !s.retransmit {
				return handshakeWaiting, nil
			}
			return handshakeSending, nil
		case <-ctx.Done():
			return handshakeErrored, ctx.Err()
		}
	}
}

func (s *handshakeFSM) finish(ctx context.Context, c flightConn) (handshakeState, error) {
	parse, errFlight := s.currentFlight.getFlightParser()
	if errFlight != nil {
		if alertErr := c.notify(ctx, alertLevelFatal, alertInternalError); alertErr != nil {
			if errFlight != nil {
				return handshakeErrored, alertErr
			}
		}
		return handshakeErrored, errFlight
	}

	retransmitTimer := time.NewTimer(s.cfg.retransmitInterval)
	select {
	case done := <-c.recvHandshake():
		nextFlight, alert, err := parse(ctx, c, s.state, s.cache, s.cfg)
		close(done)
		if alert != nil {
			if alertErr := c.notify(ctx, alert.alertLevel, alert.alertDescription); alertErr != nil {
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
		<-retransmitTimer.C
		// Retransmit last flight
		return handshakeSending, nil

	case <-ctx.Done():
		return handshakeErrored, ctx.Err()
	}
	return handshakeFinished, nil
}
