// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package dtls

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"sync"
	"sync/atomic"
	"time"

	"github.com/pion/dtls/v3/internal/closer"
	"github.com/pion/dtls/v3/pkg/crypto/elliptic"
	"github.com/pion/dtls/v3/pkg/crypto/signaturehash"
	"github.com/pion/dtls/v3/pkg/protocol"
	"github.com/pion/dtls/v3/pkg/protocol/alert"
	"github.com/pion/dtls/v3/pkg/protocol/handshake"
	"github.com/pion/dtls/v3/pkg/protocol/recordlayer"
	"github.com/pion/logging"
	"github.com/pion/transport/v3/deadline"
	"github.com/pion/transport/v3/netctx"
	"github.com/pion/transport/v3/replaydetector"
)

const (
	initialTickerInterval = time.Second
	cookieLength          = 20
	sessionLength         = 32
	defaultNamedCurve     = elliptic.X25519
	inboundBufferSize     = 8192
	// Default replay protection window is specified by RFC 6347 Section 4.1.2.6.
	defaultReplayProtectionWindow = 64
	// maxAppDataPacketQueueSize is the maximum number of app data packets we will.
	// enqueue before the handshake is completed.
	maxAppDataPacketQueueSize = 100
)

func invalidKeyingLabels() map[string]bool {
	return map[string]bool{
		"client finished": true,
		"server finished": true,
		"master secret":   true,
		"key expansion":   true,
	}
}

type addrPkt struct {
	rAddr net.Addr
	data  []byte
}

type recvHandshakeState struct {
	done         chan struct{}
	isRetransmit bool
}

// Conn represents a DTLS connection.
type Conn struct {
	lock           sync.RWMutex      // Internal lock (must not be public)
	nextConn       netctx.PacketConn // Embedded Conn, typically a udpconn we read/write from
	fragmentBuffer *fragmentBuffer   // out-of-order and missing fragment handling
	handshakeCache *handshakeCache   // caching of handshake messages for verifyData generation
	decrypted      chan interface{}  // Decrypted Application Data or error, pull by calling `Read`
	rAddr          net.Addr
	state          State // Internal state

	maximumTransmissionUnit int
	paddingLengthGenerator  func(uint) uint

	handshakeCompletedSuccessfully atomic.Value
	handshakeMutex                 sync.Mutex
	handshakeDone                  chan struct{}

	encryptedPackets []addrPkt

	connectionClosedByUser bool
	closeLock              sync.Mutex
	closed                 *closer.Closer

	readDeadline  *deadline.Deadline
	writeDeadline *deadline.Deadline

	log logging.LeveledLogger

	reading               chan struct{}
	handshakeRecv         chan recvHandshakeState
	cancelHandshaker      func()
	cancelHandshakeReader func()

	fsm *handshakeFSM

	replayProtectionWindow uint

	handshakeConfig *handshakeConfig
}

//nolint:cyclop
func createConn(
	nextConn net.PacketConn,
	rAddr net.Addr,
	config *Config,
	isClient bool,
	resumeState *State,
) (*Conn, error) {
	if err := validateConfig(config); err != nil {
		return nil, err
	}

	if nextConn == nil {
		return nil, errNilNextConn
	}

	loggerFactory := config.LoggerFactory
	if loggerFactory == nil {
		loggerFactory = logging.NewDefaultLoggerFactory()
	}

	logger := loggerFactory.NewLogger("dtls")

	mtu := config.MTU
	if mtu <= 0 {
		mtu = defaultMTU
	}

	replayProtectionWindow := config.ReplayProtectionWindow
	if replayProtectionWindow <= 0 {
		replayProtectionWindow = defaultReplayProtectionWindow
	}

	paddingLengthGenerator := config.PaddingLengthGenerator
	if paddingLengthGenerator == nil {
		paddingLengthGenerator = func(uint) uint { return 0 }
	}

	cipherSuites, err := parseCipherSuites(
		config.CipherSuites,
		config.CustomCipherSuites,
		config.includeCertificateSuites(),
		config.PSK != nil,
	)
	if err != nil {
		return nil, err
	}

	signatureSchemes, err := signaturehash.ParseSignatureSchemes(config.SignatureSchemes, config.InsecureHashes)
	if err != nil {
		return nil, err
	}

	workerInterval := initialTickerInterval
	if config.FlightInterval != 0 {
		workerInterval = config.FlightInterval
	}

	serverName := config.ServerName
	// Do not allow the use of an IP address literal as an SNI value.
	// See RFC 6066, Section 3.
	if net.ParseIP(serverName) != nil {
		serverName = ""
	}

	curves := config.EllipticCurves
	if len(curves) == 0 {
		curves = defaultCurves
	}

	handshakeConfig := &handshakeConfig{
		localPSKCallback:              config.PSK,
		localPSKIdentityHint:          config.PSKIdentityHint,
		localCipherSuites:             cipherSuites,
		localSignatureSchemes:         signatureSchemes,
		extendedMasterSecret:          config.ExtendedMasterSecret,
		localSRTPProtectionProfiles:   config.SRTPProtectionProfiles,
		localSRTPMasterKeyIdentifier:  config.SRTPMasterKeyIdentifier,
		serverName:                    serverName,
		supportedProtocols:            config.SupportedProtocols,
		clientAuth:                    config.ClientAuth,
		localCertificates:             config.Certificates,
		insecureSkipVerify:            config.InsecureSkipVerify,
		verifyPeerCertificate:         config.VerifyPeerCertificate,
		verifyConnection:              config.VerifyConnection,
		rootCAs:                       config.RootCAs,
		clientCAs:                     config.ClientCAs,
		customCipherSuites:            config.CustomCipherSuites,
		initialRetransmitInterval:     workerInterval,
		disableRetransmitBackoff:      config.DisableRetransmitBackoff,
		log:                           logger,
		initialEpoch:                  0,
		keyLogWriter:                  config.KeyLogWriter,
		sessionStore:                  config.SessionStore,
		ellipticCurves:                curves,
		localGetCertificate:           config.GetCertificate,
		localGetClientCertificate:     config.GetClientCertificate,
		insecureSkipHelloVerify:       config.InsecureSkipVerifyHello,
		connectionIDGenerator:         config.ConnectionIDGenerator,
		helloRandomBytesGenerator:     config.HelloRandomBytesGenerator,
		clientHelloMessageHook:        config.ClientHelloMessageHook,
		serverHelloMessageHook:        config.ServerHelloMessageHook,
		certificateRequestMessageHook: config.CertificateRequestMessageHook,
		resumeState:                   resumeState,
	}

	conn := &Conn{
		rAddr:                   rAddr,
		nextConn:                netctx.NewPacketConn(nextConn),
		handshakeConfig:         handshakeConfig,
		fragmentBuffer:          newFragmentBuffer(),
		handshakeCache:          newHandshakeCache(),
		maximumTransmissionUnit: mtu,
		paddingLengthGenerator:  paddingLengthGenerator,

		decrypted: make(chan interface{}, 1),
		log:       logger,

		readDeadline:  deadline.New(),
		writeDeadline: deadline.New(),

		reading:               make(chan struct{}, 1),
		handshakeRecv:         make(chan recvHandshakeState),
		closed:                closer.NewCloser(),
		cancelHandshaker:      func() {},
		cancelHandshakeReader: func() {},

		replayProtectionWindow: uint(replayProtectionWindow), //nolint:gosec // G115

		state: State{
			isClient: isClient,
		},
	}

	conn.setRemoteEpoch(0)
	conn.setLocalEpoch(0)

	return conn, nil
}

// Handshake runs the client or server DTLS handshake
// protocol if it has not yet been run.
//
// Most uses of this package need not call Handshake explicitly: the
// first [Conn.Read] or [Conn.Write] will call it automatically.
//
// For control over canceling or setting a timeout on a handshake, use
// [Conn.HandshakeContext].
func (c *Conn) Handshake() error {
	return c.HandshakeContext(context.Background())
}

// HandshakeContext runs the client or server DTLS handshake
// protocol if it has not yet been run.
//
// The provided Context must be non-nil. If the context is canceled before
// the handshake is complete, the handshake is interrupted and an error is returned.
// Once the handshake has completed, cancellation of the context will not affect the
// connection.
//
// Most uses of this package need not call HandshakeContext explicitly: the
// first [Conn.Read] or [Conn.Write] will call it automatically.
func (c *Conn) HandshakeContext(ctx context.Context) error {
	c.handshakeMutex.Lock()
	defer c.handshakeMutex.Unlock()

	if c.isHandshakeCompletedSuccessfully() {
		return nil
	}

	handshakeDone := make(chan struct{})
	defer close(handshakeDone)
	c.closeLock.Lock()
	c.handshakeDone = handshakeDone
	c.closeLock.Unlock()

	// rfc5246#section-7.4.3
	// In addition, the hash and signature algorithms MUST be compatible
	// with the key in the server's end-entity certificate.
	if !c.state.isClient {
		cert, err := c.handshakeConfig.getCertificate(&ClientHelloInfo{})
		if err != nil && !errors.Is(err, errNoCertificates) {
			return err
		}
		c.handshakeConfig.localCipherSuites = filterCipherSuitesForCertificate(cert, c.handshakeConfig.localCipherSuites)
	}

	var initialFlight flightVal
	var initialFSMState handshakeState

	if c.handshakeConfig.resumeState != nil { //nolint:nestif
		if c.state.isClient {
			initialFlight = flight5
		} else {
			initialFlight = flight6
		}
		initialFSMState = handshakeFinished

		c.state = *c.handshakeConfig.resumeState
	} else {
		if c.state.isClient {
			initialFlight = flight1
		} else {
			initialFlight = flight0
		}
		initialFSMState = handshakePreparing
	}
	// Do handshake
	if err := c.handshake(ctx, c.handshakeConfig, initialFlight, initialFSMState); err != nil {
		return err
	}

	c.log.Trace("Handshake Completed")

	return nil
}

// Dial connects to the given network address and establishes a DTLS connection on top.
func Dial(network string, rAddr *net.UDPAddr, config *Config) (*Conn, error) {
	// net.ListenUDP is used rather than net.DialUDP as the latter prevents the
	// use of net.PacketConn.WriteTo.
	// https://github.com/golang/go/blob/ce5e37ec21442c6eb13a43e68ca20129102ebac0/src/net/udpsock_posix.go#L115
	pConn, err := net.ListenUDP(network, nil)
	if err != nil {
		return nil, err
	}

	return Client(pConn, rAddr, config)
}

// Client establishes a DTLS connection over an existing connection.
func Client(conn net.PacketConn, rAddr net.Addr, config *Config) (*Conn, error) {
	switch {
	case config == nil:
		return nil, errNoConfigProvided
	case config.PSK != nil && config.PSKIdentityHint == nil:
		return nil, errPSKAndIdentityMustBeSetForClient
	}

	return createConn(conn, rAddr, config, true, nil)
}

// Server listens for incoming DTLS connections.
func Server(conn net.PacketConn, rAddr net.Addr, config *Config) (*Conn, error) {
	if config == nil {
		return nil, errNoConfigProvided
	}
	if config.OnConnectionAttempt != nil {
		if err := config.OnConnectionAttempt(rAddr); err != nil {
			return nil, err
		}
	}

	return createConn(conn, rAddr, config, false, nil)
}

// Read reads data from the connection.
func (c *Conn) Read(buff []byte) (n int, err error) { //nolint:cyclop
	if err := c.Handshake(); err != nil {
		return 0, err
	}

	select {
	case <-c.readDeadline.Done():
		return 0, errDeadlineExceeded
	default:
	}

	for {
		select {
		case <-c.readDeadline.Done():
			return 0, errDeadlineExceeded
		case out, ok := <-c.decrypted:
			if !ok {
				return 0, io.EOF
			}
			switch val := out.(type) {
			case ([]byte):
				if len(buff) < len(val) {
					return 0, errBufferTooSmall
				}
				copy(buff, val)

				return len(val), nil
			case (error):
				return 0, val
			}
		}
	}
}

// Write writes len(payload) bytes from payload to the DTLS connection.
func (c *Conn) Write(payload []byte) (int, error) {
	if c.isConnectionClosed() {
		return 0, ErrConnClosed
	}

	select {
	case <-c.writeDeadline.Done():
		return 0, errDeadlineExceeded
	default:
	}

	if err := c.Handshake(); err != nil {
		return 0, err
	}

	return len(payload), c.writePackets(c.writeDeadline, []*packet{
		{
			record: &recordlayer.RecordLayer{
				Header: recordlayer.Header{
					Epoch:   c.state.getLocalEpoch(),
					Version: protocol.Version1_2,
				},
				Content: &protocol.ApplicationData{
					Data: payload,
				},
			},
			shouldWrapCID: len(c.state.remoteConnectionID) > 0,
			shouldEncrypt: true,
		},
	})
}

// Close closes the connection.
func (c *Conn) Close() error {
	err := c.close(true) //nolint:contextcheck
	c.closeLock.Lock()
	handshakeDone := c.handshakeDone
	c.closeLock.Unlock()
	if handshakeDone != nil {
		<-handshakeDone
	}

	return err
}

// ConnectionState returns basic DTLS details about the connection.
// Note that this replaced the `Export` function of v1.
func (c *Conn) ConnectionState() (State, bool) {
	c.lock.RLock()
	defer c.lock.RUnlock()
	stateClone, err := c.state.clone()
	if err != nil {
		return State{}, false
	}

	return *stateClone, true
}

// SelectedSRTPProtectionProfile returns the selected SRTPProtectionProfile.
func (c *Conn) SelectedSRTPProtectionProfile() (SRTPProtectionProfile, bool) {
	profile := c.state.getSRTPProtectionProfile()
	if profile == 0 {
		return 0, false
	}

	return profile, true
}

// RemoteSRTPMasterKeyIdentifier returns the MasterKeyIdentifier value from the use_srtp.
func (c *Conn) RemoteSRTPMasterKeyIdentifier() ([]byte, bool) {
	if profile := c.state.getSRTPProtectionProfile(); profile == 0 {
		return nil, false
	}

	return c.state.remoteSRTPMasterKeyIdentifier, true
}

func (c *Conn) writePackets(ctx context.Context, pkts []*packet) error {
	c.lock.Lock()
	defer c.lock.Unlock()

	var rawPackets [][]byte

	for _, pkt := range pkts {
		if dtlsHandshake, ok := pkt.record.Content.(*handshake.Handshake); ok {
			handshakeRaw, err := pkt.record.Marshal()
			if err != nil {
				return err
			}

			c.log.Tracef("[handshake:%v] -> %s (epoch: %d, seq: %d)",
				srvCliStr(c.state.isClient), dtlsHandshake.Header.Type.String(),
				pkt.record.Header.Epoch, dtlsHandshake.Header.MessageSequence)

			c.handshakeCache.push(
				handshakeRaw[recordlayer.FixedHeaderSize:],
				pkt.record.Header.Epoch,
				dtlsHandshake.Header.MessageSequence,
				dtlsHandshake.Header.Type,
				c.state.isClient,
			)

			rawHandshakePackets, err := c.processHandshakePacket(pkt, dtlsHandshake)
			if err != nil {
				return err
			}
			rawPackets = append(rawPackets, rawHandshakePackets...)
		} else {
			rawPacket, err := c.processPacket(pkt)
			if err != nil {
				return err
			}
			rawPackets = append(rawPackets, rawPacket)
		}
	}
	if len(rawPackets) == 0 {
		return nil
	}
	compactedRawPackets := c.compactRawPackets(rawPackets)

	for _, compactedRawPackets := range compactedRawPackets {
		if _, err := c.nextConn.WriteToContext(ctx, compactedRawPackets, c.rAddr); err != nil {
			return netError(err)
		}
	}

	return nil
}

func (c *Conn) compactRawPackets(rawPackets [][]byte) [][]byte {
	// avoid a useless copy in the common case
	if len(rawPackets) == 1 {
		return rawPackets
	}

	combinedRawPackets := make([][]byte, 0)
	currentCombinedRawPacket := make([]byte, 0)

	for _, rawPacket := range rawPackets {
		if len(currentCombinedRawPacket) > 0 && len(currentCombinedRawPacket)+len(rawPacket) >= c.maximumTransmissionUnit {
			combinedRawPackets = append(combinedRawPackets, currentCombinedRawPacket)
			currentCombinedRawPacket = []byte{}
		}
		currentCombinedRawPacket = append(currentCombinedRawPacket, rawPacket...)
	}

	combinedRawPackets = append(combinedRawPackets, currentCombinedRawPacket)

	return combinedRawPackets
}

func (c *Conn) processPacket(pkt *packet) ([]byte, error) { //nolint:cyclop
	epoch := pkt.record.Header.Epoch
	for len(c.state.localSequenceNumber) <= int(epoch) {
		c.state.localSequenceNumber = append(c.state.localSequenceNumber, uint64(0))
	}
	seq := atomic.AddUint64(&c.state.localSequenceNumber[epoch], 1) - 1
	if seq > recordlayer.MaxSequenceNumber {
		// RFC 6347 Section 4.1.0
		// The implementation must either abandon an association or rehandshake
		// prior to allowing the sequence number to wrap.
		return nil, errSequenceNumberOverflow
	}
	pkt.record.Header.SequenceNumber = seq

	var rawPacket []byte
	if pkt.shouldWrapCID { //nolint:nestif
		// Record must be marshaled to populate fields used in inner plaintext.
		if _, err := pkt.record.Marshal(); err != nil {
			return nil, err
		}
		content, err := pkt.record.Content.Marshal()
		if err != nil {
			return nil, err
		}
		inner := &recordlayer.InnerPlaintext{
			Content:  content,
			RealType: pkt.record.Header.ContentType,
		}
		rawInner, err := inner.Marshal() //nolint:govet
		if err != nil {
			return nil, err
		}
		cidHeader := &recordlayer.Header{
			Version:        pkt.record.Header.Version,
			ContentType:    protocol.ContentTypeConnectionID,
			Epoch:          pkt.record.Header.Epoch,
			ContentLen:     uint16(len(rawInner)), //nolint:gosec //G115
			ConnectionID:   c.state.remoteConnectionID,
			SequenceNumber: pkt.record.Header.SequenceNumber,
		}
		rawPacket, err = cidHeader.Marshal()
		if err != nil {
			return nil, err
		}
		pkt.record.Header = *cidHeader
		rawPacket = append(rawPacket, rawInner...)
	} else {
		var err error
		rawPacket, err = pkt.record.Marshal()
		if err != nil {
			return nil, err
		}
	}

	if pkt.shouldEncrypt {
		var err error
		rawPacket, err = c.state.cipherSuite.Encrypt(pkt.record, rawPacket)
		if err != nil {
			return nil, err
		}
	}

	return rawPacket, nil
}

//nolint:cyclop
func (c *Conn) processHandshakePacket(pkt *packet, dtlsHandshake *handshake.Handshake) ([][]byte, error) {
	rawPackets := make([][]byte, 0)

	handshakeFragments, err := c.fragmentHandshake(dtlsHandshake)
	if err != nil {
		return nil, err
	}
	epoch := pkt.record.Header.Epoch
	for len(c.state.localSequenceNumber) <= int(epoch) {
		c.state.localSequenceNumber = append(c.state.localSequenceNumber, uint64(0))
	}

	for _, handshakeFragment := range handshakeFragments {
		seq := atomic.AddUint64(&c.state.localSequenceNumber[epoch], 1) - 1
		if seq > recordlayer.MaxSequenceNumber {
			return nil, errSequenceNumberOverflow
		}

		var rawPacket []byte
		if pkt.shouldWrapCID {
			inner := &recordlayer.InnerPlaintext{
				Content:  handshakeFragment,
				RealType: protocol.ContentTypeHandshake,
				Zeros:    c.paddingLengthGenerator(uint(len(handshakeFragment))),
			}
			rawInner, err := inner.Marshal() //nolint:govet
			if err != nil {
				return nil, err
			}
			cidHeader := &recordlayer.Header{
				Version:        pkt.record.Header.Version,
				ContentType:    protocol.ContentTypeConnectionID,
				Epoch:          pkt.record.Header.Epoch,
				ContentLen:     uint16(len(rawInner)), //nolint:gosec //G115
				ConnectionID:   c.state.remoteConnectionID,
				SequenceNumber: pkt.record.Header.SequenceNumber,
			}
			rawPacket, err = cidHeader.Marshal()
			if err != nil {
				return nil, err
			}
			pkt.record.Header = *cidHeader
			rawPacket = append(rawPacket, rawInner...)
		} else {
			recordlayerHeader := &recordlayer.Header{
				Version:        pkt.record.Header.Version,
				ContentType:    pkt.record.Header.ContentType,
				ContentLen:     uint16(len(handshakeFragment)), //nolint:gosec // G115
				Epoch:          pkt.record.Header.Epoch,
				SequenceNumber: seq,
			}

			rawPacket, err = recordlayerHeader.Marshal()
			if err != nil {
				return nil, err
			}

			pkt.record.Header = *recordlayerHeader
			rawPacket = append(rawPacket, handshakeFragment...)
		}

		if pkt.shouldEncrypt {
			var err error
			rawPacket, err = c.state.cipherSuite.Encrypt(pkt.record, rawPacket)
			if err != nil {
				return nil, err
			}
		}

		rawPackets = append(rawPackets, rawPacket)
	}

	return rawPackets, nil
}

func (c *Conn) fragmentHandshake(dtlsHandshake *handshake.Handshake) ([][]byte, error) {
	content, err := dtlsHandshake.Message.Marshal()
	if err != nil {
		return nil, err
	}

	fragmentedHandshakes := make([][]byte, 0)

	contentFragments := splitBytes(content, c.maximumTransmissionUnit)
	if len(contentFragments) == 0 {
		contentFragments = [][]byte{
			{},
		}
	}

	offset := 0
	for _, contentFragment := range contentFragments {
		contentFragmentLen := len(contentFragment)

		headerFragment := &handshake.Header{
			Type:            dtlsHandshake.Header.Type,
			Length:          dtlsHandshake.Header.Length,
			MessageSequence: dtlsHandshake.Header.MessageSequence,
			FragmentOffset:  uint32(offset),             //nolint:gosec // G115
			FragmentLength:  uint32(contentFragmentLen), //nolint:gosec // G115
		}

		offset += contentFragmentLen

		fragmentedHandshake, err := headerFragment.Marshal()
		if err != nil {
			return nil, err
		}

		fragmentedHandshake = append(fragmentedHandshake, contentFragment...)
		fragmentedHandshakes = append(fragmentedHandshakes, fragmentedHandshake)
	}

	return fragmentedHandshakes, nil
}

var poolReadBuffer = sync.Pool{ //nolint:gochecknoglobals
	New: func() interface{} {
		b := make([]byte, inboundBufferSize)

		return &b
	},
}

func (c *Conn) readAndBuffer(ctx context.Context) error { //nolint:cyclop
	bufptr, ok := poolReadBuffer.Get().(*[]byte)
	if !ok {
		return errFailedToAccessPoolReadBuffer
	}
	defer poolReadBuffer.Put(bufptr)

	b := *bufptr
	i, rAddr, err := c.nextConn.ReadFromContext(ctx, b)
	if err != nil {
		return netError(err)
	}

	pkts, err := recordlayer.ContentAwareUnpackDatagram(b[:i], len(c.state.getLocalConnectionID()))
	if err != nil {
		return err
	}

	var hasHandshake, isRetransmit bool
	for _, p := range pkts {
		hs, rtx, alert, err := c.handleIncomingPacket(ctx, p, rAddr, true)
		if alert != nil {
			if alertErr := c.notify(ctx, alert.Level, alert.Description); alertErr != nil {
				if err == nil {
					err = alertErr
				}
			}
		}

		var e *alertError
		if errors.As(err, &e) && e.IsFatalOrCloseNotify() {
			return e
		}
		if err != nil {
			return err
		}
		if hs {
			hasHandshake = true
		}
		if rtx {
			isRetransmit = true
		}
	}
	if hasHandshake {
		s := recvHandshakeState{
			done:         make(chan struct{}),
			isRetransmit: isRetransmit,
		}
		select {
		case c.handshakeRecv <- s:
			// If the other party may retransmit the flight,
			// we should respond even if it not a new message.
			<-s.done
		case <-c.fsm.Done():
		}
	}

	return nil
}

func (c *Conn) handleQueuedPackets(ctx context.Context) error {
	pkts := c.encryptedPackets
	c.encryptedPackets = nil

	for _, p := range pkts {
		_, _, alert, err := c.handleIncomingPacket(ctx, p.data, p.rAddr, false) // don't re-enqueue
		if alert != nil {
			if alertErr := c.notify(ctx, alert.Level, alert.Description); alertErr != nil {
				if err == nil {
					err = alertErr
				}
			}
		}
		var e *alertError
		if errors.As(err, &e) && e.IsFatalOrCloseNotify() {
			return e
		}
		if err != nil {
			return err
		}
	}

	return nil
}

func (c *Conn) enqueueEncryptedPackets(packet addrPkt) bool {
	if len(c.encryptedPackets) < maxAppDataPacketQueueSize {
		c.encryptedPackets = append(c.encryptedPackets, packet)

		return true
	}

	return false
}

//nolint:gocognit,gocyclo,cyclop,maintidx
func (c *Conn) handleIncomingPacket(
	ctx context.Context,
	buf []byte,
	rAddr net.Addr,
	enqueue bool,
) (bool, bool, *alert.Alert, error) {
	header := &recordlayer.Header{}
	// Set connection ID size so that records of content type tls12_cid will
	// be parsed correctly.
	if len(c.state.getLocalConnectionID()) > 0 {
		header.ConnectionID = make([]byte, len(c.state.getLocalConnectionID()))
	}
	if err := header.Unmarshal(buf); err != nil {
		// Decode error must be silently discarded
		// [RFC6347 Section-4.1.2.7]
		c.log.Debugf("discarded broken packet: %v", err)

		return false, false, nil, nil
	}
	// Validate epoch
	remoteEpoch := c.state.getRemoteEpoch()
	if header.Epoch > remoteEpoch {
		if header.Epoch > remoteEpoch+1 {
			c.log.Debugf("discarded future packet (epoch: %d, seq: %d)",
				header.Epoch, header.SequenceNumber,
			)

			return false, false, nil, nil
		}
		if enqueue {
			if ok := c.enqueueEncryptedPackets(addrPkt{rAddr, buf}); ok {
				c.log.Debug("received packet of next epoch, queuing packet")
			}
		}

		return false, false, nil, nil
	}

	// Anti-replay protection
	for len(c.state.replayDetector) <= int(header.Epoch) {
		c.state.replayDetector = append(c.state.replayDetector,
			replaydetector.New(c.replayProtectionWindow, recordlayer.MaxSequenceNumber),
		)
	}
	markPacketAsValid, ok := c.state.replayDetector[int(header.Epoch)].Check(header.SequenceNumber)
	if !ok {
		c.log.Debugf("discarded duplicated packet (epoch: %d, seq: %d)",
			header.Epoch, header.SequenceNumber,
		)

		return false, false, nil, nil
	}

	// originalCID indicates whether the original record had content type
	// Connection ID.
	originalCID := false

	// Decrypt
	if header.Epoch != 0 { //nolint:nestif
		if c.state.cipherSuite == nil || !c.state.cipherSuite.IsInitialized() {
			if enqueue {
				if ok := c.enqueueEncryptedPackets(addrPkt{rAddr, buf}); ok {
					c.log.Debug("handshake not finished, queuing packet")
				}
			}

			return false, false, nil, nil
		}

		// If a connection identifier had been negotiated and encryption is
		// enabled, the connection identifier MUST be sent.
		if len(c.state.getLocalConnectionID()) > 0 && header.ContentType != protocol.ContentTypeConnectionID {
			c.log.Debug("discarded packet missing connection ID after value negotiated")

			return false, false, nil, nil
		}

		var err error
		var hdr recordlayer.Header
		if header.ContentType == protocol.ContentTypeConnectionID {
			hdr.ConnectionID = make([]byte, len(c.state.getLocalConnectionID()))
		}
		buf, err = c.state.cipherSuite.Decrypt(hdr, buf)
		if err != nil {
			c.log.Debugf("%s: decrypt failed: %s", srvCliStr(c.state.isClient), err)

			return false, false, nil, nil
		}
		// If this is a connection ID record, make it look like a normal record for
		// further processing.
		if header.ContentType == protocol.ContentTypeConnectionID {
			originalCID = true
			ip := &recordlayer.InnerPlaintext{}
			if err := ip.Unmarshal(buf[header.Size():]); err != nil { //nolint:govet
				c.log.Debugf("unpacking inner plaintext failed: %s", err)

				return false, false, nil, nil
			}
			unpacked := &recordlayer.Header{
				ContentType:    ip.RealType,
				ContentLen:     uint16(len(ip.Content)), //nolint:gosec // G115
				Version:        header.Version,
				Epoch:          header.Epoch,
				SequenceNumber: header.SequenceNumber,
			}
			buf, err = unpacked.Marshal()
			if err != nil {
				c.log.Debugf("converting CID record to inner plaintext failed: %s", err)

				return false, false, nil, nil
			}
			buf = append(buf, ip.Content...)
		}

		// If connection ID does not match discard the packet.
		if !bytes.Equal(c.state.getLocalConnectionID(), header.ConnectionID) {
			c.log.Debug("unexpected connection ID")

			return false, false, nil, nil
		}
	}

	isHandshake, isRetransmit, err := c.fragmentBuffer.push(append([]byte{}, buf...))
	if err != nil {
		// Decode error must be silently discarded
		// [RFC6347 Section-4.1.2.7]
		c.log.Debugf("defragment failed: %s", err)

		return false, false, nil, nil
	} else if isHandshake {
		markPacketAsValid()

		for out, epoch := c.fragmentBuffer.pop(); out != nil; out, epoch = c.fragmentBuffer.pop() {
			header := &handshake.Header{}
			if err := header.Unmarshal(out); err != nil {
				c.log.Debugf("%s: handshake parse failed: %s", srvCliStr(c.state.isClient), err)

				continue
			}
			c.handshakeCache.push(out, epoch, header.MessageSequence, header.Type, !c.state.isClient)
		}

		return true, isRetransmit, nil, nil
	}

	r := &recordlayer.RecordLayer{}
	if err := r.Unmarshal(buf); err != nil {
		return false, false, &alert.Alert{Level: alert.Fatal, Description: alert.DecodeError}, err
	}

	isLatestSeqNum := false
	switch content := r.Content.(type) {
	case *alert.Alert:
		c.log.Tracef("%s: <- %s", srvCliStr(c.state.isClient), content.String())
		var a *alert.Alert
		if content.Description == alert.CloseNotify {
			// Respond with a close_notify [RFC5246 Section 7.2.1]
			a = &alert.Alert{Level: alert.Warning, Description: alert.CloseNotify}
		}
		_ = markPacketAsValid()

		return false, false, a, &alertError{content}
	case *protocol.ChangeCipherSpec:
		if c.state.cipherSuite == nil || !c.state.cipherSuite.IsInitialized() {
			if enqueue {
				if ok := c.enqueueEncryptedPackets(addrPkt{rAddr, buf}); ok {
					c.log.Debugf("CipherSuite not initialized, queuing packet")
				}
			}

			return false, false, nil, nil
		}

		newRemoteEpoch := header.Epoch + 1
		c.log.Tracef("%s: <- ChangeCipherSpec (epoch: %d)", srvCliStr(c.state.isClient), newRemoteEpoch)

		if c.state.getRemoteEpoch()+1 == newRemoteEpoch {
			c.setRemoteEpoch(newRemoteEpoch)
			isLatestSeqNum = markPacketAsValid()
		}
	case *protocol.ApplicationData:
		if header.Epoch == 0 {
			return false, false, &alert.Alert{
				Level: alert.Fatal, Description: alert.UnexpectedMessage,
			}, errApplicationDataEpochZero
		}

		isLatestSeqNum = markPacketAsValid()

		select {
		case c.decrypted <- content.Data:
		case <-c.closed.Done():
		case <-ctx.Done():
		}

	default:
		return false, false, &alert.Alert{
			Level: alert.Fatal, Description: alert.UnexpectedMessage,
		}, fmt.Errorf("%w: %d", errUnhandledContextType, content.ContentType())
	}

	// Any valid connection ID record is a candidate for updating the remote
	// address if it is the latest record received.
	// https://datatracker.ietf.org/doc/html/rfc9146#peer-address-update
	if originalCID && isLatestSeqNum {
		if rAddr != c.RemoteAddr() {
			c.lock.Lock()
			c.rAddr = rAddr
			c.lock.Unlock()
		}
	}

	return false, false, nil, nil
}

func (c *Conn) recvHandshake() <-chan recvHandshakeState {
	return c.handshakeRecv
}

func (c *Conn) notify(ctx context.Context, level alert.Level, desc alert.Description) error {
	if level == alert.Fatal && len(c.state.SessionID) > 0 {
		// According to the RFC, we need to delete the stored session.
		// https://datatracker.ietf.org/doc/html/rfc5246#section-7.2
		if ss := c.fsm.cfg.sessionStore; ss != nil {
			c.log.Tracef("clean invalid session: %s", c.state.SessionID)
			if err := ss.Del(c.sessionKey()); err != nil {
				return err
			}
		}
	}

	return c.writePackets(ctx, []*packet{
		{
			record: &recordlayer.RecordLayer{
				Header: recordlayer.Header{
					Epoch:   c.state.getLocalEpoch(),
					Version: protocol.Version1_2,
				},
				Content: &alert.Alert{
					Level:       level,
					Description: desc,
				},
			},
			shouldWrapCID: len(c.state.remoteConnectionID) > 0,
			shouldEncrypt: c.isHandshakeCompletedSuccessfully(),
		},
	})
}

func (c *Conn) setHandshakeCompletedSuccessfully() {
	c.handshakeCompletedSuccessfully.Store(struct{ bool }{true})
}

func (c *Conn) isHandshakeCompletedSuccessfully() bool {
	boolean, _ := c.handshakeCompletedSuccessfully.Load().(struct{ bool })

	return boolean.bool
}

//nolint:cyclop,gocognit,contextcheck
func (c *Conn) handshake(
	ctx context.Context,
	cfg *handshakeConfig,
	initialFlight flightVal,
	initialState handshakeState,
) error {
	c.fsm = newHandshakeFSM(&c.state, c.handshakeCache, cfg, initialFlight)

	done := make(chan struct{})
	ctxRead, cancelRead := context.WithCancel(context.Background())
	cfg.onFlightState = func(_ flightVal, s handshakeState) {
		if s == handshakeFinished && !c.isHandshakeCompletedSuccessfully() {
			c.setHandshakeCompletedSuccessfully()
			close(done)
		}
	}

	ctxHs, cancel := context.WithCancel(context.Background())

	c.closeLock.Lock()
	c.cancelHandshaker = cancel
	c.cancelHandshakeReader = cancelRead
	c.closeLock.Unlock()

	firstErr := make(chan error, 1)

	var handshakeLoopsFinished sync.WaitGroup
	handshakeLoopsFinished.Add(2)

	// Handshake routine should be live until close.
	// The other party may request retransmission of the last flight to cope with packet drop.
	go func() {
		defer handshakeLoopsFinished.Done()
		err := c.fsm.Run(ctxHs, c, initialState)
		if !errors.Is(err, context.Canceled) {
			select {
			case firstErr <- err:
			default:
			}
		}
	}()
	go func() {
		defer func() {
			if c.isHandshakeCompletedSuccessfully() {
				// Escaping read loop.
				// It's safe to close decrypted channnel now.
				close(c.decrypted)
			}

			// Force stop handshaker when the underlying connection is closed.
			cancel()
		}()
		defer handshakeLoopsFinished.Done()
		for {
			if err := c.readAndBuffer(ctxRead); err != nil { //nolint:nestif
				var alertErr *alertError
				if errors.As(err, &alertErr) {
					if !alertErr.IsFatalOrCloseNotify() {
						if c.isHandshakeCompletedSuccessfully() {
							// Pass the error to Read()
							select {
							case c.decrypted <- err:
							case <-c.closed.Done():
							case <-ctxRead.Done():
							}
						}

						continue // non-fatal alert must not stop read loop
					}
				} else {
					switch {
					case errors.Is(err, context.DeadlineExceeded),
						errors.Is(err, context.Canceled),
						errors.Is(err, io.EOF),
						errors.Is(err, net.ErrClosed):
					case errors.Is(err, recordlayer.ErrInvalidPacketLength):
						// Decode error must be silently discarded
						// [RFC6347 Section-4.1.2.7]
						continue
					default:
						if c.isHandshakeCompletedSuccessfully() {
							// Keep read loop and pass the read error to Read()
							select {
							case c.decrypted <- err:
							case <-c.closed.Done():
							case <-ctxRead.Done():
							}

							continue // non-fatal alert must not stop read loop
						}
					}
				}

				select {
				case firstErr <- err:
				default:
				}

				if alertErr != nil {
					if alertErr.IsFatalOrCloseNotify() {
						_ = c.close(false) //nolint:contextcheck
					}
				}
				if !c.isConnectionClosed() && errors.Is(err, context.Canceled) {
					c.log.Trace("handshake timeouts - closing underline connection")
					_ = c.close(false) //nolint:contextcheck
				}

				return
			}
		}
	}()

	select {
	case err := <-firstErr:
		cancelRead()
		cancel()
		handshakeLoopsFinished.Wait()

		return c.translateHandshakeCtxError(err)
	case <-ctx.Done():
		cancelRead()
		cancel()
		handshakeLoopsFinished.Wait()

		return c.translateHandshakeCtxError(ctx.Err())
	case <-done:
		return nil
	}
}

func (c *Conn) translateHandshakeCtxError(err error) error {
	if err == nil {
		return nil
	}
	if errors.Is(err, context.Canceled) && c.isHandshakeCompletedSuccessfully() {
		return nil
	}

	return &HandshakeError{Err: err}
}

func (c *Conn) close(byUser bool) error {
	c.closeLock.Lock()
	cancelHandshaker := c.cancelHandshaker
	cancelHandshakeReader := c.cancelHandshakeReader
	c.closeLock.Unlock()

	cancelHandshaker()
	cancelHandshakeReader()

	if c.isHandshakeCompletedSuccessfully() && byUser {
		// Discard error from notify() to return non-error on the first user call of Close()
		// even if the underlying connection is already closed.
		_ = c.notify(context.Background(), alert.Warning, alert.CloseNotify)
	}

	c.closeLock.Lock()
	// Don't return ErrConnClosed at the first time of the call from user.
	closedByUser := c.connectionClosedByUser
	if byUser {
		c.connectionClosedByUser = true
	}
	isClosed := c.isConnectionClosed()
	c.closed.Close()
	c.closeLock.Unlock()

	if closedByUser {
		return ErrConnClosed
	}

	if isClosed {
		return nil
	}

	return c.nextConn.Close()
}

func (c *Conn) isConnectionClosed() bool {
	select {
	case <-c.closed.Done():
		return true
	default:
		return false
	}
}

func (c *Conn) setLocalEpoch(epoch uint16) {
	c.state.localEpoch.Store(epoch)
}

func (c *Conn) setRemoteEpoch(epoch uint16) {
	c.state.remoteEpoch.Store(epoch)
}

// LocalAddr implements net.Conn.LocalAddr.
func (c *Conn) LocalAddr() net.Addr {
	return c.nextConn.LocalAddr()
}

// RemoteAddr implements net.Conn.RemoteAddr.
func (c *Conn) RemoteAddr() net.Addr {
	c.lock.RLock()
	defer c.lock.RUnlock()

	return c.rAddr
}

func (c *Conn) sessionKey() []byte {
	if c.state.isClient {
		// As ServerName can be like 0.example.com, it's better to add
		// delimiter character which is not allowed to be in
		// neither address or domain name.
		return []byte(c.rAddr.String() + "_" + c.fsm.cfg.serverName)
	}

	return c.state.SessionID
}

// SetDeadline implements net.Conn.SetDeadline.
func (c *Conn) SetDeadline(t time.Time) error {
	c.readDeadline.Set(t)

	return c.SetWriteDeadline(t)
}

// SetReadDeadline implements net.Conn.SetReadDeadline.
func (c *Conn) SetReadDeadline(t time.Time) error {
	c.readDeadline.Set(t)
	// Read deadline is fully managed by this layer.
	// Don't set read deadline to underlying connection.
	return nil
}

// SetWriteDeadline implements net.Conn.SetWriteDeadline.
func (c *Conn) SetWriteDeadline(t time.Time) error {
	c.writeDeadline.Set(t)
	// Write deadline is also fully managed by this layer.
	return nil
}
