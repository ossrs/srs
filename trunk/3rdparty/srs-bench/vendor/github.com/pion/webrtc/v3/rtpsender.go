// +build !js

package webrtc

import (
	"io"
	"sync"
	"time"

	"github.com/pion/interceptor"
	"github.com/pion/randutil"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"
)

// RTPSender allows an application to control how a given Track is encoded and transmitted to a remote peer
type RTPSender struct {
	track TrackLocal

	srtpStream      *srtpWriterFuture
	rtcpInterceptor interceptor.RTCPReader

	context TrackLocalContext

	transport *DTLSTransport

	payloadType PayloadType
	ssrc        SSRC

	// nolint:godox
	// TODO(sgotti) remove this when in future we'll avoid replacing
	// a transceiver sender since we can just check the
	// transceiver negotiation status
	negotiated bool

	// A reference to the associated api object
	api *API
	id  string

	mu                     sync.RWMutex
	sendCalled, stopCalled chan struct{}
}

// NewRTPSender constructs a new RTPSender
func (api *API) NewRTPSender(track TrackLocal, transport *DTLSTransport) (*RTPSender, error) {
	if track == nil {
		return nil, errRTPSenderTrackNil
	} else if transport == nil {
		return nil, errRTPSenderDTLSTransportNil
	}

	id, err := randutil.GenerateCryptoRandomString(32, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")
	if err != nil {
		return nil, err
	}

	r := &RTPSender{
		track:      track,
		transport:  transport,
		api:        api,
		sendCalled: make(chan struct{}),
		stopCalled: make(chan struct{}),
		ssrc:       SSRC(randutil.NewMathRandomGenerator().Uint32()),
		id:         id,
		srtpStream: &srtpWriterFuture{},
	}

	r.srtpStream.rtpSender = r

	r.rtcpInterceptor = r.api.interceptor.BindRTCPReader(interceptor.RTPReaderFunc(func(in []byte, a interceptor.Attributes) (n int, attributes interceptor.Attributes, err error) {
		n, err = r.srtpStream.Read(in)
		return n, a, err
	}))

	return r, nil
}

func (r *RTPSender) isNegotiated() bool {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return r.negotiated
}

func (r *RTPSender) setNegotiated() {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.negotiated = true
}

// Transport returns the currently-configured *DTLSTransport or nil
// if one has not yet been configured
func (r *RTPSender) Transport() *DTLSTransport {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return r.transport
}

// GetParameters describes the current configuration for the encoding and
// transmission of media on the sender's track.
func (r *RTPSender) GetParameters() RTPSendParameters {
	return RTPSendParameters{
		RTPParameters: r.api.mediaEngine.getRTPParametersByKind(
			r.track.Kind(),
			[]RTPTransceiverDirection{RTPTransceiverDirectionSendonly},
		),
		Encodings: []RTPEncodingParameters{
			{
				RTPCodingParameters: RTPCodingParameters{
					SSRC:        r.ssrc,
					PayloadType: r.payloadType,
				},
			},
		},
	}
}

// Track returns the RTCRtpTransceiver track, or nil
func (r *RTPSender) Track() TrackLocal {
	r.mu.RLock()
	defer r.mu.RUnlock()
	return r.track
}

// ReplaceTrack replaces the track currently being used as the sender's source with a new TrackLocal.
// The new track must be of the same media kind (audio, video, etc) and switching the track should not
// require negotiation.
func (r *RTPSender) ReplaceTrack(track TrackLocal) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	if r.hasSent() && r.track != nil {
		if err := r.track.Unbind(r.context); err != nil {
			return err
		}
	}

	if !r.hasSent() || track == nil {
		r.track = track
		return nil
	}

	if _, err := track.Bind(r.context); err != nil {
		// Re-bind the original track
		if _, reBindErr := r.track.Bind(r.context); reBindErr != nil {
			return reBindErr
		}

		return err
	}

	r.track = track
	return nil
}

// Send Attempts to set the parameters controlling the sending of media.
func (r *RTPSender) Send(parameters RTPSendParameters) error {
	r.mu.Lock()
	defer r.mu.Unlock()

	if r.hasSent() {
		return errRTPSenderSendAlreadyCalled
	}

	writeStream := &interceptorToTrackLocalWriter{}
	r.context = TrackLocalContext{
		id:          r.id,
		params:      r.api.mediaEngine.getRTPParametersByKind(r.track.Kind(), []RTPTransceiverDirection{RTPTransceiverDirectionSendonly}),
		ssrc:        parameters.Encodings[0].SSRC,
		writeStream: writeStream,
	}

	codec, err := r.track.Bind(r.context)
	if err != nil {
		return err
	}
	r.context.params.Codecs = []RTPCodecParameters{codec}

	streamInfo := createStreamInfo(r.id, parameters.Encodings[0].SSRC, codec.PayloadType, codec.RTPCodecCapability, parameters.HeaderExtensions)
	rtpInterceptor := r.api.interceptor.BindLocalStream(&streamInfo, interceptor.RTPWriterFunc(func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
		return r.srtpStream.WriteRTP(header, payload)
	}))
	writeStream.interceptor.Store(rtpInterceptor)

	close(r.sendCalled)
	return nil
}

// Stop irreversibly stops the RTPSender
func (r *RTPSender) Stop() error {
	r.mu.Lock()

	if stopped := r.hasStopped(); stopped {
		r.mu.Unlock()
		return nil
	}

	close(r.stopCalled)
	r.mu.Unlock()

	if !r.hasSent() {
		return nil
	}

	if err := r.ReplaceTrack(nil); err != nil {
		return err
	}

	return r.srtpStream.Close()
}

// Read reads incoming RTCP for this RTPReceiver
func (r *RTPSender) Read(b []byte) (n int, a interceptor.Attributes, err error) {
	select {
	case <-r.sendCalled:
		return r.rtcpInterceptor.Read(b, a)
	case <-r.stopCalled:
		return 0, nil, io.ErrClosedPipe
	}
}

// ReadRTCP is a convenience method that wraps Read and unmarshals for you.
func (r *RTPSender) ReadRTCP() ([]rtcp.Packet, interceptor.Attributes, error) {
	b := make([]byte, receiveMTU)
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

// SetReadDeadline sets the deadline for the Read operation.
// Setting to zero means no deadline.
func (r *RTPSender) SetReadDeadline(t time.Time) error {
	return r.srtpStream.SetReadDeadline(t)
}

// hasSent tells if data has been ever sent for this instance
func (r *RTPSender) hasSent() bool {
	select {
	case <-r.sendCalled:
		return true
	default:
		return false
	}
}

// hasStopped tells if stop has been called
func (r *RTPSender) hasStopped() bool {
	select {
	case <-r.stopCalled:
		return true
	default:
		return false
	}
}
