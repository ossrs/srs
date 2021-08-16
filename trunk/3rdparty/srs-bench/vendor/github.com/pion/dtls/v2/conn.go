package dtls

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net"
	"sync"
	"sync/atomic"
	"time"

	"github.com/pion/dtls/v2/internal/closer"
	"github.com/pion/dtls/v2/pkg/crypto/elliptic"
	"github.com/pion/dtls/v2/pkg/crypto/signaturehash"
	"github.com/pion/dtls/v2/pkg/protocol"
	"github.com/pion/dtls/v2/pkg/protocol/alert"
	"github.com/pion/dtls/v2/pkg/protocol/handshake"
	"github.com/pion/dtls/v2/pkg/protocol/recordlayer"
	"github.com/pion/logging"
	"github.com/pion/transport/connctx"
	"github.com/pion/transport/deadline"
	"github.com/pion/transport/replaydetector"
)

const (
	initialTickerInterval = time.Second
	cookieLength          = 20
	defaultNamedCurve     = elliptic.X25519
	inboundBufferSize     = 8192
	// Default replay protection window is specified by RFC 6347 Section 4.1.2.6
	defaultReplayProtectionWindow = 64
)

func invalidKeyingLabels() map[string]bool {
	return map[string]bool{
		"client finished": true,
		"server finished": true,
		"master secret":   true,
		"key expansion":   true,
	}
}

// Conn represents a DTLS connection
type Conn struct {
	lock           sync.RWMutex     // Internal lock (must not be public)
	nextConn       connctx.ConnCtx  // Embedded Conn, typically a udpconn we read/write from
	fragmentBuffer *fragmentBuffer  // out-of-order and missing fragment handling
	handshakeCache *handshakeCache  // caching of handshake messages for verifyData generation
	decrypted      chan interface{} // Decrypted Application Data or error, pull by calling `Read`

	state State // Internal state

	maximumTransmissionUnit int

	handshakeCompletedSuccessfully atomic.Value

	encryptedPackets [][]byte

	connectionClosedByUser bool
	closeLock              sync.Mutex
	closed                 *closer.Closer
	handshakeLoopsFinished sync.WaitGroup

	readDeadline  *deadline.Deadline
	writeDeadline *deadline.Deadline

	log logging.LeveledLogger

	reading               chan struct{}
	handshakeRecv         chan chan struct{}
	cancelHandshaker      func()
	cancelHandshakeReader func()

	fsm *handshakeFSM

	replayProtectionWindow uint
}

func createConn(ctx context.Context, nextConn net.Conn, config *Config, isClient bool, initialState *State) (*Conn, error) {
	err := validateConfig(config)
	if err != nil {
		return nil, err
	}

	if nextConn == nil {
		return nil, errNilNextConn
	}

	cipherSuites, err := parseCipherSuites(config.CipherSuites, config.CustomCipherSuites, config.PSK == nil || len(config.Certificates) > 0, config.PSK != nil)
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

	c := &Conn{
		nextConn:                connctx.New(nextConn),
		fragmentBuffer:          newFragmentBuffer(),
		handshakeCache:          newHandshakeCache(),
		maximumTransmissionUnit: mtu,

		decrypted: make(chan interface{}, 1),
		log:       logger,

		readDeadline:  deadline.New(),
		writeDeadline: deadline.New(),

		reading:          make(chan struct{}, 1),
		handshakeRecv:    make(chan chan struct{}),
		closed:           closer.NewCloser(),
		cancelHandshaker: func() {},

		replayProtectionWindow: uint(replayProtectionWindow),

		state: State{
			isClient: isClient,
		},
	}

	c.setRemoteEpoch(0)
	c.setLocalEpoch(0)

	serverName := config.ServerName
	// Use host from conn address when serverName is not provided
	if isClient && serverName == "" && nextConn.RemoteAddr() != nil {
		remoteAddr := nextConn.RemoteAddr().String()
		var host string
		host, _, err = net.SplitHostPort(remoteAddr)
		if err != nil {
			serverName = remoteAddr
		} else {
			serverName = host
		}
	}

	hsCfg := &handshakeConfig{
		localPSKCallback:            config.PSK,
		localPSKIdentityHint:        config.PSKIdentityHint,
		localCipherSuites:           cipherSuites,
		localSignatureSchemes:       signatureSchemes,
		extendedMasterSecret:        config.ExtendedMasterSecret,
		localSRTPProtectionProfiles: config.SRTPProtectionProfiles,
		serverName:                  serverName,
		clientAuth:                  config.ClientAuth,
		localCertificates:           config.Certificates,
		insecureSkipVerify:          config.InsecureSkipVerify,
		verifyPeerCertificate:       config.VerifyPeerCertificate,
		rootCAs:                     config.RootCAs,
		clientCAs:                   config.ClientCAs,
		customCipherSuites:          config.CustomCipherSuites,
		retransmitInterval:          workerInterval,
		log:                         logger,
		initialEpoch:                0,
		keyLogWriter:                config.KeyLogWriter,
	}

	var initialFlight flightVal
	var initialFSMState handshakeState

	if initialState != nil {
		if c.state.isClient {
			initialFlight = flight5
		} else {
			initialFlight = flight6
		}
		initialFSMState = handshakeFinished

		c.state = *initialState
	} else {
		if c.state.isClient {
			initialFlight = flight1
		} else {
			initialFlight = flight0
		}
		initialFSMState = handshakePreparing
	}
	// Do handshake
	if err := c.handshake(ctx, hsCfg, initialFlight, initialFSMState); err != nil {
		return nil, err
	}

	c.log.Trace("Handshake Completed")

	return c, nil
}

// Dial connects to the given network address and establishes a DTLS connection on top.
// Connection handshake will timeout using ConnectContextMaker in the Config.
// If you want to specify the timeout duration, use DialWithContext() instead.
func Dial(network string, raddr *net.UDPAddr, config *Config) (*Conn, error) {
	ctx, cancel := config.connectContextMaker()
	defer cancel()

	return DialWithContext(ctx, network, raddr, config)
}

// Client establishes a DTLS connection over an existing connection.
// Connection handshake will timeout using ConnectContextMaker in the Config.
// If you want to specify the timeout duration, use ClientWithContext() instead.
func Client(conn net.Conn, config *Config) (*Conn, error) {
	ctx, cancel := config.connectContextMaker()
	defer cancel()

	return ClientWithContext(ctx, conn, config)
}

// Server listens for incoming DTLS connections.
// Connection handshake will timeout using ConnectContextMaker in the Config.
// If you want to specify the timeout duration, use ServerWithContext() instead.
func Server(conn net.Conn, config *Config) (*Conn, error) {
	ctx, cancel := config.connectContextMaker()
	defer cancel()

	return ServerWithContext(ctx, conn, config)
}

// DialWithContext connects to the given network address and establishes a DTLS connection on top.
func DialWithContext(ctx context.Context, network string, raddr *net.UDPAddr, config *Config) (*Conn, error) {
	pConn, err := net.DialUDP(network, nil, raddr)
	if err != nil {
		return nil, err
	}
	return ClientWithContext(ctx, pConn, config)
}

// ClientWithContext establishes a DTLS connection over an existing connection.
func ClientWithContext(ctx context.Context, conn net.Conn, config *Config) (*Conn, error) {
	switch {
	case config == nil:
		return nil, errNoConfigProvided
	case config.PSK != nil && config.PSKIdentityHint == nil:
		return nil, errPSKAndIdentityMustBeSetForClient
	}

	return createConn(ctx, conn, config, true, nil)
}

// ServerWithContext listens for incoming DTLS connections.
func ServerWithContext(ctx context.Context, conn net.Conn, config *Config) (*Conn, error) {
	if config == nil {
		return nil, errNoConfigProvided
	}

	return createConn(ctx, conn, config, false, nil)
}

// Read reads data from the connection.
func (c *Conn) Read(p []byte) (n int, err error) {
	if !c.isHandshakeCompletedSuccessfully() {
		return 0, errHandshakeInProgress
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
				if len(p) < len(val) {
					return 0, errBufferTooSmall
				}
				copy(p, val)
				return len(val), nil
			case (error):
				return 0, val
			}
		}
	}
}

// Write writes len(p) bytes from p to the DTLS connection
func (c *Conn) Write(p []byte) (int, error) {
	if c.isConnectionClosed() {
		return 0, ErrConnClosed
	}

	select {
	case <-c.writeDeadline.Done():
		return 0, errDeadlineExceeded
	default:
	}

	if !c.isHandshakeCompletedSuccessfully() {
		return 0, errHandshakeInProgress
	}

	return len(p), c.writePackets(c.writeDeadline, []*packet{
		{
			record: &recordlayer.RecordLayer{
				Header: recordlayer.Header{
					Epoch:   c.getLocalEpoch(),
					Version: protocol.Version1_2,
				},
				Content: &protocol.ApplicationData{
					Data: p,
				},
			},
			shouldEncrypt: true,
		},
	})
}

// Close closes the connection.
func (c *Conn) Close() error {
	err := c.close(true)
	c.handshakeLoopsFinished.Wait()
	return err
}

// ConnectionState returns basic DTLS details about the connection.
// Note that this replaced the `Export` function of v1.
func (c *Conn) ConnectionState() State {
	c.lock.RLock()
	defer c.lock.RUnlock()
	return *c.state.clone()
}

// SelectedSRTPProtectionProfile returns the selected SRTPProtectionProfile
func (c *Conn) SelectedSRTPProtectionProfile() (SRTPProtectionProfile, bool) {
	c.lock.RLock()
	defer c.lock.RUnlock()

	if c.state.srtpProtectionProfile == 0 {
		return 0, false
	}

	return c.state.srtpProtectionProfile, true
}

func (c *Conn) writePackets(ctx context.Context, pkts []*packet) error {
	c.lock.Lock()
	defer c.lock.Unlock()

	var rawPackets [][]byte

	for _, p := range pkts {
		if h, ok := p.record.Content.(*handshake.Handshake); ok {
			handshakeRaw, err := p.record.Marshal()
			if err != nil {
				return err
			}

			c.log.Tracef("[handshake:%v] -> %s (epoch: %d, seq: %d)",
				srvCliStr(c.state.isClient), h.Header.Type.String(),
				p.record.Header.Epoch, h.Header.MessageSequence)
			c.handshakeCache.push(handshakeRaw[recordlayer.HeaderSize:], p.record.Header.Epoch, h.Header.MessageSequence, h.Header.Type, c.state.isClient)

			rawHandshakePackets, err := c.processHandshakePacket(p, h)
			if err != nil {
				return err
			}
			rawPackets = append(rawPackets, rawHandshakePackets...)
		} else {
			rawPacket, err := c.processPacket(p)
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
		if _, err := c.nextConn.WriteContext(ctx, compactedRawPackets); err != nil {
			return netError(err)
		}
	}

	return nil
}

func (c *Conn) compactRawPackets(rawPackets [][]byte) [][]byte {
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

func (c *Conn) processPacket(p *packet) ([]byte, error) {
	epoch := p.record.Header.Epoch
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
	p.record.Header.SequenceNumber = seq

	rawPacket, err := p.record.Marshal()
	if err != nil {
		return nil, err
	}

	if p.shouldEncrypt {
		var err error
		rawPacket, err = c.state.cipherSuite.Encrypt(p.record, rawPacket)
		if err != nil {
			return nil, err
		}
	}

	return rawPacket, nil
}

func (c *Conn) processHandshakePacket(p *packet, h *handshake.Handshake) ([][]byte, error) {
	rawPackets := make([][]byte, 0)

	handshakeFragments, err := c.fragmentHandshake(h)
	if err != nil {
		return nil, err
	}
	epoch := p.record.Header.Epoch
	for len(c.state.localSequenceNumber) <= int(epoch) {
		c.state.localSequenceNumber = append(c.state.localSequenceNumber, uint64(0))
	}

	for _, handshakeFragment := range handshakeFragments {
		seq := atomic.AddUint64(&c.state.localSequenceNumber[epoch], 1) - 1
		if seq > recordlayer.MaxSequenceNumber {
			return nil, errSequenceNumberOverflow
		}

		recordlayerHeader := &recordlayer.Header{
			Version:        p.record.Header.Version,
			ContentType:    p.record.Header.ContentType,
			ContentLen:     uint16(len(handshakeFragment)),
			Epoch:          p.record.Header.Epoch,
			SequenceNumber: seq,
		}

		recordlayerHeaderBytes, err := recordlayerHeader.Marshal()
		if err != nil {
			return nil, err
		}

		p.record.Header = *recordlayerHeader

		rawPacket := append(recordlayerHeaderBytes, handshakeFragment...)
		if p.shouldEncrypt {
			var err error
			rawPacket, err = c.state.cipherSuite.Encrypt(p.record, rawPacket)
			if err != nil {
				return nil, err
			}
		}

		rawPackets = append(rawPackets, rawPacket)
	}

	return rawPackets, nil
}

func (c *Conn) fragmentHandshake(h *handshake.Handshake) ([][]byte, error) {
	content, err := h.Message.Marshal()
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
			Type:            h.Header.Type,
			Length:          h.Header.Length,
			MessageSequence: h.Header.MessageSequence,
			FragmentOffset:  uint32(offset),
			FragmentLength:  uint32(contentFragmentLen),
		}

		offset += contentFragmentLen

		headerFragmentRaw, err := headerFragment.Marshal()
		if err != nil {
			return nil, err
		}

		fragmentedHandshake := append(headerFragmentRaw, contentFragment...)
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

func (c *Conn) readAndBuffer(ctx context.Context) error {
	bufptr := poolReadBuffer.Get().(*[]byte)
	defer poolReadBuffer.Put(bufptr)

	b := *bufptr
	i, err := c.nextConn.ReadContext(ctx, b)
	if err != nil {
		return netError(err)
	}

	pkts, err := recordlayer.UnpackDatagram(b[:i])
	if err != nil {
		return err
	}

	var hasHandshake bool
	for _, p := range pkts {
		hs, alert, err := c.handleIncomingPacket(p, true)
		if alert != nil {
			if alertErr := c.notify(ctx, alert.Level, alert.Description); alertErr != nil {
				if err == nil {
					err = alertErr
				}
			}
		}
		if hs {
			hasHandshake = true
		}
		switch e := err.(type) {
		case nil:
		case *errAlert:
			if e.IsFatalOrCloseNotify() {
				return e
			}
		default:
			return e
		}
	}
	if hasHandshake {
		done := make(chan struct{})
		select {
		case c.handshakeRecv <- done:
			// If the other party may retransmit the flight,
			// we should respond even if it not a new message.
			<-done
		case <-c.fsm.Done():
		}
	}
	return nil
}

func (c *Conn) handleQueuedPackets(ctx context.Context) error {
	pkts := c.encryptedPackets
	c.encryptedPackets = nil

	for _, p := range pkts {
		_, alert, err := c.handleIncomingPacket(p, false) // don't re-enqueue
		if alert != nil {
			if alertErr := c.notify(ctx, alert.Level, alert.Description); alertErr != nil {
				if err == nil {
					err = alertErr
				}
			}
		}
		switch e := err.(type) {
		case nil:
		case *errAlert:
			if e.IsFatalOrCloseNotify() {
				return e
			}
		default:
			return e
		}
	}
	return nil
}

func (c *Conn) handleIncomingPacket(buf []byte, enqueue bool) (bool, *alert.Alert, error) { //nolint:gocognit
	h := &recordlayer.Header{}
	if err := h.Unmarshal(buf); err != nil {
		// Decode error must be silently discarded
		// [RFC6347 Section-4.1.2.7]
		c.log.Debugf("discarded broken packet: %v", err)
		return false, nil, nil
	}

	// Validate epoch
	remoteEpoch := c.getRemoteEpoch()
	if h.Epoch > remoteEpoch {
		if h.Epoch > remoteEpoch+1 {
			c.log.Debugf("discarded future packet (epoch: %d, seq: %d)",
				h.Epoch, h.SequenceNumber,
			)
			return false, nil, nil
		}
		if enqueue {
			c.log.Debug("received packet of next epoch, queuing packet")
			c.encryptedPackets = append(c.encryptedPackets, buf)
		}
		return false, nil, nil
	}

	// Anti-replay protection
	for len(c.state.replayDetector) <= int(h.Epoch) {
		c.state.replayDetector = append(c.state.replayDetector,
			replaydetector.New(c.replayProtectionWindow, recordlayer.MaxSequenceNumber),
		)
	}
	markPacketAsValid, ok := c.state.replayDetector[int(h.Epoch)].Check(h.SequenceNumber)
	if !ok {
		c.log.Debugf("discarded duplicated packet (epoch: %d, seq: %d)",
			h.Epoch, h.SequenceNumber,
		)
		return false, nil, nil
	}

	// Decrypt
	if h.Epoch != 0 {
		if c.state.cipherSuite == nil || !c.state.cipherSuite.IsInitialized() {
			if enqueue {
				c.encryptedPackets = append(c.encryptedPackets, buf)
				c.log.Debug("handshake not finished, queuing packet")
			}
			return false, nil, nil
		}

		var err error
		buf, err = c.state.cipherSuite.Decrypt(buf)
		if err != nil {
			c.log.Debugf("%s: decrypt failed: %s", srvCliStr(c.state.isClient), err)
			return false, nil, nil
		}
	}

	isHandshake, err := c.fragmentBuffer.push(append([]byte{}, buf...))
	if err != nil {
		// Decode error must be silently discarded
		// [RFC6347 Section-4.1.2.7]
		c.log.Debugf("defragment failed: %s", err)
		return false, nil, nil
	} else if isHandshake {
		markPacketAsValid()
		for out, epoch := c.fragmentBuffer.pop(); out != nil; out, epoch = c.fragmentBuffer.pop() {
			rawHandshake := &handshake.Handshake{}
			if err := rawHandshake.Unmarshal(out); err != nil {
				c.log.Debugf("%s: handshake parse failed: %s", srvCliStr(c.state.isClient), err)
				continue
			}

			_ = c.handshakeCache.push(out, epoch, rawHandshake.Header.MessageSequence, rawHandshake.Header.Type, !c.state.isClient)
		}

		return true, nil, nil
	}

	r := &recordlayer.RecordLayer{}
	if err := r.Unmarshal(buf); err != nil {
		return false, &alert.Alert{Level: alert.Fatal, Description: alert.DecodeError}, err
	}

	switch content := r.Content.(type) {
	case *alert.Alert:
		c.log.Tracef("%s: <- %s", srvCliStr(c.state.isClient), content.String())
		var a *alert.Alert
		if content.Description == alert.CloseNotify {
			// Respond with a close_notify [RFC5246 Section 7.2.1]
			a = &alert.Alert{Level: alert.Warning, Description: alert.CloseNotify}
		}
		markPacketAsValid()
		return false, a, &errAlert{content}
	case *protocol.ChangeCipherSpec:
		if c.state.cipherSuite == nil || !c.state.cipherSuite.IsInitialized() {
			if enqueue {
				c.encryptedPackets = append(c.encryptedPackets, buf)
				c.log.Debugf("CipherSuite not initialized, queuing packet")
			}
			return false, nil, nil
		}

		newRemoteEpoch := h.Epoch + 1
		c.log.Tracef("%s: <- ChangeCipherSpec (epoch: %d)", srvCliStr(c.state.isClient), newRemoteEpoch)

		if c.getRemoteEpoch()+1 == newRemoteEpoch {
			c.setRemoteEpoch(newRemoteEpoch)
			markPacketAsValid()
		}
	case *protocol.ApplicationData:
		if h.Epoch == 0 {
			return false, &alert.Alert{Level: alert.Fatal, Description: alert.UnexpectedMessage}, errApplicationDataEpochZero
		}

		markPacketAsValid()

		select {
		case c.decrypted <- content.Data:
		case <-c.closed.Done():
		}

	default:
		return false, &alert.Alert{Level: alert.Fatal, Description: alert.UnexpectedMessage}, fmt.Errorf("%w: %d", errUnhandledContextType, content.ContentType())
	}
	return false, nil, nil
}

func (c *Conn) recvHandshake() <-chan chan struct{} {
	return c.handshakeRecv
}

func (c *Conn) notify(ctx context.Context, level alert.Level, desc alert.Description) error {
	return c.writePackets(ctx, []*packet{
		{
			record: &recordlayer.RecordLayer{
				Header: recordlayer.Header{
					Epoch:   c.getLocalEpoch(),
					Version: protocol.Version1_2,
				},
				Content: &alert.Alert{
					Level:       level,
					Description: desc,
				},
			},
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

func (c *Conn) handshake(ctx context.Context, cfg *handshakeConfig, initialFlight flightVal, initialState handshakeState) error { //nolint:gocognit
	c.fsm = newHandshakeFSM(&c.state, c.handshakeCache, cfg, initialFlight)

	done := make(chan struct{})
	ctxRead, cancelRead := context.WithCancel(context.Background())
	c.cancelHandshakeReader = cancelRead
	cfg.onFlightState = func(f flightVal, s handshakeState) {
		if s == handshakeFinished && !c.isHandshakeCompletedSuccessfully() {
			c.setHandshakeCompletedSuccessfully()
			close(done)
		}
	}

	ctxHs, cancel := context.WithCancel(context.Background())
	c.cancelHandshaker = cancel

	firstErr := make(chan error, 1)

	c.handshakeLoopsFinished.Add(2)

	// Handshake routine should be live until close.
	// The other party may request retransmission of the last flight to cope with packet drop.
	go func() {
		defer c.handshakeLoopsFinished.Done()
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
			// Escaping read loop.
			// It's safe to close decrypted channnel now.
			close(c.decrypted)

			// Force stop handshaker when the underlying connection is closed.
			cancel()
		}()
		defer c.handshakeLoopsFinished.Done()
		for {
			if err := c.readAndBuffer(ctxRead); err != nil {
				switch e := err.(type) {
				case *errAlert:
					if !e.IsFatalOrCloseNotify() {
						if c.isHandshakeCompletedSuccessfully() {
							// Pass the error to Read()
							select {
							case c.decrypted <- err:
							case <-c.closed.Done():
							}
						}
						continue // non-fatal alert must not stop read loop
					}
				case error:
					switch err {
					case context.DeadlineExceeded, context.Canceled, io.EOF:
					default:
						if c.isHandshakeCompletedSuccessfully() {
							// Keep read loop and pass the read error to Read()
							select {
							case c.decrypted <- err:
							case <-c.closed.Done():
							}
							continue // non-fatal alert must not stop read loop
						}
					}
				}
				select {
				case firstErr <- err:
				default:
				}

				if e, ok := err.(*errAlert); ok {
					if e.IsFatalOrCloseNotify() {
						_ = c.close(false)
					}
				}
				return
			}
		}
	}()

	select {
	case err := <-firstErr:
		cancelRead()
		cancel()
		return c.translateHandshakeCtxError(err)
	case <-ctx.Done():
		cancelRead()
		cancel()
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
	c.cancelHandshaker()
	c.cancelHandshakeReader()

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
	c.closed.Close()
	c.closeLock.Unlock()

	if closedByUser {
		return ErrConnClosed
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

func (c *Conn) getLocalEpoch() uint16 {
	return c.state.localEpoch.Load().(uint16)
}

func (c *Conn) setRemoteEpoch(epoch uint16) {
	c.state.remoteEpoch.Store(epoch)
}

func (c *Conn) getRemoteEpoch() uint16 {
	return c.state.remoteEpoch.Load().(uint16)
}

// LocalAddr implements net.Conn.LocalAddr
func (c *Conn) LocalAddr() net.Addr {
	return c.nextConn.LocalAddr()
}

// RemoteAddr implements net.Conn.RemoteAddr
func (c *Conn) RemoteAddr() net.Addr {
	return c.nextConn.RemoteAddr()
}

// SetDeadline implements net.Conn.SetDeadline
func (c *Conn) SetDeadline(t time.Time) error {
	c.readDeadline.Set(t)
	return c.SetWriteDeadline(t)
}

// SetReadDeadline implements net.Conn.SetReadDeadline
func (c *Conn) SetReadDeadline(t time.Time) error {
	c.readDeadline.Set(t)
	// Read deadline is fully managed by this layer.
	// Don't set read deadline to underlying connection.
	return nil
}

// SetWriteDeadline implements net.Conn.SetWriteDeadline
func (c *Conn) SetWriteDeadline(t time.Time) error {
	c.writeDeadline.Set(t)
	// Write deadline is also fully managed by this layer.
	return nil
}
