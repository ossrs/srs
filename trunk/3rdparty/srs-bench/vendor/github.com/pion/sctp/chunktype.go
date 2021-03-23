package sctp

import "fmt"

// chunkType is an enum for SCTP Chunk Type field
// This field identifies the type of information contained in the
// Chunk Value field.
type chunkType uint8

// List of known chunkType enums
const (
	ctPayloadData      chunkType = 0
	ctInit             chunkType = 1
	ctInitAck          chunkType = 2
	ctSack             chunkType = 3
	ctHeartbeat        chunkType = 4
	ctHeartbeatAck     chunkType = 5
	ctAbort            chunkType = 6
	ctShutdown         chunkType = 7
	ctShutdownAck      chunkType = 8
	ctError            chunkType = 9
	ctCookieEcho       chunkType = 10
	ctCookieAck        chunkType = 11
	ctCWR              chunkType = 13
	ctShutdownComplete chunkType = 14
	ctReconfig         chunkType = 130
	ctForwardTSN       chunkType = 192
)

func (c chunkType) String() string {
	switch c {
	case ctPayloadData:
		return "DATA"
	case ctInit:
		return "INIT"
	case ctInitAck:
		return "INIT-ACK"
	case ctSack:
		return "SACK"
	case ctHeartbeat:
		return "HEARTBEAT"
	case ctHeartbeatAck:
		return "HEARTBEAT-ACK"
	case ctAbort:
		return "ABORT"
	case ctShutdown:
		return "SHUTDOWN"
	case ctShutdownAck:
		return "SHUTDOWN-ACK"
	case ctError:
		return "ERROR"
	case ctCookieEcho:
		return "COOKIE-ECHO"
	case ctCookieAck:
		return "COOKIE-ACK"
	case ctCWR:
		return "ECNE" // Explicit Congestion Notification Echo
	case ctShutdownComplete:
		return "SHUTDOWN-COMPLETE"
	case ctReconfig:
		return "RECONFIG" // Re-configuration
	case ctForwardTSN:
		return "FORWARD-TSN"
	default:
		return fmt.Sprintf("Unknown ChunkType: %d", c)
	}
}
