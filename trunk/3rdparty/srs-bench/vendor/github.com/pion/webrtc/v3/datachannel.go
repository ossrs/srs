// +build !js

package webrtc

import (
	"errors"
	"fmt"
	"io"
	"math"
	"sync"
	"sync/atomic"
	"time"

	"github.com/pion/datachannel"
	"github.com/pion/logging"
	"github.com/pion/webrtc/v3/pkg/rtcerr"
)

const dataChannelBufferSize = math.MaxUint16 // message size limit for Chromium
var errSCTPNotEstablished = errors.New("SCTP not established")

// DataChannel represents a WebRTC DataChannel
// The DataChannel interface represents a network channel
// which can be used for bidirectional peer-to-peer transfers of arbitrary data
type DataChannel struct {
	mu sync.RWMutex

	statsID                    string
	label                      string
	ordered                    bool
	maxPacketLifeTime          *uint16
	maxRetransmits             *uint16
	protocol                   string
	negotiated                 bool
	id                         *uint16
	readyState                 atomic.Value // DataChannelState
	bufferedAmountLowThreshold uint64
	detachCalled               bool

	// The binaryType represents attribute MUST, on getting, return the value to
	// which it was last set. On setting, if the new value is either the string
	// "blob" or the string "arraybuffer", then set the IDL attribute to this
	// new value. Otherwise, throw a SyntaxError. When an DataChannel object
	// is created, the binaryType attribute MUST be initialized to the string
	// "blob". This attribute controls how binary data is exposed to scripts.
	// binaryType                 string

	onMessageHandler    func(DataChannelMessage)
	openHandlerOnce     sync.Once
	onOpenHandler       func()
	onCloseHandler      func()
	onBufferedAmountLow func()
	onErrorHandler      func(error)

	sctpTransport *SCTPTransport
	dataChannel   *datachannel.DataChannel

	// A reference to the associated api object used by this datachannel
	api *API
	log logging.LeveledLogger
}

// NewDataChannel creates a new DataChannel.
// This constructor is part of the ORTC API. It is not
// meant to be used together with the basic WebRTC API.
func (api *API) NewDataChannel(transport *SCTPTransport, params *DataChannelParameters) (*DataChannel, error) {
	d, err := api.newDataChannel(params, api.settingEngine.LoggerFactory.NewLogger("ortc"))
	if err != nil {
		return nil, err
	}

	err = d.open(transport)
	if err != nil {
		return nil, err
	}

	return d, nil
}

// newDataChannel is an internal constructor for the data channel used to
// create the DataChannel object before the networking is set up.
func (api *API) newDataChannel(params *DataChannelParameters, log logging.LeveledLogger) (*DataChannel, error) {
	// https://w3c.github.io/webrtc-pc/#peer-to-peer-data-api (Step #5)
	if len(params.Label) > 65535 {
		return nil, &rtcerr.TypeError{Err: ErrStringSizeLimit}
	}

	d := &DataChannel{
		statsID:           fmt.Sprintf("DataChannel-%d", time.Now().UnixNano()),
		label:             params.Label,
		protocol:          params.Protocol,
		negotiated:        params.Negotiated,
		id:                params.ID,
		ordered:           params.Ordered,
		maxPacketLifeTime: params.MaxPacketLifeTime,
		maxRetransmits:    params.MaxRetransmits,
		api:               api,
		log:               log,
	}

	d.setReadyState(DataChannelStateConnecting)
	return d, nil
}

// open opens the datachannel over the sctp transport
func (d *DataChannel) open(sctpTransport *SCTPTransport) error {
	d.mu.Lock()
	if d.sctpTransport != nil {
		// already open
		d.mu.Unlock()
		return nil
	}
	d.sctpTransport = sctpTransport

	if err := d.ensureSCTP(); err != nil {
		d.mu.Unlock()
		return err
	}

	var channelType datachannel.ChannelType
	var reliabilityParameter uint32

	switch {
	case d.maxPacketLifeTime == nil && d.maxRetransmits == nil:
		if d.ordered {
			channelType = datachannel.ChannelTypeReliable
		} else {
			channelType = datachannel.ChannelTypeReliableUnordered
		}

	case d.maxRetransmits != nil:
		reliabilityParameter = uint32(*d.maxRetransmits)
		if d.ordered {
			channelType = datachannel.ChannelTypePartialReliableRexmit
		} else {
			channelType = datachannel.ChannelTypePartialReliableRexmitUnordered
		}
	default:
		reliabilityParameter = uint32(*d.maxPacketLifeTime)
		if d.ordered {
			channelType = datachannel.ChannelTypePartialReliableTimed
		} else {
			channelType = datachannel.ChannelTypePartialReliableTimedUnordered
		}
	}

	cfg := &datachannel.Config{
		ChannelType:          channelType,
		Priority:             datachannel.ChannelPriorityNormal,
		ReliabilityParameter: reliabilityParameter,
		Label:                d.label,
		Protocol:             d.protocol,
		Negotiated:           d.negotiated,
		LoggerFactory:        d.api.settingEngine.LoggerFactory,
	}

	if d.id == nil {
		err := d.sctpTransport.generateAndSetDataChannelID(d.sctpTransport.dtlsTransport.role(), &d.id)
		if err != nil {
			return err
		}
	}

	dc, err := datachannel.Dial(d.sctpTransport.association, *d.id, cfg)
	if err != nil {
		d.mu.Unlock()
		return err
	}

	// bufferedAmountLowThreshold and onBufferedAmountLow might be set earlier
	dc.SetBufferedAmountLowThreshold(d.bufferedAmountLowThreshold)
	dc.OnBufferedAmountLow(d.onBufferedAmountLow)
	d.mu.Unlock()

	d.handleOpen(dc)
	return nil
}

func (d *DataChannel) ensureSCTP() error {
	if d.sctpTransport == nil {
		return errSCTPNotEstablished
	}

	d.sctpTransport.lock.RLock()
	defer d.sctpTransport.lock.RUnlock()
	if d.sctpTransport.association == nil {
		return errSCTPNotEstablished
	}
	return nil
}

// Transport returns the SCTPTransport instance the DataChannel is sending over.
func (d *DataChannel) Transport() *SCTPTransport {
	d.mu.RLock()
	defer d.mu.RUnlock()

	return d.sctpTransport
}

// After onOpen is complete check that the user called detach
// and provide an error message if the call was missed
func (d *DataChannel) checkDetachAfterOpen() {
	d.mu.RLock()
	defer d.mu.RUnlock()

	if d.api.settingEngine.detach.DataChannels && !d.detachCalled {
		d.log.Warn("webrtc.DetachDataChannels() enabled but didn't Detach, call Detach from OnOpen")
	}
}

// OnOpen sets an event handler which is invoked when
// the underlying data transport has been established (or re-established).
func (d *DataChannel) OnOpen(f func()) {
	d.mu.Lock()
	d.openHandlerOnce = sync.Once{}
	d.onOpenHandler = f
	d.mu.Unlock()

	if d.ReadyState() == DataChannelStateOpen {
		// If the data channel is already open, call the handler immediately.
		go d.openHandlerOnce.Do(func() {
			f()
			d.checkDetachAfterOpen()
		})
	}
}

func (d *DataChannel) onOpen() {
	d.mu.RLock()
	handler := d.onOpenHandler
	d.mu.RUnlock()

	if handler != nil {
		go d.openHandlerOnce.Do(func() {
			handler()
			d.checkDetachAfterOpen()
		})
	}
}

// OnClose sets an event handler which is invoked when
// the underlying data transport has been closed.
func (d *DataChannel) OnClose(f func()) {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.onCloseHandler = f
}

func (d *DataChannel) onClose() {
	d.mu.RLock()
	handler := d.onCloseHandler
	d.mu.RUnlock()

	if handler != nil {
		go handler()
	}
}

// OnMessage sets an event handler which is invoked on a binary
// message arrival over the sctp transport from a remote peer.
// OnMessage can currently receive messages up to 16384 bytes
// in size. Check out the detach API if you want to use larger
// message sizes. Note that browser support for larger messages
// is also limited.
func (d *DataChannel) OnMessage(f func(msg DataChannelMessage)) {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.onMessageHandler = f
}

func (d *DataChannel) onMessage(msg DataChannelMessage) {
	d.mu.RLock()
	handler := d.onMessageHandler
	d.mu.RUnlock()

	if handler == nil {
		return
	}
	handler(msg)
}

func (d *DataChannel) handleOpen(dc *datachannel.DataChannel) {
	d.mu.Lock()
	d.dataChannel = dc
	d.mu.Unlock()
	d.setReadyState(DataChannelStateOpen)

	d.onOpen()

	d.mu.Lock()
	defer d.mu.Unlock()

	if !d.api.settingEngine.detach.DataChannels {
		go d.readLoop()
	}
}

// OnError sets an event handler which is invoked when
// the underlying data transport cannot be read.
func (d *DataChannel) OnError(f func(err error)) {
	d.mu.Lock()
	defer d.mu.Unlock()
	d.onErrorHandler = f
}

func (d *DataChannel) onError(err error) {
	d.mu.RLock()
	handler := d.onErrorHandler
	d.mu.RUnlock()

	if handler != nil {
		go handler(err)
	}
}

// See https://github.com/pion/webrtc/issues/1516
// nolint:gochecknoglobals
var rlBufPool = sync.Pool{New: func() interface{} {
	return make([]byte, dataChannelBufferSize)
}}

func (d *DataChannel) readLoop() {
	for {
		buffer := rlBufPool.Get().([]byte)
		n, isString, err := d.dataChannel.ReadDataChannel(buffer)
		if err != nil {
			rlBufPool.Put(buffer) // nolint:staticcheck
			d.setReadyState(DataChannelStateClosed)
			if err != io.EOF {
				d.onError(err)
			}
			d.onClose()
			return
		}

		m := DataChannelMessage{Data: make([]byte, n), IsString: isString}
		copy(m.Data, buffer[:n])
		// The 'staticcheck' pragma is a false positive on the part of the CI linter.
		rlBufPool.Put(buffer) // nolint:staticcheck

		// NB: Why was DataChannelMessage not passed as a pointer value?
		d.onMessage(m) // nolint:staticcheck
	}
}

// Send sends the binary message to the DataChannel peer
func (d *DataChannel) Send(data []byte) error {
	err := d.ensureOpen()
	if err != nil {
		return err
	}

	_, err = d.dataChannel.WriteDataChannel(data, false)
	return err
}

// SendText sends the text message to the DataChannel peer
func (d *DataChannel) SendText(s string) error {
	err := d.ensureOpen()
	if err != nil {
		return err
	}

	_, err = d.dataChannel.WriteDataChannel([]byte(s), true)
	return err
}

func (d *DataChannel) ensureOpen() error {
	d.mu.RLock()
	defer d.mu.RUnlock()
	if d.ReadyState() != DataChannelStateOpen {
		return io.ErrClosedPipe
	}
	return nil
}

// Detach allows you to detach the underlying datachannel. This provides
// an idiomatic API to work with, however it disables the OnMessage callback.
// Before calling Detach you have to enable this behavior by calling
// webrtc.DetachDataChannels(). Combining detached and normal data channels
// is not supported.
// Please refer to the data-channels-detach example and the
// pion/datachannel documentation for the correct way to handle the
// resulting DataChannel object.
func (d *DataChannel) Detach() (datachannel.ReadWriteCloser, error) {
	d.mu.Lock()
	defer d.mu.Unlock()

	if !d.api.settingEngine.detach.DataChannels {
		return nil, errDetachNotEnabled
	}

	if d.dataChannel == nil {
		return nil, errDetachBeforeOpened
	}

	d.detachCalled = true

	return d.dataChannel, nil
}

// Close Closes the DataChannel. It may be called regardless of whether
// the DataChannel object was created by this peer or the remote peer.
func (d *DataChannel) Close() error {
	d.mu.Lock()
	haveSctpTransport := d.dataChannel != nil
	d.mu.Unlock()

	if d.ReadyState() == DataChannelStateClosed {
		return nil
	}

	d.setReadyState(DataChannelStateClosing)
	if !haveSctpTransport {
		return nil
	}

	return d.dataChannel.Close()
}

// Label represents a label that can be used to distinguish this
// DataChannel object from other DataChannel objects. Scripts are
// allowed to create multiple DataChannel objects with the same label.
func (d *DataChannel) Label() string {
	d.mu.RLock()
	defer d.mu.RUnlock()

	return d.label
}

// Ordered represents if the DataChannel is ordered, and false if
// out-of-order delivery is allowed.
func (d *DataChannel) Ordered() bool {
	d.mu.RLock()
	defer d.mu.RUnlock()

	return d.ordered
}

// MaxPacketLifeTime represents the length of the time window (msec) during
// which transmissions and retransmissions may occur in unreliable mode.
func (d *DataChannel) MaxPacketLifeTime() *uint16 {
	d.mu.RLock()
	defer d.mu.RUnlock()

	return d.maxPacketLifeTime
}

// MaxRetransmits represents the maximum number of retransmissions that are
// attempted in unreliable mode.
func (d *DataChannel) MaxRetransmits() *uint16 {
	d.mu.RLock()
	defer d.mu.RUnlock()

	return d.maxRetransmits
}

// Protocol represents the name of the sub-protocol used with this
// DataChannel.
func (d *DataChannel) Protocol() string {
	d.mu.RLock()
	defer d.mu.RUnlock()

	return d.protocol
}

// Negotiated represents whether this DataChannel was negotiated by the
// application (true), or not (false).
func (d *DataChannel) Negotiated() bool {
	d.mu.RLock()
	defer d.mu.RUnlock()

	return d.negotiated
}

// ID represents the ID for this DataChannel. The value is initially
// null, which is what will be returned if the ID was not provided at
// channel creation time, and the DTLS role of the SCTP transport has not
// yet been negotiated. Otherwise, it will return the ID that was either
// selected by the script or generated. After the ID is set to a non-null
// value, it will not change.
func (d *DataChannel) ID() *uint16 {
	d.mu.RLock()
	defer d.mu.RUnlock()

	return d.id
}

// ReadyState represents the state of the DataChannel object.
func (d *DataChannel) ReadyState() DataChannelState {
	if v := d.readyState.Load(); v != nil {
		return v.(DataChannelState)
	}
	return DataChannelState(0)
}

// BufferedAmount represents the number of bytes of application data
// (UTF-8 text and binary data) that have been queued using send(). Even
// though the data transmission can occur in parallel, the returned value
// MUST NOT be decreased before the current task yielded back to the event
// loop to prevent race conditions. The value does not include framing
// overhead incurred by the protocol, or buffering done by the operating
// system or network hardware. The value of BufferedAmount slot will only
// increase with each call to the send() method as long as the ReadyState is
// open; however, BufferedAmount does not reset to zero once the channel
// closes.
func (d *DataChannel) BufferedAmount() uint64 {
	d.mu.RLock()
	defer d.mu.RUnlock()

	if d.dataChannel == nil {
		return 0
	}
	return d.dataChannel.BufferedAmount()
}

// BufferedAmountLowThreshold represents the threshold at which the
// bufferedAmount is considered to be low. When the bufferedAmount decreases
// from above this threshold to equal or below it, the bufferedamountlow
// event fires. BufferedAmountLowThreshold is initially zero on each new
// DataChannel, but the application may change its value at any time.
// The threshold is set to 0 by default.
func (d *DataChannel) BufferedAmountLowThreshold() uint64 {
	d.mu.RLock()
	defer d.mu.RUnlock()

	if d.dataChannel == nil {
		return d.bufferedAmountLowThreshold
	}
	return d.dataChannel.BufferedAmountLowThreshold()
}

// SetBufferedAmountLowThreshold is used to update the threshold.
// See BufferedAmountLowThreshold().
func (d *DataChannel) SetBufferedAmountLowThreshold(th uint64) {
	d.mu.Lock()
	defer d.mu.Unlock()

	d.bufferedAmountLowThreshold = th

	if d.dataChannel != nil {
		d.dataChannel.SetBufferedAmountLowThreshold(th)
	}
}

// OnBufferedAmountLow sets an event handler which is invoked when
// the number of bytes of outgoing data becomes lower than the
// BufferedAmountLowThreshold.
func (d *DataChannel) OnBufferedAmountLow(f func()) {
	d.mu.Lock()
	defer d.mu.Unlock()

	d.onBufferedAmountLow = f
	if d.dataChannel != nil {
		d.dataChannel.OnBufferedAmountLow(f)
	}
}

func (d *DataChannel) getStatsID() string {
	d.mu.Lock()
	defer d.mu.Unlock()
	return d.statsID
}

func (d *DataChannel) collectStats(collector *statsReportCollector) {
	collector.Collecting()

	d.mu.Lock()
	defer d.mu.Unlock()

	stats := DataChannelStats{
		Timestamp: statsTimestampNow(),
		Type:      StatsTypeDataChannel,
		ID:        d.statsID,
		Label:     d.label,
		Protocol:  d.protocol,
		// TransportID string `json:"transportId"`
		State: d.ReadyState(),
	}

	if d.id != nil {
		stats.DataChannelIdentifier = int32(*d.id)
	}

	if d.dataChannel != nil {
		stats.MessagesSent = d.dataChannel.MessagesSent()
		stats.BytesSent = d.dataChannel.BytesSent()
		stats.MessagesReceived = d.dataChannel.MessagesReceived()
		stats.BytesReceived = d.dataChannel.BytesReceived()
	}

	collector.Collect(stats.ID, stats)
}

func (d *DataChannel) setReadyState(r DataChannelState) {
	d.readyState.Store(r)
}
