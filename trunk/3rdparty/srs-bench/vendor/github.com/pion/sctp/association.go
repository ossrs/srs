package sctp

import (
	"bytes"
	"fmt"
	"io"
	"math"
	"net"
	"sync"
	"sync/atomic"
	"time"

	"github.com/pion/logging"
	"github.com/pion/randutil"
	"github.com/pkg/errors"
)

// Use global random generator to properly seed by crypto grade random.
var (
	globalMathRandomGenerator = randutil.NewMathRandomGenerator() // nolint:gochecknoglobals
	errChunk                  = errors.New("Abort chunk, with following errors")
)

const (
	receiveMTU            uint32 = 8192 // MTU for inbound packet (from DTLS)
	initialMTU            uint32 = 1228 // initial MTU for outgoing packets (to DTLS)
	initialRecvBufSize    uint32 = 1024 * 1024
	commonHeaderSize      uint32 = 12
	dataChunkHeaderSize   uint32 = 16
	defaultMaxMessageSize uint32 = 65536
)

// association state enums
const (
	closed uint32 = iota
	cookieWait
	cookieEchoed
	established
	shutdownAckSent
	shutdownPending
	shutdownReceived
	shutdownSent
)

// retransmission timer IDs
const (
	timerT1Init int = iota
	timerT1Cookie
	timerT3RTX
	timerReconfig
)

// ack mode (for testing)
const (
	ackModeNormal int = iota
	ackModeNoDelay
	ackModeAlwaysDelay
)

// ack transmission state
const (
	ackStateIdle      int = iota // ack timer is off
	ackStateImmediate            // ack timer is on (ack is being delayed)
	ackStateDelay                // will send ack immediately
)

// other constants
const (
	acceptChSize = 16
)

func getAssociationStateString(a uint32) string {
	switch a {
	case closed:
		return "Closed"
	case cookieWait:
		return "CookieWait"
	case cookieEchoed:
		return "CookieEchoed"
	case established:
		return "Established"
	case shutdownPending:
		return "ShutdownPending"
	case shutdownSent:
		return "ShutdownSent"
	case shutdownReceived:
		return "ShutdownReceived"
	case shutdownAckSent:
		return "ShutdownAckSent"
	default:
		return fmt.Sprintf("Invalid association state %d", a)
	}
}

// Association represents an SCTP association
// 13.2.  Parameters Necessary per Association (i.e., the TCB)
// Peer        : Tag value to be sent in every packet and is received
// Verification: in the INIT or INIT ACK chunk.
// Tag         :
//
// My          : Tag expected in every inbound packet and sent in the
// Verification: INIT or INIT ACK chunk.
//
// Tag         :
// State       : A state variable indicating what state the association
//             : is in, i.e., COOKIE-WAIT, COOKIE-ECHOED, ESTABLISHED,
//             : SHUTDOWN-PENDING, SHUTDOWN-SENT, SHUTDOWN-RECEIVED,
//             : SHUTDOWN-ACK-SENT.
//
//               Note: No "CLOSED" state is illustrated since if a
//               association is "CLOSED" its TCB SHOULD be removed.
type Association struct {
	bytesReceived uint64
	bytesSent     uint64

	lock sync.RWMutex

	netConn net.Conn

	peerVerificationTag    uint32
	myVerificationTag      uint32
	state                  uint32
	myNextTSN              uint32 // nextTSN
	peerLastTSN            uint32 // lastRcvdTSN
	minTSN2MeasureRTT      uint32 // for RTT measurement
	willSendForwardTSN     bool
	willRetransmitFast     bool
	willRetransmitReconfig bool

	// Reconfig
	myNextRSN        uint32
	reconfigs        map[uint32]*chunkReconfig
	reconfigRequests map[uint32]*paramOutgoingResetRequest

	// Non-RFC internal data
	sourcePort              uint16
	destinationPort         uint16
	myMaxNumInboundStreams  uint16
	myMaxNumOutboundStreams uint16
	myCookie                *paramStateCookie
	payloadQueue            *payloadQueue
	inflightQueue           *payloadQueue
	pendingQueue            *pendingQueue
	controlQueue            *controlQueue
	mtu                     uint32
	maxPayloadSize          uint32 // max DATA chunk payload size
	cumulativeTSNAckPoint   uint32
	advancedPeerTSNAckPoint uint32
	useForwardTSN           bool

	// Congestion control parameters
	maxReceiveBufferSize uint32
	maxMessageSize       uint32
	cwnd                 uint32 // my congestion window size
	rwnd                 uint32 // calculated peer's receiver windows size
	ssthresh             uint32 // slow start threshold
	partialBytesAcked    uint32
	inFastRecovery       bool
	fastRecoverExitPoint uint32

	// RTX & Ack timer
	rtoMgr    *rtoManager
	t1Init    *rtxTimer
	t1Cookie  *rtxTimer
	t3RTX     *rtxTimer
	tReconfig *rtxTimer
	ackTimer  *ackTimer

	// Chunks stored for retransmission
	storedInit       *chunkInit
	storedCookieEcho *chunkCookieEcho

	streams              map[uint16]*Stream
	acceptCh             chan *Stream
	readLoopCloseCh      chan struct{}
	awakeWriteLoopCh     chan struct{}
	closeWriteLoopCh     chan struct{}
	handshakeCompletedCh chan error

	closeWriteLoopOnce sync.Once

	// local error
	silentError error

	ackState int
	ackMode  int // for testing

	// stats
	stats *associationStats

	// per inbound packet context
	delayedAckTriggered   bool
	immediateAckTriggered bool

	name string
	log  logging.LeveledLogger
}

// Config collects the arguments to createAssociation construction into
// a single structure
type Config struct {
	NetConn              net.Conn
	MaxReceiveBufferSize uint32
	MaxMessageSize       uint32
	LoggerFactory        logging.LoggerFactory
}

// Server accepts a SCTP stream over a conn
func Server(config Config) (*Association, error) {
	a := createAssociation(config)
	a.init(false)

	select {
	case err := <-a.handshakeCompletedCh:
		if err != nil {
			return nil, err
		}
		return a, nil
	case <-a.readLoopCloseCh:
		return nil, errors.Errorf("association closed before connecting")
	}
}

// Client opens a SCTP stream over a conn
func Client(config Config) (*Association, error) {
	a := createAssociation(config)
	a.init(true)

	select {
	case err := <-a.handshakeCompletedCh:
		if err != nil {
			return nil, err
		}
		return a, nil
	case <-a.readLoopCloseCh:
		return nil, errors.Errorf("association closed before connecting")
	}
}

func createAssociation(config Config) *Association {
	var maxReceiveBufferSize uint32
	if config.MaxReceiveBufferSize == 0 {
		maxReceiveBufferSize = initialRecvBufSize
	} else {
		maxReceiveBufferSize = config.MaxReceiveBufferSize
	}

	var maxMessageSize uint32
	if config.MaxMessageSize == 0 {
		maxMessageSize = defaultMaxMessageSize
	} else {
		maxMessageSize = config.MaxMessageSize
	}

	tsn := globalMathRandomGenerator.Uint32()
	a := &Association{
		netConn:                 config.NetConn,
		maxReceiveBufferSize:    maxReceiveBufferSize,
		maxMessageSize:          maxMessageSize,
		myMaxNumOutboundStreams: math.MaxUint16,
		myMaxNumInboundStreams:  math.MaxUint16,
		payloadQueue:            newPayloadQueue(),
		inflightQueue:           newPayloadQueue(),
		pendingQueue:            newPendingQueue(),
		controlQueue:            newControlQueue(),
		mtu:                     initialMTU,
		maxPayloadSize:          initialMTU - (commonHeaderSize + dataChunkHeaderSize),
		myVerificationTag:       globalMathRandomGenerator.Uint32(),
		myNextTSN:               tsn,
		myNextRSN:               tsn,
		minTSN2MeasureRTT:       tsn,
		state:                   closed,
		rtoMgr:                  newRTOManager(),
		streams:                 map[uint16]*Stream{},
		reconfigs:               map[uint32]*chunkReconfig{},
		reconfigRequests:        map[uint32]*paramOutgoingResetRequest{},
		acceptCh:                make(chan *Stream, acceptChSize),
		readLoopCloseCh:         make(chan struct{}),
		awakeWriteLoopCh:        make(chan struct{}, 1),
		closeWriteLoopCh:        make(chan struct{}),
		handshakeCompletedCh:    make(chan error),
		cumulativeTSNAckPoint:   tsn - 1,
		advancedPeerTSNAckPoint: tsn - 1,
		silentError:             errors.Errorf("silently discard"),
		stats:                   &associationStats{},
		log:                     config.LoggerFactory.NewLogger("sctp"),
	}

	a.name = fmt.Sprintf("%p", a)

	// RFC 4690 Sec 7.2.1
	//  o  The initial cwnd before DATA transmission or after a sufficiently
	//     long idle period MUST be set to min(4*MTU, max (2*MTU, 4380
	//     bytes)).
	a.cwnd = min32(4*a.mtu, max32(2*a.mtu, 4380))
	a.log.Tracef("[%s] updated cwnd=%d ssthresh=%d inflight=%d (INI)",
		a.name, a.cwnd, a.ssthresh, a.inflightQueue.getNumBytes())

	a.t1Init = newRTXTimer(timerT1Init, a, maxInitRetrans)
	a.t1Cookie = newRTXTimer(timerT1Cookie, a, maxInitRetrans)
	a.t3RTX = newRTXTimer(timerT3RTX, a, noMaxRetrans)        // retransmit forever
	a.tReconfig = newRTXTimer(timerReconfig, a, noMaxRetrans) // retransmit forever
	a.ackTimer = newAckTimer(a)

	return a
}

func (a *Association) init(isClient bool) {
	a.lock.Lock()
	defer a.lock.Unlock()

	go a.readLoop()
	go a.writeLoop()

	if isClient {
		a.setState(cookieWait)
		init := &chunkInit{}
		init.initialTSN = a.myNextTSN
		init.numOutboundStreams = a.myMaxNumOutboundStreams
		init.numInboundStreams = a.myMaxNumInboundStreams
		init.initiateTag = a.myVerificationTag
		init.advertisedReceiverWindowCredit = a.maxReceiveBufferSize
		setSupportedExtensions(&init.chunkInitCommon)
		a.storedInit = init

		err := a.sendInit()
		if err != nil {
			a.log.Errorf("[%s] failed to send init: %s", a.name, err.Error())
		}

		a.t1Init.start(a.rtoMgr.getRTO())
	}
}

// caller must hold a.lock
func (a *Association) sendInit() error {
	a.log.Debugf("[%s] sending INIT", a.name)
	if a.storedInit == nil {
		return errors.Errorf("the init not stored to send")
	}

	outbound := &packet{}
	outbound.verificationTag = a.peerVerificationTag
	a.sourcePort = 5000      // Spec??
	a.destinationPort = 5000 // Spec??
	outbound.sourcePort = a.sourcePort
	outbound.destinationPort = a.destinationPort

	outbound.chunks = []chunk{a.storedInit}

	a.controlQueue.push(outbound)
	a.awakeWriteLoop()

	return nil
}

// caller must hold a.lock
func (a *Association) sendCookieEcho() error {
	if a.storedCookieEcho == nil {
		return errors.Errorf("cookieEcho not stored to send")
	}

	a.log.Debugf("[%s] sending COOKIE-ECHO", a.name)

	outbound := &packet{}
	outbound.verificationTag = a.peerVerificationTag
	outbound.sourcePort = a.sourcePort
	outbound.destinationPort = a.destinationPort
	outbound.chunks = []chunk{a.storedCookieEcho}

	a.controlQueue.push(outbound)
	a.awakeWriteLoop()

	return nil
}

// Close ends the SCTP Association and cleans up any state
func (a *Association) Close() error {
	a.log.Debugf("[%s] closing association..", a.name)

	a.setState(closed)

	err := a.netConn.Close()

	a.closeAllTimers()

	// awake writeLoop to exit
	a.closeWriteLoopOnce.Do(func() { close(a.closeWriteLoopCh) })

	// Wait for readLoop to end
	<-a.readLoopCloseCh

	a.log.Debugf("[%s] association closed", a.name)
	a.log.Debugf("[%s] stats nDATAs (in) : %d", a.name, a.stats.getNumDATAs())
	a.log.Debugf("[%s] stats nSACKs (in) : %d", a.name, a.stats.getNumSACKs())
	a.log.Debugf("[%s] stats nT3Timeouts : %d", a.name, a.stats.getNumT3Timeouts())
	a.log.Debugf("[%s] stats nAckTimeouts: %d", a.name, a.stats.getNumAckTimeouts())
	a.log.Debugf("[%s] stats nFastRetrans: %d", a.name, a.stats.getNumFastRetrans())
	return err
}

func (a *Association) closeAllTimers() {
	// Close all retransmission & ack timers
	a.t1Init.close()
	a.t1Cookie.close()
	a.t3RTX.close()
	a.tReconfig.close()
	a.ackTimer.close()
}

func (a *Association) readLoop() {
	var closeErr error
	defer func() {
		// also stop writeLoop, otherwise writeLoop can be leaked
		// if connection is lost when there is no writing packet.
		a.closeWriteLoopOnce.Do(func() { close(a.closeWriteLoopCh) })

		a.lock.Lock()
		for _, s := range a.streams {
			a.unregisterStream(s, closeErr)
		}
		a.lock.Unlock()
		close(a.acceptCh)
		close(a.readLoopCloseCh)
	}()

	a.log.Debugf("[%s] readLoop entered", a.name)
	buffer := make([]byte, receiveMTU)

	for {
		n, err := a.netConn.Read(buffer)
		if err != nil {
			closeErr = err
			break
		}
		// Make a buffer sized to what we read, then copy the data we
		// read from the underlying transport. We do this because the
		// user data is passed to the reassembly queue without
		// copying.
		inbound := make([]byte, n)
		copy(inbound, buffer[:n])
		atomic.AddUint64(&a.bytesReceived, uint64(n))
		if err = a.handleInbound(inbound); err != nil {
			closeErr = err
			break
		}
	}

	a.log.Debugf("[%s] readLoop exited %s", a.name, closeErr)
}

func (a *Association) writeLoop() {
	a.log.Debugf("[%s] writeLoop entered", a.name)

loop:
	for {
		rawPackets := a.gatherOutbound()

		for _, raw := range rawPackets {
			_, err := a.netConn.Write(raw)
			if err != nil {
				if err != io.EOF {
					a.log.Warnf("[%s] failed to write packets on netConn: %v", a.name, err)
				}
				a.log.Debugf("[%s] writeLoop ended", a.name)
				break loop
			}
			atomic.AddUint64(&a.bytesSent, uint64(len(raw)))
		}

		select {
		case <-a.awakeWriteLoopCh:
		case <-a.closeWriteLoopCh:
			break loop
		}
	}

	a.setState(closed)
	a.closeAllTimers()

	a.log.Debugf("[%s] writeLoop exited", a.name)
}

func (a *Association) awakeWriteLoop() {
	select {
	case a.awakeWriteLoopCh <- struct{}{}:
	default:
	}
}

// unregisterStream un-registers a stream from the association
// The caller should hold the association write lock.
func (a *Association) unregisterStream(s *Stream, err error) {
	s.lock.Lock()
	defer s.lock.Unlock()

	delete(a.streams, s.streamIdentifier)
	s.readErr = err
	s.readNotifier.Broadcast()
}

// handleInbound parses incoming raw packets
func (a *Association) handleInbound(raw []byte) error {
	p := &packet{}
	if err := p.unmarshal(raw); err != nil {
		a.log.Warnf("[%s] unable to parse SCTP packet %s", a.name, err)
		return nil
	}

	if err := checkPacket(p); err != nil {
		a.log.Warnf("[%s] failed validating packet %s", a.name, err)
		return nil
	}

	a.handleChunkStart()

	for _, c := range p.chunks {
		if err := a.handleChunk(p, c); err != nil {
			return err
		}
	}

	a.handleChunkEnd()

	return nil
}

// The caller should hold the lock
func (a *Association) gatherOutboundDataAndReconfigPackets(rawPackets [][]byte) [][]byte {
	for _, p := range a.getDataPacketsToRetransmit() {
		raw, err := p.marshal()
		if err != nil {
			a.log.Warnf("[%s] failed to serialize a DATA packet to be retransmitted", a.name)
			continue
		}
		rawPackets = append(rawPackets, raw)
	}

	// Pop unsent data chunks from the pending queue to send as much as
	// cwnd and rwnd allow.
	chunks, sisToReset := a.popPendingDataChunksToSend()
	if len(chunks) > 0 {
		// Start timer. (noop if already started)
		a.log.Tracef("[%s] T3-rtx timer start (pt1)", a.name)
		a.t3RTX.start(a.rtoMgr.getRTO())
		for _, p := range a.bundleDataChunksIntoPackets(chunks) {
			raw, err := p.marshal()
			if err != nil {
				a.log.Warnf("[%s] failed to serialize a DATA packet", a.name)
				continue
			}
			rawPackets = append(rawPackets, raw)
		}
	}

	if len(sisToReset) > 0 || a.willRetransmitReconfig {
		if a.willRetransmitReconfig {
			a.willRetransmitReconfig = false
			a.log.Debugf("[%s] retransmit %d RECONFIG chunk(s)", a.name, len(a.reconfigs))
			for _, c := range a.reconfigs {
				p := a.createPacket([]chunk{c})
				raw, err := p.marshal()
				if err != nil {
					a.log.Warnf("[%s] failed to serialize a RECONFIG packet to be retransmitted", a.name)
				} else {
					rawPackets = append(rawPackets, raw)
				}
			}
		}

		if len(sisToReset) > 0 {
			rsn := a.generateNextRSN()
			tsn := a.myNextTSN - 1
			c := &chunkReconfig{
				paramA: &paramOutgoingResetRequest{
					reconfigRequestSequenceNumber: rsn,
					senderLastTSN:                 tsn,
					streamIdentifiers:             sisToReset,
				},
			}
			a.reconfigs[rsn] = c // store in the map for retransmission
			a.log.Debugf("[%s] sending RECONFIG: rsn=%d tsn=%d streams=%v",
				a.name, rsn, a.myNextTSN-1, sisToReset)
			p := a.createPacket([]chunk{c})
			raw, err := p.marshal()
			if err != nil {
				a.log.Warnf("[%s] failed to serialize a RECONFIG packet to be transmitted", a.name)
			} else {
				rawPackets = append(rawPackets, raw)
			}
		}

		if len(a.reconfigs) > 0 {
			a.tReconfig.start(a.rtoMgr.getRTO())
		}
	}

	return rawPackets
}

// The caller should hold the lock
func (a *Association) gatherOutboundFrastRetransmissionPackets(rawPackets [][]byte) [][]byte {
	if a.willRetransmitFast {
		a.willRetransmitFast = false

		toFastRetrans := []chunk{}
		fastRetransSize := commonHeaderSize

		for i := 0; ; i++ {
			c, ok := a.inflightQueue.get(a.cumulativeTSNAckPoint + uint32(i) + 1)
			if !ok {
				break // end of pending data
			}

			if c.acked || c.abandoned() {
				continue
			}

			if c.nSent > 1 || c.missIndicator < 3 {
				continue
			}

			// RFC 4960 Sec 7.2.4 Fast Retransmit on Gap Reports
			//  3)  Determine how many of the earliest (i.e., lowest TSN) DATA chunks
			//      marked for retransmission will fit into a single packet, subject
			//      to constraint of the path MTU of the destination transport
			//      address to which the packet is being sent.  Call this value K.
			//      Retransmit those K DATA chunks in a single packet.  When a Fast
			//      Retransmit is being performed, the sender SHOULD ignore the value
			//      of cwnd and SHOULD NOT delay retransmission for this single
			//		packet.

			dataChunkSize := dataChunkHeaderSize + uint32(len(c.userData))
			if a.mtu < fastRetransSize+dataChunkSize {
				break
			}

			fastRetransSize += dataChunkSize
			a.stats.incFastRetrans()
			c.nSent++
			a.checkPartialReliabilityStatus(c)
			toFastRetrans = append(toFastRetrans, c)
			a.log.Tracef("[%s] fast-retransmit: tsn=%d sent=%d htna=%d",
				a.name, c.tsn, c.nSent, a.fastRecoverExitPoint)
		}

		if len(toFastRetrans) > 0 {
			raw, err := a.createPacket(toFastRetrans).marshal()
			if err != nil {
				a.log.Warnf("[%s] failed to serialize a DATA packet to be fast-retransmitted", a.name)
			} else {
				rawPackets = append(rawPackets, raw)
			}
		}
	}

	return rawPackets
}

// The caller should hold the lock
func (a *Association) gatherOutboundSackPackets(rawPackets [][]byte) [][]byte {
	if a.ackState == ackStateImmediate {
		a.ackState = ackStateIdle
		sack := a.createSelectiveAckChunk()
		a.log.Debugf("[%s] sending SACK: %s", a.name, sack.String())
		raw, err := a.createPacket([]chunk{sack}).marshal()
		if err != nil {
			a.log.Warnf("[%s] failed to serialize a SACK packet", a.name)
		} else {
			rawPackets = append(rawPackets, raw)
		}
	}

	return rawPackets
}

// The caller should hold the lock
func (a *Association) gatherOutboundForwardTSNPackets(rawPackets [][]byte) [][]byte {
	if a.willSendForwardTSN {
		a.willSendForwardTSN = false
		if sna32GT(a.advancedPeerTSNAckPoint, a.cumulativeTSNAckPoint) {
			fwdtsn := a.createForwardTSN()
			raw, err := a.createPacket([]chunk{fwdtsn}).marshal()
			if err != nil {
				a.log.Warnf("[%s] failed to serialize a Forward TSN packet", a.name)
			} else {
				rawPackets = append(rawPackets, raw)
			}
		}
	}

	return rawPackets
}

// gatherOutbound gathers outgoing packets
func (a *Association) gatherOutbound() [][]byte {
	a.lock.Lock()
	defer a.lock.Unlock()

	rawPackets := [][]byte{}

	if a.controlQueue.size() > 0 {
		for _, p := range a.controlQueue.popAll() {
			raw, err := p.marshal()
			if err != nil {
				a.log.Warnf("[%s] failed to serialize a control packet", a.name)
				continue
			}
			rawPackets = append(rawPackets, raw)
		}
	}

	state := a.getState()

	if state == established {
		rawPackets = a.gatherOutboundDataAndReconfigPackets(rawPackets)
		rawPackets = a.gatherOutboundFrastRetransmissionPackets(rawPackets)
		rawPackets = a.gatherOutboundSackPackets(rawPackets)
		rawPackets = a.gatherOutboundForwardTSNPackets(rawPackets)
	}

	return rawPackets
}

func checkPacket(p *packet) error {
	// All packets must adhere to these rules

	// This is the SCTP sender's port number.  It can be used by the
	// receiver in combination with the source IP address, the SCTP
	// destination port, and possibly the destination IP address to
	// identify the association to which this packet belongs.  The port
	// number 0 MUST NOT be used.
	if p.sourcePort == 0 {
		return errors.Errorf("sctp packet must not have a source port of 0")
	}

	// This is the SCTP port number to which this packet is destined.
	// The receiving host will use this port number to de-multiplex the
	// SCTP packet to the correct receiving endpoint/application.  The
	// port number 0 MUST NOT be used.
	if p.destinationPort == 0 {
		return errors.Errorf("sctp packet must not have a destination port of 0")
	}

	// Check values on the packet that are specific to a particular chunk type
	for _, c := range p.chunks {
		switch c.(type) { // nolint:gocritic
		case *chunkInit:
			// An INIT or INIT ACK chunk MUST NOT be bundled with any other chunk.
			// They MUST be the only chunks present in the SCTP packets that carry
			// them.
			if len(p.chunks) != 1 {
				return errors.Errorf("init chunk must not be bundled with any other chunk")
			}

			// A packet containing an INIT chunk MUST have a zero Verification
			// Tag.
			if p.verificationTag != 0 {
				return errors.Errorf("init chunk expects a verification tag of 0 on the packet when out-of-the-blue")
			}
		}
	}

	return nil
}

func min16(a, b uint16) uint16 {
	if a < b {
		return a
	}
	return b
}

func max32(a, b uint32) uint32 {
	if a > b {
		return a
	}
	return b
}

func min32(a, b uint32) uint32 {
	if a < b {
		return a
	}
	return b
}

// setState atomically sets the state of the Association.
// The caller should hold the lock.
func (a *Association) setState(newState uint32) {
	oldState := atomic.SwapUint32(&a.state, newState)
	if newState != oldState {
		a.log.Debugf("[%s] state change: '%s' => '%s'",
			a.name,
			getAssociationStateString(oldState),
			getAssociationStateString(newState))
	}
}

// getState atomically returns the state of the Association.
func (a *Association) getState() uint32 {
	return atomic.LoadUint32(&a.state)
}

// BytesSent returns the number of bytes sent
func (a *Association) BytesSent() uint64 {
	return atomic.LoadUint64(&a.bytesSent)
}

// BytesReceived returns the number of bytes received
func (a *Association) BytesReceived() uint64 {
	return atomic.LoadUint64(&a.bytesReceived)
}

func setSupportedExtensions(init *chunkInitCommon) {
	// nolint:godox
	// TODO RFC5061 https://tools.ietf.org/html/rfc6525#section-5.2
	// An implementation supporting this (Supported Extensions Parameter)
	// extension MUST list the ASCONF, the ASCONF-ACK, and the AUTH chunks
	// in its INIT and INIT-ACK parameters.
	init.params = append(init.params, &paramSupportedExtensions{
		ChunkTypes: []chunkType{ctReconfig, ctForwardTSN},
	})
}

// The caller should hold the lock.
func (a *Association) handleInit(p *packet, i *chunkInit) ([]*packet, error) {
	state := a.getState()
	a.log.Debugf("[%s] chunkInit received in state '%s'", a.name, getAssociationStateString(state))

	// https://tools.ietf.org/html/rfc4960#section-5.2.1
	// Upon receipt of an INIT in the COOKIE-WAIT state, an endpoint MUST
	// respond with an INIT ACK using the same parameters it sent in its
	// original INIT chunk (including its Initiate Tag, unchanged).  When
	// responding, the endpoint MUST send the INIT ACK back to the same
	// address that the original INIT (sent by this endpoint) was sent.

	if state != closed && state != cookieWait && state != cookieEchoed {
		// 5.2.2.  Unexpected INIT in States Other than CLOSED, COOKIE-ECHOED,
		//        COOKIE-WAIT, and SHUTDOWN-ACK-SENT
		return nil, errors.Errorf("todo: handle Init when in state %s", getAssociationStateString(state))
	}

	// Should we be setting any of these permanently until we've ACKed further?
	a.myMaxNumInboundStreams = min16(i.numInboundStreams, a.myMaxNumInboundStreams)
	a.myMaxNumOutboundStreams = min16(i.numOutboundStreams, a.myMaxNumOutboundStreams)
	a.peerVerificationTag = i.initiateTag
	a.sourcePort = p.destinationPort
	a.destinationPort = p.sourcePort

	// 13.2 This is the last TSN received in sequence.  This value
	// is set initially by taking the peer's initial TSN,
	// received in the INIT or INIT ACK chunk, and
	// subtracting one from it.
	a.peerLastTSN = i.initialTSN - 1

	for _, param := range i.params {
		switch v := param.(type) { // nolint:gocritic
		case *paramSupportedExtensions:
			for _, t := range v.ChunkTypes {
				if t == ctForwardTSN {
					a.log.Debugf("[%s] use ForwardTSN (on init)\n", a.name)
					a.useForwardTSN = true
				}
			}
		}
	}
	if !a.useForwardTSN {
		a.log.Warnf("[%s] not using ForwardTSN (on init)\n", a.name)
	}

	outbound := &packet{}
	outbound.verificationTag = a.peerVerificationTag
	outbound.sourcePort = a.sourcePort
	outbound.destinationPort = a.destinationPort

	initAck := &chunkInitAck{}

	initAck.initialTSN = a.myNextTSN
	initAck.numOutboundStreams = a.myMaxNumOutboundStreams
	initAck.numInboundStreams = a.myMaxNumInboundStreams
	initAck.initiateTag = a.myVerificationTag
	initAck.advertisedReceiverWindowCredit = a.maxReceiveBufferSize

	if a.myCookie == nil {
		var err error
		if a.myCookie, err = newRandomStateCookie(); err != nil {
			return nil, err
		}
	}

	initAck.params = []param{a.myCookie}

	setSupportedExtensions(&initAck.chunkInitCommon)

	outbound.chunks = []chunk{initAck}

	return pack(outbound), nil
}

// The caller should hold the lock.
func (a *Association) handleInitAck(p *packet, i *chunkInitAck) error {
	state := a.getState()
	a.log.Debugf("[%s] chunkInitAck received in state '%s'", a.name, getAssociationStateString(state))
	if state != cookieWait {
		// RFC 4960
		// 5.2.3.  Unexpected INIT ACK
		//   If an INIT ACK is received by an endpoint in any state other than the
		//   COOKIE-WAIT state, the endpoint should discard the INIT ACK chunk.
		//   An unexpected INIT ACK usually indicates the processing of an old or
		//   duplicated INIT chunk.
		return nil
	}

	a.myMaxNumInboundStreams = min16(i.numInboundStreams, a.myMaxNumInboundStreams)
	a.myMaxNumOutboundStreams = min16(i.numOutboundStreams, a.myMaxNumOutboundStreams)
	a.peerVerificationTag = i.initiateTag
	a.peerLastTSN = i.initialTSN - 1
	if a.sourcePort != p.destinationPort ||
		a.destinationPort != p.sourcePort {
		a.log.Warnf("[%s] handleInitAck: port mismatch", a.name)
		return nil
	}

	a.rwnd = i.advertisedReceiverWindowCredit
	a.log.Debugf("[%s] initial rwnd=%d", a.name, a.rwnd)

	// RFC 4690 Sec 7.2.1
	//  o  The initial value of ssthresh MAY be arbitrarily high (for
	//     example, implementations MAY use the size of the receiver
	//     advertised window).
	a.ssthresh = a.rwnd
	a.log.Tracef("[%s] updated cwnd=%d ssthresh=%d inflight=%d (INI)",
		a.name, a.cwnd, a.ssthresh, a.inflightQueue.getNumBytes())

	a.t1Init.stop()
	a.storedInit = nil

	var cookieParam *paramStateCookie
	for _, param := range i.params {
		switch v := param.(type) {
		case *paramStateCookie:
			cookieParam = v
		case *paramSupportedExtensions:
			for _, t := range v.ChunkTypes {
				if t == ctForwardTSN {
					a.log.Debugf("[%s] use ForwardTSN (on initAck)\n", a.name)
					a.useForwardTSN = true
				}
			}
		}
	}
	if !a.useForwardTSN {
		a.log.Warnf("[%s] not using ForwardTSN (on initAck)\n", a.name)
	}
	if cookieParam == nil {
		return errors.Errorf("no cookie in InitAck")
	}

	a.storedCookieEcho = &chunkCookieEcho{}
	a.storedCookieEcho.cookie = cookieParam.cookie

	err := a.sendCookieEcho()
	if err != nil {
		a.log.Errorf("[%s] failed to send init: %s", a.name, err.Error())
	}

	a.t1Cookie.start(a.rtoMgr.getRTO())
	a.setState(cookieEchoed)
	return nil
}

// The caller should hold the lock.
func (a *Association) handleHeartbeat(c *chunkHeartbeat) []*packet {
	a.log.Tracef("[%s] chunkHeartbeat", a.name)
	hbi, ok := c.params[0].(*paramHeartbeatInfo)
	if !ok {
		a.log.Warnf("[%s] failed to handle Heartbeat, no ParamHeartbeatInfo", a.name)
	}

	return pack(&packet{
		verificationTag: a.peerVerificationTag,
		sourcePort:      a.sourcePort,
		destinationPort: a.destinationPort,
		chunks: []chunk{&chunkHeartbeatAck{
			params: []param{
				&paramHeartbeatInfo{
					heartbeatInformation: hbi.heartbeatInformation,
				},
			},
		}},
	})
}

// The caller should hold the lock.
func (a *Association) handleCookieEcho(c *chunkCookieEcho) []*packet {
	state := a.getState()
	a.log.Debugf("[%s] COOKIE-ECHO received in state '%s'", a.name, getAssociationStateString(state))
	switch state {
	default:
		return nil
	case established:
		if !bytes.Equal(a.myCookie.cookie, c.cookie) {
			return nil
		}
	case closed, cookieWait, cookieEchoed:
		if !bytes.Equal(a.myCookie.cookie, c.cookie) {
			return nil
		}

		a.t1Init.stop()
		a.storedInit = nil

		a.t1Cookie.stop()
		a.storedCookieEcho = nil

		a.setState(established)
		a.handshakeCompletedCh <- nil
	}

	p := &packet{
		verificationTag: a.peerVerificationTag,
		sourcePort:      a.sourcePort,
		destinationPort: a.destinationPort,
		chunks:          []chunk{&chunkCookieAck{}},
	}
	return pack(p)
}

// The caller should hold the lock.
func (a *Association) handleCookieAck() {
	state := a.getState()
	a.log.Debugf("[%s] COOKIE-ACK received in state '%s'", a.name, getAssociationStateString(state))
	if state != cookieEchoed {
		// RFC 4960
		// 5.2.5.  Handle Duplicate COOKIE-ACK.
		//   At any state other than COOKIE-ECHOED, an endpoint should silently
		//   discard a received COOKIE ACK chunk.
		return
	}

	a.t1Cookie.stop()
	a.storedCookieEcho = nil

	a.setState(established)
	a.handshakeCompletedCh <- nil
}

// The caller should hold the lock.
func (a *Association) handleData(d *chunkPayloadData) []*packet {
	a.log.Tracef("[%s] DATA: tsn=%d immediateSack=%v len=%d",
		a.name, d.tsn, d.immediateSack, len(d.userData))
	a.stats.incDATAs()

	canPush := a.payloadQueue.canPush(d, a.peerLastTSN)
	if canPush {
		s := a.getOrCreateStream(d.streamIdentifier)
		if s == nil {
			// silentely discard the data. (sender will retry on T3-rtx timeout)
			// see pion/sctp#30
			a.log.Debugf("discard %d", d.streamSequenceNumber)
			return nil
		}

		if a.getMyReceiverWindowCredit() > 0 {
			// Pass the new chunk to stream level as soon as it arrives
			a.payloadQueue.push(d, a.peerLastTSN)
			s.handleData(d)
		} else {
			// Receive buffer is full
			lastTSN, ok := a.payloadQueue.getLastTSNReceived()
			if ok && sna32LT(d.tsn, lastTSN) {
				a.log.Debugf("[%s] receive buffer full, but accepted as this is a missing chunk with tsn=%d ssn=%d", a.name, d.tsn, d.streamSequenceNumber)
				a.payloadQueue.push(d, a.peerLastTSN)
				s.handleData(d)
			} else {
				a.log.Debugf("[%s] receive buffer full. dropping DATA with tsn=%d ssn=%d", a.name, d.tsn, d.streamSequenceNumber)
			}
		}
	}

	return a.handlePeerLastTSNAndAcknowledgement(d.immediateSack)
}

// A common routine for handleData and handleForwardTSN routines
// The caller should hold the lock.
func (a *Association) handlePeerLastTSNAndAcknowledgement(sackImmediately bool) []*packet {
	var reply []*packet

	// Try to advance peerLastTSN

	// From RFC 3758 Sec 3.6:
	//   .. and then MUST further advance its cumulative TSN point locally
	//   if possible
	// Meaning, if peerLastTSN+1 points to a chunk that is received,
	// advance peerLastTSN until peerLastTSN+1 points to unreceived chunk.
	for {
		if _, popOk := a.payloadQueue.pop(a.peerLastTSN + 1); !popOk {
			break
		}
		a.peerLastTSN++

		for _, rstReq := range a.reconfigRequests {
			resp := a.resetStreamsIfAny(rstReq)
			if resp != nil {
				a.log.Debugf("[%s] RESET RESPONSE: %+v", a.name, resp)
				reply = append(reply, resp)
			}
		}
	}

	hasPacketLoss := (a.payloadQueue.size() > 0)
	if hasPacketLoss {
		a.log.Tracef("[%s] packetloss: %s", a.name, a.payloadQueue.getGapAckBlocksString(a.peerLastTSN))
	}

	if (a.ackState != ackStateImmediate && !sackImmediately && !hasPacketLoss && a.ackMode == ackModeNormal) || a.ackMode == ackModeAlwaysDelay {
		if a.ackState == ackStateIdle {
			a.delayedAckTriggered = true
		} else {
			a.immediateAckTriggered = true
		}
	} else {
		a.immediateAckTriggered = true
	}

	return reply
}

// The caller should hold the lock.
func (a *Association) getMyReceiverWindowCredit() uint32 {
	var bytesQueued uint32
	for _, s := range a.streams {
		bytesQueued += uint32(s.getNumBytesInReassemblyQueue())
	}

	if bytesQueued >= a.maxReceiveBufferSize {
		return 0
	}
	return a.maxReceiveBufferSize - bytesQueued
}

// OpenStream opens a stream
func (a *Association) OpenStream(streamIdentifier uint16, defaultPayloadType PayloadProtocolIdentifier) (*Stream, error) {
	a.lock.Lock()
	defer a.lock.Unlock()

	if _, ok := a.streams[streamIdentifier]; ok {
		return nil, errors.Errorf("there already exists a stream with identifier %d", streamIdentifier)
	}

	s := a.createStream(streamIdentifier, false)
	s.setDefaultPayloadType(defaultPayloadType)

	return s, nil
}

// AcceptStream accepts a stream
func (a *Association) AcceptStream() (*Stream, error) {
	s, ok := <-a.acceptCh
	if !ok {
		return nil, io.EOF // no more incoming streams
	}
	return s, nil
}

// createStream creates a stream. The caller should hold the lock and check no stream exists for this id.
func (a *Association) createStream(streamIdentifier uint16, accept bool) *Stream {
	s := &Stream{
		association:      a,
		streamIdentifier: streamIdentifier,
		reassemblyQueue:  newReassemblyQueue(streamIdentifier),
		log:              a.log,
		name:             fmt.Sprintf("%d:%s", streamIdentifier, a.name),
	}

	s.readNotifier = sync.NewCond(&s.lock)

	if accept {
		select {
		case a.acceptCh <- s:
			a.streams[streamIdentifier] = s
			a.log.Debugf("[%s] accepted a new stream (streamIdentifier: %d)",
				a.name, streamIdentifier)
		default:
			a.log.Debugf("[%s] dropped a new stream (acceptCh size: %d)",
				a.name, len(a.acceptCh))
			return nil
		}
	} else {
		a.streams[streamIdentifier] = s
	}

	return s
}

// getOrCreateStream gets or creates a stream. The caller should hold the lock.
func (a *Association) getOrCreateStream(streamIdentifier uint16) *Stream {
	if s, ok := a.streams[streamIdentifier]; ok {
		return s
	}

	return a.createStream(streamIdentifier, true)
}

// The caller should hold the lock.
func (a *Association) processSelectiveAck(d *chunkSelectiveAck) (map[uint16]int, uint32, error) { // nolint:gocognit
	bytesAckedPerStream := map[uint16]int{}

	// New ack point, so pop all ACKed packets from inflightQueue
	// We add 1 because the "currentAckPoint" has already been popped from the inflight queue
	// For the first SACK we take care of this by setting the ackpoint to cumAck - 1
	for i := a.cumulativeTSNAckPoint + 1; sna32LTE(i, d.cumulativeTSNAck); i++ {
		c, ok := a.inflightQueue.pop(i)
		if !ok {
			return nil, 0, errors.Errorf("tsn %v unable to be popped from inflight queue", i)
		}

		if !c.acked {
			// RFC 4096 sec 6.3.2.  Retransmission Timer Rules
			//   R3)  Whenever a SACK is received that acknowledges the DATA chunk
			//        with the earliest outstanding TSN for that address, restart the
			//        T3-rtx timer for that address with its current RTO (if there is
			//        still outstanding data on that address).
			if i == a.cumulativeTSNAckPoint+1 {
				// T3 timer needs to be reset. Stop it for now.
				a.t3RTX.stop()
			}

			nBytesAcked := len(c.userData)

			// Sum the number of bytes acknowledged per stream
			if amount, ok := bytesAckedPerStream[c.streamIdentifier]; ok {
				bytesAckedPerStream[c.streamIdentifier] = amount + nBytesAcked
			} else {
				bytesAckedPerStream[c.streamIdentifier] = nBytesAcked
			}

			// RFC 4960 sec 6.3.1.  RTO Calculation
			//   C4)  When data is in flight and when allowed by rule C5 below, a new
			//        RTT measurement MUST be made each round trip.  Furthermore, new
			//        RTT measurements SHOULD be made no more than once per round trip
			//        for a given destination transport address.
			//   C5)  Karn's algorithm: RTT measurements MUST NOT be made using
			//        packets that were retransmitted (and thus for which it is
			//        ambiguous whether the reply was for the first instance of the
			//        chunk or for a later instance)
			if c.nSent == 1 && sna32GTE(c.tsn, a.minTSN2MeasureRTT) {
				a.minTSN2MeasureRTT = a.myNextTSN
				rtt := time.Since(c.since).Seconds() * 1000.0
				srtt := a.rtoMgr.setNewRTT(rtt)
				a.log.Tracef("[%s] SACK: measured-rtt=%f srtt=%f new-rto=%f",
					a.name, rtt, srtt, a.rtoMgr.getRTO())
			}
		}

		if a.inFastRecovery && c.tsn == a.fastRecoverExitPoint {
			a.log.Debugf("[%s] exit fast-recovery", a.name)
			a.inFastRecovery = false
		}
	}

	htna := d.cumulativeTSNAck

	// Mark selectively acknowledged chunks as "acked"
	for _, g := range d.gapAckBlocks {
		for i := g.start; i <= g.end; i++ {
			tsn := d.cumulativeTSNAck + uint32(i)
			c, ok := a.inflightQueue.get(tsn)
			if !ok {
				return nil, 0, errors.Errorf("requested non-existent TSN %v", tsn)
			}

			if !c.acked {
				nBytesAcked := a.inflightQueue.markAsAcked(tsn)

				// Sum the number of bytes acknowledged per stream
				if amount, ok := bytesAckedPerStream[c.streamIdentifier]; ok {
					bytesAckedPerStream[c.streamIdentifier] = amount + nBytesAcked
				} else {
					bytesAckedPerStream[c.streamIdentifier] = nBytesAcked
				}

				a.log.Tracef("[%s] tsn=%d has been sacked", a.name, c.tsn)

				if c.nSent == 1 {
					a.minTSN2MeasureRTT = a.myNextTSN
					rtt := time.Since(c.since).Seconds() * 1000.0
					srtt := a.rtoMgr.setNewRTT(rtt)
					a.log.Tracef("[%s] SACK: measured-rtt=%f srtt=%f new-rto=%f",
						a.name, rtt, srtt, a.rtoMgr.getRTO())
				}

				if sna32LT(htna, tsn) {
					htna = tsn
				}
			}
		}
	}

	return bytesAckedPerStream, htna, nil
}

// The caller should hold the lock.
func (a *Association) onCumulativeTSNAckPointAdvanced(totalBytesAcked int) {
	// RFC 4096, sec 6.3.2.  Retransmission Timer Rules
	//   R2)  Whenever all outstanding data sent to an address have been
	//        acknowledged, turn off the T3-rtx timer of that address.
	if a.inflightQueue.size() == 0 {
		a.log.Tracef("[%s] SACK: no more packet in-flight (pending=%d)", a.name, a.pendingQueue.size())
		a.t3RTX.stop()
	} else {
		a.log.Tracef("[%s] T3-rtx timer start (pt2)", a.name)
		a.t3RTX.start(a.rtoMgr.getRTO())
	}

	// Update congestion control parameters
	if a.cwnd <= a.ssthresh {
		// RFC 4096, sec 7.2.1.  Slow-Start
		//   o  When cwnd is less than or equal to ssthresh, an SCTP endpoint MUST
		//		use the slow-start algorithm to increase cwnd only if the current
		//      congestion window is being fully utilized, an incoming SACK
		//      advances the Cumulative TSN Ack Point, and the data sender is not
		//      in Fast Recovery.  Only when these three conditions are met can
		//      the cwnd be increased; otherwise, the cwnd MUST not be increased.
		//		If these conditions are met, then cwnd MUST be increased by, at
		//      most, the lesser of 1) the total size of the previously
		//      outstanding DATA chunk(s) acknowledged, and 2) the destination's
		//      path MTU.
		if !a.inFastRecovery &&
			a.pendingQueue.size() > 0 {
			a.cwnd += min32(uint32(totalBytesAcked), a.cwnd) // TCP way
			// a.cwnd += min32(uint32(totalBytesAcked), a.mtu) // SCTP way (slow)
			a.log.Tracef("[%s] updated cwnd=%d ssthresh=%d acked=%d (SS)",
				a.name, a.cwnd, a.ssthresh, totalBytesAcked)
		} else {
			a.log.Tracef("[%s] cwnd did not grow: cwnd=%d ssthresh=%d acked=%d FR=%v pending=%d",
				a.name, a.cwnd, a.ssthresh, totalBytesAcked, a.inFastRecovery, a.pendingQueue.size())
		}
	} else {
		// RFC 4096, sec 7.2.2.  Congestion Avoidance
		//   o  Whenever cwnd is greater than ssthresh, upon each SACK arrival
		//      that advances the Cumulative TSN Ack Point, increase
		//      partial_bytes_acked by the total number of bytes of all new chunks
		//      acknowledged in that SACK including chunks acknowledged by the new
		//      Cumulative TSN Ack and by Gap Ack Blocks.
		a.partialBytesAcked += uint32(totalBytesAcked)

		//   o  When partial_bytes_acked is equal to or greater than cwnd and
		//      before the arrival of the SACK the sender had cwnd or more bytes
		//      of data outstanding (i.e., before arrival of the SACK, flight size
		//      was greater than or equal to cwnd), increase cwnd by MTU, and
		//      reset partial_bytes_acked to (partial_bytes_acked - cwnd).
		if a.partialBytesAcked >= a.cwnd && a.pendingQueue.size() > 0 {
			a.partialBytesAcked -= a.cwnd
			a.cwnd += a.mtu
			a.log.Tracef("[%s] updated cwnd=%d ssthresh=%d acked=%d (CA)",
				a.name, a.cwnd, a.ssthresh, totalBytesAcked)
		}
	}
}

// The caller should hold the lock.
func (a *Association) processFastRetransmission(cumTSNAckPoint, htna uint32, cumTSNAckPointAdvanced bool) error {
	// HTNA algorithm - RFC 4960 Sec 7.2.4
	// Increment missIndicator of each chunks that the SACK reported missing
	// when either of the following is met:
	// a)  Not in fast-recovery
	//     miss indications are incremented only for missing TSNs prior to the
	//     highest TSN newly acknowledged in the SACK.
	// b)  In fast-recovery AND the Cumulative TSN Ack Point advanced
	//     the miss indications are incremented for all TSNs reported missing
	//     in the SACK.
	if !a.inFastRecovery || (a.inFastRecovery && cumTSNAckPointAdvanced) {
		var maxTSN uint32
		if !a.inFastRecovery {
			// a) increment only for missing TSNs prior to the HTNA
			maxTSN = htna
		} else {
			// b) increment for all TSNs reported missing
			maxTSN = cumTSNAckPoint + uint32(a.inflightQueue.size()) + 1
		}

		for tsn := cumTSNAckPoint + 1; sna32LT(tsn, maxTSN); tsn++ {
			c, ok := a.inflightQueue.get(tsn)
			if !ok {
				return errors.Errorf("requested non-existent TSN %v", tsn)
			}
			if !c.acked && !c.abandoned() && c.missIndicator < 3 {
				c.missIndicator++
				if c.missIndicator == 3 {
					if !a.inFastRecovery {
						// 2)  If not in Fast Recovery, adjust the ssthresh and cwnd of the
						//     destination address(es) to which the missing DATA chunks were
						//     last sent, according to the formula described in Section 7.2.3.
						a.inFastRecovery = true
						a.fastRecoverExitPoint = htna
						a.ssthresh = max32(a.cwnd/2, 4*a.mtu)
						a.cwnd = a.ssthresh
						a.partialBytesAcked = 0
						a.willRetransmitFast = true

						a.log.Tracef("[%s] updated cwnd=%d ssthresh=%d inflight=%d (FR)",
							a.name, a.cwnd, a.ssthresh, a.inflightQueue.getNumBytes())
					}
				}
			}
		}
	}

	if a.inFastRecovery && cumTSNAckPointAdvanced {
		a.willRetransmitFast = true
	}

	return nil
}

// The caller should hold the lock.
func (a *Association) handleSack(d *chunkSelectiveAck) error {
	a.log.Tracef("[%s] SACK: cumTSN=%d a_rwnd=%d", a.name, d.cumulativeTSNAck, d.advertisedReceiverWindowCredit)
	state := a.getState()
	if state != established {
		return nil
	}

	a.stats.incSACKs()

	if sna32GT(a.cumulativeTSNAckPoint, d.cumulativeTSNAck) {
		// RFC 4960 sec 6.2.1.  Processing a Received SACK
		// D)
		//   i) If Cumulative TSN Ack is less than the Cumulative TSN Ack
		//      Point, then drop the SACK.  Since Cumulative TSN Ack is
		//      monotonically increasing, a SACK whose Cumulative TSN Ack is
		//      less than the Cumulative TSN Ack Point indicates an out-of-
		//      order SACK.

		a.log.Debugf("[%s] SACK Cumulative ACK %v is older than ACK point %v",
			a.name,
			d.cumulativeTSNAck,
			a.cumulativeTSNAckPoint)

		return nil
	}

	// Process selective ack
	bytesAckedPerStream, htna, err := a.processSelectiveAck(d)
	if err != nil {
		return err
	}

	var totalBytesAcked int
	for _, nBytesAcked := range bytesAckedPerStream {
		totalBytesAcked += nBytesAcked
	}

	cumTSNAckPointAdvanced := false
	if sna32LT(a.cumulativeTSNAckPoint, d.cumulativeTSNAck) {
		a.log.Tracef("[%s] SACK: cumTSN advanced: %d -> %d",
			a.name,
			a.cumulativeTSNAckPoint,
			d.cumulativeTSNAck)

		a.cumulativeTSNAckPoint = d.cumulativeTSNAck
		cumTSNAckPointAdvanced = true
		a.onCumulativeTSNAckPointAdvanced(totalBytesAcked)
	}

	for si, nBytesAcked := range bytesAckedPerStream {
		if s, ok := a.streams[si]; ok {
			a.lock.Unlock()
			s.onBufferReleased(nBytesAcked)
			a.lock.Lock()
		}
	}

	// New rwnd value
	// RFC 4960 sec 6.2.1.  Processing a Received SACK
	// D)
	//   ii) Set rwnd equal to the newly received a_rwnd minus the number
	//       of bytes still outstanding after processing the Cumulative
	//       TSN Ack and the Gap Ack Blocks.

	// bytes acked were already subtracted by markAsAcked() method
	bytesOutstanding := uint32(a.inflightQueue.getNumBytes())
	if bytesOutstanding >= d.advertisedReceiverWindowCredit {
		a.rwnd = 0
	} else {
		a.rwnd = d.advertisedReceiverWindowCredit - bytesOutstanding
	}

	err = a.processFastRetransmission(d.cumulativeTSNAck, htna, cumTSNAckPointAdvanced)
	if err != nil {
		return err
	}

	if a.useForwardTSN {
		// RFC 3758 Sec 3.5 C1
		if sna32LT(a.advancedPeerTSNAckPoint, a.cumulativeTSNAckPoint) {
			a.advancedPeerTSNAckPoint = a.cumulativeTSNAckPoint
		}

		// RFC 3758 Sec 3.5 C2
		for i := a.advancedPeerTSNAckPoint + 1; ; i++ {
			c, ok := a.inflightQueue.get(i)
			if !ok {
				break
			}
			if !c.abandoned() {
				break
			}
			a.advancedPeerTSNAckPoint = i
		}

		// RFC 3758 Sec 3.5 C3
		if sna32GT(a.advancedPeerTSNAckPoint, a.cumulativeTSNAckPoint) {
			a.willSendForwardTSN = true
		}
		a.awakeWriteLoop()
	}

	if a.inflightQueue.size() > 0 {
		// Start timer. (noop if already started)
		a.log.Tracef("[%s] T3-rtx timer start (pt3)", a.name)
		a.t3RTX.start(a.rtoMgr.getRTO())
	}

	if cumTSNAckPointAdvanced {
		a.awakeWriteLoop()
	}

	return nil
}

// createForwardTSN generates ForwardTSN chunk.
// This method will be be called if useForwardTSN is set to false.
// The caller should hold the lock.
func (a *Association) createForwardTSN() *chunkForwardTSN {
	// RFC 3758 Sec 3.5 C4
	streamMap := map[uint16]uint16{} // to report only once per SI
	for i := a.cumulativeTSNAckPoint + 1; sna32LTE(i, a.advancedPeerTSNAckPoint); i++ {
		c, ok := a.inflightQueue.get(i)
		if !ok {
			break
		}

		ssn, ok := streamMap[c.streamIdentifier]
		if !ok {
			streamMap[c.streamIdentifier] = c.streamSequenceNumber
		} else if sna16LT(ssn, c.streamSequenceNumber) {
			// to report only once with greatest SSN
			streamMap[c.streamIdentifier] = c.streamSequenceNumber
		}
	}

	fwdtsn := &chunkForwardTSN{
		newCumulativeTSN: a.advancedPeerTSNAckPoint,
		streams:          []chunkForwardTSNStream{},
	}

	var streamStr string
	for si, ssn := range streamMap {
		streamStr += fmt.Sprintf("(si=%d ssn=%d)", si, ssn)
		fwdtsn.streams = append(fwdtsn.streams, chunkForwardTSNStream{
			identifier: si,
			sequence:   ssn,
		})
	}
	a.log.Tracef("[%s] building fwdtsn: newCumulativeTSN=%d cumTSN=%d - %s", a.name, fwdtsn.newCumulativeTSN, a.cumulativeTSNAckPoint, streamStr)

	return fwdtsn
}

// createPacket wraps chunks in a packet.
// The caller should hold the read lock.
func (a *Association) createPacket(cs []chunk) *packet {
	return &packet{
		verificationTag: a.peerVerificationTag,
		sourcePort:      a.sourcePort,
		destinationPort: a.destinationPort,
		chunks:          cs,
	}
}

// The caller should hold the lock.
func (a *Association) handleReconfig(c *chunkReconfig) ([]*packet, error) {
	a.log.Tracef("[%s] handleReconfig", a.name)

	pp := make([]*packet, 0)

	p, err := a.handleReconfigParam(c.paramA)
	if err != nil {
		return nil, err
	}
	if p != nil {
		pp = append(pp, p)
	}

	if c.paramB != nil {
		p, err = a.handleReconfigParam(c.paramB)
		if err != nil {
			return nil, err
		}
		if p != nil {
			pp = append(pp, p)
		}
	}
	return pp, nil
}

// The caller should hold the lock.
func (a *Association) handleForwardTSN(c *chunkForwardTSN) []*packet {
	a.log.Tracef("[%s] FwdTSN: %s", a.name, c.String())

	if !a.useForwardTSN {
		a.log.Warn("[%s] received FwdTSN but not enabled")
		// Return an error chunk
		cerr := &chunkError{
			errorCauses: []errorCause{&errorCauseUnrecognizedChunkType{}},
		}
		outbound := &packet{}
		outbound.verificationTag = a.peerVerificationTag
		outbound.sourcePort = a.sourcePort
		outbound.destinationPort = a.destinationPort
		outbound.chunks = []chunk{cerr}
		return []*packet{outbound}
	}

	// From RFC 3758 Sec 3.6:
	//   Note, if the "New Cumulative TSN" value carried in the arrived
	//   FORWARD TSN chunk is found to be behind or at the current cumulative
	//   TSN point, the data receiver MUST treat this FORWARD TSN as out-of-
	//   date and MUST NOT update its Cumulative TSN.  The receiver SHOULD
	//   send a SACK to its peer (the sender of the FORWARD TSN) since such a
	//   duplicate may indicate the previous SACK was lost in the network.

	a.log.Tracef("[%s] should send ack? newCumTSN=%d peerLastTSN=%d\n",
		a.name, c.newCumulativeTSN, a.peerLastTSN)
	if sna32LTE(c.newCumulativeTSN, a.peerLastTSN) {
		a.log.Tracef("[%s] sending ack on Forward TSN", a.name)
		a.ackState = ackStateImmediate
		a.ackTimer.stop()
		a.awakeWriteLoop()
		return nil
	}

	// From RFC 3758 Sec 3.6:
	//   the receiver MUST perform the same TSN handling, including duplicate
	//   detection, gap detection, SACK generation, cumulative TSN
	//   advancement, etc. as defined in RFC 2960 [2]---with the following
	//   exceptions and additions.

	//   When a FORWARD TSN chunk arrives, the data receiver MUST first update
	//   its cumulative TSN point to the value carried in the FORWARD TSN
	//   chunk,

	// Advance peerLastTSN
	for sna32LT(a.peerLastTSN, c.newCumulativeTSN) {
		a.payloadQueue.pop(a.peerLastTSN + 1) // may not exist
		a.peerLastTSN++
	}

	// Report new peerLastTSN value and abandoned largest SSN value to
	// corresponding streams so that the abandoned chunks can be removed
	// from the reassemblyQueue.
	for _, forwarded := range c.streams {
		if s, ok := a.streams[forwarded.identifier]; ok {
			s.handleForwardTSNForOrdered(forwarded.sequence)
		}
	}

	// TSN may be forewared for unordered chunks. ForwardTSN chunk does not
	// report which stream identifier it skipped for unordered chunks.
	// Therefore, we need to broadcast this event to all existing streams for
	// unordered chunks.
	// See https://github.com/pion/sctp/issues/106
	for _, s := range a.streams {
		s.handleForwardTSNForUnordered(c.newCumulativeTSN)
	}

	return a.handlePeerLastTSNAndAcknowledgement(false)
}

func (a *Association) sendResetRequest(streamIdentifier uint16) error {
	a.lock.Lock()
	defer a.lock.Unlock()

	state := a.getState()
	if state != established {
		return errors.Errorf("sending reset packet in non-established state: state=%s",
			getAssociationStateString(state))
	}

	// Create DATA chunk which only contains valid stream identifier with
	// nil userData and use it as a EOS from the stream.
	c := &chunkPayloadData{
		streamIdentifier:  streamIdentifier,
		beginningFragment: true,
		endingFragment:    true,
		userData:          nil,
	}

	a.pendingQueue.push(c)
	a.awakeWriteLoop()
	return nil
}

// The caller should hold the lock.
func (a *Association) handleReconfigParam(raw param) (*packet, error) {
	switch p := raw.(type) {
	case *paramOutgoingResetRequest:
		a.reconfigRequests[p.reconfigRequestSequenceNumber] = p
		resp := a.resetStreamsIfAny(p)
		if resp != nil {
			return resp, nil
		}
		return nil, nil

	case *paramReconfigResponse:
		delete(a.reconfigs, p.reconfigResponseSequenceNumber)
		if len(a.reconfigs) == 0 {
			a.tReconfig.stop()
		}
		return nil, nil
	default:
		return nil, errors.Errorf("unexpected parameter type %T", p)
	}
}

// The caller should hold the lock.
func (a *Association) resetStreamsIfAny(p *paramOutgoingResetRequest) *packet {
	result := reconfigResultSuccessPerformed
	if sna32LTE(p.senderLastTSN, a.peerLastTSN) {
		a.log.Debugf("[%s] resetStream(): senderLastTSN=%d <= peerLastTSN=%d",
			a.name, p.senderLastTSN, a.peerLastTSN)
		for _, id := range p.streamIdentifiers {
			s, ok := a.streams[id]
			if !ok {
				continue
			}
			a.unregisterStream(s, io.EOF)
		}
		delete(a.reconfigRequests, p.reconfigRequestSequenceNumber)
	} else {
		a.log.Debugf("[%s] resetStream(): senderLastTSN=%d > peerLastTSN=%d",
			a.name, p.senderLastTSN, a.peerLastTSN)
		result = reconfigResultInProgress
	}

	return a.createPacket([]chunk{&chunkReconfig{
		paramA: &paramReconfigResponse{
			reconfigResponseSequenceNumber: p.reconfigRequestSequenceNumber,
			result:                         result,
		},
	}})
}

// Move the chunk peeked with a.pendingQueue.peek() to the inflightQueue.
// The caller should hold the lock.
func (a *Association) movePendingDataChunkToInflightQueue(c *chunkPayloadData) {
	if err := a.pendingQueue.pop(c); err != nil {
		a.log.Errorf("[%s] failed to pop from pending queue: %s", a.name, err.Error())
	}

	// Mark all fragements are in-flight now
	if c.endingFragment {
		c.setAllInflight()
	}

	// Assign TSN
	c.tsn = a.generateNextTSN()

	c.since = time.Now() // use to calculate RTT and also for maxPacketLifeTime
	c.nSent = 1          // being sent for the first time

	a.checkPartialReliabilityStatus(c)

	a.log.Tracef("[%s] sending ppi=%d tsn=%d ssn=%d sent=%d len=%d (%v,%v)",
		a.name, c.payloadType, c.tsn, c.streamSequenceNumber, c.nSent, len(c.userData), c.beginningFragment, c.endingFragment)

	// Push it into the inflightQueue
	a.inflightQueue.pushNoCheck(c)
}

// popPendingDataChunksToSend pops chunks from the pending queues as many as
// the cwnd and rwnd allows to send.
// The caller should hold the lock.
func (a *Association) popPendingDataChunksToSend() ([]*chunkPayloadData, []uint16) {
	chunks := []*chunkPayloadData{}
	var sisToReset []uint16 // stream identifieres to reset

	if a.pendingQueue.size() > 0 {
		// RFC 4960 sec 6.1.  Transmission of DATA Chunks
		//   A) At any given time, the data sender MUST NOT transmit new data to
		//      any destination transport address if its peer's rwnd indicates
		//      that the peer has no buffer space (i.e., rwnd is 0; see Section
		//      6.2.1).  However, regardless of the value of rwnd (including if it
		//      is 0), the data sender can always have one DATA chunk in flight to
		//      the receiver if allowed by cwnd (see rule B, below).

		for {
			c := a.pendingQueue.peek()
			if c == nil {
				break // no more pending data
			}

			dataLen := uint32(len(c.userData))
			if dataLen == 0 {
				sisToReset = append(sisToReset, c.streamIdentifier)
				err := a.pendingQueue.pop(c)
				if err != nil {
					a.log.Errorf("failed to pop from pending queue: %s", err.Error())
				}
				continue
			}

			if uint32(a.inflightQueue.getNumBytes())+dataLen > a.cwnd {
				break // would exceeds cwnd
			}

			if dataLen > a.rwnd {
				break // no more rwnd
			}

			a.rwnd -= dataLen

			a.movePendingDataChunkToInflightQueue(c)
			chunks = append(chunks, c)
		}

		// the data sender can always have one DATA chunk in flight to the receiver
		if len(chunks) == 0 && a.inflightQueue.size() == 0 {
			// Send zero window probe
			c := a.pendingQueue.peek()
			if c != nil {
				a.movePendingDataChunkToInflightQueue(c)
				chunks = append(chunks, c)
			}
		}
	}

	return chunks, sisToReset
}

// bundleDataChunksIntoPackets packs DATA chunks into packets. It tries to bundle
// DATA chunks into a packet so long as the resulting packet size does not exceed
// the path MTU.
// The caller should hold the lock.
func (a *Association) bundleDataChunksIntoPackets(chunks []*chunkPayloadData) []*packet {
	packets := []*packet{}
	chunksToSend := []chunk{}
	bytesInPacket := int(commonHeaderSize)

	for _, c := range chunks {
		// RFC 4960 sec 6.1.  Transmission of DATA Chunks
		//   Multiple DATA chunks committed for transmission MAY be bundled in a
		//   single packet.  Furthermore, DATA chunks being retransmitted MAY be
		//   bundled with new DATA chunks, as long as the resulting packet size
		//   does not exceed the path MTU.
		if bytesInPacket+len(c.userData) > int(a.mtu) {
			packets = append(packets, a.createPacket(chunksToSend))
			chunksToSend = []chunk{}
			bytesInPacket = int(commonHeaderSize)
		}

		chunksToSend = append(chunksToSend, c)
		bytesInPacket += int(dataChunkHeaderSize) + len(c.userData)
	}

	if len(chunksToSend) > 0 {
		packets = append(packets, a.createPacket(chunksToSend))
	}

	return packets
}

// sendPayloadData sends the data chunks.
func (a *Association) sendPayloadData(chunks []*chunkPayloadData) error {
	a.lock.Lock()
	defer a.lock.Unlock()

	state := a.getState()
	if state != established {
		return errors.Errorf("sending payload data in non-established state: state=%s",
			getAssociationStateString(state))
	}

	// Push the chunks into the pending queue first.
	for _, c := range chunks {
		a.pendingQueue.push(c)
	}

	a.awakeWriteLoop()
	return nil
}

// The caller should hold the lock.
func (a *Association) checkPartialReliabilityStatus(c *chunkPayloadData) {
	if !a.useForwardTSN {
		return
	}

	// draft-ietf-rtcweb-data-protocol-09.txt section 6
	//	6.  Procedures
	//		All Data Channel Establishment Protocol messages MUST be sent using
	//		ordered delivery and reliable transmission.
	//
	if c.payloadType == PayloadTypeWebRTCDCEP {
		return
	}

	// PR-SCTP
	if s, ok := a.streams[c.streamIdentifier]; ok {
		s.lock.RLock()
		if s.reliabilityType == ReliabilityTypeRexmit {
			if c.nSent >= s.reliabilityValue {
				c.setAbandoned(true)
				a.log.Tracef("[%s] marked as abandoned: tsn=%d ppi=%d (remix: %d)", a.name, c.tsn, c.payloadType, c.nSent)
			}
		} else if s.reliabilityType == ReliabilityTypeTimed {
			elapsed := int64(time.Since(c.since).Seconds() * 1000)
			if elapsed >= int64(s.reliabilityValue) {
				c.setAbandoned(true)
				a.log.Tracef("[%s] marked as abandoned: tsn=%d ppi=%d (timed: %d)", a.name, c.tsn, c.payloadType, elapsed)
			}
		}
		s.lock.RUnlock()
	} else {
		a.log.Errorf("[%s] stream %d not found)", a.name, c.streamIdentifier)
	}
}

// getDataPacketsToRetransmit is called when T3-rtx is timed out and retransmit outstanding data chunks
// that are not acked or abandoned yet.
// The caller should hold the lock.
func (a *Association) getDataPacketsToRetransmit() []*packet {
	awnd := min32(a.cwnd, a.rwnd)
	chunks := []*chunkPayloadData{}
	var bytesToSend int
	var done bool

	for i := 0; !done; i++ {
		c, ok := a.inflightQueue.get(a.cumulativeTSNAckPoint + uint32(i) + 1)
		if !ok {
			break // end of pending data
		}

		if !c.retransmit {
			continue
		}

		if i == 0 && int(a.rwnd) < len(c.userData) {
			// Send it as a zero window probe
			done = true
		} else if bytesToSend+len(c.userData) > int(awnd) {
			break
		}

		// reset the retransmit flag not to retransmit again before the next
		// t3-rtx timer fires
		c.retransmit = false
		bytesToSend += len(c.userData)

		c.nSent++

		a.checkPartialReliabilityStatus(c)

		a.log.Tracef("[%s] retransmitting tsn=%d ssn=%d sent=%d", a.name, c.tsn, c.streamSequenceNumber, c.nSent)

		chunks = append(chunks, c)
	}

	return a.bundleDataChunksIntoPackets(chunks)
}

// generateNextTSN returns the myNextTSN and increases it. The caller should hold the lock.
// The caller should hold the lock.
func (a *Association) generateNextTSN() uint32 {
	tsn := a.myNextTSN
	a.myNextTSN++
	return tsn
}

// generateNextRSN returns the myNextRSN and increases it. The caller should hold the lock.
// The caller should hold the lock.
func (a *Association) generateNextRSN() uint32 {
	rsn := a.myNextRSN
	a.myNextRSN++
	return rsn
}

func (a *Association) createSelectiveAckChunk() *chunkSelectiveAck {
	sack := &chunkSelectiveAck{}
	sack.cumulativeTSNAck = a.peerLastTSN
	sack.advertisedReceiverWindowCredit = a.getMyReceiverWindowCredit()
	sack.duplicateTSN = a.payloadQueue.popDuplicates()
	sack.gapAckBlocks = a.payloadQueue.getGapAckBlocks(a.peerLastTSN)
	return sack
}

func pack(p *packet) []*packet {
	return []*packet{p}
}

func (a *Association) handleChunkStart() {
	a.lock.Lock()
	defer a.lock.Unlock()

	a.delayedAckTriggered = false
	a.immediateAckTriggered = false
}

func (a *Association) handleChunkEnd() {
	a.lock.Lock()
	defer a.lock.Unlock()

	if a.immediateAckTriggered {
		// Send SACK now!
		a.ackState = ackStateImmediate
		a.ackTimer.stop()
		a.awakeWriteLoop()
	} else if a.delayedAckTriggered {
		// Will send delayed ack in the next ack timeout
		a.ackState = ackStateDelay
		a.ackTimer.start()
	}
}

func (a *Association) handleChunk(p *packet, c chunk) error {
	a.lock.Lock()
	defer a.lock.Unlock()

	var packets []*packet
	var err error

	if _, err = c.check(); err != nil {
		a.log.Errorf("[ %s ] failed validating chunk: %s ", a.name, err)
		return nil
	}

	switch c := c.(type) {
	case *chunkInit:
		packets, err = a.handleInit(p, c)

	case *chunkInitAck:
		err = a.handleInitAck(p, c)

	case *chunkAbort:
		var errStr string
		for _, e := range c.errorCauses {
			errStr += fmt.Sprintf("(%s)", e)
		}
		return fmt.Errorf("[%s] %w: %s", a.name, errChunk, errStr)

	case *chunkError:
		var errStr string
		for _, e := range c.errorCauses {
			errStr += fmt.Sprintf("(%s)", e)
		}
		a.log.Debugf("[%s] Error chunk, with following errors: %s", a.name, errStr)

	case *chunkHeartbeat:
		packets = a.handleHeartbeat(c)

	case *chunkCookieEcho:
		packets = a.handleCookieEcho(c)

	case *chunkCookieAck:
		a.handleCookieAck()

	case *chunkPayloadData:
		packets = a.handleData(c)

	case *chunkSelectiveAck:
		err = a.handleSack(c)

	case *chunkReconfig:
		packets, err = a.handleReconfig(c)

	case *chunkForwardTSN:
		packets = a.handleForwardTSN(c)

	default:
		err = errors.Errorf("unhandled chunk type")
	}

	// Log and return, the only condition that is fatal is a ABORT chunk
	if err != nil {
		a.log.Errorf("Failed to handle chunk: %v", err)
		return nil
	}

	if len(packets) > 0 {
		a.controlQueue.pushAll(packets)
		a.awakeWriteLoop()
	}

	return nil
}

func (a *Association) onRetransmissionTimeout(id int, nRtos uint) {
	a.lock.Lock()
	defer a.lock.Unlock()

	if id == timerT1Init {
		err := a.sendInit()
		if err != nil {
			a.log.Debugf("[%s] failed to retransmit init (nRtos=%d): %v", a.name, nRtos, err)
		}
		return
	}

	if id == timerT1Cookie {
		err := a.sendCookieEcho()
		if err != nil {
			a.log.Debugf("[%s] failed to retransmit cookie-echo (nRtos=%d): %v", a.name, nRtos, err)
		}
		return
	}

	if id == timerT3RTX {
		a.stats.incT3Timeouts()

		// RFC 4960 sec 6.3.3
		//  E1)  For the destination address for which the timer expires, adjust
		//       its ssthresh with rules defined in Section 7.2.3 and set the
		//       cwnd <- MTU.
		// RFC 4960 sec 7.2.3
		//   When the T3-rtx timer expires on an address, SCTP should perform slow
		//   start by:
		//      ssthresh = max(cwnd/2, 4*MTU)
		//      cwnd = 1*MTU

		a.ssthresh = max32(a.cwnd/2, 4*a.mtu)
		a.cwnd = a.mtu
		a.log.Tracef("[%s] updated cwnd=%d ssthresh=%d inflight=%d (RTO)",
			a.name, a.cwnd, a.ssthresh, a.inflightQueue.getNumBytes())

		// RFC 3758 sec 3.5
		//  A5) Any time the T3-rtx timer expires, on any destination, the sender
		//  SHOULD try to advance the "Advanced.Peer.Ack.Point" by following
		//  the procedures outlined in C2 - C5.
		if a.useForwardTSN {
			// RFC 3758 Sec 3.5 C2
			for i := a.advancedPeerTSNAckPoint + 1; ; i++ {
				c, ok := a.inflightQueue.get(i)
				if !ok {
					break
				}
				if !c.abandoned() {
					break
				}
				a.advancedPeerTSNAckPoint = i
			}

			// RFC 3758 Sec 3.5 C3
			if sna32GT(a.advancedPeerTSNAckPoint, a.cumulativeTSNAckPoint) {
				a.willSendForwardTSN = true
			}
		}

		a.log.Debugf("[%s] T3-rtx timed out: nRtos=%d cwnd=%d ssthresh=%d", a.name, nRtos, a.cwnd, a.ssthresh)

		/*
			a.log.Debugf("   - advancedPeerTSNAckPoint=%d", a.advancedPeerTSNAckPoint)
			a.log.Debugf("   - cumulativeTSNAckPoint=%d", a.cumulativeTSNAckPoint)
			a.inflightQueue.updateSortedKeys()
			for i, tsn := range a.inflightQueue.sorted {
				if c, ok := a.inflightQueue.get(tsn); ok {
					a.log.Debugf("   - [%d] tsn=%d acked=%v abandoned=%v (%v,%v) len=%d",
						i, c.tsn, c.acked, c.abandoned(), c.beginningFragment, c.endingFragment, len(c.userData))
				}
			}
		*/

		a.inflightQueue.markAllToRetrasmit()
		a.awakeWriteLoop()

		return
	}

	if id == timerReconfig {
		a.willRetransmitReconfig = true
		a.awakeWriteLoop()
	}
}

func (a *Association) onRetransmissionFailure(id int) {
	a.lock.Lock()
	defer a.lock.Unlock()

	if id == timerT1Init {
		a.log.Errorf("[%s] retransmission failure: T1-init", a.name)
		a.handshakeCompletedCh <- errors.Errorf("handshake failed (INIT ACK)")
		return
	}

	if id == timerT1Cookie {
		a.log.Errorf("[%s] retransmission failure: T1-cookie", a.name)
		a.handshakeCompletedCh <- errors.Errorf("handshake failed (COOKIE ECHO)")
		return
	}

	if id == timerT3RTX {
		// T3-rtx timer will not fail by design
		// Justifications:
		//  * ICE would fail if the connectivity is lost
		//  * WebRTC spec is not clear how this incident should be reported to ULP
		a.log.Errorf("[%s] retransmission failure: T3-rtx (DATA)", a.name)
		return
	}
}

func (a *Association) onAckTimeout() {
	a.lock.Lock()
	defer a.lock.Unlock()

	a.log.Tracef("[%s] ack timed out (ackState: %d)", a.name, a.ackState)
	a.stats.incAckTimeouts()

	a.ackState = ackStateImmediate
	a.awakeWriteLoop()
}

// bufferedAmount returns total amount (in bytes) of currently buffered user data.
// This is used only by testing.
func (a *Association) bufferedAmount() int {
	a.lock.RLock()
	defer a.lock.RUnlock()

	return a.pendingQueue.getNumBytes() + a.inflightQueue.getNumBytes()
}

// MaxMessageSize returns the maximum message size you can send.
func (a *Association) MaxMessageSize() uint32 {
	return atomic.LoadUint32(&a.maxMessageSize)
}

// SetMaxMessageSize sets the maximum message size you can send.
func (a *Association) SetMaxMessageSize(maxMsgSize uint32) {
	atomic.StoreUint32(&a.maxMessageSize, maxMsgSize)
}
