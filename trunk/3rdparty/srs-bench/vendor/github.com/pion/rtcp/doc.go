/*
Package rtcp implements encoding and decoding of RTCP packets according to RFCs 3550 and 5506.

RTCP is a sister protocol of the Real-time Transport Protocol (RTP). Its basic functionality
and packet structure is defined in RFC 3550. RTCP provides out-of-band statistics and control
information for an RTP session. It partners with RTP in the delivery and packaging of multimedia data,
but does not transport any media data itself.

The primary function of RTCP is to provide feedback on the quality of service (QoS)
in media distribution by periodically sending statistics information such as transmitted octet
and packet counts, packet loss, packet delay variation, and round-trip delay time to participants
in a streaming multimedia session. An application may use this information to control quality of
service parameters, perhaps by limiting flow, or using a different codec.

Decoding RTCP packets:

	pkts, err := rtcp.Unmarshal(rtcpData)
	// ...
	for _, pkt := range pkts {
		switch p := pkt.(type) {
		case *rtcp.CompoundPacket:
			...
		case *rtcp.PictureLossIndication:
			...
		default:
			...
		}
	}

Encoding RTCP packets:

	pkt := &rtcp.PictureLossIndication{
		SenderSSRC: senderSSRC,
		MediaSSRC: mediaSSRC
	}
	pliData, err := pkt.Marshal()
	// ...

*/
package rtcp
