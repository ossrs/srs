package webrtc

import (
	"fmt"
	"sync"
	"time"

	"github.com/pion/ice/v2"
)

// A Stats object contains a set of statistics copies out of a monitored component
// of the WebRTC stack at a specific time.
type Stats interface{}

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

	// StatsTypePeerConnection used by PeerConnectionStats.
	StatsTypePeerConnection StatsType = "peer-connection"

	// StatsTypeDataChannel is used by DataChannelStats.
	StatsTypeDataChannel StatsType = "data-channel"

	// StatsTypeStream is used by MediaStreamStats.
	StatsTypeStream StatsType = "stream"

	// StatsTypeTrack is used by SenderVideoTrackAttachmentStats and SenderAudioTrackAttachmentStats.
	StatsTypeTrack StatsType = "track"

	// StatsTypeSender is used by by the AudioSenderStats or VideoSenderStats depending on kind.
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
// that is being encoded or decoded
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

// InboundRTPStreamStats contains statistics for an inbound RTP stream that is
// currently received with this PeerConnection object.
type InboundRTPStreamStats struct {
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

	// LastPacketReceivedTimestamp represents the timestamp at which the last packet was
	// received for this SSRC. This differs from Timestamp, which represents the time
	// at which the statistics were generated by the local endpoint.
	LastPacketReceivedTimestamp StatsTimestamp `json:"lastPacketReceivedTimestamp"`

	// AverageRTCPInterval is the average RTCP interval between two consecutive compound RTCP packets.
	// This is calculated by the sending endpoint when sending compound RTCP reports.
	// Compound packets must contain at least a RTCP RR or SR packet and an SDES packet
	// with the CNAME item.
	AverageRTCPInterval float64 `json:"averageRtcpInterval"`

	// FECPacketsReceived is the total number of RTP FEC packets received for this SSRC.
	// This counter can also be incremented when receiving FEC packets in-band with media packets (e.g., with Opus).
	FECPacketsReceived uint32 `json:"fecPacketsReceived"`

	// BytesReceived is the total number of bytes received for this SSRC.
	BytesReceived uint64 `json:"bytesReceived"`

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
}

// QualityLimitationReason lists the reason for limiting the resolution and/or framerate.
// Only valid for video.
type QualityLimitationReason string

const (
	// QualityLimitationReasonNone means the resolution and/or framerate is not limited.
	QualityLimitationReasonNone QualityLimitationReason = "none"

	// QualityLimitationReasonCPU means the resolution and/or framerate is primarily limited due to CPU load.
	QualityLimitationReasonCPU QualityLimitationReason = "cpu"

	// QualityLimitationReasonBandwidth means the resolution and/or framerate is primarily limited due to congestion cues during bandwidth estimation. Typical, congestion control algorithms use inter-arrival time, round-trip time, packet or other congestion cues to perform bandwidth estimation.
	QualityLimitationReasonBandwidth QualityLimitationReason = "bandwidth"

	// QualityLimitationReasonOther means the resolution and/or framerate is primarily limited for a reason other than the above.
	QualityLimitationReasonOther QualityLimitationReason = "other"
)

// OutboundRTPStreamStats contains statistics for an outbound RTP stream that is
// currently sent with this PeerConnection object.
type OutboundRTPStreamStats struct {
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

	// FramesEncoded represents the total number of frames successfully encoded for this RTP media stream.
	// Only valid for video.
	FramesEncoded uint32 `json:"framesEncoded"`

	// TotalEncodeTime is the total number of seconds that has been spent encoding the
	// framesEncoded frames of this stream. The average encode time can be calculated by
	// dividing this value with FramesEncoded. The time it takes to encode one frame is the
	// time passed between feeding the encoder a frame and the encoder returning encoded data
	// for that frame. This does not include any additional time it may take to packetize the resulting data.
	TotalEncodeTime float64 `json:"totalEncodeTime"`

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

	// PerDSCPPacketsSent is the total number of packets sent for this SSRC, per DSCP.
	// DSCPs are identified as decimal integers in string form.
	PerDSCPPacketsSent map[string]uint32 `json:"perDscpPacketsSent"`
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

	// FractionLost is the the fraction packet loss reported for this SSRC.
	FractionLost float64 `json:"fractionLost"`
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

	// Kind is either "audio" or "video". This reflects the "kind" attribute of the MediaStreamTrack.
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
	// DTLSTransport's "transport".
	ICERole ICERole `json:"iceRole"`

	// DTLSState is set to the current value of the "state" attribute of the underlying DTLSTransport.
	DTLSState DTLSTransportState `json:"dtlsState"`

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

	// LastResponseTimestamp represents the timestamp at which the last STUN response
	// was received on this particular candidate pair.
	LastResponseTimestamp StatsTimestamp `json:"lastResponseTimestamp"`

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
	NetworkType NetworkType `json:"networkType"`

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
