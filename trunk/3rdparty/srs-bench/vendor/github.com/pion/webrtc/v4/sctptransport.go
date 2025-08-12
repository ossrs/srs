// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package webrtc

import (
	"errors"
	"io"
	"sync"
	"time"

	"github.com/pion/datachannel"
	"github.com/pion/logging"
	"github.com/pion/sctp"
	"github.com/pion/webrtc/v4/pkg/rtcerr"
)

const sctpMaxChannels = uint16(65535)

// SCTPTransport provides details about the SCTP transport.
type SCTPTransport struct {
	lock sync.RWMutex

	dtlsTransport *DTLSTransport

	// State represents the current state of the SCTP transport.
	state SCTPTransportState

	// SCTPTransportState doesn't have an enum to distinguish between New/Connecting
	// so we need a dedicated field
	isStarted bool

	// MaxChannels represents the maximum amount of DataChannel's that can
	// be used simultaneously.
	maxChannels *uint16

	// OnStateChange  func()

	onErrorHandler func(error)
	onCloseHandler func(error)

	sctpAssociation            *sctp.Association
	onDataChannelHandler       func(*DataChannel)
	onDataChannelOpenedHandler func(*DataChannel)

	// DataChannels
	dataChannels          []*DataChannel
	dataChannelIDsUsed    map[uint16]struct{}
	dataChannelsOpened    uint32
	dataChannelsRequested uint32
	dataChannelsAccepted  uint32

	api *API
	log logging.LeveledLogger
}

// NewSCTPTransport creates a new SCTPTransport.
// This constructor is part of the ORTC API. It is not
// meant to be used together with the basic WebRTC API.
func (api *API) NewSCTPTransport(dtls *DTLSTransport) *SCTPTransport {
	res := &SCTPTransport{
		dtlsTransport:      dtls,
		state:              SCTPTransportStateConnecting,
		api:                api,
		log:                api.settingEngine.LoggerFactory.NewLogger("ortc"),
		dataChannelIDsUsed: make(map[uint16]struct{}),
	}

	res.updateMaxChannels()

	return res
}

// Transport returns the DTLSTransport instance the SCTPTransport is sending over.
func (r *SCTPTransport) Transport() *DTLSTransport {
	r.lock.RLock()
	defer r.lock.RUnlock()

	return r.dtlsTransport
}

// GetCapabilities returns the SCTPCapabilities of the SCTPTransport.
func (r *SCTPTransport) GetCapabilities() SCTPCapabilities {
	var maxMessageSize uint32
	if a := r.association(); a != nil {
		maxMessageSize = a.MaxMessageSize()
	}

	return SCTPCapabilities{
		MaxMessageSize: maxMessageSize,
	}
}

// Start the SCTPTransport. Since both local and remote parties must mutually
// create an SCTPTransport, SCTP SO (Simultaneous Open) is used to establish
// a connection over SCTP.
func (r *SCTPTransport) Start(capabilities SCTPCapabilities) error {
	if r.isStarted {
		return nil
	}
	r.isStarted = true

	maxMessageSize := capabilities.MaxMessageSize
	if maxMessageSize == 0 {
		maxMessageSize = sctpMaxMessageSizeUnsetValue
	}

	dtlsTransport := r.Transport()
	if dtlsTransport == nil || dtlsTransport.conn == nil {
		return errSCTPTransportDTLS
	}
	sctpAssociation, err := sctp.Client(sctp.Config{
		NetConn:              dtlsTransport.conn,
		MaxReceiveBufferSize: r.api.settingEngine.sctp.maxReceiveBufferSize,
		EnableZeroChecksum:   r.api.settingEngine.sctp.enableZeroChecksum,
		LoggerFactory:        r.api.settingEngine.LoggerFactory,
		RTOMax:               float64(r.api.settingEngine.sctp.rtoMax) / float64(time.Millisecond),
		BlockWrite:           r.api.settingEngine.detach.DataChannels && r.api.settingEngine.dataChannelBlockWrite,
		MaxMessageSize:       maxMessageSize,
		MTU:                  outboundMTU,
		MinCwnd:              r.api.settingEngine.sctp.minCwnd,
		FastRtxWnd:           r.api.settingEngine.sctp.fastRtxWnd,
		CwndCAStep:           r.api.settingEngine.sctp.cwndCAStep,
	})
	if err != nil {
		return err
	}

	r.lock.Lock()
	r.sctpAssociation = sctpAssociation
	r.state = SCTPTransportStateConnected
	dataChannels := append([]*DataChannel{}, r.dataChannels...)
	r.lock.Unlock()

	var openedDCCount uint32
	for _, d := range dataChannels {
		if d.ReadyState() == DataChannelStateConnecting {
			err := d.open(r)
			if err != nil {
				r.log.Warnf("failed to open data channel: %s", err)

				continue
			}
			openedDCCount++
		}
	}

	r.lock.Lock()
	r.dataChannelsOpened += openedDCCount
	r.lock.Unlock()

	go r.acceptDataChannels(sctpAssociation, dataChannels)

	return nil
}

// Stop stops the SCTPTransport.
func (r *SCTPTransport) Stop() error {
	r.lock.Lock()
	defer r.lock.Unlock()
	if r.sctpAssociation == nil {
		return nil
	}

	r.sctpAssociation.Abort("")

	r.sctpAssociation = nil
	r.state = SCTPTransportStateClosed

	return nil
}

//nolint:cyclop
func (r *SCTPTransport) acceptDataChannels(
	assoc *sctp.Association,
	existingDataChannels []*DataChannel,
) {
	dataChannels := make([]*datachannel.DataChannel, 0, len(existingDataChannels))
	for _, dc := range existingDataChannels {
		dc.mu.Lock()
		isNil := dc.dataChannel == nil
		dc.mu.Unlock()
		if isNil {
			continue
		}
		dataChannels = append(dataChannels, dc.dataChannel)
	}
ACCEPT:
	for {
		dc, err := datachannel.Accept(assoc, &datachannel.Config{
			LoggerFactory: r.api.settingEngine.LoggerFactory,
		}, dataChannels...)
		if err != nil {
			if !errors.Is(err, io.EOF) {
				r.log.Errorf("Failed to accept data channel: %v", err)
				r.onError(err)
				r.onClose(err)
			} else {
				r.onClose(nil)
			}

			return
		}
		for _, ch := range dataChannels {
			if ch.StreamIdentifier() == dc.StreamIdentifier() {
				continue ACCEPT
			}
		}

		var (
			maxRetransmits    *uint16
			maxPacketLifeTime *uint16
		)
		val := uint16(dc.Config.ReliabilityParameter) //nolint:gosec //G115
		ordered := true

		switch dc.Config.ChannelType {
		case datachannel.ChannelTypeReliable:
			ordered = true
		case datachannel.ChannelTypeReliableUnordered:
			ordered = false
		case datachannel.ChannelTypePartialReliableRexmit:
			ordered = true
			maxRetransmits = &val
		case datachannel.ChannelTypePartialReliableRexmitUnordered:
			ordered = false
			maxRetransmits = &val
		case datachannel.ChannelTypePartialReliableTimed:
			ordered = true
			maxPacketLifeTime = &val
		case datachannel.ChannelTypePartialReliableTimedUnordered:
			ordered = false
			maxPacketLifeTime = &val
		default:
		}

		sid := dc.StreamIdentifier()
		rtcDC, err := r.api.newDataChannel(&DataChannelParameters{
			ID:                &sid,
			Label:             dc.Config.Label,
			Protocol:          dc.Config.Protocol,
			Negotiated:        dc.Config.Negotiated,
			Ordered:           ordered,
			MaxPacketLifeTime: maxPacketLifeTime,
			MaxRetransmits:    maxRetransmits,
		}, r, r.api.settingEngine.LoggerFactory.NewLogger("ortc"))
		if err != nil {
			// This data channel is invalid. Close it and log an error.
			if err1 := dc.Close(); err1 != nil {
				r.log.Errorf("Failed to close invalid data channel: %v", err1)
			}
			r.log.Errorf("Failed to accept data channel: %v", err)
			r.onError(err)
			// We've received a datachannel with invalid configuration. We can still receive other datachannels.
			continue ACCEPT
		}

		<-r.onDataChannel(rtcDC)
		rtcDC.handleOpen(dc, true, dc.Config.Negotiated)

		r.lock.Lock()
		r.dataChannelsOpened++
		handler := r.onDataChannelOpenedHandler
		r.lock.Unlock()

		if handler != nil {
			handler(rtcDC)
		}
	}
}

// OnError sets an event handler which is invoked when the SCTP Association errors.
func (r *SCTPTransport) OnError(f func(err error)) {
	r.lock.Lock()
	defer r.lock.Unlock()
	r.onErrorHandler = f
}

func (r *SCTPTransport) onError(err error) {
	r.lock.RLock()
	handler := r.onErrorHandler
	r.lock.RUnlock()

	if handler != nil {
		go handler(err)
	}
}

// OnClose sets an event handler which is invoked when the SCTP Association closes.
func (r *SCTPTransport) OnClose(f func(err error)) {
	r.lock.Lock()
	defer r.lock.Unlock()
	r.onCloseHandler = f
}

func (r *SCTPTransport) onClose(err error) {
	r.lock.RLock()
	handler := r.onCloseHandler
	r.lock.RUnlock()

	if handler != nil {
		go handler(err)
	}
}

// OnDataChannel sets an event handler which is invoked when a data
// channel message arrives from a remote peer.
func (r *SCTPTransport) OnDataChannel(f func(*DataChannel)) {
	r.lock.Lock()
	defer r.lock.Unlock()
	r.onDataChannelHandler = f
}

// OnDataChannelOpened sets an event handler which is invoked when a data
// channel is opened.
func (r *SCTPTransport) OnDataChannelOpened(f func(*DataChannel)) {
	r.lock.Lock()
	defer r.lock.Unlock()
	r.onDataChannelOpenedHandler = f
}

func (r *SCTPTransport) onDataChannel(dc *DataChannel) (done chan struct{}) {
	r.lock.Lock()
	r.dataChannels = append(r.dataChannels, dc)
	r.dataChannelsAccepted++
	if dc.ID() != nil {
		r.dataChannelIDsUsed[*dc.ID()] = struct{}{}
	} else {
		// This cannot happen, the constructor for this datachannel in the caller
		// takes a pointer to the id.
		r.log.Errorf("accepted data channel with no ID")
	}
	handler := r.onDataChannelHandler
	r.lock.Unlock()

	done = make(chan struct{})
	if handler == nil || dc == nil {
		close(done)

		return
	}

	// Run this synchronously to allow setup done in onDataChannelFn()
	// to complete before datachannel event handlers might be called.
	go func() {
		handler(dc)
		close(done)
	}()

	return
}

func (r *SCTPTransport) updateMaxChannels() {
	val := sctpMaxChannels
	r.maxChannels = &val
}

// MaxChannels is the maximum number of RTCDataChannels that can be open simultaneously.
func (r *SCTPTransport) MaxChannels() uint16 {
	r.lock.Lock()
	defer r.lock.Unlock()

	if r.maxChannels == nil {
		return sctpMaxChannels
	}

	return *r.maxChannels
}

// State returns the current state of the SCTPTransport.
func (r *SCTPTransport) State() SCTPTransportState {
	r.lock.RLock()
	defer r.lock.RUnlock()

	return r.state
}

func (r *SCTPTransport) collectStats(collector *statsReportCollector) {
	collector.Collecting()

	stats := SCTPTransportStats{
		Timestamp: statsTimestampFrom(time.Now()),
		Type:      StatsTypeSCTPTransport,
		ID:        "sctpTransport",
	}

	association := r.association()
	if association != nil {
		stats.BytesSent = association.BytesSent()
		stats.BytesReceived = association.BytesReceived()
		stats.SmoothedRoundTripTime = association.SRTT() * 0.001 // convert milliseconds to seconds
		stats.CongestionWindow = association.CWND()
		stats.ReceiverWindow = association.RWND()
		stats.MTU = association.MTU()
	}

	collector.Collect(stats.ID, stats)
}

func (r *SCTPTransport) generateAndSetDataChannelID(dtlsRole DTLSRole, idOut **uint16) error {
	var id uint16
	if dtlsRole != DTLSRoleClient {
		id++
	}

	maxVal := r.MaxChannels()

	r.lock.Lock()
	defer r.lock.Unlock()

	for ; id < maxVal-1; id += 2 {
		if _, ok := r.dataChannelIDsUsed[id]; ok {
			continue
		}
		*idOut = &id
		r.dataChannelIDsUsed[id] = struct{}{}

		return nil
	}

	return &rtcerr.OperationError{Err: ErrMaxDataChannelID}
}

func (r *SCTPTransport) association() *sctp.Association {
	if r == nil {
		return nil
	}
	r.lock.RLock()
	association := r.sctpAssociation
	r.lock.RUnlock()

	return association
}

// BufferedAmount returns total amount (in bytes) of currently buffered user data.
func (r *SCTPTransport) BufferedAmount() int {
	r.lock.Lock()
	defer r.lock.Unlock()
	if r.sctpAssociation == nil {
		return 0
	}

	return r.sctpAssociation.BufferedAmount()
}
