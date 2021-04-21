package sctp

import (
	"io"
	"math"
	"sync"

	"github.com/pion/logging"
	"github.com/pkg/errors"
)

const (
	// ReliabilityTypeReliable is used for reliable transmission
	ReliabilityTypeReliable byte = 0
	// ReliabilityTypeRexmit is used for partial reliability by retransmission count
	ReliabilityTypeRexmit byte = 1
	// ReliabilityTypeTimed is used for partial reliability by retransmission duration
	ReliabilityTypeTimed byte = 2
)

// Stream represents an SCTP stream
type Stream struct {
	association         *Association
	lock                sync.RWMutex
	streamIdentifier    uint16
	defaultPayloadType  PayloadProtocolIdentifier
	reassemblyQueue     *reassemblyQueue
	sequenceNumber      uint16
	readNotifier        *sync.Cond
	readErr             error
	writeErr            error
	unordered           bool
	reliabilityType     byte
	reliabilityValue    uint32
	bufferedAmount      uint64
	bufferedAmountLow   uint64
	onBufferedAmountLow func()
	log                 logging.LeveledLogger
	name                string
}

// StreamIdentifier returns the Stream identifier associated to the stream.
func (s *Stream) StreamIdentifier() uint16 {
	s.lock.RLock()
	defer s.lock.RUnlock()
	return s.streamIdentifier
}

// SetDefaultPayloadType sets the default payload type used by Write.
func (s *Stream) SetDefaultPayloadType(defaultPayloadType PayloadProtocolIdentifier) {
	s.lock.Lock()
	defer s.lock.Unlock()

	s.setDefaultPayloadType(defaultPayloadType)
}

// setDefaultPayloadType sets the defaultPayloadType. The caller should hold the lock.
func (s *Stream) setDefaultPayloadType(defaultPayloadType PayloadProtocolIdentifier) {
	s.defaultPayloadType = defaultPayloadType
}

// SetReliabilityParams sets reliability parameters for this stream.
func (s *Stream) SetReliabilityParams(unordered bool, relType byte, relVal uint32) {
	s.lock.Lock()
	defer s.lock.Unlock()

	s.setReliabilityParams(unordered, relType, relVal)
}

// setReliabilityParams sets reliability parameters for this stream.
// The caller should hold the lock.
func (s *Stream) setReliabilityParams(unordered bool, relType byte, relVal uint32) {
	s.log.Debugf("[%s] reliability params: ordered=%v type=%d value=%d",
		s.name, !unordered, relType, relVal)
	s.unordered = unordered
	s.reliabilityType = relType
	s.reliabilityValue = relVal
}

// Read reads a packet of len(p) bytes, dropping the Payload Protocol Identifier.
// Returns EOF when the stream is reset or an error if the stream is closed
// otherwise.
func (s *Stream) Read(p []byte) (int, error) {
	n, _, err := s.ReadSCTP(p)
	return n, err
}

// ReadSCTP reads a packet of len(p) bytes and returns the associated Payload
// Protocol Identifier.
// Returns EOF when the stream is reset or an error if the stream is closed
// otherwise.
func (s *Stream) ReadSCTP(p []byte) (int, PayloadProtocolIdentifier, error) {
	s.lock.Lock()
	defer s.lock.Unlock()

	for {
		n, ppi, err := s.reassemblyQueue.read(p)
		if err == nil {
			return n, ppi, nil
		} else if errors.Is(err, io.ErrShortBuffer) {
			return 0, PayloadProtocolIdentifier(0), err
		}

		err = s.readErr
		if err != nil {
			return 0, PayloadProtocolIdentifier(0), err
		}

		s.readNotifier.Wait()
	}
}

func (s *Stream) handleData(pd *chunkPayloadData) {
	s.lock.Lock()
	defer s.lock.Unlock()

	var readable bool
	if s.reassemblyQueue.push(pd) {
		readable = s.reassemblyQueue.isReadable()
		s.log.Debugf("[%s] reassemblyQueue readable=%v", s.name, readable)
		if readable {
			s.log.Debugf("[%s] readNotifier.signal()", s.name)
			s.readNotifier.Signal()
			s.log.Debugf("[%s] readNotifier.signal() done", s.name)
		}
	}
}

func (s *Stream) handleForwardTSNForOrdered(ssn uint16) {
	var readable bool

	func() {
		s.lock.Lock()
		defer s.lock.Unlock()

		if s.unordered {
			return // unordered chunks are handled by handleForwardUnordered method
		}

		// Remove all chunks older than or equal to the new TSN from
		// the reassemblyQueue.
		s.reassemblyQueue.forwardTSNForOrdered(ssn)
		readable = s.reassemblyQueue.isReadable()
	}()

	// Notify the reader asynchronously if there's a data chunk to read.
	if readable {
		s.readNotifier.Signal()
	}
}

func (s *Stream) handleForwardTSNForUnordered(newCumulativeTSN uint32) {
	var readable bool

	func() {
		s.lock.Lock()
		defer s.lock.Unlock()

		if !s.unordered {
			return // ordered chunks are handled by handleForwardTSNOrdered method
		}

		// Remove all chunks older than or equal to the new TSN from
		// the reassemblyQueue.
		s.reassemblyQueue.forwardTSNForUnordered(newCumulativeTSN)
		readable = s.reassemblyQueue.isReadable()
	}()

	// Notify the reader asynchronously if there's a data chunk to read.
	if readable {
		s.readNotifier.Signal()
	}
}

// Write writes len(p) bytes from p with the default Payload Protocol Identifier
func (s *Stream) Write(p []byte) (n int, err error) {
	return s.WriteSCTP(p, s.defaultPayloadType)
}

// WriteSCTP writes len(p) bytes from p to the DTLS connection
func (s *Stream) WriteSCTP(p []byte, ppi PayloadProtocolIdentifier) (n int, err error) {
	maxMessageSize := s.association.MaxMessageSize()
	if len(p) > int(maxMessageSize) {
		return 0, errors.Errorf("Outbound packet larger than maximum message size %v", math.MaxUint16)
	}

	s.lock.RLock()
	err = s.writeErr
	s.lock.RUnlock()
	if err != nil {
		return 0, err
	}

	chunks := s.packetize(p, ppi)

	return len(p), s.association.sendPayloadData(chunks)
}

func (s *Stream) packetize(raw []byte, ppi PayloadProtocolIdentifier) []*chunkPayloadData {
	s.lock.Lock()
	defer s.lock.Unlock()

	i := uint32(0)
	remaining := uint32(len(raw))

	// From draft-ietf-rtcweb-data-protocol-09, section 6:
	//   All Data Channel Establishment Protocol messages MUST be sent using
	//   ordered delivery and reliable transmission.
	unordered := ppi != PayloadTypeWebRTCDCEP && s.unordered

	var chunks []*chunkPayloadData
	var head *chunkPayloadData
	for remaining != 0 {
		fragmentSize := min32(s.association.maxPayloadSize, remaining)

		// Copy the userdata since we'll have to store it until acked
		// and the caller may re-use the buffer in the mean time
		userData := make([]byte, fragmentSize)
		copy(userData, raw[i:i+fragmentSize])

		chunk := &chunkPayloadData{
			streamIdentifier:     s.streamIdentifier,
			userData:             userData,
			unordered:            unordered,
			beginningFragment:    i == 0,
			endingFragment:       remaining-fragmentSize == 0,
			immediateSack:        false,
			payloadType:          ppi,
			streamSequenceNumber: s.sequenceNumber,
			head:                 head,
		}

		if head == nil {
			head = chunk
		}

		chunks = append(chunks, chunk)

		remaining -= fragmentSize
		i += fragmentSize
	}

	// RFC 4960 Sec 6.6
	// Note: When transmitting ordered and unordered data, an endpoint does
	// not increment its Stream Sequence Number when transmitting a DATA
	// chunk with U flag set to 1.
	if !unordered {
		s.sequenceNumber++
	}

	s.bufferedAmount += uint64(len(raw))
	s.log.Tracef("[%s] bufferedAmount = %d", s.name, s.bufferedAmount)

	return chunks
}

// Close closes the write-direction of the stream.
// Future calls to Write are not permitted after calling Close.
func (s *Stream) Close() error {
	if sid, isOpen := func() (uint16, bool) {
		s.lock.Lock()
		defer s.lock.Unlock()

		isOpen := true
		if s.writeErr == nil {
			s.writeErr = errors.New("Stream closed")
		} else {
			isOpen = false
		}

		if s.readErr == nil {
			s.readErr = io.EOF
		} else {
			isOpen = false
		}
		s.readNotifier.Broadcast() // broadcast regardless

		return s.streamIdentifier, isOpen
	}(); isOpen {
		// Reset the outgoing stream
		// https://tools.ietf.org/html/rfc6525
		return s.association.sendResetRequest(sid)
	}

	return nil
}

// BufferedAmount returns the number of bytes of data currently queued to be sent over this stream.
func (s *Stream) BufferedAmount() uint64 {
	s.lock.RLock()
	defer s.lock.RUnlock()

	return s.bufferedAmount
}

// BufferedAmountLowThreshold returns the number of bytes of buffered outgoing data that is
// considered "low." Defaults to 0.
func (s *Stream) BufferedAmountLowThreshold() uint64 {
	s.lock.RLock()
	defer s.lock.RUnlock()

	return s.bufferedAmountLow
}

// SetBufferedAmountLowThreshold is used to update the threshold.
// See BufferedAmountLowThreshold().
func (s *Stream) SetBufferedAmountLowThreshold(th uint64) {
	s.lock.Lock()
	defer s.lock.Unlock()

	s.bufferedAmountLow = th
}

// OnBufferedAmountLow sets the callback handler which would be called when the number of
// bytes of outgoing data buffered is lower than the threshold.
func (s *Stream) OnBufferedAmountLow(f func()) {
	s.lock.Lock()
	defer s.lock.Unlock()

	s.onBufferedAmountLow = f
}

// This method is called by association's readLoop (go-)routine to notify this stream
// of the specified amount of outgoing data has been delivered to the peer.
func (s *Stream) onBufferReleased(nBytesReleased int) {
	if nBytesReleased <= 0 {
		return
	}

	s.lock.Lock()

	fromAmount := s.bufferedAmount

	if s.bufferedAmount < uint64(nBytesReleased) {
		s.bufferedAmount = 0
		s.log.Errorf("[%s] released buffer size %d should be <= %d",
			s.name, nBytesReleased, s.bufferedAmount)
	} else {
		s.bufferedAmount -= uint64(nBytesReleased)
	}

	s.log.Tracef("[%s] bufferedAmount = %d", s.name, s.bufferedAmount)

	if s.onBufferedAmountLow != nil && fromAmount > s.bufferedAmountLow && s.bufferedAmount <= s.bufferedAmountLow {
		f := s.onBufferedAmountLow
		s.lock.Unlock()
		f()
		return
	}

	s.lock.Unlock()
}

func (s *Stream) getNumBytesInReassemblyQueue() int {
	// No lock is required as it reads the size with atomic load function.
	return s.reassemblyQueue.getNumBytes()
}
