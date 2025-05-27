// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package webrtc

import (
	"encoding/binary"
	"fmt"
	"io"
	"sync"
	"time"

	"github.com/pion/interceptor"
	"github.com/pion/rtcp"
	"github.com/pion/srtp/v3"
	"github.com/pion/webrtc/v4/internal/util"
)

// trackStreams maintains a mapping of RTP/RTCP streams to a specific track
// a RTPReceiver may contain multiple streams if we are dealing with Simulcast.
type trackStreams struct {
	track *TrackRemote

	streamInfo, repairStreamInfo *interceptor.StreamInfo

	rtpReadStream  *srtp.ReadStreamSRTP
	rtpInterceptor interceptor.RTPReader

	rtcpReadStream  *srtp.ReadStreamSRTCP
	rtcpInterceptor interceptor.RTCPReader

	repairReadStream    *srtp.ReadStreamSRTP
	repairInterceptor   interceptor.RTPReader
	repairStreamChannel chan rtxPacketWithAttributes

	repairRtcpReadStream  *srtp.ReadStreamSRTCP
	repairRtcpInterceptor interceptor.RTCPReader
}

type rtxPacketWithAttributes struct {
	pkt        []byte
	attributes interceptor.Attributes
	pool       *sync.Pool
}

func (p *rtxPacketWithAttributes) release() {
	if p.pkt != nil {
		b := p.pkt[:cap(p.pkt)]
		p.pool.Put(b) // nolint:staticcheck
		p.pkt = nil
	}
}

// RTPReceiver allows an application to inspect the receipt of a TrackRemote.
type RTPReceiver struct {
	kind      RTPCodecType
	transport *DTLSTransport

	tracks []trackStreams

	closed, received chan interface{}
	mu               sync.RWMutex

	tr *RTPTransceiver

	// A reference to the associated api object
	api *API

	rtxPool sync.Pool
}

// NewRTPReceiver constructs a new RTPReceiver.
func (api *API) NewRTPReceiver(kind RTPCodecType, transport *DTLSTransport) (*RTPReceiver, error) {
	if transport == nil {
		return nil, errRTPReceiverDTLSTransportNil
	}

	r := &RTPReceiver{
		kind:      kind,
		transport: transport,
		api:       api,
		closed:    make(chan interface{}),
		received:  make(chan interface{}),
		tracks:    []trackStreams{},
		rtxPool: sync.Pool{New: func() interface{} {
			return make([]byte, api.settingEngine.getReceiveMTU())
		}},
	}

	return r, nil
}

func (r *RTPReceiver) setRTPTransceiver(tr *RTPTransceiver) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.tr = tr
}

// Transport returns the currently-configured *DTLSTransport or nil
// if one has not yet been configured.
func (r *RTPReceiver) Transport() *DTLSTransport {
	r.mu.RLock()
	defer r.mu.RUnlock()

	return r.transport
}

func (r *RTPReceiver) getParameters() RTPParameters {
	parameters := r.api.mediaEngine.getRTPParametersByKind(
		r.kind,
		[]RTPTransceiverDirection{RTPTransceiverDirectionRecvonly},
	)
	if r.tr != nil {
		parameters.Codecs = r.tr.getCodecs()
	}

	return parameters
}

// GetParameters describes the current configuration for the encoding and
// transmission of media on the receiver's track.
func (r *RTPReceiver) GetParameters() RTPParameters {
	r.mu.RLock()
	defer r.mu.RUnlock()

	return r.getParameters()
}

// Track returns the RtpTransceiver TrackRemote.
func (r *RTPReceiver) Track() *TrackRemote {
	r.mu.RLock()
	defer r.mu.RUnlock()

	if len(r.tracks) != 1 {
		return nil
	}

	return r.tracks[0].track
}

// Tracks returns the RtpTransceiver tracks
// A RTPReceiver to support Simulcast may now have multiple tracks.
func (r *RTPReceiver) Tracks() []*TrackRemote {
	r.mu.RLock()
	defer r.mu.RUnlock()

	var tracks []*TrackRemote
	for i := range r.tracks {
		tracks = append(tracks, r.tracks[i].track)
	}

	return tracks
}

// RTPTransceiver returns the RTPTransceiver this
// RTPReceiver belongs too, or nil if none.
func (r *RTPReceiver) RTPTransceiver() *RTPTransceiver {
	r.mu.Lock()
	defer r.mu.Unlock()

	return r.tr
}

// configureReceive initialize the track.
func (r *RTPReceiver) configureReceive(parameters RTPReceiveParameters) {
	r.mu.Lock()
	defer r.mu.Unlock()

	for i := range parameters.Encodings {
		t := trackStreams{
			track: newTrackRemote(
				r.kind,
				parameters.Encodings[i].SSRC,
				parameters.Encodings[i].RTX.SSRC,
				parameters.Encodings[i].RID,
				r,
			),
		}

		r.tracks = append(r.tracks, t)
	}
}

// startReceive starts all the transports.
func (r *RTPReceiver) startReceive(parameters RTPReceiveParameters) error { //nolint:cyclop
	r.mu.Lock()
	defer r.mu.Unlock()
	select {
	case <-r.received:
		return errRTPReceiverReceiveAlreadyCalled
	default:
	}

	globalParams := r.getParameters()
	codec := RTPCodecCapability{}
	if len(globalParams.Codecs) != 0 {
		codec = globalParams.Codecs[0].RTPCodecCapability
	}

	for i := range parameters.Encodings {
		if parameters.Encodings[i].RID != "" {
			// RID based tracks will be set up in receiveForRid
			continue
		}

		var streams *trackStreams
		for idx, ts := range r.tracks {
			if ts.track != nil && ts.track.SSRC() == parameters.Encodings[i].SSRC {
				streams = &r.tracks[idx]

				break
			}
		}
		if streams == nil {
			return fmt.Errorf("%w: %d", errRTPReceiverWithSSRCTrackStreamNotFound, parameters.Encodings[i].SSRC)
		}

		streams.streamInfo = createStreamInfo(
			"",
			parameters.Encodings[i].SSRC,
			0, 0, 0, 0, 0,
			codec,
			globalParams.HeaderExtensions,
		)
		var err error

		//nolint:lll // # TODO refactor
		if streams.rtpReadStream, streams.rtpInterceptor, streams.rtcpReadStream, streams.rtcpInterceptor, err = r.transport.streamsForSSRC(parameters.Encodings[i].SSRC, *streams.streamInfo); err != nil {
			return err
		}

		if rtxSsrc := parameters.Encodings[i].RTX.SSRC; rtxSsrc != 0 {
			streamInfo := createStreamInfo("", rtxSsrc, 0, 0, 0, 0, 0, codec, globalParams.HeaderExtensions)
			rtpReadStream, rtpInterceptor, rtcpReadStream, rtcpInterceptor, err := r.transport.streamsForSSRC(
				rtxSsrc,
				*streamInfo,
			)
			if err != nil {
				return err
			}

			if err = r.receiveForRtx(
				rtxSsrc,
				"",
				streamInfo,
				rtpReadStream,
				rtpInterceptor,
				rtcpReadStream,
				rtcpInterceptor,
			); err != nil {
				return err
			}
		}
	}

	close(r.received)

	return nil
}

// Receive initialize the track and starts all the transports.
func (r *RTPReceiver) Receive(parameters RTPReceiveParameters) error {
	r.configureReceive(parameters)

	return r.startReceive(parameters)
}

// Read reads incoming RTCP for this RTPReceiver.
func (r *RTPReceiver) Read(b []byte) (n int, a interceptor.Attributes, err error) {
	select {
	case <-r.received:
		return r.tracks[0].rtcpInterceptor.Read(b, a)
	case <-r.closed:
		return 0, nil, io.ErrClosedPipe
	}
}

// ReadSimulcast reads incoming RTCP for this RTPReceiver for given rid.
func (r *RTPReceiver) ReadSimulcast(b []byte, rid string) (n int, a interceptor.Attributes, err error) {
	select {
	case <-r.received:
		var rtcpInterceptor interceptor.RTCPReader

		r.mu.Lock()
		for _, t := range r.tracks {
			if t.track != nil && t.track.rid == rid {
				rtcpInterceptor = t.rtcpInterceptor
			}
		}
		r.mu.Unlock()

		if rtcpInterceptor == nil {
			return 0, nil, fmt.Errorf("%w: %s", errRTPReceiverForRIDTrackStreamNotFound, rid)
		}

		return rtcpInterceptor.Read(b, a)

	case <-r.closed:
		return 0, nil, io.ErrClosedPipe
	}
}

// ReadRTCP is a convenience method that wraps Read and unmarshal for you.
// It also runs any configured interceptors.
func (r *RTPReceiver) ReadRTCP() ([]rtcp.Packet, interceptor.Attributes, error) {
	b := make([]byte, r.api.settingEngine.getReceiveMTU())
	i, attributes, err := r.Read(b)
	if err != nil {
		return nil, nil, err
	}

	pkts, err := rtcp.Unmarshal(b[:i])
	if err != nil {
		return nil, nil, err
	}

	return pkts, attributes, nil
}

// ReadSimulcastRTCP is a convenience method that wraps ReadSimulcast and unmarshal for you.
func (r *RTPReceiver) ReadSimulcastRTCP(rid string) ([]rtcp.Packet, interceptor.Attributes, error) {
	b := make([]byte, r.api.settingEngine.getReceiveMTU())
	i, attributes, err := r.ReadSimulcast(b, rid)
	if err != nil {
		return nil, nil, err
	}

	pkts, err := rtcp.Unmarshal(b[:i])

	return pkts, attributes, err
}

func (r *RTPReceiver) haveReceived() bool {
	select {
	case <-r.received:
		return true
	default:
		return false
	}
}

// Stop irreversibly stops the RTPReceiver.
func (r *RTPReceiver) Stop() error { //nolint:cyclop
	r.mu.Lock()
	defer r.mu.Unlock()
	var err error

	select {
	case <-r.closed:
		return err
	default:
	}

	select {
	case <-r.received:
		for i := range r.tracks {
			errs := []error{}

			if r.tracks[i].rtcpReadStream != nil {
				errs = append(errs, r.tracks[i].rtcpReadStream.Close())
			}

			if r.tracks[i].rtpReadStream != nil {
				errs = append(errs, r.tracks[i].rtpReadStream.Close())
			}

			if r.tracks[i].repairReadStream != nil {
				errs = append(errs, r.tracks[i].repairReadStream.Close())
			}

			if r.tracks[i].repairRtcpReadStream != nil {
				errs = append(errs, r.tracks[i].repairRtcpReadStream.Close())
			}

			if r.tracks[i].streamInfo != nil {
				r.api.interceptor.UnbindRemoteStream(r.tracks[i].streamInfo)
			}

			if r.tracks[i].repairStreamInfo != nil {
				r.api.interceptor.UnbindRemoteStream(r.tracks[i].repairStreamInfo)
			}

			err = util.FlattenErrs(errs)
		}
	default:
	}

	close(r.closed)

	return err
}

func (r *RTPReceiver) streamsForTrack(t *TrackRemote) *trackStreams {
	for i := range r.tracks {
		if r.tracks[i].track == t {
			return &r.tracks[i]
		}
	}

	return nil
}

// readRTP should only be called by a track, this only exists so we can keep state in one place.
func (r *RTPReceiver) readRTP(b []byte, reader *TrackRemote) (n int, a interceptor.Attributes, err error) {
	select {
	case <-r.received:
	case <-r.closed:
		return 0, nil, io.EOF
	}

	if t := r.streamsForTrack(reader); t != nil {
		return t.rtpInterceptor.Read(b, a)
	}

	return 0, nil, fmt.Errorf("%w: %d", errRTPReceiverWithSSRCTrackStreamNotFound, reader.SSRC())
}

// receiveForRid is the sibling of Receive expect for RIDs instead of SSRCs
// It populates all the internal state for the given RID.
func (r *RTPReceiver) receiveForRid(
	rid string,
	params RTPParameters,
	streamInfo *interceptor.StreamInfo,
	rtpReadStream *srtp.ReadStreamSRTP,
	rtpInterceptor interceptor.RTPReader,
	rtcpReadStream *srtp.ReadStreamSRTCP,
	rtcpInterceptor interceptor.RTCPReader,
) (*TrackRemote, error) {
	r.mu.Lock()
	defer r.mu.Unlock()

	for i := range r.tracks {
		if r.tracks[i].track.RID() == rid {
			r.tracks[i].track.mu.Lock()
			r.tracks[i].track.kind = r.kind
			r.tracks[i].track.codec = params.Codecs[0]
			r.tracks[i].track.params = params
			r.tracks[i].track.ssrc = SSRC(streamInfo.SSRC)
			r.tracks[i].track.mu.Unlock()

			r.tracks[i].streamInfo = streamInfo
			r.tracks[i].rtpReadStream = rtpReadStream
			r.tracks[i].rtpInterceptor = rtpInterceptor
			r.tracks[i].rtcpReadStream = rtcpReadStream
			r.tracks[i].rtcpInterceptor = rtcpInterceptor

			return r.tracks[i].track, nil
		}
	}

	return nil, fmt.Errorf("%w: %s", errRTPReceiverForRIDTrackStreamNotFound, rid)
}

// receiveForRtx starts a routine that processes the repair stream.
//
//nolint:cyclop
func (r *RTPReceiver) receiveForRtx(
	ssrc SSRC,
	rsid string,
	streamInfo *interceptor.StreamInfo,
	rtpReadStream *srtp.ReadStreamSRTP,
	rtpInterceptor interceptor.RTPReader,
	rtcpReadStream *srtp.ReadStreamSRTCP,
	rtcpInterceptor interceptor.RTCPReader,
) error {
	var track *trackStreams
	if ssrc != 0 && len(r.tracks) == 1 {
		track = &r.tracks[0]
	} else {
		for i := range r.tracks {
			if r.tracks[i].track.RID() == rsid {
				track = &r.tracks[i]
				if track.track.RtxSSRC() == 0 {
					track.track.setRtxSSRC(SSRC(streamInfo.SSRC))
				}

				break
			}
		}
	}

	if track == nil {
		return fmt.Errorf("%w: ssrc(%d) rsid(%s)", errRTPReceiverForRIDTrackStreamNotFound, ssrc, rsid)
	}

	track.repairStreamInfo = streamInfo
	track.repairReadStream = rtpReadStream
	track.repairInterceptor = rtpInterceptor
	track.repairRtcpReadStream = rtcpReadStream
	track.repairRtcpInterceptor = rtcpInterceptor
	track.repairStreamChannel = make(chan rtxPacketWithAttributes, 50)

	go func() {
		for {
			b := r.rtxPool.Get().([]byte) // nolint:forcetypeassert
			i, attributes, err := track.repairInterceptor.Read(b, nil)
			if err != nil {
				r.rtxPool.Put(b) // nolint:staticcheck

				return
			}

			// RTX packets have a different payload format. Move the OSN in the payload to the RTP header and rewrite the
			// payload type and SSRC, so that we can return RTX packets to the caller 'transparently' i.e. in the same format
			// as non-RTX RTP packets
			hasExtension := b[0]&0b10000 > 0
			hasPadding := b[0]&0b100000 > 0
			csrcCount := b[0] & 0b1111
			headerLength := uint16(12 + (4 * csrcCount))
			paddingLength := 0
			if hasExtension {
				headerLength += 4 * (1 + binary.BigEndian.Uint16(b[headerLength+2:headerLength+4]))
			}
			if hasPadding {
				paddingLength = int(b[i-1])
			}

			if i-int(headerLength)-paddingLength < 2 {
				// BWE probe packet, ignore
				r.rtxPool.Put(b) // nolint:staticcheck

				continue
			}

			if attributes == nil {
				attributes = make(interceptor.Attributes)
			}
			attributes.Set(AttributeRtxPayloadType, b[1]&0x7F)
			attributes.Set(AttributeRtxSequenceNumber, binary.BigEndian.Uint16(b[2:4]))
			attributes.Set(AttributeRtxSsrc, binary.BigEndian.Uint32(b[8:12]))

			b[1] = (b[1] & 0x80) | uint8(track.track.PayloadType())
			b[2] = b[headerLength]
			b[3] = b[headerLength+1]
			binary.BigEndian.PutUint32(b[8:12], uint32(track.track.SSRC()))
			copy(b[headerLength:i-2], b[headerLength+2:i])

			select {
			case <-r.closed:
				r.rtxPool.Put(b) // nolint:staticcheck

				return
			case track.repairStreamChannel <- rtxPacketWithAttributes{pkt: b[:i-2], attributes: attributes, pool: &r.rtxPool}:
			default:
				// skip the RTX packet if the repair stream channel is full, could be blocked in the application's read loop
			}
		}
	}()

	return nil
}

// SetReadDeadline sets the max amount of time the RTCP stream will block before returning. 0 is forever.
func (r *RTPReceiver) SetReadDeadline(t time.Time) error {
	r.mu.RLock()
	defer r.mu.RUnlock()

	return r.tracks[0].rtcpReadStream.SetReadDeadline(t)
}

// SetReadDeadlineSimulcast sets the max amount of time the RTCP stream for a given rid will block before returning.
// 0 is forever.
func (r *RTPReceiver) SetReadDeadlineSimulcast(deadline time.Time, rid string) error {
	r.mu.RLock()
	defer r.mu.RUnlock()

	for _, t := range r.tracks {
		if t.track != nil && t.track.rid == rid {
			return t.rtcpReadStream.SetReadDeadline(deadline)
		}
	}

	return fmt.Errorf("%w: %s", errRTPReceiverForRIDTrackStreamNotFound, rid)
}

// setRTPReadDeadline sets the max amount of time the RTP stream will block before returning. 0 is forever.
// This should be fired by calling SetReadDeadline on the TrackRemote.
func (r *RTPReceiver) setRTPReadDeadline(deadline time.Time, reader *TrackRemote) error {
	r.mu.RLock()
	defer r.mu.RUnlock()

	if t := r.streamsForTrack(reader); t != nil {
		return t.rtpReadStream.SetReadDeadline(deadline)
	}

	return fmt.Errorf("%w: %d", errRTPReceiverWithSSRCTrackStreamNotFound, reader.SSRC())
}

// readRTX returns an RTX packet if one is available on the RTX track, otherwise returns nil.
func (r *RTPReceiver) readRTX(reader *TrackRemote) *rtxPacketWithAttributes {
	if !reader.HasRTX() {
		return nil
	}

	select {
	case <-r.received:
	default:
		return nil
	}

	if t := r.streamsForTrack(reader); t != nil {
		select {
		case rtxPacketReceived := <-t.repairStreamChannel:
			return &rtxPacketReceived
		default:
		}
	}

	return nil
}
