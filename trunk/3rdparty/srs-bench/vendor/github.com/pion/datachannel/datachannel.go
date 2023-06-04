// Package datachannel implements WebRTC Data Channels
package datachannel

import (
	"errors"
	"fmt"
	"io"
	"sync"
	"sync/atomic"
	"time"

	"github.com/pion/logging"
	"github.com/pion/sctp"
)

const receiveMTU = 8192

// Reader is an extended io.Reader
// that also returns if the message is text.
type Reader interface {
	ReadDataChannel([]byte) (int, bool, error)
}

// ReadDeadliner extends an io.Reader to expose setting a read deadline.
type ReadDeadliner interface {
	SetReadDeadline(time.Time) error
}

// Writer is an extended io.Writer
// that also allows indicating if a message is text.
type Writer interface {
	WriteDataChannel([]byte, bool) (int, error)
}

// ReadWriteCloser is an extended io.ReadWriteCloser
// that also implements our Reader and Writer.
type ReadWriteCloser interface {
	io.Reader
	io.Writer
	Reader
	Writer
	io.Closer
}

// DataChannel represents a data channel
type DataChannel struct {
	Config

	// stats
	messagesSent     uint32
	messagesReceived uint32
	bytesSent        uint64
	bytesReceived    uint64

	mu                      sync.Mutex
	onOpenCompleteHandler   func()
	openCompleteHandlerOnce sync.Once

	stream *sctp.Stream
	log    logging.LeveledLogger
}

// Config is used to configure the data channel.
type Config struct {
	ChannelType          ChannelType
	Negotiated           bool
	Priority             uint16
	ReliabilityParameter uint32
	Label                string
	Protocol             string
	LoggerFactory        logging.LoggerFactory
}

func newDataChannel(stream *sctp.Stream, config *Config) (*DataChannel, error) {
	return &DataChannel{
		Config: *config,
		stream: stream,
		log:    config.LoggerFactory.NewLogger("datachannel"),
	}, nil
}

// Dial opens a data channels over SCTP
func Dial(a *sctp.Association, id uint16, config *Config) (*DataChannel, error) {
	stream, err := a.OpenStream(id, sctp.PayloadTypeWebRTCBinary)
	if err != nil {
		return nil, err
	}

	dc, err := Client(stream, config)
	if err != nil {
		return nil, err
	}

	return dc, nil
}

// Client opens a data channel over an SCTP stream
func Client(stream *sctp.Stream, config *Config) (*DataChannel, error) {
	msg := &channelOpen{
		ChannelType:          config.ChannelType,
		Priority:             config.Priority,
		ReliabilityParameter: config.ReliabilityParameter,

		Label:    []byte(config.Label),
		Protocol: []byte(config.Protocol),
	}

	if !config.Negotiated {
		rawMsg, err := msg.Marshal()
		if err != nil {
			return nil, fmt.Errorf("failed to marshal ChannelOpen %w", err)
		}

		if _, err = stream.WriteSCTP(rawMsg, sctp.PayloadTypeWebRTCDCEP); err != nil {
			return nil, fmt.Errorf("failed to send ChannelOpen %w", err)
		}
	}
	return newDataChannel(stream, config)
}

// Accept is used to accept incoming data channels over SCTP
func Accept(a *sctp.Association, config *Config, existingChannels ...*DataChannel) (*DataChannel, error) {
	stream, err := a.AcceptStream()
	if err != nil {
		return nil, err
	}
	for _, ch := range existingChannels {
		if ch.StreamIdentifier() == stream.StreamIdentifier() {
			ch.stream.SetDefaultPayloadType(sctp.PayloadTypeWebRTCBinary)
			return ch, nil
		}
	}

	stream.SetDefaultPayloadType(sctp.PayloadTypeWebRTCBinary)

	dc, err := Server(stream, config)
	if err != nil {
		return nil, err
	}

	return dc, nil
}

// Server accepts a data channel over an SCTP stream
func Server(stream *sctp.Stream, config *Config) (*DataChannel, error) {
	buffer := make([]byte, receiveMTU)
	n, ppi, err := stream.ReadSCTP(buffer)
	if err != nil {
		return nil, err
	}

	if ppi != sctp.PayloadTypeWebRTCDCEP {
		return nil, fmt.Errorf("%w %s", ErrInvalidPayloadProtocolIdentifier, ppi)
	}

	openMsg, err := parseExpectDataChannelOpen(buffer[:n])
	if err != nil {
		return nil, fmt.Errorf("failed to parse DataChannelOpen packet %w", err)
	}

	config.ChannelType = openMsg.ChannelType
	config.Priority = openMsg.Priority
	config.ReliabilityParameter = openMsg.ReliabilityParameter
	config.Label = string(openMsg.Label)
	config.Protocol = string(openMsg.Protocol)

	dataChannel, err := newDataChannel(stream, config)
	if err != nil {
		return nil, err
	}

	err = dataChannel.writeDataChannelAck()
	if err != nil {
		return nil, err
	}

	err = dataChannel.commitReliabilityParams()
	if err != nil {
		return nil, err
	}
	return dataChannel, nil
}

// Read reads a packet of len(p) bytes as binary data
func (c *DataChannel) Read(p []byte) (int, error) {
	n, _, err := c.ReadDataChannel(p)
	return n, err
}

// ReadDataChannel reads a packet of len(p) bytes
func (c *DataChannel) ReadDataChannel(p []byte) (int, bool, error) {
	for {
		n, ppi, err := c.stream.ReadSCTP(p)
		if errors.Is(err, io.EOF) {
			// When the peer sees that an incoming stream was
			// reset, it also resets its corresponding outgoing stream.
			if closeErr := c.stream.Close(); closeErr != nil {
				return 0, false, closeErr
			}
		}
		if err != nil {
			return 0, false, err
		}

		if ppi == sctp.PayloadTypeWebRTCDCEP {
			if err = c.handleDCEP(p[:n]); err != nil {
				c.log.Errorf("Failed to handle DCEP: %s", err.Error())
			}
			continue
		} else if ppi == sctp.PayloadTypeWebRTCBinaryEmpty || ppi == sctp.PayloadTypeWebRTCStringEmpty {
			n = 0
		}

		atomic.AddUint32(&c.messagesReceived, 1)
		atomic.AddUint64(&c.bytesReceived, uint64(n))

		isString := ppi == sctp.PayloadTypeWebRTCString || ppi == sctp.PayloadTypeWebRTCStringEmpty
		return n, isString, err
	}
}

// SetReadDeadline sets a deadline for reads to return
func (c *DataChannel) SetReadDeadline(t time.Time) error {
	return c.stream.SetReadDeadline(t)
}

// MessagesSent returns the number of messages sent
func (c *DataChannel) MessagesSent() uint32 {
	return atomic.LoadUint32(&c.messagesSent)
}

// MessagesReceived returns the number of messages received
func (c *DataChannel) MessagesReceived() uint32 {
	return atomic.LoadUint32(&c.messagesReceived)
}

// OnOpen sets an event handler which is invoked when
// a DATA_CHANNEL_ACK message is received.
// The handler is called only on thefor the channel opened
// https://datatracker.ietf.org/doc/html/draft-ietf-rtcweb-data-protocol-09#section-5.2
func (c *DataChannel) OnOpen(f func()) {
	c.mu.Lock()
	c.openCompleteHandlerOnce = sync.Once{}
	c.onOpenCompleteHandler = f
	c.mu.Unlock()
}

func (c *DataChannel) onOpenComplete() {
	c.mu.Lock()
	hdlr := c.onOpenCompleteHandler
	c.mu.Unlock()

	if hdlr != nil {
		go c.openCompleteHandlerOnce.Do(func() {
			hdlr()
		})
	}
}

// BytesSent returns the number of bytes sent
func (c *DataChannel) BytesSent() uint64 {
	return atomic.LoadUint64(&c.bytesSent)
}

// BytesReceived returns the number of bytes received
func (c *DataChannel) BytesReceived() uint64 {
	return atomic.LoadUint64(&c.bytesReceived)
}

// StreamIdentifier returns the Stream identifier associated to the stream.
func (c *DataChannel) StreamIdentifier() uint16 {
	return c.stream.StreamIdentifier()
}

func (c *DataChannel) handleDCEP(data []byte) error {
	msg, err := parse(data)
	if err != nil {
		return fmt.Errorf("failed to parse DataChannel packet %w", err)
	}

	switch msg := msg.(type) {
	case *channelAck:
		c.log.Debug("Received DATA_CHANNEL_ACK")
		if err = c.commitReliabilityParams(); err != nil {
			return err
		}
		c.onOpenComplete()
	default:
		return fmt.Errorf("%w %v", ErrInvalidMessageType, msg)
	}

	return nil
}

// Write writes len(p) bytes from p as binary data
func (c *DataChannel) Write(p []byte) (n int, err error) {
	return c.WriteDataChannel(p, false)
}

// WriteDataChannel writes len(p) bytes from p
func (c *DataChannel) WriteDataChannel(p []byte, isString bool) (n int, err error) {
	// https://tools.ietf.org/html/draft-ietf-rtcweb-data-channel-12#section-6.6
	// SCTP does not support the sending of empty user messages.  Therefore,
	// if an empty message has to be sent, the appropriate PPID (WebRTC
	// String Empty or WebRTC Binary Empty) is used and the SCTP user
	// message of one zero byte is sent.  When receiving an SCTP user
	// message with one of these PPIDs, the receiver MUST ignore the SCTP
	// user message and process it as an empty message.
	var ppi sctp.PayloadProtocolIdentifier
	switch {
	case !isString && len(p) > 0:
		ppi = sctp.PayloadTypeWebRTCBinary
	case !isString && len(p) == 0:
		ppi = sctp.PayloadTypeWebRTCBinaryEmpty
	case isString && len(p) > 0:
		ppi = sctp.PayloadTypeWebRTCString
	case isString && len(p) == 0:
		ppi = sctp.PayloadTypeWebRTCStringEmpty
	}

	atomic.AddUint32(&c.messagesSent, 1)
	atomic.AddUint64(&c.bytesSent, uint64(len(p)))

	if len(p) == 0 {
		_, err := c.stream.WriteSCTP([]byte{0}, ppi)
		return 0, err
	}
	return c.stream.WriteSCTP(p, ppi)
}

func (c *DataChannel) writeDataChannelAck() error {
	ack := channelAck{}
	ackMsg, err := ack.Marshal()
	if err != nil {
		return fmt.Errorf("failed to marshal ChannelOpen ACK: %w", err)
	}

	if _, err = c.stream.WriteSCTP(ackMsg, sctp.PayloadTypeWebRTCDCEP); err != nil {
		return fmt.Errorf("failed to send ChannelOpen ACK: %w", err)
	}

	return err
}

// Close closes the DataChannel and the underlying SCTP stream.
func (c *DataChannel) Close() error {
	// https://tools.ietf.org/html/draft-ietf-rtcweb-data-channel-13#section-6.7
	// Closing of a data channel MUST be signaled by resetting the
	// corresponding outgoing streams [RFC6525].  This means that if one
	// side decides to close the data channel, it resets the corresponding
	// outgoing stream.  When the peer sees that an incoming stream was
	// reset, it also resets its corresponding outgoing stream.  Once this
	// is completed, the data channel is closed.  Resetting a stream sets
	// the Stream Sequence Numbers (SSNs) of the stream back to 'zero' with
	// a corresponding notification to the application layer that the reset
	// has been performed.  Streams are available for reuse after a reset
	// has been performed.
	return c.stream.Close()
}

// BufferedAmount returns the number of bytes of data currently queued to be
// sent over this stream.
func (c *DataChannel) BufferedAmount() uint64 {
	return c.stream.BufferedAmount()
}

// BufferedAmountLowThreshold returns the number of bytes of buffered outgoing
// data that is considered "low." Defaults to 0.
func (c *DataChannel) BufferedAmountLowThreshold() uint64 {
	return c.stream.BufferedAmountLowThreshold()
}

// SetBufferedAmountLowThreshold is used to update the threshold.
// See BufferedAmountLowThreshold().
func (c *DataChannel) SetBufferedAmountLowThreshold(th uint64) {
	c.stream.SetBufferedAmountLowThreshold(th)
}

// OnBufferedAmountLow sets the callback handler which would be called when the
// number of bytes of outgoing data buffered is lower than the threshold.
func (c *DataChannel) OnBufferedAmountLow(f func()) {
	c.stream.OnBufferedAmountLow(f)
}

func (c *DataChannel) commitReliabilityParams() error {
	switch c.Config.ChannelType {
	case ChannelTypeReliable:
		c.stream.SetReliabilityParams(false, sctp.ReliabilityTypeReliable, c.Config.ReliabilityParameter)
	case ChannelTypeReliableUnordered:
		c.stream.SetReliabilityParams(true, sctp.ReliabilityTypeReliable, c.Config.ReliabilityParameter)
	case ChannelTypePartialReliableRexmit:
		c.stream.SetReliabilityParams(false, sctp.ReliabilityTypeRexmit, c.Config.ReliabilityParameter)
	case ChannelTypePartialReliableRexmitUnordered:
		c.stream.SetReliabilityParams(true, sctp.ReliabilityTypeRexmit, c.Config.ReliabilityParameter)
	case ChannelTypePartialReliableTimed:
		c.stream.SetReliabilityParams(false, sctp.ReliabilityTypeTimed, c.Config.ReliabilityParameter)
	case ChannelTypePartialReliableTimedUnordered:
		c.stream.SetReliabilityParams(true, sctp.ReliabilityTypeTimed, c.Config.ReliabilityParameter)
	default:
		return fmt.Errorf("%w %v", ErrInvalidChannelType, c.Config.ChannelType)
	}
	return nil
}
