// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package webrtc

import (
	"fmt"
	"io"
	"sync"
	"time"

	"github.com/pion/interceptor"
	"github.com/pion/rtcp"
	"github.com/pion/srtp/v2"
	"github.com/pion/webrtc/v3/internal/util"
)

// trackStreams maintains a mapping of RTP/RTCP streams to a specific track
// a RTPReceiver may contain multiple streams if we are dealing with Simulcast
type trackStreams struct {
	track *TrackRemote

	streamInfo, repairStreamInfo *interceptor.StreamInfo

	rtpReadStream  *srtp.ReadStreamSRTP
	rtpInterceptor interceptor.RTPReader

	rtcpReadStream  *srtp.ReadStreamSRTCP
	rtcpInterceptor interceptor.RTCPReader

	repairReadStream  *srtp.ReadStreamSRTP
	repairInterceptor interceptor.RTPReader

	repairRtcpReadStream  *srtp.ReadStreamSRTCP
	repairRtcpInterceptor interceptor.RTCPReader
}

// RTPReceiver allows an application to inspect the receipt of a TrackRemote
type RTPReceiver struct {
	kind      RTPCodecType
	transport *DTLSTransport

	tracks []trackStreams

	closed, received chan interface{}
	mu               sync.RWMutex

	tr *RTPTransceiver

	// A reference to the associated api object
	api *API
}

// NewRTPReceiver constructs a new RTPReceiver
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
	}

	return r, nil
}

func (r *RTPReceiver) setRTPTransceiver(tr *RTPTransceiver) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.tr = tr
}

// Transport returns the currently-configured *DTLSTransport or nil
// if one has not yet been configured
func (r *RTPReceiver) Transport() *DTLSTransport {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return r.transport
}

func (r *RTPReceiver) getParameters() RTPParameters {
	parameters := r.api.mediaEngine.getRTPParametersByKind(r.kind, []RTPTransceiverDirection{RTPTransceiverDirectionRecvonly})
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

// Track returns the RtpTransceiver TrackRemote
func (r *RTPReceiver) Track() *TrackRemote {
	r.mu.RLock()
	defer r.mu.RUnlock()

	if len(r.tracks) != 1 {
		return nil
	}
	return r.tracks[0].track
}

// Tracks returns the RtpTransceiver tracks
// A RTPReceiver to support Simulcast may now have multiple tracks
func (r *RTPReceiver) Tracks() []*TrackRemote {
	r.mu.RLock()
	defer r.mu.RUnlock()

	var tracks []*TrackRemote
	for i := range r.tracks {
		tracks = append(tracks, r.tracks[i].track)
	}
	return tracks
}

// configureReceive initialize the track
func (r *RTPReceiver) configureReceive(parameters RTPReceiveParameters) {
	r.mu.Lock()
	defer r.mu.Unlock()

	for i := range parameters.Encodings {
		t := trackStreams{
			track: newTrackRemote(
				r.kind,
				parameters.Encodings[i].SSRC,
				parameters.Encodings[i].RID,
				r,
			),
		}

		r.tracks = append(r.tracks, t)
	}
}

// startReceive starts all the transports
func (r *RTPReceiver) startReceive(parameters RTPReceiveParameters) error {
	r.mu.Lock()
	defer r.mu.Unlock()
	select {
	case <-r.received:
		return errRTPReceiverReceiveAlreadyCalled
	default:
	}
	defer close(r.received)

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

		var t *trackStreams
		for idx, ts := range r.tracks {
			if ts.track != nil && parameters.Encodings[i].SSRC != 0 && ts.track.SSRC() == parameters.Encodings[i].SSRC {
				t = &r.tracks[idx]
				break
			}
		}
		if t == nil {
			return fmt.Errorf("%w: %d", errRTPReceiverWithSSRCTrackStreamNotFound, parameters.Encodings[i].SSRC)
		}

		if parameters.Encodings[i].SSRC != 0 {
			t.streamInfo = createStreamInfo("", parameters.Encodings[i].SSRC, 0, codec, globalParams.HeaderExtensions)
			var err error
			if t.rtpReadStream, t.rtpInterceptor, t.rtcpReadStream, t.rtcpInterceptor, err = r.transport.streamsForSSRC(parameters.Encodings[i].SSRC, *t.streamInfo); err != nil {
				return err
			}
		}

		if rtxSsrc := parameters.Encodings[i].RTX.SSRC; rtxSsrc != 0 {
			streamInfo := createStreamInfo("", rtxSsrc, 0, codec, globalParams.HeaderExtensions)
			rtpReadStream, rtpInterceptor, rtcpReadStream, rtcpInterceptor, err := r.transport.streamsForSSRC(rtxSsrc, *streamInfo)
			if err != nil {
				return err
			}

			if err = r.receiveForRtx(rtxSsrc, "", streamInfo, rtpReadStream, rtpInterceptor, rtcpReadStream, rtcpInterceptor); err != nil {
				return err
			}
		}
	}

	return nil
}

// Receive initialize the track and starts all the transports
func (r *RTPReceiver) Receive(parameters RTPReceiveParameters) error {
	r.configureReceive(parameters)
	return r.startReceive(parameters)
}

// Read reads incoming RTCP for this RTPReceiver
func (r *RTPReceiver) Read(b []byte) (n int, a interceptor.Attributes, err error) {
	select {
	case <-r.received:
		return r.tracks[0].rtcpInterceptor.Read(b, a)
	case <-r.closed:
		return 0, nil, io.ErrClosedPipe
	}
}

// ReadSimulcast reads incoming RTCP for this RTPReceiver for given rid
func (r *RTPReceiver) ReadSimulcast(b []byte, rid string) (n int, a interceptor.Attributes, err error) {
	select {
	case <-r.received:
		for _, t := range r.tracks {
			if t.track != nil && t.track.rid == rid {
				return t.rtcpInterceptor.Read(b, a)
			}
		}
		return 0, nil, fmt.Errorf("%w: %s", errRTPReceiverForRIDTrackStreamNotFound, rid)
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

// ReadSimulcastRTCP is a convenience method that wraps ReadSimulcast and unmarshal for you
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

// Stop irreversibly stops the RTPReceiver
func (r *RTPReceiver) Stop() error {
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

// readRTP should only be called by a track, this only exists so we can keep state in one place
func (r *RTPReceiver) readRTP(b []byte, reader *TrackRemote) (n int, a interceptor.Attributes, err error) {
	<-r.received
	if t := r.streamsForTrack(reader); t != nil {
		return t.rtpInterceptor.Read(b, a)
	}

	return 0, nil, fmt.Errorf("%w: %d", errRTPReceiverWithSSRCTrackStreamNotFound, reader.SSRC())
}

// receiveForRid is the sibling of Receive expect for RIDs instead of SSRCs
// It populates all the internal state for the given RID
func (r *RTPReceiver) receiveForRid(rid string, params RTPParameters, streamInfo *interceptor.StreamInfo, rtpReadStream *srtp.ReadStreamSRTP, rtpInterceptor interceptor.RTPReader, rtcpReadStream *srtp.ReadStreamSRTCP, rtcpInterceptor interceptor.RTCPReader) (*TrackRemote, error) {
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

// receiveForRtx starts a routine that processes the repair stream
// These packets aren't exposed to the user yet, but we need to process them for
// TWCC
func (r *RTPReceiver) receiveForRtx(ssrc SSRC, rsid string, streamInfo *interceptor.StreamInfo, rtpReadStream *srtp.ReadStreamSRTP, rtpInterceptor interceptor.RTPReader, rtcpReadStream *srtp.ReadStreamSRTCP, rtcpInterceptor interceptor.RTCPReader) error {
	var track *trackStreams
	if ssrc != 0 && len(r.tracks) == 1 {
		track = &r.tracks[0]
	} else {
		for i := range r.tracks {
			if r.tracks[i].track.RID() == rsid {
				track = &r.tracks[i]
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

	go func() {
		b := make([]byte, r.api.settingEngine.getReceiveMTU())
		for {
			if _, _, readErr := track.repairInterceptor.Read(b, nil); readErr != nil {
				return
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

// SetReadDeadlineSimulcast sets the max amount of time the RTCP stream for a given rid will block before returning. 0 is forever.
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
// This should be fired by calling SetReadDeadline on the TrackRemote
func (r *RTPReceiver) setRTPReadDeadline(deadline time.Time, reader *TrackRemote) error {
	r.mu.RLock()
	defer r.mu.RUnlock()

	if t := r.streamsForTrack(reader); t != nil {
		return t.rtpReadStream.SetReadDeadline(deadline)
	}
	return fmt.Errorf("%w: %d", errRTPReceiverWithSSRCTrackStreamNotFound, reader.SSRC())
}
