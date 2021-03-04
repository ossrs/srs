package allocation

import "errors"

var (
	errAllocatePacketConnMustBeSet = errors.New("AllocatePacketConn must be set")
	errAllocateConnMustBeSet       = errors.New("AllocateConn must be set")
	errLeveledLoggerMustBeSet      = errors.New("LeveledLogger must be set")
	errSameChannelDifferentPeer    = errors.New("you cannot use the same channel number with different peer")
	errNilFiveTuple                = errors.New("allocations must not be created with nil FivTuple")
	errNilFiveTupleSrcAddr         = errors.New("allocations must not be created with nil FiveTuple.SrcAddr")
	errNilFiveTupleDstAddr         = errors.New("allocations must not be created with nil FiveTuple.DstAddr")
	errNilTurnSocket               = errors.New("allocations must not be created with nil turnSocket")
	errLifetimeZero                = errors.New("allocations must not be created with a lifetime of 0")
	errDupeFiveTuple               = errors.New("allocation attempt created with duplicate FiveTuple")
	errFailedToCastUDPAddr         = errors.New("failed to cast net.Addr to *net.UDPAddr")
)
