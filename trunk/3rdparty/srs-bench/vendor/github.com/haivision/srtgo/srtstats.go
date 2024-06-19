package srtgo

// #cgo LDFLAGS: -lsrt
// #include <srt/srt.h>
import "C"

type SrtStats struct {
	// Global measurements
	MsTimeStamp        int64 // time since the UDT entity is started, in milliseconds
	PktSentTotal       int64 // total number of sent data packets, including retransmissions
	PktRecvTotal       int64 // total number of received packets
	PktSndLossTotal    int   // total number of lost packets (sender side)
	PktRcvLossTotal    int   // total number of lost packets (receiver side)
	PktRetransTotal    int   // total number of retransmitted packets
	PktSentACKTotal    int   // total number of sent ACK packets
	PktRecvACKTotal    int   // total number of received ACK packets
	PktSentNAKTotal    int   // total number of sent NAK packets
	PktRecvNAKTotal    int   // total number of received NAK packets
	UsSndDurationTotal int64 // total time duration when UDT is sending data (idle time exclusive)

	PktSndDropTotal      int   // number of too-late-to-send dropped packets
	PktRcvDropTotal      int   // number of too-late-to play missing packets
	PktRcvUndecryptTotal int   // number of undecrypted packets
	ByteSentTotal        int64 // total number of sent data bytes, including retransmissions
	ByteRecvTotal        int64 // total number of received bytes
	ByteRcvLossTotal     int64 // total number of lost bytes

	ByteRetransTotal      int64 // total number of retransmitted bytes
	ByteSndDropTotal      int64 // number of too-late-to-send dropped bytes
	ByteRcvDropTotal      int64 // number of too-late-to play missing bytes (estimate based on average packet size)
	ByteRcvUndecryptTotal int64 // number of undecrypted bytes

	// Local measurements
	PktSent              int64   // number of sent data packets, including retransmissions
	PktRecv              int64   // number of received packets
	PktSndLoss           int     // number of lost packets (sender side)
	PktRcvLoss           int     // number of lost packets (receiver side)
	PktRetrans           int     // number of retransmitted packets
	PktRcvRetrans        int     // number of retransmitted packets received
	PktSentACK           int     // number of sent ACK packets
	PktRecvACK           int     // number of received ACK packets
	PktSentNAK           int     // number of sent NAK packets
	PktRecvNAK           int     // number of received NAK packets
	MbpsSendRate         float64 // sending rate in Mb/s
	MbpsRecvRate         float64 // receiving rate in Mb/s
	UsSndDuration        int64   // busy sending time (i.e., idle time exclusive)
	PktReorderDistance   int     // size of order discrepancy in received sequences
	PktRcvAvgBelatedTime float64 // average time of packet delay for belated packets (packets with sequence past the ACK)
	PktRcvBelated        int64   // number of received AND IGNORED packets due to having come too late

	PktSndDrop      int   // number of too-late-to-send dropped packets
	PktRcvDrop      int   // number of too-late-to play missing packets
	PktRcvUndecrypt int   // number of undecrypted packets
	ByteSent        int64 // number of sent data bytes, including retransmissions
	ByteRecv        int64 // number of received bytes

	ByteRcvLoss      int64 // number of retransmitted Bytes
	ByteRetrans      int64 // number of retransmitted Bytes
	ByteSndDrop      int64 // number of too-late-to-send dropped Bytes
	ByteRcvDrop      int64 // number of too-late-to play missing Bytes (estimate based on average packet size)
	ByteRcvUndecrypt int64 // number of undecrypted bytes

	// Instant measurements
	UsPktSndPeriod      float64 // packet sending period, in microseconds
	PktFlowWindow       int     // flow window size, in number of packets
	PktCongestionWindow int     // congestion window size, in number of packets
	PktFlightSize       int     // number of packets on flight
	MsRTT               float64 // RTT, in milliseconds
	MbpsBandwidth       float64 // estimated bandwidth, in Mb/s
	ByteAvailSndBuf     int     // available UDT sender buffer size
	ByteAvailRcvBuf     int     // available UDT receiver buffer size

	MbpsMaxBW float64 // Transmit Bandwidth ceiling (Mbps)
	ByteMSS   int     // MTU

	PktSndBuf       int // UnACKed packets in UDT sender
	ByteSndBuf      int // UnACKed bytes in UDT sender
	MsSndBuf        int // UnACKed timespan (msec) of UDT sender
	MsSndTsbPdDelay int // Timestamp-based Packet Delivery Delay

	PktRcvBuf       int // Undelivered packets in UDT receiver
	ByteRcvBuf      int // Undelivered bytes of UDT receiver
	MsRcvBuf        int // Undelivered timespan (msec) of UDT receiver
	MsRcvTsbPdDelay int // Timestamp-based Packet Delivery Delay

	PktSndFilterExtraTotal  int // number of control packets supplied by packet filter
	PktRcvFilterExtraTotal  int // number of control packets received and not supplied back
	PktRcvFilterSupplyTotal int // number of packets that the filter supplied extra (e.g. FEC rebuilt)
	PktRcvFilterLossTotal   int // number of packet loss not coverable by filter

	PktSndFilterExtra   int // number of control packets supplied by packet filter
	PktRcvFilterExtra   int // number of control packets received and not supplied back
	PktRcvFilterSupply  int // number of packets that the filter supplied extra (e.g. FEC rebuilt)
	PktRcvFilterLoss    int // number of packet loss not coverable by filter
	PktReorderTolerance int // packet reorder tolerance value
}

func newSrtStats(stats *C.SRT_TRACEBSTATS) *SrtStats {
	s := new(SrtStats)

	s.MsTimeStamp = int64(stats.msTimeStamp)
	s.PktSentTotal = int64(stats.pktSentTotal)
	s.PktRecvTotal = int64(stats.pktRecvTotal)
	s.PktSndLossTotal = int(stats.pktSndLossTotal)
	s.PktRcvLossTotal = int(stats.pktRcvLossTotal)
	s.PktRetransTotal = int(stats.pktRetransTotal)
	s.PktSentACKTotal = int(stats.pktSentACKTotal)
	s.PktRecvACKTotal = int(stats.pktRecvACKTotal)
	s.PktSentNAKTotal = int(stats.pktSentNAKTotal)
	s.PktRecvNAKTotal = int(stats.pktRecvNAKTotal)
	s.UsSndDurationTotal = int64(stats.usSndDurationTotal)

	s.PktSndDropTotal = int(stats.pktSndDropTotal)
	s.PktRcvDropTotal = int(stats.pktRcvDropTotal)
	s.PktRcvUndecryptTotal = int(stats.pktRcvUndecryptTotal)
	s.ByteSentTotal = int64(stats.byteSentTotal)
	s.ByteRecvTotal = int64(stats.byteRecvTotal)
	s.ByteRcvLossTotal = int64(stats.byteRcvLossTotal)

	s.ByteRetransTotal = int64(stats.byteRetransTotal)
	s.ByteSndDropTotal = int64(stats.byteSndDropTotal)
	s.ByteRcvDropTotal = int64(stats.byteRcvDropTotal)
	s.ByteRcvUndecryptTotal = int64(stats.byteRcvUndecryptTotal)

	s.PktSent = int64(stats.pktSent)
	s.PktRecv = int64(stats.pktRecv)
	s.PktSndLoss = int(stats.pktSndLoss)
	s.PktRcvLoss = int(stats.pktRcvLoss)
	s.PktRetrans = int(stats.pktRetrans)
	s.PktRcvRetrans = int(stats.pktRcvRetrans)
	s.PktSentACK = int(stats.pktSentACK)
	s.PktRecvACK = int(stats.pktRecvACK)
	s.PktSentNAK = int(stats.pktSentNAK)
	s.PktRecvNAK = int(stats.pktRecvNAK)
	s.MbpsSendRate = float64(stats.mbpsSendRate)
	s.MbpsRecvRate = float64(stats.mbpsRecvRate)
	s.UsSndDuration = int64(stats.usSndDuration)
	s.PktReorderDistance = int(stats.pktReorderDistance)
	s.PktRcvAvgBelatedTime = float64(stats.pktRcvAvgBelatedTime)
	s.PktRcvBelated = int64(stats.pktRcvBelated)

	s.PktSndDrop = int(stats.pktSndDrop)
	s.PktRcvDrop = int(stats.pktRcvDrop)
	s.PktRcvUndecrypt = int(stats.pktRcvUndecrypt)
	s.ByteSent = int64(stats.byteSent)
	s.ByteRecv = int64(stats.byteRecv)

	s.ByteRcvLoss = int64(stats.byteRcvLoss)
	s.ByteRetrans = int64(stats.byteRetrans)
	s.ByteSndDrop = int64(stats.byteSndDrop)
	s.ByteRcvDrop = int64(stats.byteRcvDrop)
	s.ByteRcvUndecrypt = int64(stats.byteRcvUndecrypt)

	s.UsPktSndPeriod = float64(stats.usPktSndPeriod)
	s.PktFlowWindow = int(stats.pktFlowWindow)
	s.PktCongestionWindow = int(stats.pktCongestionWindow)
	s.PktFlightSize = int(stats.pktFlightSize)
	s.MsRTT = float64(stats.msRTT)
	s.MbpsBandwidth = float64(stats.mbpsBandwidth)
	s.ByteAvailSndBuf = int(stats.byteAvailSndBuf)
	s.ByteAvailRcvBuf = int(stats.byteAvailRcvBuf)

	s.MbpsMaxBW = float64(stats.mbpsMaxBW)
	s.ByteMSS = int(stats.byteMSS)

	s.PktSndBuf = int(stats.pktSndBuf)
	s.ByteSndBuf = int(stats.byteSndBuf)
	s.MsSndBuf = int(stats.msSndBuf)
	s.MsSndTsbPdDelay = int(stats.msSndTsbPdDelay)

	s.PktRcvBuf = int(stats.pktRcvBuf)
	s.ByteRcvBuf = int(stats.byteRcvBuf)
	s.MsRcvBuf = int(stats.msRcvBuf)
	s.MsRcvTsbPdDelay = int(stats.msRcvTsbPdDelay)

	s.PktSndFilterExtraTotal = int(stats.pktSndFilterExtraTotal)
	s.PktRcvFilterExtraTotal = int(stats.pktRcvFilterExtraTotal)
	s.PktRcvFilterSupplyTotal = int(stats.pktRcvFilterSupplyTotal)
	s.PktRcvFilterLossTotal = int(stats.pktRcvFilterLossTotal)

	s.PktSndFilterExtra = int(stats.pktSndFilterExtra)
	s.PktRcvFilterExtra = int(stats.pktRcvFilterExtra)
	s.PktRcvFilterSupply = int(stats.pktRcvFilterSupply)
	s.PktRcvFilterLoss = int(stats.pktRcvFilterLoss)
	s.PktReorderTolerance = int(stats.pktReorderTolerance)

	return s
}
