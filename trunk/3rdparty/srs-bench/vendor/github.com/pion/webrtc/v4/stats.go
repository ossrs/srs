// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

import (
	"encoding/json"
	"fmt"
	"sync"
	"time"

	"github.com/pion/ice/v4"
)

// A Stats object contains a set of statistics copies out of a monitored component
// of the WebRTC stack at a specific time.
type Stats interface {
	statsMarker()
}

// UnmarshalStatsJSON unmarshals a Stats object from JSON.
func UnmarshalStatsJSON(b []byte) (Stats, error) { //nolint:cyclop
	type typeJSON struct {
		Type StatsType `json:"type"`
	}
	typeHolder := typeJSON{}

	err := json.Unmarshal(b, &typeHolder)
	if err != nil {
		return nil, fmt.Errorf("unmarshal json type: %w", err)
	}

	switch typeHolder.Type {
	case StatsTypeCodec:
		return unmarshalCodecStats(b)
	case StatsTypeInboundRTP:
		return unmarshalInboundRTPStreamStats(b)
	case StatsTypeOutboundRTP:
		return unmarshalOutboundRTPStreamStats(b)
	case StatsTypeRemoteInboundRTP:
		return unmarshalRemoteInboundRTPStreamStats(b)
	case StatsTypeRemoteOutboundRTP:
		return unmarshalRemoteOutboundRTPStreamStats(b)
	case StatsTypeCSRC:
		return unmarshalCSRCStats(b)
	case StatsTypeMediaSource:
		return unmarshalMediaSourceStats(b)
	case StatsTypeMediaPlayout:
		return unmarshalMediaPlayoutStats(b)
	case StatsTypePeerConnection:
		return unmarshalPeerConnectionStats(b)
	case StatsTypeDataChannel:
		return unmarshalDataChannelStats(b)
	case StatsTypeStream:
		return unmarshalStreamStats(b)
	case StatsTypeTrack:
		return unmarshalTrackStats(b)
	case StatsTypeSender:
		return unmarshalSenderStats(b)
	case StatsTypeReceiver:
		return unmarshalReceiverStats(b)
	case StatsTypeTransport:
		return unmarshalTransportStats(b)
	case StatsTypeCandidatePair:
		return unmarshalICECandidatePairStats(b)
	case StatsTypeLocalCandidate, StatsTypeRemoteCandidate:
		return unmarshalICECandidateStats(b)
	case StatsTypeCertificate:
		return unmarshalCertificateStats(b)
	case StatsTypeSCTPTransport:
		return unmarshalSCTPTransportStats(b)
	default:
		return nil, fmt.Errorf("type: %w", ErrUnknownType)
	}
}

// StatsType indicates the type of the object that a Stats object represents.
type StatsType string

const (
	// StatsTypeCodec is used by CodecStats.
	StatsTypeCodec StatsType = "codec"

	// StatsTypeInboundRTP is used by InboundRTPStreamStats.
	StatsTypeInboundRTP StatsType = "inbound-rtp"

	// StatsTypeOutboundRTP is used by OutboundRTPStreamStats.
	StatsTypeOutboundRTP StatsType = "outbound-rtp"

	// StatsTypeRemoteInboundRTP is used by RemoteInboundRTPStreamStats.
	StatsTypeRemoteInboundRTP StatsType = "remote-inbound-rtp"

	// StatsTypeRemoteOutboundRTP is used by RemoteOutboundRTPStreamStats.
	StatsTypeRemoteOutboundRTP StatsType = "remote-outbound-rtp"

	// StatsTypeCSRC is used by RTPContributingSourceStats.
	StatsTypeCSRC StatsType = "csrc"

	// StatsTypeMediaSource is used by AudioSourceStats or VideoSourceStats depending on kind.
	StatsTypeMediaSource = "media-source"

	// StatsTypeMediaPlayout is used by AudioPlayoutStats.
	StatsTypeMediaPlayout StatsType = "media-playout"

	// StatsTypePeerConnection used by PeerConnectionStats.
	StatsTypePeerConnection StatsType = "peer-connection"

	// StatsTypeDataChannel is used by DataChannelStats.
	StatsTypeDataChannel StatsType = "data-channel"

	// StatsTypeStream is used by MediaStreamStats.
	StatsTypeStream StatsType = "stream"

	// StatsTypeTrack is used by SenderVideoTrackAttachmentStats and SenderAudioTrackAttachmentStats depending on kind.
	StatsTypeTrack StatsType = "track"

	// StatsTypeSender is used by the AudioSenderStats or VideoSenderStats depending on kind.
	StatsTypeSender StatsType = "sender"

	// StatsTypeReceiver is used by the AudioReceiverStats or VideoReceiverStats depending on kind.
	StatsTypeReceiver StatsType = "receiver"

	// StatsTypeTransport is used by TransportStats.
	StatsTypeTransport StatsType = "transport"

	// StatsTypeCandidatePair is used by ICECandidatePairStats.
	StatsTypeCandidatePair StatsType = "candidate-pair"

	// StatsTypeLocalCandidate is used by ICECandidateStats for the local candidate.
	StatsTypeLocalCandidate StatsType = "local-candidate"

	// StatsTypeRemoteCandidate is used by ICECandidateStats for the remote candidate.
	StatsTypeRemoteCandidate StatsType = "remote-candidate"

	// StatsTypeCertificate is used by CertificateStats.
	StatsTypeCertificate StatsType = "certificate"

	// StatsTypeSCTPTransport is used by SCTPTransportStats.
	StatsTypeSCTPTransport StatsType = "sctp-transport"
)

// MediaKind indicates the kind of media (audio or video).
type MediaKind string

const (
	// MediaKindAudio indicates this is audio stats.
	MediaKindAudio MediaKind = "audio"
	// MediaKindVideo indicates this is video stats.
	MediaKindVideo MediaKind = "video"
)

// StatsTimestamp is a timestamp represented by the floating point number of
// milliseconds since the epoch.
type StatsTimestamp float64

// Time returns the time.Time represented by this timestamp.
func (s StatsTimestamp) Time() time.Time {
	millis := float64(s)
	nanos := int64(millis * float64(time.Millisecond))

	return time.Unix(0, nanos).UTC()
}

func statsTimestampFrom(t time.Time) StatsTimestamp {
	return StatsTimestamp(t.UnixNano() / int64(time.Millisecond))
}

func statsTimestampNow() StatsTimestamp {
	return statsTimestampFrom(time.Now())
}

// StatsReport collects Stats objects indexed by their ID.
type StatsReport map[string]Stats

type statsReportCollector struct {
	collectingGroup sync.WaitGroup
	report          StatsReport
	mux             sync.Mutex
}

func newStatsReportCollector() *statsReportCollector {
	return &statsReportCollector{report: make(StatsReport)}
}

func (src *statsReportCollector) Collecting() {
	src.collectingGroup.Add(1)
}

func (src *statsReportCollector) Collect(id string, stats Stats) {
	src.mux.Lock()
	defer src.mux.Unlock()

	src.report[id] = stats
	src.collectingGroup.Done()
}

func (src *statsReportCollector) Done() {
	src.collectingGroup.Done()
}

func (src *statsReportCollector) Ready() StatsReport {
	src.collectingGroup.Wait()
	src.mux.Lock()
	defer src.mux.Unlock()

	return src.report
}

// CodecType specifies whether a CodecStats objects represents a media format
// that is being encoded or decoded.
type CodecType string

const (
	// CodecTypeEncode means the attached CodecStats represents a media format that
	// is being encoded, or that the implementation is prepared to encode.
	CodecTypeEncode CodecType = "encode"

	// CodecTypeDecode means the attached CodecStats represents a media format
	// that the implementation is prepared to decode.
	CodecTypeDecode CodecType = "decode"
)

// CodecStats contains statistics for a codec that is currently being used by RTP streams
// being sent or received by this PeerConnection object.
type CodecStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// PayloadType as used in RTP encoding or decoding
	PayloadType PayloadType `json:"payloadType"`

	// CodecType of this CodecStats
	CodecType CodecType `json:"codecType"`

	// TransportID is the unique identifier of the transport on which this codec is
	// being used, which can be used to look up the corresponding TransportStats object.
	TransportID string `json:"transportId"`

	// MimeType is the codec MIME media type/subtype. e.g., video/vp8 or equivalent.
	MimeType string `json:"mimeType"`

	// ClockRate represents the media sampling rate.
	ClockRate uint32 `json:"clockRate"`

	// Channels is 2 for stereo, missing for most other cases.
	Channels uint8 `json:"channels"`

	// SDPFmtpLine is the a=fmtp line in the SDP corresponding to the codec,
	// i.e., after the colon following the PT.
	SDPFmtpLine string `json:"sdpFmtpLine"`

	// Implementation identifies the implementation used. This is useful for diagnosing
	// interoperability issues.
	Implementation string `json:"implementation"`
}

func (s CodecStats) statsMarker() {}

func unmarshalCodecStats(b []byte) (CodecStats, error) {
	var codecStats CodecStats
	err := json.Unmarshal(b, &codecStats)
	if err != nil {
		return CodecStats{}, fmt.Errorf("unmarshal codec stats: %w", err)
	}

	return codecStats, nil
}

// InboundRTPStreamStats contains statistics for an inbound RTP stream that is
// currently received with this PeerConnection object.
type InboundRTPStreamStats struct {
	// Mid represents a mid value of RTPTransceiver owning this stream, if that value is not
	// null. Otherwise, this member is not present.
	Mid string `json:"mid"`

	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// SSRC is the 32-bit unsigned integer value used to identify the source of the
	// stream of RTP packets that this stats object concerns.
	SSRC SSRC `json:"ssrc"`

	// Kind is either "audio" or "video"
	Kind string `json:"kind"`

	// It is a unique identifier that is associated to the object that was inspected
	// to produce the TransportStats associated with this RTP stream.
	TransportID string `json:"transportId"`

	// CodecID is a unique identifier that is associated to the object that was inspected
	// to produce the CodecStats associated with this RTP stream.
	CodecID string `json:"codecId"`

	// FIRCount counts the total number of Full Intra Request (FIR) packets received
	// by the sender. This metric is only valid for video and is sent by receiver.
	FIRCount uint32 `json:"firCount"`

	// PLICount counts the total number of Picture Loss Indication (PLI) packets
	// received by the sender. This metric is only valid for video and is sent by receiver.
	PLICount uint32 `json:"pliCount"`

	// TotalProcessingDelay is the sum of the time, in seconds, each audio sample or video frame
	// takes from the time the first RTP packet is received (reception timestamp) and to the time
	// the corresponding sample or frame is decoded (decoded timestamp). At this point the audio
	// sample or video frame is ready for playout by the MediaStreamTrack. Typically ready for
	// playout here means after the audio sample or video frame is fully decoded by the decoder.
	TotalProcessingDelay float64 `json:"totalProcessingDelay"`

	// NACKCount counts the total number of Negative ACKnowledgement (NACK) packets
	// received by the sender and is sent by receiver.
	NACKCount uint32 `json:"nackCount"`

	// JitterBufferDelay is the sum of the time, in seconds, each audio sample or a video frame
	// takes from the time the first packet is received by the jitter buffer (ingest timestamp)
	// to the time it exits the jitter buffer (emit timestamp). The average jitter buffer delay
	// can be calculated by dividing the JitterBufferDelay with the JitterBufferEmittedCount.
	JitterBufferDelay float64 `json:"jitterBufferDelay"`

	// JitterBufferTargetDelay is increased by the target jitter buffer delay every time a sample is emitted
	// by the jitter buffer. The added target is the target delay, in seconds, at the time that
	// the sample was emitted from the jitter buffer. To get the average target delay,
	// divide by JitterBufferEmittedCount
	JitterBufferTargetDelay float64 `json:"jitterBufferTargetDelay"`

	// JitterBufferEmittedCount is the total number of audio samples or video frames that
	// have come out of the jitter buffer (increasing jitterBufferDelay).
	JitterBufferEmittedCount uint64 `json:"jitterBufferEmittedCount"`

	// JitterBufferMinimumDelay works the same way as jitterBufferTargetDelay, except that
	// it is not affected by external mechanisms that increase the jitter buffer target delay,
	// such as  jitterBufferTarget, AV sync, or any other mechanisms. This metric is purely
	// based on the network characteristics such as jitter and packet loss, and can be seen
	// as the minimum obtainable jitter  buffer delay if no external factors would affect it.
	// The metric is updated every time JitterBufferEmittedCount is updated.
	JitterBufferMinimumDelay float64 `json:"jitterBufferMinimumDelay"`

	// TotalSamplesReceived is the total number of samples that have been received on
	// this RTP stream. This includes concealedSamples. Does not exist for video.
	TotalSamplesReceived uint64 `json:"totalSamplesReceived"`

	// ConcealedSamples is the total number of samples that are concealed samples.
	// A concealed sample is a sample that was replaced with synthesized samples generated
	// locally before being played out. Examples of samples that have to be concealed are
	// samples from lost packets (reported in packetsLost) or samples from packets that
	// arrive too late to be played out (reported in packetsDiscarded). Does not exist for video.
	ConcealedSamples uint64 `json:"concealedSamples"`

	// SilentConcealedSamples is the total number of concealed samples inserted that
	// are "silent". Playing out silent samples results in silence or comfort noise.
	// This is a subset of concealedSamples. Does not exist for video.
	SilentConcealedSamples uint64 `json:"silentConcealedSamples"`

	// ConcealmentEvents increases every time a concealed sample is synthesized after
	// a non-concealed sample. That is, multiple consecutive concealed samples will increase
	// the concealedSamples count multiple times but is a single concealment event.
	// Does not exist for video.
	ConcealmentEvents uint64 `json:"concealmentEvents"`

	// InsertedSamplesForDeceleration is increased by the difference between the number of
	// samples received and the number of samples played out when playout is slowed down.
	// If playout is slowed down by inserting samples, this will be the number of inserted samples.
	// Does not exist for video.
	InsertedSamplesForDeceleration uint64 `json:"insertedSamplesForDeceleration"`

	// RemovedSamplesForAcceleration is increased by the difference between the number of
	// samples received and the number of samples played out when playout is sped up. If speedup
	// is achieved by removing samples, this will be the count of samples removed.
	// Does not exist for video.
	RemovedSamplesForAcceleration uint64 `json:"removedSamplesForAcceleration"`

	// AudioLevel represents the audio level of the receiving track..
	//
	// The value is a value between 0..1 (linear), where 1.0 represents 0 dBov,
	// 0 represents silence, and 0.5 represents approximately 6 dBSPL change in
	// the sound pressure level from 0 dBov. Does not exist for video.
	AudioLevel float64 `json:"audioLevel"`

	// TotalAudioEnergy represents the audio energy of the receiving track. It is calculated
	// by duration * Math.pow(energy/maxEnergy, 2) for each audio sample received (and thus
	// counted by TotalSamplesReceived). Does not exist for video.
	TotalAudioEnergy float64 `json:"totalAudioEnergy"`

	// TotalSamplesDuration represents the total duration in seconds of all samples that have been
	// received (and thus counted by TotalSamplesReceived). Can be used with totalAudioEnergy to
	// compute an average audio level over different intervals. Does not exist for video.
	TotalSamplesDuration float64 `json:"totalSamplesDuration"`

	// SLICount counts the total number of Slice Loss Indication (SLI) packets received
	// by the sender. This metric is only valid for video and is sent by receiver.
	SLICount uint32 `json:"sliCount"`

	// QPSum is the sum of the QP values of frames passed. The count of frames is
	// in FramesDecoded for inbound stream stats, and in FramesEncoded for outbound stream stats.
	QPSum uint64 `json:"qpSum"`

	// TotalDecodeTime is the total number of seconds that have been spent decoding the FramesDecoded
	// frames of this stream. The average decode time can be calculated by dividing this value
	// with FramesDecoded. The time it takes to decode one frame is the time passed between
	// feeding the decoder a frame and the decoder returning decoded data for that frame.
	TotalDecodeTime float64 `json:"totalDecodeTime"`

	// TotalInterFrameDelay is the sum of the interframe delays in seconds between consecutively
	// rendered frames, recorded just after a frame has been rendered. The interframe delay variance
	// be calculated from TotalInterFrameDelay, TotalSquaredInterFrameDelay, and FramesRendered according
	// to the formula: (TotalSquaredInterFrameDelay - TotalInterFrameDelay^2 / FramesRendered) / FramesRendered.
	// Does not exist for audio.
	TotalInterFrameDelay float64 `json:"totalInterFrameDelay"`

	// TotalSquaredInterFrameDelay is the sum of the squared interframe delays in seconds
	// between consecutively rendered frames, recorded just after a frame has been rendered.
	// See TotalInterFrameDelay for details on how to calculate the interframe delay variance.
	// Does not exist for audio.
	TotalSquaredInterFrameDelay float64 `json:"totalSquaredInterFrameDelay"`

	// PacketsReceived is the total number of RTP packets received for this SSRC.
	PacketsReceived uint32 `json:"packetsReceived"`

	// PacketsLost is the total number of RTP packets lost for this SSRC. Note that
	// because of how this is estimated, it can be negative if more packets are received than sent.
	PacketsLost int32 `json:"packetsLost"`

	// Jitter is the packet jitter measured in seconds for this SSRC
	Jitter float64 `json:"jitter"`

	// PacketsDiscarded is the cumulative number of RTP packets discarded by the jitter
	// buffer due to late or early-arrival, i.e., these packets are not played out.
	// RTP packets discarded due to packet duplication are not reported in this metric.
	PacketsDiscarded uint32 `json:"packetsDiscarded"`

	// PacketsRepaired is the cumulative number of lost RTP packets repaired after applying
	// an error-resilience mechanism. It is measured for the primary source RTP packets
	// and only counted for RTP packets that have no further chance of repair.
	PacketsRepaired uint32 `json:"packetsRepaired"`

	// BurstPacketsLost is the cumulative number of RTP packets lost during loss bursts.
	BurstPacketsLost uint32 `json:"burstPacketsLost"`

	// BurstPacketsDiscarded is the cumulative number of RTP packets discarded during discard bursts.
	BurstPacketsDiscarded uint32 `json:"burstPacketsDiscarded"`

	// BurstLossCount is the cumulative number of bursts of lost RTP packets.
	BurstLossCount uint32 `json:"burstLossCount"`

	// BurstDiscardCount is the cumulative number of bursts of discarded RTP packets.
	BurstDiscardCount uint32 `json:"burstDiscardCount"`

	// BurstLossRate is the fraction of RTP packets lost during bursts to the
	// total number of RTP packets expected in the bursts.
	BurstLossRate float64 `json:"burstLossRate"`

	// BurstDiscardRate is the fraction of RTP packets discarded during bursts to
	// the total number of RTP packets expected in bursts.
	BurstDiscardRate float64 `json:"burstDiscardRate"`

	// GapLossRate is the fraction of RTP packets lost during the gap periods.
	GapLossRate float64 `json:"gapLossRate"`

	// GapDiscardRate is the fraction of RTP packets discarded during the gap periods.
	GapDiscardRate float64 `json:"gapDiscardRate"`

	// TrackID is the identifier of the stats object representing the receiving track,
	// a ReceiverAudioTrackAttachmentStats or ReceiverVideoTrackAttachmentStats.
	TrackID string `json:"trackId"`

	// ReceiverID is the stats ID used to look up the AudioReceiverStats or VideoReceiverStats
	// object receiving this stream.
	ReceiverID string `json:"receiverId"`

	// RemoteID is used for looking up the remote RemoteOutboundRTPStreamStats object
	// for the same SSRC.
	RemoteID string `json:"remoteId"`

	// FramesDecoded represents the total number of frames correctly decoded for this SSRC,
	// i.e., frames that would be displayed if no frames are dropped. Only valid for video.
	FramesDecoded uint32 `json:"framesDecoded"`

	// KeyFramesDecoded represents the total number of key frames, such as key frames in
	// VP8 [RFC6386] or IDR-frames in H.264 [RFC6184], successfully decoded for this RTP
	// media stream. This is a subset of FramesDecoded. FramesDecoded - KeyFramesDecoded
	// gives you the number of delta frames decoded. Does not exist for audio.
	KeyFramesDecoded uint32 `json:"keyFramesDecoded"`

	// FramesRendered represents the total number of frames that have been rendered.
	// It is incremented just after a frame has been rendered. Does not exist for audio.
	FramesRendered uint32 `json:"framesRendered"`

	// FramesDropped is the total number of frames dropped prior to decode or dropped
	// because the frame missed its display deadline for this receiver's track.
	// The measurement begins when the receiver is created and is a cumulative metric
	// as defined in Appendix A (g) of [RFC7004]. Does not exist for audio.
	FramesDropped uint32 `json:"framesDropped"`

	// FrameWidth represents the width of the last decoded frame. Before the first
	// frame is decoded this member does not exist. Does not exist for audio.
	FrameWidth uint32 `json:"frameWidth"`

	// FrameHeight represents the height of the last decoded frame. Before the first
	// frame is decoded this member does not exist. Does not exist for audio.
	FrameHeight uint32 `json:"frameHeight"`

	// LastPacketReceivedTimestamp represents the timestamp at which the last packet was
	// received for this SSRC. This differs from Timestamp, which represents the time
	// at which the statistics were generated by the local endpoint.
	LastPacketReceivedTimestamp StatsTimestamp `json:"lastPacketReceivedTimestamp"`

	// HeaderBytesReceived is the total number of RTP header and padding bytes received for this SSRC.
	// This includes retransmissions. This does not include the size of transport layer headers such
	// as IP or UDP. headerBytesReceived + bytesReceived equals the number of bytes received as
	// payload over the transport.
	HeaderBytesReceived uint64 `json:"headerBytesReceived"`

	// AverageRTCPInterval is the average RTCP interval between two consecutive compound RTCP packets.
	// This is calculated by the sending endpoint when sending compound RTCP reports.
	// Compound packets must contain at least a RTCP RR or SR packet and an SDES packet
	// with the CNAME item.
	AverageRTCPInterval float64 `json:"averageRtcpInterval"`

	// FECPacketsReceived is the total number of RTP FEC packets received for this SSRC.
	// This counter can also be incremented when receiving FEC packets in-band with media packets (e.g., with Opus).
	FECPacketsReceived uint32 `json:"fecPacketsReceived"`

	// FECPacketsDiscarded is the total number of RTP FEC packets received for this SSRC where the
	// error correction payload was discarded by the application. This may happen
	// 1. if all the source packets protected by the FEC packet were received or already
	// recovered by a separate FEC packet, or
	// 2. if the FEC packet arrived late, i.e., outside the recovery window, and the
	// lost RTP packets have already been skipped during playout.
	// This is a subset of FECPacketsReceived.
	FECPacketsDiscarded uint64 `json:"fecPacketsDiscarded"`

	// BytesReceived is the total number of bytes received for this SSRC.
	BytesReceived uint64 `json:"bytesReceived"`

	// FramesReceived represents the total number of complete frames received on this RTP stream.
	// This metric is incremented when the complete frame is received. Does not exist for audio.
	FramesReceived uint32 `json:"framesReceived"`

	// PacketsFailedDecryption is the cumulative number of RTP packets that failed
	// to be decrypted. These packets are not counted by PacketsDiscarded.
	PacketsFailedDecryption uint32 `json:"packetsFailedDecryption"`

	// PacketsDuplicated is the cumulative number of packets discarded because they
	// are duplicated. Duplicate packets are not counted in PacketsDiscarded.
	//
	// Duplicated packets have the same RTP sequence number and content as a previously
	// received packet. If multiple duplicates of a packet are received, all of them are counted.
	// An improved estimate of lost packets can be calculated by adding PacketsDuplicated to PacketsLost.
	PacketsDuplicated uint32 `json:"packetsDuplicated"`

	// PerDSCPPacketsReceived is the total number of packets received for this SSRC,
	// per Differentiated Services code point (DSCP) [RFC2474]. DSCPs are identified
	// as decimal integers in string form. Note that due to network remapping and bleaching,
	// these numbers are not expected to match the numbers seen on sending. Not all
	// OSes make this information available.
	PerDSCPPacketsReceived map[string]uint32 `json:"perDscpPacketsReceived"`

	// Identifies the decoder implementation used. This is useful for diagnosing interoperability issues.
	// Does not exist for audio.
	DecoderImplementation string `json:"decoderImplementation"`

	// PauseCount is the total number of video pauses experienced by this receiver.
	// Video is considered to be paused if time passed since last rendered frame exceeds 5 seconds.
	// PauseCount is incremented when a frame is rendered after such a pause. Does not exist for audio.
	PauseCount uint32 `json:"pauseCount"`

	// TotalPausesDuration is the total duration of pauses (for definition of pause see PauseCount), in seconds.
	// Does not exist for audio.
	TotalPausesDuration float64 `json:"totalPausesDuration"`

	// FreezeCount is the total number of video freezes experienced by this receiver.
	// It is a freeze if frame duration, which is time interval between two consecutively rendered frames,
	// is equal or exceeds Max(3 * avg_frame_duration_ms, avg_frame_duration_ms + 150),
	// where avg_frame_duration_ms is linear average of durations of last 30 rendered frames.
	// Does not exist for audio.
	FreezeCount uint32 `json:"freezeCount"`

	// TotalFreezesDuration is the total duration of rendered frames which are considered as frozen
	// (for definition of freeze see freezeCount), in seconds. Does not exist for audio.
	TotalFreezesDuration float64 `json:"totalFreezesDuration"`

	// PowerEfficientDecoder indicates whether the decoder currently used is considered power efficient
	// by the user agent. Does not exist for audio.
	PowerEfficientDecoder bool `json:"powerEfficientDecoder"`
}

func (s InboundRTPStreamStats) statsMarker() {}

func unmarshalInboundRTPStreamStats(b []byte) (InboundRTPStreamStats, error) {
	var inboundRTPStreamStats InboundRTPStreamStats
	err := json.Unmarshal(b, &inboundRTPStreamStats)
	if err != nil {
		return InboundRTPStreamStats{}, fmt.Errorf("unmarshal inbound rtp stream stats: %w", err)
	}

	return inboundRTPStreamStats, nil
}

// QualityLimitationReason lists the reason for limiting the resolution and/or framerate.
// Only valid for video.
type QualityLimitationReason string

const (
	// QualityLimitationReasonNone means the resolution and/or framerate is not limited.
	QualityLimitationReasonNone QualityLimitationReason = "none"

	// QualityLimitationReasonCPU means the resolution and/or framerate is primarily limited due to CPU load.
	QualityLimitationReasonCPU QualityLimitationReason = "cpu"

	// QualityLimitationReasonBandwidth means the resolution and/or framerate is primarily limited
	// due to congestion cues during bandwidth estimation.
	// Typical, congestion control algorithms use inter-arrival time, round-trip time,
	//  packet or other congestion cues to perform bandwidth estimation.
	QualityLimitationReasonBandwidth QualityLimitationReason = "bandwidth"

	// QualityLimitationReasonOther means the resolution and/or framerate is primarily limited
	//  for a reason other than the above.
	QualityLimitationReasonOther QualityLimitationReason = "other"
)

// OutboundRTPStreamStats contains statistics for an outbound RTP stream that is
// currently sent with this PeerConnection object.
type OutboundRTPStreamStats struct {
	// Mid represents a mid value of RTPTransceiver owning this stream, if that value is not
	// null. Otherwise, this member is not present.
	Mid string `json:"mid"`

	// Rid only exists if a rid has been set for this RTP stream.
	// Must not exist for audio.
	Rid string `json:"rid"`

	// MediaSourceID is the identifier of the stats object representing the track currently
	// attached to the sender of this stream, an RTCMediaSourceStats.
	MediaSourceID string `json:"mediaSourceId"`

	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// SSRC is the 32-bit unsigned integer value used to identify the source of the
	// stream of RTP packets that this stats object concerns.
	SSRC SSRC `json:"ssrc"`

	// Kind is either "audio" or "video"
	Kind string `json:"kind"`

	// It is a unique identifier that is associated to the object that was inspected
	// to produce the TransportStats associated with this RTP stream.
	TransportID string `json:"transportId"`

	// CodecID is a unique identifier that is associated to the object that was inspected
	// to produce the CodecStats associated with this RTP stream.
	CodecID string `json:"codecId"`

	// HeaderBytesSent is the total number of RTP header and padding bytes sent for this SSRC. This does not
	// include the size of transport layer headers such as IP or UDP.
	// HeaderBytesSent + BytesSent equals the number of bytes sent as payload over the transport.
	HeaderBytesSent uint64 `json:"headerBytesSent"`

	// RetransmittedPacketsSent is the total number of packets that were retransmitted for this SSRC.
	// This is a subset of packetsSent. If RTX is not negotiated, retransmitted packets are sent
	// over this ssrc. If RTX was negotiated, retransmitted packets are sent over a separate SSRC
	// but is still accounted for here.
	RetransmittedPacketsSent uint64 `json:"retransmittedPacketsSent"`

	// RetransmittedBytesSent is the total number of bytes that were retransmitted for this SSRC,
	// only including payload bytes. This is a subset of bytesSent. If RTX is not negotiated,
	// retransmitted bytes are sent over this ssrc. If RTX was negotiated, retransmitted bytes
	// are sent over a separate SSRC but is still accounted for here.
	RetransmittedBytesSent uint64 `json:"retransmittedBytesSent"`

	// FIRCount counts the total number of Full Intra Request (FIR) packets received
	// by the sender. This metric is only valid for video and is sent by receiver.
	FIRCount uint32 `json:"firCount"`

	// PLICount counts the total number of Picture Loss Indication (PLI) packets
	// received by the sender. This metric is only valid for video and is sent by receiver.
	PLICount uint32 `json:"pliCount"`

	// NACKCount counts the total number of Negative ACKnowledgement (NACK) packets
	// received by the sender and is sent by receiver.
	NACKCount uint32 `json:"nackCount"`

	// SLICount counts the total number of Slice Loss Indication (SLI) packets received
	// by the sender. This metric is only valid for video and is sent by receiver.
	SLICount uint32 `json:"sliCount"`

	// QPSum is the sum of the QP values of frames passed. The count of frames is
	// in FramesDecoded for inbound stream stats, and in FramesEncoded for outbound stream stats.
	QPSum uint64 `json:"qpSum"`

	// PacketsSent is the total number of RTP packets sent for this SSRC.
	PacketsSent uint32 `json:"packetsSent"`

	// PacketsDiscardedOnSend is the total number of RTP packets for this SSRC that
	// have been discarded due to socket errors, i.e. a socket error occurred when handing
	// the packets to the socket. This might happen due to various reasons, including
	// full buffer or no available memory.
	PacketsDiscardedOnSend uint32 `json:"packetsDiscardedOnSend"`

	// FECPacketsSent is the total number of RTP FEC packets sent for this SSRC.
	// This counter can also be incremented when sending FEC packets in-band with
	// media packets (e.g., with Opus).
	FECPacketsSent uint32 `json:"fecPacketsSent"`

	// BytesSent is the total number of bytes sent for this SSRC.
	BytesSent uint64 `json:"bytesSent"`

	// BytesDiscardedOnSend is the total number of bytes for this SSRC that have
	// been discarded due to socket errors, i.e. a socket error occurred when handing
	// the packets containing the bytes to the socket. This might happen due to various
	// reasons, including full buffer or no available memory.
	BytesDiscardedOnSend uint64 `json:"bytesDiscardedOnSend"`

	// TrackID is the identifier of the stats object representing the current track
	// attachment to the sender of this stream, a SenderAudioTrackAttachmentStats
	// or SenderVideoTrackAttachmentStats.
	TrackID string `json:"trackId"`

	// SenderID is the stats ID used to look up the AudioSenderStats or VideoSenderStats
	// object sending this stream.
	SenderID string `json:"senderId"`

	// RemoteID is used for looking up the remote RemoteInboundRTPStreamStats object
	// for the same SSRC.
	RemoteID string `json:"remoteId"`

	// LastPacketSentTimestamp represents the timestamp at which the last packet was
	// sent for this SSRC. This differs from timestamp, which represents the time at
	// which the statistics were generated by the local endpoint.
	LastPacketSentTimestamp StatsTimestamp `json:"lastPacketSentTimestamp"`

	// TargetBitrate is the current target bitrate configured for this particular SSRC
	// and is the Transport Independent Application Specific (TIAS) bitrate [RFC3890].
	// Typically, the target bitrate is a configuration parameter provided to the codec's
	// encoder and does not count the size of the IP or other transport layers like TCP or UDP.
	// It is measured in bits per second and the bitrate is calculated over a 1 second window.
	TargetBitrate float64 `json:"targetBitrate"`

	// TotalEncodedBytesTarget is increased by the target frame size in bytes every time
	// a frame has been encoded. The actual frame size may be bigger or smaller than this number.
	// This value goes up every time framesEncoded goes up.
	TotalEncodedBytesTarget uint64 `json:"totalEncodedBytesTarget"`

	// FrameWidth represents the width of the last encoded frame. The resolution of the
	// encoded frame may be lower than the media source. Before the first frame is encoded
	// this member does not exist. Does not exist for audio.
	FrameWidth uint32 `json:"frameWidth"`

	// FrameHeight represents the height of the last encoded frame. The resolution of the
	// encoded frame may be lower than the media source. Before the first frame is encoded
	// this member does not exist. Does not exist for audio.
	FrameHeight uint32 `json:"frameHeight"`

	// FramesPerSecond is the number of encoded frames during the last second. This may be
	// lower than the media source frame rate. Does not exist for audio.
	FramesPerSecond float64 `json:"framesPerSecond"`

	// FramesSent represents the total number of frames sent on this RTP stream. Does not exist for audio.
	FramesSent uint32 `json:"framesSent"`

	// HugeFramesSent represents the total number of huge frames sent by this RTP stream.
	// Huge frames, by definition, are frames that have an encoded size at least 2.5 times
	// the average size of the frames. The average size of the frames is defined as the
	// target bitrate per second divided by the target FPS at the time the frame was encoded.
	// These are usually complex to encode frames with a lot of changes in the picture.
	// This can be used to estimate, e.g slide changes in the streamed presentation.
	// Does not exist for audio.
	HugeFramesSent uint32 `json:"hugeFramesSent"`

	// FramesEncoded represents the total number of frames successfully encoded for this RTP media stream.
	// Only valid for video.
	FramesEncoded uint32 `json:"framesEncoded"`

	// KeyFramesEncoded represents the total number of key frames, such as key frames in VP8 [RFC6386] or
	// IDR-frames in H.264 [RFC6184], successfully encoded for this RTP media stream. This is a subset of
	// FramesEncoded. FramesEncoded - KeyFramesEncoded gives you the number of delta frames encoded.
	// Does not exist for audio.
	KeyFramesEncoded uint32 `json:"keyFramesEncoded"`

	// TotalEncodeTime is the total number of seconds that has been spent encoding the
	// framesEncoded frames of this stream. The average encode time can be calculated by
	// dividing this value with FramesEncoded. The time it takes to encode one frame is the
	// time passed between feeding the encoder a frame and the encoder returning encoded data
	// for that frame. This does not include any additional time it may take to packetize the resulting data.
	TotalEncodeTime float64 `json:"totalEncodeTime"`

	// TotalPacketSendDelay is the total number of seconds that packets have spent buffered
	// locally before being transmitted onto the network. The time is measured from when
	// a packet is emitted from the RTP packetizer until it is handed over to the OS network socket.
	// This measurement is added to totalPacketSendDelay when packetsSent is incremented.
	TotalPacketSendDelay float64 `json:"totalPacketSendDelay"`

	// AverageRTCPInterval is the average RTCP interval between two consecutive compound RTCP
	// packets. This is calculated by the sending endpoint when sending compound RTCP reports.
	// Compound packets must contain at least a RTCP RR or SR packet and an SDES packet with the CNAME item.
	AverageRTCPInterval float64 `json:"averageRtcpInterval"`

	// QualityLimitationReason is the current reason for limiting the resolution and/or framerate,
	// or "none" if not limited. Only valid for video.
	QualityLimitationReason QualityLimitationReason `json:"qualityLimitationReason"`

	// QualityLimitationDurations is record of the total time, in seconds, that this
	// stream has spent in each quality limitation state. The record includes a mapping
	// for all QualityLimitationReason types, including "none". Only valid for video.
	QualityLimitationDurations map[string]float64 `json:"qualityLimitationDurations"`

	// QualityLimitationResolutionChanges is the number of times that the resolution has changed
	// because we are quality limited (qualityLimitationReason has a value other than "none").
	// The counter is initially zero and increases when the resolution goes up or down.
	// For example, if a 720p track is sent as 480p for some time and then recovers to 720p,
	// qualityLimitationResolutionChanges will have the value 2. Does not exist for audio.
	QualityLimitationResolutionChanges uint32 `json:"qualityLimitationResolutionChanges"`

	// PerDSCPPacketsSent is the total number of packets sent for this SSRC, per DSCP.
	// DSCPs are identified as decimal integers in string form.
	PerDSCPPacketsSent map[string]uint32 `json:"perDscpPacketsSent"`

	// Active indicates whether this RTP stream is configured to be sent or disabled. Note that an
	// active stream can still not be sending, e.g. when being limited by network conditions.
	Active bool `json:"active"`

	// Identifies the encoder implementation used. This is useful for diagnosing interoperability issues.
	// Does not exist for audio.
	EncoderImplementation string `json:"encoderImplementation"`

	// PowerEfficientEncoder indicates whether the encoder currently used is considered power efficient.
	// by the user agent. Does not exist for audio.
	PowerEfficientEncoder bool `json:"powerEfficientEncoder"`

	// ScalabilityMode identifies the layering mode used for video encoding. Does not exist for audio.
	ScalabilityMode string `json:"scalabilityMode"`
}

func (s OutboundRTPStreamStats) statsMarker() {}

func unmarshalOutboundRTPStreamStats(b []byte) (OutboundRTPStreamStats, error) {
	var outboundRTPStreamStats OutboundRTPStreamStats
	err := json.Unmarshal(b, &outboundRTPStreamStats)
	if err != nil {
		return OutboundRTPStreamStats{}, fmt.Errorf("unmarshal outbound rtp stream stats: %w", err)
	}

	return outboundRTPStreamStats, nil
}

// RemoteInboundRTPStreamStats contains statistics for the remote endpoint's inbound
// RTP stream corresponding to an outbound stream that is currently sent with this
// PeerConnection object. It is measured at the remote endpoint and reported in an RTCP
// Receiver Report (RR) or RTCP Extended Report (XR).
type RemoteInboundRTPStreamStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// SSRC is the 32-bit unsigned integer value used to identify the source of the
	// stream of RTP packets that this stats object concerns.
	SSRC SSRC `json:"ssrc"`

	// Kind is either "audio" or "video"
	Kind string `json:"kind"`

	// It is a unique identifier that is associated to the object that was inspected
	// to produce the TransportStats associated with this RTP stream.
	TransportID string `json:"transportId"`

	// CodecID is a unique identifier that is associated to the object that was inspected
	// to produce the CodecStats associated with this RTP stream.
	CodecID string `json:"codecId"`

	// FIRCount counts the total number of Full Intra Request (FIR) packets received
	// by the sender. This metric is only valid for video and is sent by receiver.
	FIRCount uint32 `json:"firCount"`

	// PLICount counts the total number of Picture Loss Indication (PLI) packets
	// received by the sender. This metric is only valid for video and is sent by receiver.
	PLICount uint32 `json:"pliCount"`

	// NACKCount counts the total number of Negative ACKnowledgement (NACK) packets
	// received by the sender and is sent by receiver.
	NACKCount uint32 `json:"nackCount"`

	// SLICount counts the total number of Slice Loss Indication (SLI) packets received
	// by the sender. This metric is only valid for video and is sent by receiver.
	SLICount uint32 `json:"sliCount"`

	// QPSum is the sum of the QP values of frames passed. The count of frames is
	// in FramesDecoded for inbound stream stats, and in FramesEncoded for outbound stream stats.
	QPSum uint64 `json:"qpSum"`

	// PacketsReceived is the total number of RTP packets received for this SSRC.
	PacketsReceived uint32 `json:"packetsReceived"`

	// PacketsLost is the total number of RTP packets lost for this SSRC. Note that
	// because of how this is estimated, it can be negative if more packets are received than sent.
	PacketsLost int32 `json:"packetsLost"`

	// Jitter is the packet jitter measured in seconds for this SSRC
	Jitter float64 `json:"jitter"`

	// PacketsDiscarded is the cumulative number of RTP packets discarded by the jitter
	// buffer due to late or early-arrival, i.e., these packets are not played out.
	// RTP packets discarded due to packet duplication are not reported in this metric.
	PacketsDiscarded uint32 `json:"packetsDiscarded"`

	// PacketsRepaired is the cumulative number of lost RTP packets repaired after applying
	// an error-resilience mechanism. It is measured for the primary source RTP packets
	// and only counted for RTP packets that have no further chance of repair.
	PacketsRepaired uint32 `json:"packetsRepaired"`

	// BurstPacketsLost is the cumulative number of RTP packets lost during loss bursts.
	BurstPacketsLost uint32 `json:"burstPacketsLost"`

	// BurstPacketsDiscarded is the cumulative number of RTP packets discarded during discard bursts.
	BurstPacketsDiscarded uint32 `json:"burstPacketsDiscarded"`

	// BurstLossCount is the cumulative number of bursts of lost RTP packets.
	BurstLossCount uint32 `json:"burstLossCount"`

	// BurstDiscardCount is the cumulative number of bursts of discarded RTP packets.
	BurstDiscardCount uint32 `json:"burstDiscardCount"`

	// BurstLossRate is the fraction of RTP packets lost during bursts to the
	// total number of RTP packets expected in the bursts.
	BurstLossRate float64 `json:"burstLossRate"`

	// BurstDiscardRate is the fraction of RTP packets discarded during bursts to
	// the total number of RTP packets expected in bursts.
	BurstDiscardRate float64 `json:"burstDiscardRate"`

	// GapLossRate is the fraction of RTP packets lost during the gap periods.
	GapLossRate float64 `json:"gapLossRate"`

	// GapDiscardRate is the fraction of RTP packets discarded during the gap periods.
	GapDiscardRate float64 `json:"gapDiscardRate"`

	// LocalID is used for looking up the local OutboundRTPStreamStats object for the same SSRC.
	LocalID string `json:"localId"`

	// RoundTripTime is the estimated round trip time for this SSRC based on the
	// RTCP timestamps in the RTCP Receiver Report (RR) and measured in seconds.
	RoundTripTime float64 `json:"roundTripTime"`

	// TotalRoundTripTime represents the cumulative sum of all round trip time measurements
	// in seconds since the beginning of the session. The individual round trip time is calculated
	// based on the RTCP timestamps in the RTCP Receiver Report (RR) [RFC3550], hence requires
	// a DLSR value other than 0. The average round trip time can be computed from
	// TotalRoundTripTime by dividing it by RoundTripTimeMeasurements.
	TotalRoundTripTime float64 `json:"totalRoundTripTime"`

	// FractionLost is the fraction packet loss reported for this SSRC.
	FractionLost float64 `json:"fractionLost"`

	// RoundTripTimeMeasurements represents the total number of RTCP RR blocks received for this SSRC
	// that contain a valid round trip time. This counter will not increment if the RoundTripTime can
	// not be calculated because no RTCP Receiver Report with a DLSR value other than 0 has been received.
	RoundTripTimeMeasurements uint64 `json:"roundTripTimeMeasurements"`
}

func (s RemoteInboundRTPStreamStats) statsMarker() {}

func unmarshalRemoteInboundRTPStreamStats(b []byte) (RemoteInboundRTPStreamStats, error) {
	var remoteInboundRTPStreamStats RemoteInboundRTPStreamStats
	err := json.Unmarshal(b, &remoteInboundRTPStreamStats)
	if err != nil {
		return RemoteInboundRTPStreamStats{}, fmt.Errorf("unmarshal remote inbound rtp stream stats: %w", err)
	}

	return remoteInboundRTPStreamStats, nil
}

// RemoteOutboundRTPStreamStats contains statistics for the remote endpoint's outbound
// RTP stream corresponding to an inbound stream that is currently received with this
// PeerConnection object. It is measured at the remote endpoint and reported in an
// RTCP Sender Report (SR).
type RemoteOutboundRTPStreamStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// SSRC is the 32-bit unsigned integer value used to identify the source of the
	// stream of RTP packets that this stats object concerns.
	SSRC SSRC `json:"ssrc"`

	// Kind is either "audio" or "video"
	Kind string `json:"kind"`

	// It is a unique identifier that is associated to the object that was inspected
	// to produce the TransportStats associated with this RTP stream.
	TransportID string `json:"transportId"`

	// CodecID is a unique identifier that is associated to the object that was inspected
	// to produce the CodecStats associated with this RTP stream.
	CodecID string `json:"codecId"`

	// FIRCount counts the total number of Full Intra Request (FIR) packets received
	// by the sender. This metric is only valid for video and is sent by receiver.
	FIRCount uint32 `json:"firCount"`

	// PLICount counts the total number of Picture Loss Indication (PLI) packets
	// received by the sender. This metric is only valid for video and is sent by receiver.
	PLICount uint32 `json:"pliCount"`

	// NACKCount counts the total number of Negative ACKnowledgement (NACK) packets
	// received by the sender and is sent by receiver.
	NACKCount uint32 `json:"nackCount"`

	// SLICount counts the total number of Slice Loss Indication (SLI) packets received
	// by the sender. This metric is only valid for video and is sent by receiver.
	SLICount uint32 `json:"sliCount"`

	// QPSum is the sum of the QP values of frames passed. The count of frames is
	// in FramesDecoded for inbound stream stats, and in FramesEncoded for outbound stream stats.
	QPSum uint64 `json:"qpSum"`

	// PacketsSent is the total number of RTP packets sent for this SSRC.
	PacketsSent uint32 `json:"packetsSent"`

	// PacketsDiscardedOnSend is the total number of RTP packets for this SSRC that
	// have been discarded due to socket errors, i.e. a socket error occurred when handing
	// the packets to the socket. This might happen due to various reasons, including
	// full buffer or no available memory.
	PacketsDiscardedOnSend uint32 `json:"packetsDiscardedOnSend"`

	// FECPacketsSent is the total number of RTP FEC packets sent for this SSRC.
	// This counter can also be incremented when sending FEC packets in-band with
	// media packets (e.g., with Opus).
	FECPacketsSent uint32 `json:"fecPacketsSent"`

	// BytesSent is the total number of bytes sent for this SSRC.
	BytesSent uint64 `json:"bytesSent"`

	// BytesDiscardedOnSend is the total number of bytes for this SSRC that have
	// been discarded due to socket errors, i.e. a socket error occurred when handing
	// the packets containing the bytes to the socket. This might happen due to various
	// reasons, including full buffer or no available memory.
	BytesDiscardedOnSend uint64 `json:"bytesDiscardedOnSend"`

	// LocalID is used for looking up the local InboundRTPStreamStats object for the same SSRC.
	LocalID string `json:"localId"`

	// RemoteTimestamp represents the remote timestamp at which these statistics were
	// sent by the remote endpoint. This differs from timestamp, which represents the
	// time at which the statistics were generated or received by the local endpoint.
	// The RemoteTimestamp, if present, is derived from the NTP timestamp in an RTCP
	// Sender Report (SR) packet, which reflects the remote endpoint's clock.
	// That clock may not be synchronized with the local clock.
	RemoteTimestamp StatsTimestamp `json:"remoteTimestamp"`

	// ReportsSent represents the total number of RTCP Sender Report (SR) blocks sent for this SSRC.
	ReportsSent uint64 `json:"reportsSent"`

	// RoundTripTime is estimated round trip time for this SSRC based on the latest
	// RTCP Sender Report (SR) that contains a DLRR report block as defined in [RFC3611].
	// The Calculation of the round trip time is defined in section 4.5. of [RFC3611].
	// Does not exist if the latest SR does not contain the DLRR report block, or if the last RR timestamp
	// in the DLRR report block is zero, or if the delay since last RR value in the DLRR report block is zero.
	RoundTripTime float64 `json:"roundTripTime"`

	// TotalRoundTripTime represents the cumulative sum of all round trip time measurements in seconds
	// since the beginning of the session. The individual round trip time is calculated based on the DLRR
	// report block in the RTCP Sender Report (SR) [RFC3611]. This counter will not increment if the
	// RoundTripTime can not be calculated. The average round trip time can be computed from
	// TotalRoundTripTime by dividing it by RoundTripTimeMeasurements.
	TotalRoundTripTime float64 `json:"totalRoundTripTime"`

	// RoundTripTimeMeasurements represents the total number of RTCP Sender Report (SR) blocks
	// received for this SSRC that contain a DLRR report block that can derive a valid round trip time
	// according to [RFC3611]. This counter will not increment if the RoundTripTime can not be calculated.
	RoundTripTimeMeasurements uint64 `json:"roundTripTimeMeasurements"`
}

func (s RemoteOutboundRTPStreamStats) statsMarker() {}

func unmarshalRemoteOutboundRTPStreamStats(b []byte) (RemoteOutboundRTPStreamStats, error) {
	var remoteOutboundRTPStreamStats RemoteOutboundRTPStreamStats
	err := json.Unmarshal(b, &remoteOutboundRTPStreamStats)
	if err != nil {
		return RemoteOutboundRTPStreamStats{}, fmt.Errorf("unmarshal remote outbound rtp stream stats: %w", err)
	}

	return remoteOutboundRTPStreamStats, nil
}

// RTPContributingSourceStats contains statistics for a contributing source (CSRC) that contributed
// to an inbound RTP stream.
type RTPContributingSourceStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// ContributorSSRC is the SSRC identifier of the contributing source represented
	// by this stats object. It is a 32-bit unsigned integer that appears in the CSRC
	// list of any packets the relevant source contributed to.
	ContributorSSRC SSRC `json:"contributorSsrc"`

	// InboundRTPStreamID is the ID of the InboundRTPStreamStats object representing
	// the inbound RTP stream that this contributing source is contributing to.
	InboundRTPStreamID string `json:"inboundRtpStreamId"`

	// PacketsContributedTo is the total number of RTP packets that this contributing
	// source contributed to. This value is incremented each time a packet is counted
	// by InboundRTPStreamStats.packetsReceived, and the packet's CSRC list contains
	// the SSRC identifier of this contributing source, ContributorSSRC.
	PacketsContributedTo uint32 `json:"packetsContributedTo"`

	// AudioLevel is present if the last received RTP packet that this source contributed
	// to contained an [RFC6465] mixer-to-client audio level header extension. The value
	// of audioLevel is between 0..1 (linear), where 1.0 represents 0 dBov, 0 represents
	// silence, and 0.5 represents approximately 6 dBSPL change in the sound pressure level from 0 dBov.
	AudioLevel float64 `json:"audioLevel"`
}

func (s RTPContributingSourceStats) statsMarker() {}

func unmarshalCSRCStats(b []byte) (RTPContributingSourceStats, error) {
	var csrcStats RTPContributingSourceStats
	err := json.Unmarshal(b, &csrcStats)
	if err != nil {
		return RTPContributingSourceStats{}, fmt.Errorf("unmarshal csrc stats: %w", err)
	}

	return csrcStats, nil
}

// AudioSourceStats represents an audio track that is attached to one or more senders.
type AudioSourceStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// TrackIdentifier represents the id property of the track.
	TrackIdentifier string `json:"trackIdentifier"`

	// Kind is "audio"
	Kind string `json:"kind"`

	// AudioLevel represents the output audio level of the track.
	//
	// The value is a value between 0..1 (linear), where 1.0 represents 0 dBov,
	// 0 represents silence, and 0.5 represents approximately 6 dBSPL change in
	// the sound pressure level from 0 dBov.
	//
	// If the track is sourced from an Receiver, does no audio processing, has a
	// constant level, and has a volume setting of 1.0, the audio level is expected
	// to be the same as the audio level of the source SSRC, while if the volume setting
	// is 0.5, the AudioLevel is expected to be half that value.
	AudioLevel float64 `json:"audioLevel"`

	// TotalAudioEnergy is the total energy of all the audio samples sent/received
	// for this object, calculated by duration * Math.pow(energy/maxEnergy, 2) for
	// each audio sample seen.
	TotalAudioEnergy float64 `json:"totalAudioEnergy"`

	// TotalSamplesDuration represents the total duration in seconds of all samples
	// that have sent or received (and thus counted by TotalSamplesSent or TotalSamplesReceived).
	// Can be used with TotalAudioEnergy to compute an average audio level over different intervals.
	TotalSamplesDuration float64 `json:"totalSamplesDuration"`

	// EchoReturnLoss is only present while the sender is sending a track sourced from
	// a microphone where echo cancellation is applied. Calculated in decibels.
	EchoReturnLoss float64 `json:"echoReturnLoss"`

	// EchoReturnLossEnhancement is only present while the sender is sending a track
	// sourced from a microphone where echo cancellation is applied. Calculated in decibels.
	EchoReturnLossEnhancement float64 `json:"echoReturnLossEnhancement"`

	// DroppedSamplesDuration represents the total duration, in seconds, of samples produced by the device that got
	// dropped before reaching the media source. Only applicable if this media source is backed by an audio capture device.
	DroppedSamplesDuration float64 `json:"droppedSamplesDuration"`

	// DroppedSamplesEvents is the number of dropped samples events. This counter increases every time a sample is
	// dropped after a non-dropped sample. That is, multiple consecutive dropped samples will increase
	// droppedSamplesDuration multiple times but is a single dropped samples event.
	DroppedSamplesEvents uint64 `json:"droppedSamplesEvents"`

	// TotalCaptureDelay is the total delay, in seconds, for each audio sample between the time the sample was emitted
	// by the capture device and the sample reaching the source. This can be used together with totalSamplesCaptured to
	// calculate the average capture delay per sample.
	// Only applicable if the audio source represents an audio capture device.
	TotalCaptureDelay float64 `json:"totalCaptureDelay"`

	// TotalSamplesCaptured is the total number of captured samples reaching the audio source, i.e. that were not dropped
	// by the capture pipeline. The frequency of the media source is not necessarily the same as the frequency of encoders
	// later in the pipeline. Only applicable if the audio source represents an audio capture device.
	TotalSamplesCaptured uint64 `json:"totalSamplesCaptured"`
}

func (s AudioSourceStats) statsMarker() {}

// VideoSourceStats represents a video track that is attached to one or more senders.
type VideoSourceStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// TrackIdentifier represents the id property of the track.
	TrackIdentifier string `json:"trackIdentifier"`

	// Kind is "video"
	Kind string `json:"kind"`

	// Width is width of the last frame originating from this source in pixels.
	Width uint32 `json:"width"`

	// Height is height of the last frame originating from this source in pixels.
	Height uint32 `json:"height"`

	// Frames is the total number of frames originating from this source.
	Frames uint32 `json:"frames"`

	// FramesPerSecond is the number of frames originating from this source, measured during the last second.
	FramesPerSecond float64 `json:"framesPerSecond"`
}

func (s VideoSourceStats) statsMarker() {}

func unmarshalMediaSourceStats(b []byte) (Stats, error) {
	type kindJSON struct {
		Kind string `json:"kind"`
	}
	kindHolder := kindJSON{}

	err := json.Unmarshal(b, &kindHolder)
	if err != nil {
		return nil, fmt.Errorf("unmarshal json kind: %w", err)
	}

	switch MediaKind(kindHolder.Kind) {
	case MediaKindAudio:
		var mediaSourceStats AudioSourceStats
		err := json.Unmarshal(b, &mediaSourceStats)
		if err != nil {
			return nil, fmt.Errorf("unmarshal audio source stats: %w", err)
		}

		return mediaSourceStats, nil
	case MediaKindVideo:
		var mediaSourceStats VideoSourceStats
		err := json.Unmarshal(b, &mediaSourceStats)
		if err != nil {
			return nil, fmt.Errorf("unmarshal video source stats: %w", err)
		}

		return mediaSourceStats, nil
	default:
		return nil, fmt.Errorf("kind: %w", ErrUnknownType)
	}
}

// AudioPlayoutStats represents one playout path - if the same playout stats object is referenced by multiple
// RTCInboundRtpStreamStats this is an indication that audio mixing is happening in which case sample counters in this
// stats object refer to the samples after mixing. Only applicable if the playout path represents an audio device.
type AudioPlayoutStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// Kind is "audio"
	Kind string `json:"kind"`

	// SynthesizedSamplesDuration is measured in seconds and is incremented each time an audio sample is synthesized by
	// this playout path. This metric can be used together with totalSamplesDuration to calculate the percentage of played
	// out media being synthesized. If the playout path is unable to produce audio samples on time for device playout,
	// samples are synthesized to be playout out instead. Synthesization typically only happens if the pipeline is
	// underperforming. Samples synthesized by the RTCInboundRtpStreamStats are not counted for here, but in
	// InboundRtpStreamStats.concealedSamples.
	SynthesizedSamplesDuration float64 `json:"synthesizedSamplesDuration"`

	// SynthesizedSamplesEvents is the number of synthesized samples events. This counter increases every time a sample
	// is synthesized after a non-synthesized sample. That is, multiple consecutive synthesized samples will increase
	// synthesizedSamplesDuration multiple times but is a single synthesization samples event.
	SynthesizedSamplesEvents uint64 `json:"synthesizedSamplesEvents"`

	// TotalSamplesDuration represents the total duration in seconds of all samples
	// that have sent or received (and thus counted by TotalSamplesSent or TotalSamplesReceived).
	// Can be used with TotalAudioEnergy to compute an average audio level over different intervals.
	TotalSamplesDuration float64 `json:"totalSamplesDuration"`

	// When audio samples are pulled by the playout device, this counter is incremented with the estimated delay of the
	// playout path for that audio sample. The playout delay includes the delay from being emitted to the actual time of
	// playout on the device. This metric can be used together with totalSamplesCount to calculate the average
	// playout delay per sample.
	TotalPlayoutDelay float64 `json:"totalPlayoutDelay"`

	// When audio samples are pulled by the playout device, this counter is incremented with the number of samples
	// emitted for playout.
	TotalSamplesCount uint64 `json:"totalSamplesCount"`
}

func (s AudioPlayoutStats) statsMarker() {}

func unmarshalMediaPlayoutStats(b []byte) (Stats, error) {
	var audioPlayoutStats AudioPlayoutStats
	err := json.Unmarshal(b, &audioPlayoutStats)
	if err != nil {
		return nil, fmt.Errorf("unmarshal audio playout stats: %w", err)
	}

	return audioPlayoutStats, nil
}

// PeerConnectionStats contains statistics related to the PeerConnection object.
type PeerConnectionStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// DataChannelsOpened represents the number of unique DataChannels that have
	// entered the "open" state during their lifetime.
	DataChannelsOpened uint32 `json:"dataChannelsOpened"`

	// DataChannelsClosed represents the number of unique DataChannels that have
	// left the "open" state during their lifetime (due to being closed by either
	// end or the underlying transport being closed). DataChannels that transition
	// from "connecting" to "closing" or "closed" without ever being "open"
	// are not counted in this number.
	DataChannelsClosed uint32 `json:"dataChannelsClosed"`

	// DataChannelsRequested Represents the number of unique DataChannels returned
	// from a successful createDataChannel() call on the PeerConnection. If the
	// underlying data transport is not established, these may be in the "connecting" state.
	DataChannelsRequested uint32 `json:"dataChannelsRequested"`

	// DataChannelsAccepted represents the number of unique DataChannels signaled
	// in a "datachannel" event on the PeerConnection.
	DataChannelsAccepted uint32 `json:"dataChannelsAccepted"`
}

func (s PeerConnectionStats) statsMarker() {}

func unmarshalPeerConnectionStats(b []byte) (PeerConnectionStats, error) {
	var pcStats PeerConnectionStats
	err := json.Unmarshal(b, &pcStats)
	if err != nil {
		return PeerConnectionStats{}, fmt.Errorf("unmarshal pc stats: %w", err)
	}

	return pcStats, nil
}

// DataChannelStats contains statistics related to each DataChannel ID.
type DataChannelStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// Label is the "label" value of the DataChannel object.
	Label string `json:"label"`

	// Protocol is the "protocol" value of the DataChannel object.
	Protocol string `json:"protocol"`

	// DataChannelIdentifier is the "id" attribute of the DataChannel object.
	DataChannelIdentifier int32 `json:"dataChannelIdentifier"`

	// TransportID the ID of the TransportStats object for transport used to carry this datachannel.
	TransportID string `json:"transportId"`

	// State is the "readyState" value of the DataChannel object.
	State DataChannelState `json:"state"`

	// MessagesSent represents the total number of API "message" events sent.
	MessagesSent uint32 `json:"messagesSent"`

	// BytesSent represents the total number of payload bytes sent on this
	// datachannel not including headers or padding.
	BytesSent uint64 `json:"bytesSent"`

	// MessagesReceived represents the total number of API "message" events received.
	MessagesReceived uint32 `json:"messagesReceived"`

	// BytesReceived represents the total number of bytes received on this
	// datachannel not including headers or padding.
	BytesReceived uint64 `json:"bytesReceived"`
}

func (s DataChannelStats) statsMarker() {}

func unmarshalDataChannelStats(b []byte) (DataChannelStats, error) {
	var dataChannelStats DataChannelStats
	err := json.Unmarshal(b, &dataChannelStats)
	if err != nil {
		return DataChannelStats{}, fmt.Errorf("unmarshal data channel stats: %w", err)
	}

	return dataChannelStats, nil
}

// MediaStreamStats contains statistics related to a specific MediaStream.
type MediaStreamStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// StreamIdentifier is the "id" property of the MediaStream
	StreamIdentifier string `json:"streamIdentifier"`

	// TrackIDs is a list of the identifiers of the stats object representing the
	// stream's tracks, either ReceiverAudioTrackAttachmentStats or ReceiverVideoTrackAttachmentStats.
	TrackIDs []string `json:"trackIds"`
}

func (s MediaStreamStats) statsMarker() {}

func unmarshalStreamStats(b []byte) (MediaStreamStats, error) {
	var streamStats MediaStreamStats
	err := json.Unmarshal(b, &streamStats)
	if err != nil {
		return MediaStreamStats{}, fmt.Errorf("unmarshal stream stats: %w", err)
	}

	return streamStats, nil
}

// AudioSenderStats represents the stats about one audio sender of a PeerConnection
// object for which one calls GetStats.
//
// It appears in the stats as soon as the RTPSender is added by either AddTrack
// or AddTransceiver, or by media negotiation.
type AudioSenderStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// TrackIdentifier represents the id property of the track.
	TrackIdentifier string `json:"trackIdentifier"`

	// RemoteSource is true if the source is remote, for instance if it is sourced
	// from another host via a PeerConnection. False otherwise. Only applicable for 'track' stats.
	RemoteSource bool `json:"remoteSource"`

	// Ended reflects the "ended" state of the track.
	Ended bool `json:"ended"`

	// Kind is "audio"
	Kind string `json:"kind"`

	// AudioLevel represents the output audio level of the track.
	//
	// The value is a value between 0..1 (linear), where 1.0 represents 0 dBov,
	// 0 represents silence, and 0.5 represents approximately 6 dBSPL change in
	// the sound pressure level from 0 dBov.
	//
	// If the track is sourced from an Receiver, does no audio processing, has a
	// constant level, and has a volume setting of 1.0, the audio level is expected
	// to be the same as the audio level of the source SSRC, while if the volume setting
	// is 0.5, the AudioLevel is expected to be half that value.
	//
	// For outgoing audio tracks, the AudioLevel is the level of the audio being sent.
	AudioLevel float64 `json:"audioLevel"`

	// TotalAudioEnergy is the total energy of all the audio samples sent/received
	// for this object, calculated by duration * Math.pow(energy/maxEnergy, 2) for
	// each audio sample seen.
	TotalAudioEnergy float64 `json:"totalAudioEnergy"`

	// VoiceActivityFlag represents whether the last RTP packet sent or played out
	// by this track contained voice activity or not based on the presence of the
	// V bit in the extension header, as defined in [RFC6464].
	//
	// This value indicates the voice activity in the latest RTP packet played out
	// from a given SSRC, and is defined in RTPSynchronizationSource.voiceActivityFlag.
	VoiceActivityFlag bool `json:"voiceActivityFlag"`

	// TotalSamplesDuration represents the total duration in seconds of all samples
	// that have sent or received (and thus counted by TotalSamplesSent or TotalSamplesReceived).
	// Can be used with TotalAudioEnergy to compute an average audio level over different intervals.
	TotalSamplesDuration float64 `json:"totalSamplesDuration"`

	// EchoReturnLoss is only present while the sender is sending a track sourced from
	// a microphone where echo cancellation is applied. Calculated in decibels.
	EchoReturnLoss float64 `json:"echoReturnLoss"`

	// EchoReturnLossEnhancement is only present while the sender is sending a track
	// sourced from a microphone where echo cancellation is applied. Calculated in decibels.
	EchoReturnLossEnhancement float64 `json:"echoReturnLossEnhancement"`

	// TotalSamplesSent is the total number of samples that have been sent by this sender.
	TotalSamplesSent uint64 `json:"totalSamplesSent"`
}

func (s AudioSenderStats) statsMarker() {}

// SenderAudioTrackAttachmentStats object represents the stats about one attachment
// of an audio MediaStreamTrack to the PeerConnection object for which one calls GetStats.
//
// It appears in the stats as soon as it is attached (via AddTrack, via AddTransceiver,
// via ReplaceTrack on an RTPSender object).
//
// If an audio track is attached twice (via AddTransceiver or ReplaceTrack), there
// will be two SenderAudioTrackAttachmentStats objects, one for each attachment.
// They will have the same "TrackIdentifier" attribute, but different "ID" attributes.
//
// If the track is detached from the PeerConnection (via removeTrack or via replaceTrack),
// it continues to appear, but with the "ObjectDeleted" member set to true.
type SenderAudioTrackAttachmentStats AudioSenderStats

func (s SenderAudioTrackAttachmentStats) statsMarker() {}

// VideoSenderStats represents the stats about one video sender of a PeerConnection
// object for which one calls GetStats.
//
// It appears in the stats as soon as the sender is added by either AddTrack or
// AddTransceiver, or by media negotiation.
type VideoSenderStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// Kind is "video"
	Kind string `json:"kind"`

	// FramesCaptured represents the total number of frames captured, before encoding,
	// for this RTPSender (or for this MediaStreamTrack, if type is "track"). For example,
	// if type is "sender" and this sender's track represents a camera, then this is the
	// number of frames produced by the camera for this track while being sent by this sender,
	// combined with the number of frames produced by all tracks previously attached to this
	// sender while being sent by this sender. Framerates can vary due to hardware limitations
	// or environmental factors such as lighting conditions.
	FramesCaptured uint32 `json:"framesCaptured"`

	// FramesSent represents the total number of frames sent by this RTPSender
	// (or for this MediaStreamTrack, if type is "track").
	FramesSent uint32 `json:"framesSent"`

	// HugeFramesSent represents the total number of huge frames sent by this RTPSender
	// (or for this MediaStreamTrack, if type is "track"). Huge frames, by definition,
	// are frames that have an encoded size at least 2.5 times the average size of the frames.
	// The average size of the frames is defined as the target bitrate per second divided
	// by the target fps at the time the frame was encoded. These are usually complex
	// to encode frames with a lot of changes in the picture. This can be used to estimate,
	// e.g slide changes in the streamed presentation. If a huge frame is also a key frame,
	// then both counters HugeFramesSent and KeyFramesSent are incremented.
	HugeFramesSent uint32 `json:"hugeFramesSent"`

	// KeyFramesSent represents the total number of key frames sent by this RTPSender
	// (or for this MediaStreamTrack, if type is "track"), such as Infra-frames in
	// VP8 [RFC6386] or I-frames in H.264 [RFC6184]. This is a subset of FramesSent.
	// FramesSent - KeyFramesSent gives you the number of delta frames sent.
	KeyFramesSent uint32 `json:"keyFramesSent"`
}

func (s VideoSenderStats) statsMarker() {}

// SenderVideoTrackAttachmentStats represents the stats about one attachment of a
// video MediaStreamTrack to the PeerConnection object for which one calls GetStats.
//
// It appears in the stats as soon as it is attached (via AddTrack, via AddTransceiver,
// via ReplaceTrack on an RTPSender object).
//
// If a video track is attached twice (via AddTransceiver or ReplaceTrack), there
// will be two SenderVideoTrackAttachmentStats objects, one for each attachment.
// They will have the same "TrackIdentifier" attribute, but different "ID" attributes.
//
// If the track is detached from the PeerConnection (via RemoveTrack or via ReplaceTrack),
// it continues to appear, but with the "ObjectDeleted" member set to true.
type SenderVideoTrackAttachmentStats VideoSenderStats

func (s SenderVideoTrackAttachmentStats) statsMarker() {}

func unmarshalSenderStats(b []byte) (Stats, error) {
	type kindJSON struct {
		Kind string `json:"kind"`
	}
	kindHolder := kindJSON{}

	err := json.Unmarshal(b, &kindHolder)
	if err != nil {
		return nil, fmt.Errorf("unmarshal json kind: %w", err)
	}

	switch MediaKind(kindHolder.Kind) {
	case MediaKindAudio:
		var senderStats AudioSenderStats
		err := json.Unmarshal(b, &senderStats)
		if err != nil {
			return nil, fmt.Errorf("unmarshal audio sender stats: %w", err)
		}

		return senderStats, nil
	case MediaKindVideo:
		var senderStats VideoSenderStats
		err := json.Unmarshal(b, &senderStats)
		if err != nil {
			return nil, fmt.Errorf("unmarshal video sender stats: %w", err)
		}

		return senderStats, nil
	default:
		return nil, fmt.Errorf("kind: %w", ErrUnknownType)
	}
}

func unmarshalTrackStats(b []byte) (Stats, error) {
	type kindJSON struct {
		Kind string `json:"kind"`
	}
	kindHolder := kindJSON{}

	err := json.Unmarshal(b, &kindHolder)
	if err != nil {
		return nil, fmt.Errorf("unmarshal json kind: %w", err)
	}

	switch MediaKind(kindHolder.Kind) {
	case MediaKindAudio:
		var trackStats SenderAudioTrackAttachmentStats
		err := json.Unmarshal(b, &trackStats)
		if err != nil {
			return nil, fmt.Errorf("unmarshal audio track stats: %w", err)
		}

		return trackStats, nil
	case MediaKindVideo:
		var trackStats SenderVideoTrackAttachmentStats
		err := json.Unmarshal(b, &trackStats)
		if err != nil {
			return nil, fmt.Errorf("unmarshal video track stats: %w", err)
		}

		return trackStats, nil
	default:
		return nil, fmt.Errorf("kind: %w", ErrUnknownType)
	}
}

// AudioReceiverStats contains audio metrics related to a specific receiver.
type AudioReceiverStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// Kind is "audio"
	Kind string `json:"kind"`

	// AudioLevel represents the output audio level of the track.
	//
	// The value is a value between 0..1 (linear), where 1.0 represents 0 dBov,
	// 0 represents silence, and 0.5 represents approximately 6 dBSPL change in
	// the sound pressure level from 0 dBov.
	//
	// If the track is sourced from an Receiver, does no audio processing, has a
	// constant level, and has a volume setting of 1.0, the audio level is expected
	// to be the same as the audio level of the source SSRC, while if the volume setting
	// is 0.5, the AudioLevel is expected to be half that value.
	//
	// For outgoing audio tracks, the AudioLevel is the level of the audio being sent.
	AudioLevel float64 `json:"audioLevel"`

	// TotalAudioEnergy is the total energy of all the audio samples sent/received
	// for this object, calculated by duration * Math.pow(energy/maxEnergy, 2) for
	// each audio sample seen.
	TotalAudioEnergy float64 `json:"totalAudioEnergy"`

	// VoiceActivityFlag represents whether the last RTP packet sent or played out
	// by this track contained voice activity or not based on the presence of the
	// V bit in the extension header, as defined in [RFC6464].
	//
	// This value indicates the voice activity in the latest RTP packet played out
	// from a given SSRC, and is defined in RTPSynchronizationSource.voiceActivityFlag.
	VoiceActivityFlag bool `json:"voiceActivityFlag"`

	// TotalSamplesDuration represents the total duration in seconds of all samples
	// that have sent or received (and thus counted by TotalSamplesSent or TotalSamplesReceived).
	// Can be used with TotalAudioEnergy to compute an average audio level over different intervals.
	TotalSamplesDuration float64 `json:"totalSamplesDuration"`

	// EstimatedPlayoutTimestamp is the estimated playout time of this receiver's
	// track. The playout time is the NTP timestamp of the last playable sample that
	// has a known timestamp (from an RTCP SR packet mapping RTP timestamps to NTP
	// timestamps), extrapolated with the time elapsed since it was ready to be played out.
	// This is the "current time" of the track in NTP clock time of the sender and
	// can be present even if there is no audio currently playing.
	//
	// This can be useful for estimating how much audio and video is out of
	// sync for two tracks from the same source:
	// 		AudioTrackStats.EstimatedPlayoutTimestamp - VideoTrackStats.EstimatedPlayoutTimestamp
	EstimatedPlayoutTimestamp StatsTimestamp `json:"estimatedPlayoutTimestamp"`

	// JitterBufferDelay is the sum of the time, in seconds, each sample takes from
	// the time it is received and to the time it exits the jitter buffer.
	// This increases upon samples exiting, having completed their time in the buffer
	// (incrementing JitterBufferEmittedCount). The average jitter buffer delay can
	// be calculated by dividing the JitterBufferDelay with the JitterBufferEmittedCount.
	JitterBufferDelay float64 `json:"jitterBufferDelay"`

	// JitterBufferEmittedCount is the total number of samples that have come out
	// of the jitter buffer (increasing JitterBufferDelay).
	JitterBufferEmittedCount uint64 `json:"jitterBufferEmittedCount"`

	// TotalSamplesReceived is the total number of samples that have been received
	// by this receiver. This includes ConcealedSamples.
	TotalSamplesReceived uint64 `json:"totalSamplesReceived"`

	// ConcealedSamples is the total number of samples that are concealed samples.
	// A concealed sample is a sample that is based on data that was synthesized
	// to conceal packet loss and does not represent incoming data.
	ConcealedSamples uint64 `json:"concealedSamples"`

	// ConcealmentEvents is the number of concealment events. This counter increases
	// every time a concealed sample is synthesized after a non-concealed sample.
	// That is, multiple consecutive concealed samples will increase the concealedSamples
	// count multiple times but is a single concealment event.
	ConcealmentEvents uint64 `json:"concealmentEvents"`
}

func (s AudioReceiverStats) statsMarker() {}

// VideoReceiverStats contains video metrics related to a specific receiver.
type VideoReceiverStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// Kind is "video"
	Kind string `json:"kind"`

	// FrameWidth represents the width of the last processed frame for this track.
	// Before the first frame is processed this attribute is missing.
	FrameWidth uint32 `json:"frameWidth"`

	// FrameHeight represents the height of the last processed frame for this track.
	// Before the first frame is processed this attribute is missing.
	FrameHeight uint32 `json:"frameHeight"`

	// FramesPerSecond represents the nominal FPS value before the degradation preference
	// is applied. It is the number of complete frames in the last second. For sending
	// tracks it is the current captured FPS and for the receiving tracks it is the
	// current decoding framerate.
	FramesPerSecond float64 `json:"framesPerSecond"`

	// EstimatedPlayoutTimestamp is the estimated playout time of this receiver's
	// track. The playout time is the NTP timestamp of the last playable sample that
	// has a known timestamp (from an RTCP SR packet mapping RTP timestamps to NTP
	// timestamps), extrapolated with the time elapsed since it was ready to be played out.
	// This is the "current time" of the track in NTP clock time of the sender and
	// can be present even if there is no audio currently playing.
	//
	// This can be useful for estimating how much audio and video is out of
	// sync for two tracks from the same source:
	// 		AudioTrackStats.EstimatedPlayoutTimestamp - VideoTrackStats.EstimatedPlayoutTimestamp
	EstimatedPlayoutTimestamp StatsTimestamp `json:"estimatedPlayoutTimestamp"`

	// JitterBufferDelay is the sum of the time, in seconds, each sample takes from
	// the time it is received and to the time it exits the jitter buffer.
	// This increases upon samples exiting, having completed their time in the buffer
	// (incrementing JitterBufferEmittedCount). The average jitter buffer delay can
	// be calculated by dividing the JitterBufferDelay with the JitterBufferEmittedCount.
	JitterBufferDelay float64 `json:"jitterBufferDelay"`

	// JitterBufferEmittedCount is the total number of samples that have come out
	// of the jitter buffer (increasing JitterBufferDelay).
	JitterBufferEmittedCount uint64 `json:"jitterBufferEmittedCount"`

	// FramesReceived Represents the total number of complete frames received for
	// this receiver. This metric is incremented when the complete frame is received.
	FramesReceived uint32 `json:"framesReceived"`

	// KeyFramesReceived represents the total number of complete key frames received
	// for this MediaStreamTrack, such as Infra-frames in VP8 [RFC6386] or I-frames
	// in H.264 [RFC6184]. This is a subset of framesReceived. `framesReceived - keyFramesReceived`
	// gives you the number of delta frames received. This metric is incremented when
	// the complete key frame is received. It is not incremented if a partial key
	// frames is received and sent for decoding, i.e., the frame could not be recovered
	// via retransmission or FEC.
	KeyFramesReceived uint32 `json:"keyFramesReceived"`

	// FramesDecoded represents the total number of frames correctly decoded for this
	// SSRC, i.e., frames that would be displayed if no frames are dropped.
	FramesDecoded uint32 `json:"framesDecoded"`

	// FramesDropped is the total number of frames dropped predecode or dropped
	// because the frame missed its display deadline for this receiver's track.
	FramesDropped uint32 `json:"framesDropped"`

	// The cumulative number of partial frames lost. This metric is incremented when
	// the frame is sent to the decoder. If the partial frame is received and recovered
	// via retransmission or FEC before decoding, the FramesReceived counter is incremented.
	PartialFramesLost uint32 `json:"partialFramesLost"`

	// FullFramesLost is the cumulative number of full frames lost.
	FullFramesLost uint32 `json:"fullFramesLost"`
}

func (s VideoReceiverStats) statsMarker() {}

func unmarshalReceiverStats(b []byte) (Stats, error) {
	type kindJSON struct {
		Kind string `json:"kind"`
	}
	kindHolder := kindJSON{}

	err := json.Unmarshal(b, &kindHolder)
	if err != nil {
		return nil, fmt.Errorf("unmarshal json kind: %w", err)
	}

	switch MediaKind(kindHolder.Kind) {
	case MediaKindAudio:
		var receiverStats AudioReceiverStats
		err := json.Unmarshal(b, &receiverStats)
		if err != nil {
			return nil, fmt.Errorf("unmarshal audio receiver stats: %w", err)
		}

		return receiverStats, nil
	case MediaKindVideo:
		var receiverStats VideoReceiverStats
		err := json.Unmarshal(b, &receiverStats)
		if err != nil {
			return nil, fmt.Errorf("unmarshal video receiver stats: %w", err)
		}

		return receiverStats, nil
	default:
		return nil, fmt.Errorf("kind: %w", ErrUnknownType)
	}
}

// TransportStats contains transport statistics related to the PeerConnection object.
type TransportStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// PacketsSent represents the total number of packets sent over this transport.
	PacketsSent uint32 `json:"packetsSent"`

	// PacketsReceived represents the total number of packets received on this transport.
	PacketsReceived uint32 `json:"packetsReceived"`

	// BytesSent represents the total number of payload bytes sent on this PeerConnection
	// not including headers or padding.
	BytesSent uint64 `json:"bytesSent"`

	// BytesReceived represents the total number of bytes received on this PeerConnection
	// not including headers or padding.
	BytesReceived uint64 `json:"bytesReceived"`

	// RTCPTransportStatsID is the ID of the transport that gives stats for the RTCP
	// component If RTP and RTCP are not multiplexed and this record has only
	// the RTP component stats.
	RTCPTransportStatsID string `json:"rtcpTransportStatsId"`

	// ICERole is set to the current value of the "role" attribute of the underlying
	// DTLSTransport's "iceTransport".
	ICERole ICERole `json:"iceRole"`

	// DTLSState is set to the current value of the "state" attribute of the underlying DTLSTransport.
	DTLSState DTLSTransportState `json:"dtlsState"`

	// ICEState is set to the current value of the "state" attribute of the underlying
	// RTCIceTransport's "state".
	ICEState ICETransportState `json:"iceState"`

	// SelectedCandidatePairID is a unique identifier that is associated to the object
	// that was inspected to produce the ICECandidatePairStats associated with this transport.
	SelectedCandidatePairID string `json:"selectedCandidatePairId"`

	// LocalCertificateID is the ID of the CertificateStats for the local certificate.
	// Present only if DTLS is negotiated.
	LocalCertificateID string `json:"localCertificateId"`

	// LocalCertificateID is the ID of the CertificateStats for the remote certificate.
	// Present only if DTLS is negotiated.
	RemoteCertificateID string `json:"remoteCertificateId"`

	// DTLSCipher is the descriptive name of the cipher suite used for the DTLS transport,
	// as defined in the "Description" column of the IANA cipher suite registry.
	DTLSCipher string `json:"dtlsCipher"`

	// SRTPCipher is the descriptive name of the protection profile used for the SRTP
	// transport, as defined in the "Profile" column of the IANA DTLS-SRTP protection
	// profile registry.
	SRTPCipher string `json:"srtpCipher"`
}

func (s TransportStats) statsMarker() {}

func unmarshalTransportStats(b []byte) (TransportStats, error) {
	var transportStats TransportStats
	err := json.Unmarshal(b, &transportStats)
	if err != nil {
		return TransportStats{}, fmt.Errorf("unmarshal transport stats: %w", err)
	}

	return transportStats, nil
}

// StatsICECandidatePairState is the state of an ICE candidate pair used in the
// ICECandidatePairStats object.
type StatsICECandidatePairState string

func toStatsICECandidatePairState(state ice.CandidatePairState) (StatsICECandidatePairState, error) {
	switch state {
	case ice.CandidatePairStateWaiting:
		return StatsICECandidatePairStateWaiting, nil
	case ice.CandidatePairStateInProgress:
		return StatsICECandidatePairStateInProgress, nil
	case ice.CandidatePairStateFailed:
		return StatsICECandidatePairStateFailed, nil
	case ice.CandidatePairStateSucceeded:
		return StatsICECandidatePairStateSucceeded, nil
	default:
		// NOTE: this should never happen[tm]
		err := fmt.Errorf("%w: %s", errStatsICECandidateStateInvalid, state.String())

		return StatsICECandidatePairState("Unknown"), err
	}
}

func toICECandidatePairStats(candidatePairStats ice.CandidatePairStats) (ICECandidatePairStats, error) {
	state, err := toStatsICECandidatePairState(candidatePairStats.State)
	if err != nil {
		return ICECandidatePairStats{}, err
	}

	return ICECandidatePairStats{
		Timestamp: statsTimestampFrom(candidatePairStats.Timestamp),
		Type:      StatsTypeCandidatePair,
		ID:        newICECandidatePairStatsID(candidatePairStats.LocalCandidateID, candidatePairStats.RemoteCandidateID),
		// TransportID:
		LocalCandidateID:              candidatePairStats.LocalCandidateID,
		RemoteCandidateID:             candidatePairStats.RemoteCandidateID,
		State:                         state,
		Nominated:                     candidatePairStats.Nominated,
		PacketsSent:                   candidatePairStats.PacketsSent,
		PacketsReceived:               candidatePairStats.PacketsReceived,
		BytesSent:                     candidatePairStats.BytesSent,
		BytesReceived:                 candidatePairStats.BytesReceived,
		LastPacketSentTimestamp:       statsTimestampFrom(candidatePairStats.LastPacketSentTimestamp),
		LastPacketReceivedTimestamp:   statsTimestampFrom(candidatePairStats.LastPacketReceivedTimestamp),
		FirstRequestTimestamp:         statsTimestampFrom(candidatePairStats.FirstRequestTimestamp),
		LastRequestTimestamp:          statsTimestampFrom(candidatePairStats.LastRequestTimestamp),
		FirstResponseTimestamp:        statsTimestampFrom(candidatePairStats.FirstResponseTimestamp),
		LastResponseTimestamp:         statsTimestampFrom(candidatePairStats.LastResponseTimestamp),
		FirstRequestReceivedTimestamp: statsTimestampFrom(candidatePairStats.FirstRequestReceivedTimestamp),
		LastRequestReceivedTimestamp:  statsTimestampFrom(candidatePairStats.LastRequestReceivedTimestamp),
		TotalRoundTripTime:            candidatePairStats.TotalRoundTripTime,
		CurrentRoundTripTime:          candidatePairStats.CurrentRoundTripTime,
		AvailableOutgoingBitrate:      candidatePairStats.AvailableOutgoingBitrate,
		AvailableIncomingBitrate:      candidatePairStats.AvailableIncomingBitrate,
		CircuitBreakerTriggerCount:    candidatePairStats.CircuitBreakerTriggerCount,
		RequestsReceived:              candidatePairStats.RequestsReceived,
		RequestsSent:                  candidatePairStats.RequestsSent,
		ResponsesReceived:             candidatePairStats.ResponsesReceived,
		ResponsesSent:                 candidatePairStats.ResponsesSent,
		RetransmissionsReceived:       candidatePairStats.RetransmissionsReceived,
		RetransmissionsSent:           candidatePairStats.RetransmissionsSent,
		ConsentRequestsSent:           candidatePairStats.ConsentRequestsSent,
		ConsentExpiredTimestamp:       statsTimestampFrom(candidatePairStats.ConsentExpiredTimestamp),
	}, nil
}

const (
	// StatsICECandidatePairStateFrozen means a check for this pair hasn't been
	// performed, and it can't yet be performed until some other check succeeds,
	// allowing this pair to unfreeze and move into the Waiting state.
	StatsICECandidatePairStateFrozen StatsICECandidatePairState = "frozen"

	// StatsICECandidatePairStateWaiting means a check has not been performed for
	// this pair, and can be performed as soon as it is the highest-priority Waiting
	// pair on the check list.
	StatsICECandidatePairStateWaiting StatsICECandidatePairState = "waiting"

	// StatsICECandidatePairStateInProgress means a check has been sent for this pair,
	// but the transaction is in progress.
	StatsICECandidatePairStateInProgress StatsICECandidatePairState = "in-progress"

	// StatsICECandidatePairStateFailed means a check for this pair was already done
	// and failed, either never producing any response or producing an unrecoverable
	// failure response.
	StatsICECandidatePairStateFailed StatsICECandidatePairState = "failed"

	// StatsICECandidatePairStateSucceeded means a check for this pair was already
	// done and produced a successful result.
	StatsICECandidatePairStateSucceeded StatsICECandidatePairState = "succeeded"
)

// ICECandidatePairStats contains ICE candidate pair statistics related
// to the ICETransport objects.
type ICECandidatePairStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// TransportID is a unique identifier that is associated to the object that
	// was inspected to produce the TransportStats associated with this candidate pair.
	TransportID string `json:"transportId"`

	// LocalCandidateID is a unique identifier that is associated to the object
	// that was inspected to produce the ICECandidateStats for the local candidate
	// associated with this candidate pair.
	LocalCandidateID string `json:"localCandidateId"`

	// RemoteCandidateID is a unique identifier that is associated to the object
	// that was inspected to produce the ICECandidateStats for the remote candidate
	// associated with this candidate pair.
	RemoteCandidateID string `json:"remoteCandidateId"`

	// State represents the state of the checklist for the local and remote
	// candidates in a pair.
	State StatsICECandidatePairState `json:"state"`

	// Nominated is true when this valid pair that should be used for media
	// if it is the highest-priority one amongst those whose nominated flag is set
	Nominated bool `json:"nominated"`

	// PacketsSent represents the total number of packets sent on this candidate pair.
	PacketsSent uint32 `json:"packetsSent"`

	// PacketsReceived represents the total number of packets received on this candidate pair.
	PacketsReceived uint32 `json:"packetsReceived"`

	// BytesSent represents the total number of payload bytes sent on this candidate pair
	// not including headers or padding.
	BytesSent uint64 `json:"bytesSent"`

	// BytesReceived represents the total number of payload bytes received on this candidate pair
	// not including headers or padding.
	BytesReceived uint64 `json:"bytesReceived"`

	// LastPacketSentTimestamp represents the timestamp at which the last packet was
	// sent on this particular candidate pair, excluding STUN packets.
	LastPacketSentTimestamp StatsTimestamp `json:"lastPacketSentTimestamp"`

	// LastPacketReceivedTimestamp represents the timestamp at which the last packet
	// was received on this particular candidate pair, excluding STUN packets.
	LastPacketReceivedTimestamp StatsTimestamp `json:"lastPacketReceivedTimestamp"`

	// FirstRequestTimestamp represents the timestamp at which the first STUN request
	// was sent on this particular candidate pair.
	FirstRequestTimestamp StatsTimestamp `json:"firstRequestTimestamp"`

	// LastRequestTimestamp represents the timestamp at which the last STUN request
	// was sent on this particular candidate pair. The average interval between two
	// consecutive connectivity checks sent can be calculated with
	// (LastRequestTimestamp - FirstRequestTimestamp) / RequestsSent.
	LastRequestTimestamp StatsTimestamp `json:"lastRequestTimestamp"`

	// FirstResponseTimestamp represents the timestamp at which the first STUN response
	// was received on this particular candidate pair.
	FirstResponseTimestamp StatsTimestamp `json:"firstResponseTimestamp"`

	// LastResponseTimestamp represents the timestamp at which the last STUN response
	// was received on this particular candidate pair.
	LastResponseTimestamp StatsTimestamp `json:"lastResponseTimestamp"`

	// FirstRequestReceivedTimestamp represents the timestamp at which the first
	// connectivity check request was received.
	FirstRequestReceivedTimestamp StatsTimestamp `json:"firstRequestReceivedTimestamp"`

	// LastRequestReceivedTimestamp represents the timestamp at which the last
	// connectivity check request was received.
	LastRequestReceivedTimestamp StatsTimestamp `json:"lastRequestReceivedTimestamp"`

	// TotalRoundTripTime represents the sum of all round trip time measurements
	// in seconds since the beginning of the session, based on STUN connectivity
	// check responses (ResponsesReceived), including those that reply to requests
	// that are sent in order to verify consent. The average round trip time can
	// be computed from TotalRoundTripTime by dividing it by ResponsesReceived.
	TotalRoundTripTime float64 `json:"totalRoundTripTime"`

	// CurrentRoundTripTime represents the latest round trip time measured in seconds,
	// computed from both STUN connectivity checks, including those that are sent
	// for consent verification.
	CurrentRoundTripTime float64 `json:"currentRoundTripTime"`

	// AvailableOutgoingBitrate is calculated by the underlying congestion control
	// by combining the available bitrate for all the outgoing RTP streams using
	// this candidate pair. The bitrate measurement does not count the size of the
	// IP or other transport layers like TCP or UDP. It is similar to the TIAS defined
	// in RFC 3890, i.e., it is measured in bits per second and the bitrate is calculated
	// over a 1 second window.
	AvailableOutgoingBitrate float64 `json:"availableOutgoingBitrate"`

	// AvailableIncomingBitrate is calculated by the underlying congestion control
	// by combining the available bitrate for all the incoming RTP streams using
	// this candidate pair. The bitrate measurement does not count the size of the
	// IP or other transport layers like TCP or UDP. It is similar to the TIAS defined
	// in  RFC 3890, i.e., it is measured in bits per second and the bitrate is
	// calculated over a 1 second window.
	AvailableIncomingBitrate float64 `json:"availableIncomingBitrate"`

	// CircuitBreakerTriggerCount represents the number of times the circuit breaker
	// is triggered for this particular 5-tuple, ceasing transmission.
	CircuitBreakerTriggerCount uint32 `json:"circuitBreakerTriggerCount"`

	// RequestsReceived represents the total number of connectivity check requests
	// received (including retransmissions). It is impossible for the receiver to
	// tell whether the request was sent in order to check connectivity or check
	// consent, so all connectivity checks requests are counted here.
	RequestsReceived uint64 `json:"requestsReceived"`

	// RequestsSent represents the total number of connectivity check requests
	// sent (not including retransmissions).
	RequestsSent uint64 `json:"requestsSent"`

	// ResponsesReceived represents the total number of connectivity check responses received.
	ResponsesReceived uint64 `json:"responsesReceived"`

	// ResponsesSent represents the total number of connectivity check responses sent.
	// Since we cannot distinguish connectivity check requests and consent requests,
	// all responses are counted.
	ResponsesSent uint64 `json:"responsesSent"`

	// RetransmissionsReceived represents the total number of connectivity check
	// request retransmissions received.
	RetransmissionsReceived uint64 `json:"retransmissionsReceived"`

	// RetransmissionsSent represents the total number of connectivity check
	// request retransmissions sent.
	RetransmissionsSent uint64 `json:"retransmissionsSent"`

	// ConsentRequestsSent represents the total number of consent requests sent.
	ConsentRequestsSent uint64 `json:"consentRequestsSent"`

	// ConsentExpiredTimestamp represents the timestamp at which the latest valid
	// STUN binding response expired.
	ConsentExpiredTimestamp StatsTimestamp `json:"consentExpiredTimestamp"`

	// PacketsDiscardedOnSend retpresents the total number of packets for this candidate pair
	// that have been discarded due to socket errors, i.e. a socket error occurred
	// when handing the packets to the socket. This might happen due to various reasons,
	// including full buffer or no available memory.
	PacketsDiscardedOnSend uint32 `json:"packetsDiscardedOnSend"`

	// BytesDiscardedOnSend represents the total number of bytes for this candidate pair
	// that have been discarded due to socket errors, i.e. a socket error occurred
	// when handing the packets containing the bytes to the socket. This might happen due
	// to various reasons, including full buffer or no available memory.
	// Calculated as defined in [RFC3550] section 6.4.1.
	BytesDiscardedOnSend uint32 `json:"bytesDiscardedOnSend"`
}

func (s ICECandidatePairStats) statsMarker() {}

func unmarshalICECandidatePairStats(b []byte) (ICECandidatePairStats, error) {
	var iceCandidatePairStats ICECandidatePairStats
	err := json.Unmarshal(b, &iceCandidatePairStats)
	if err != nil {
		return ICECandidatePairStats{}, fmt.Errorf("unmarshal ice candidate pair stats: %w", err)
	}

	return iceCandidatePairStats, nil
}

// ICECandidateStats contains ICE candidate statistics related to the ICETransport objects.
type ICECandidateStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// TransportID is a unique identifier that is associated to the object that
	// was inspected to produce the TransportStats associated with this candidate.
	TransportID string `json:"transportId"`

	// NetworkType represents the type of network interface used by the base of a
	// local candidate (the address the ICE agent sends from). Only present for
	// local candidates; it's not possible to know what type of network interface
	// a remote candidate is using.
	//
	// Note:
	// This stat only tells you about the network interface used by the first "hop";
	// it's possible that a connection will be bottlenecked by another type of network.
	// For example, when using Wi-Fi tethering, the networkType of the relevant candidate
	// would be "wifi", even when the next hop is over a cellular connection.
	//
	// DEPRECATED. Although it may still work in some browsers, the networkType property was deprecated for
	// preserving privacy.
	NetworkType string `json:"networkType,omitempty"`

	// IP is the IP address of the candidate, allowing for IPv4 addresses and
	// IPv6 addresses, but fully qualified domain names (FQDNs) are not allowed.
	IP string `json:"ip"`

	// Port is the port number of the candidate.
	Port int32 `json:"port"`

	// Protocol is one of udp and tcp.
	Protocol string `json:"protocol"`

	// CandidateType is the "Type" field of the ICECandidate.
	CandidateType ICECandidateType `json:"candidateType"`

	// Priority is the "Priority" field of the ICECandidate.
	Priority int32 `json:"priority"`

	// URL is the URL of the TURN or STUN server indicated in the that translated
	// this IP address. It is the URL address surfaced in an PeerConnectionICEEvent.
	URL string `json:"url"`

	// RelayProtocol is the protocol used by the endpoint to communicate with the
	// TURN server. This is only present for local candidates. Valid values for
	// the TURN URL protocol is one of udp, tcp, or tls.
	RelayProtocol string `json:"relayProtocol"`

	// Deleted is true if the candidate has been deleted/freed. For host candidates,
	// this means that any network resources (typically a socket) associated with the
	// candidate have been released. For TURN candidates, this means the TURN allocation
	// is no longer active.
	//
	// Only defined for local candidates. For remote candidates, this property is not applicable.
	Deleted bool `json:"deleted"`
}

func (s ICECandidateStats) statsMarker() {}

func unmarshalICECandidateStats(b []byte) (ICECandidateStats, error) {
	var iceCandidateStats ICECandidateStats
	err := json.Unmarshal(b, &iceCandidateStats)
	if err != nil {
		return ICECandidateStats{}, fmt.Errorf("unmarshal ice candidate stats: %w", err)
	}

	return iceCandidateStats, nil
}

// CertificateStats contains information about a certificate used by an ICETransport.
type CertificateStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// Fingerprint is the fingerprint of the certificate.
	Fingerprint string `json:"fingerprint"`

	// FingerprintAlgorithm is the hash function used to compute the certificate fingerprint. For instance, "sha-256".
	FingerprintAlgorithm string `json:"fingerprintAlgorithm"`

	// Base64Certificate is the DER-encoded base-64 representation of the certificate.
	Base64Certificate string `json:"base64Certificate"`

	// IssuerCertificateID refers to the stats object that contains the next certificate
	// in the certificate chain. If the current certificate is at the end of the chain
	// (i.e. a self-signed certificate), this will not be set.
	IssuerCertificateID string `json:"issuerCertificateId"`
}

func (s CertificateStats) statsMarker() {}

func unmarshalCertificateStats(b []byte) (CertificateStats, error) {
	var certificateStats CertificateStats
	err := json.Unmarshal(b, &certificateStats)
	if err != nil {
		return CertificateStats{}, fmt.Errorf("unmarshal certificate stats: %w", err)
	}

	return certificateStats, nil
}

// SCTPTransportStats contains information about a certificate used by an SCTPTransport.
type SCTPTransportStats struct {
	// Timestamp is the timestamp associated with this object.
	Timestamp StatsTimestamp `json:"timestamp"`

	// Type is the object's StatsType
	Type StatsType `json:"type"`

	// ID is a unique id that is associated with the component inspected to produce
	// this Stats object. Two Stats objects will have the same ID if they were produced
	// by inspecting the same underlying object.
	ID string `json:"id"`

	// TransportID is the identifier of the object that was inspected to produce the
	// RTCTransportStats for the DTLSTransport and ICETransport supporting the SCTP transport.
	TransportID string `json:"transportId"`

	// SmoothedRoundTripTime is the latest smoothed round-trip time value,
	// corresponding to spinfo_srtt defined in [RFC6458] but converted to seconds.
	// If there has been no round-trip time measurements yet, this value is undefined.
	SmoothedRoundTripTime float64 `json:"smoothedRoundTripTime"`

	// CongestionWindow is the latest congestion window, corresponding to spinfo_cwnd defined in [RFC6458].
	CongestionWindow uint32 `json:"congestionWindow"`

	// ReceiverWindow is the latest receiver window, corresponding to sstat_rwnd defined in [RFC6458].
	ReceiverWindow uint32 `json:"receiverWindow"`

	// MTU is the latest maximum transmission unit, corresponding to spinfo_mtu defined in [RFC6458].
	MTU uint32 `json:"mtu"`

	// UNACKData is the number of unacknowledged DATA chunks, corresponding to sstat_unackdata defined in [RFC6458].
	UNACKData uint32 `json:"unackData"`

	// BytesSent represents the total number of bytes sent on this SCTPTransport
	BytesSent uint64 `json:"bytesSent"`

	// BytesReceived represents the total number of bytes received on this SCTPTransport
	BytesReceived uint64 `json:"bytesReceived"`
}

func (s SCTPTransportStats) statsMarker() {}

func unmarshalSCTPTransportStats(b []byte) (SCTPTransportStats, error) {
	var sctpTransportStats SCTPTransportStats
	if err := json.Unmarshal(b, &sctpTransportStats); err != nil {
		return SCTPTransportStats{}, fmt.Errorf("unmarshal sctp transport stats: %w", err)
	}

	return sctpTransportStats, nil
}
