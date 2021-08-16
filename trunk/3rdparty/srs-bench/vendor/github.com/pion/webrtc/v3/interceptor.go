// +build !js

package webrtc

import (
	"sync/atomic"

	"github.com/pion/interceptor"
	"github.com/pion/interceptor/pkg/nack"
	"github.com/pion/interceptor/pkg/report"
	"github.com/pion/rtp"
)

// RegisterDefaultInterceptors will register some useful interceptors.
// If you want to customize which interceptors are loaded, you should copy the
// code from this method and remove unwanted interceptors.
func RegisterDefaultInterceptors(mediaEngine *MediaEngine, interceptorRegistry *interceptor.Registry) error {
	if err := ConfigureNack(mediaEngine, interceptorRegistry); err != nil {
		return err
	}

	if err := ConfigureRTCPReports(interceptorRegistry); err != nil {
		return err
	}

	return nil
}

// ConfigureRTCPReports will setup everything necessary for generating Sender and Receiver Reports
func ConfigureRTCPReports(interceptorRegistry *interceptor.Registry) error {
	reciver, err := report.NewReceiverInterceptor()
	if err != nil {
		return err
	}

	sender, err := report.NewSenderInterceptor()
	if err != nil {
		return err
	}

	interceptorRegistry.Add(reciver)
	interceptorRegistry.Add(sender)
	return nil
}

// ConfigureNack will setup everything necessary for handling generating/responding to nack messages.
func ConfigureNack(mediaEngine *MediaEngine, interceptorRegistry *interceptor.Registry) error {
	generator, err := nack.NewGeneratorInterceptor()
	if err != nil {
		return err
	}

	responder, err := nack.NewResponderInterceptor()
	if err != nil {
		return err
	}

	mediaEngine.RegisterFeedback(RTCPFeedback{Type: "nack"}, RTPCodecTypeVideo)
	mediaEngine.RegisterFeedback(RTCPFeedback{Type: "nack", Parameter: "pli"}, RTPCodecTypeVideo)
	interceptorRegistry.Add(responder)
	interceptorRegistry.Add(generator)
	return nil
}

type interceptorToTrackLocalWriter struct{ interceptor atomic.Value } // interceptor.RTPWriter }

func (i *interceptorToTrackLocalWriter) WriteRTP(header *rtp.Header, payload []byte) (int, error) {
	if writer, ok := i.interceptor.Load().(interceptor.RTPWriter); ok && writer != nil {
		return writer.Write(header, payload, interceptor.Attributes{})
	}

	return 0, nil
}

func (i *interceptorToTrackLocalWriter) Write(b []byte) (int, error) {
	packet := &rtp.Packet{}
	if err := packet.Unmarshal(b); err != nil {
		return 0, err
	}

	return i.WriteRTP(&packet.Header, packet.Payload)
}

func createStreamInfo(id string, ssrc SSRC, payloadType PayloadType, codec RTPCodecCapability, webrtcHeaderExtensions []RTPHeaderExtensionParameter) interceptor.StreamInfo {
	headerExtensions := make([]interceptor.RTPHeaderExtension, 0, len(webrtcHeaderExtensions))
	for _, h := range webrtcHeaderExtensions {
		headerExtensions = append(headerExtensions, interceptor.RTPHeaderExtension{ID: h.ID, URI: h.URI})
	}

	feedbacks := make([]interceptor.RTCPFeedback, 0, len(codec.RTCPFeedback))
	for _, f := range codec.RTCPFeedback {
		feedbacks = append(feedbacks, interceptor.RTCPFeedback{Type: f.Type, Parameter: f.Parameter})
	}

	return interceptor.StreamInfo{
		ID:                  id,
		Attributes:          interceptor.Attributes{},
		SSRC:                uint32(ssrc),
		PayloadType:         uint8(payloadType),
		RTPHeaderExtensions: headerExtensions,
		MimeType:            codec.MimeType,
		ClockRate:           codec.ClockRate,
		Channels:            codec.Channels,
		SDPFmtpLine:         codec.SDPFmtpLine,
		RTCPFeedback:        feedbacks,
	}
}
