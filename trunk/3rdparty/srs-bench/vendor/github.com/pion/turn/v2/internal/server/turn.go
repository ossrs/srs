package server

import (
	"fmt"
	"net"

	"github.com/pion/stun"
	"github.com/pion/turn/v2/internal/allocation"
	"github.com/pion/turn/v2/internal/ipnet"
	"github.com/pion/turn/v2/internal/proto"
)

// // https://tools.ietf.org/html/rfc5766#section-6.2
func handleAllocateRequest(r Request, m *stun.Message) error {
	r.Log.Debugf("received AllocateRequest from %s", r.SrcAddr.String())

	// 1. The server MUST require that the request be authenticated.  This
	//    authentication MUST be done using the long-term credential
	//    mechanism of [https://tools.ietf.org/html/rfc5389#section-10.2.2]
	//    unless the client and server agree to use another mechanism through
	//    some procedure outside the scope of this document.
	messageIntegrity, hasAuth, err := authenticateRequest(r, m, stun.MethodAllocate)
	if !hasAuth {
		return err
	}

	fiveTuple := &allocation.FiveTuple{
		SrcAddr:  r.SrcAddr,
		DstAddr:  r.Conn.LocalAddr(),
		Protocol: allocation.UDP,
	}
	requestedPort := 0
	reservationToken := ""

	badRequestMsg := buildMsg(m.TransactionID, stun.NewType(stun.MethodAllocate, stun.ClassErrorResponse), &stun.ErrorCodeAttribute{Code: stun.CodeBadRequest})
	insufficentCapacityMsg := buildMsg(m.TransactionID, stun.NewType(stun.MethodAllocate, stun.ClassErrorResponse), &stun.ErrorCodeAttribute{Code: stun.CodeInsufficientCapacity})

	// 2. The server checks if the 5-tuple is currently in use by an
	//    existing allocation.  If yes, the server rejects the request with
	//    a 437 (Allocation Mismatch) error.
	if alloc := r.AllocationManager.GetAllocation(fiveTuple); alloc != nil {
		msg := buildMsg(m.TransactionID, stun.NewType(stun.MethodAllocate, stun.ClassErrorResponse), &stun.ErrorCodeAttribute{Code: stun.CodeAllocMismatch})
		return buildAndSendErr(r.Conn, r.SrcAddr, errRelayAlreadyAllocatedForFiveTuple, msg...)
	}

	// 3. The server checks if the request contains a REQUESTED-TRANSPORT
	//    attribute.  If the REQUESTED-TRANSPORT attribute is not included
	//    or is malformed, the server rejects the request with a 400 (Bad
	//    Request) error.  Otherwise, if the attribute is included but
	//    specifies a protocol other that UDP, the server rejects the
	//    request with a 442 (Unsupported Transport Protocol) error.
	var requestedTransport proto.RequestedTransport
	if err = requestedTransport.GetFrom(m); err != nil {
		return buildAndSendErr(r.Conn, r.SrcAddr, err, badRequestMsg...)
	} else if requestedTransport.Protocol != proto.ProtoUDP {
		msg := buildMsg(m.TransactionID, stun.NewType(stun.MethodAllocate, stun.ClassErrorResponse), &stun.ErrorCodeAttribute{Code: stun.CodeUnsupportedTransProto})
		return buildAndSendErr(r.Conn, r.SrcAddr, errRequestedTransportMustBeUDP, msg...)
	}

	// 4. The request may contain a DONT-FRAGMENT attribute.  If it does,
	//    but the server does not support sending UDP datagrams with the DF
	//    bit set to 1 (see Section 12), then the server treats the DONT-
	//    FRAGMENT attribute in the Allocate request as an unknown
	//    comprehension-required attribute.
	if m.Contains(stun.AttrDontFragment) {
		msg := buildMsg(m.TransactionID, stun.NewType(stun.MethodAllocate, stun.ClassErrorResponse), &stun.ErrorCodeAttribute{Code: stun.CodeUnknownAttribute}, &stun.UnknownAttributes{stun.AttrDontFragment})
		return buildAndSendErr(r.Conn, r.SrcAddr, errNoDontFragmentSupport, msg...)
	}

	// 5.  The server checks if the request contains a RESERVATION-TOKEN
	//     attribute.  If yes, and the request also contains an EVEN-PORT
	//     attribute, then the server rejects the request with a 400 (Bad
	//     Request) error.  Otherwise, it checks to see if the token is
	//     valid (i.e., the token is in range and has not expired and the
	//     corresponding relayed transport address is still available).  If
	//     the token is not valid for some reason, the server rejects the
	//     request with a 508 (Insufficient Capacity) error.
	var reservationTokenAttr proto.ReservationToken
	if err = reservationTokenAttr.GetFrom(m); err == nil {
		var evenPort proto.EvenPort
		if err = evenPort.GetFrom(m); err == nil {
			return buildAndSendErr(r.Conn, r.SrcAddr, errRequestWithReservationTokenAndEvenPort, badRequestMsg...)
		}
	}

	// 6. The server checks if the request contains an EVEN-PORT attribute.
	//    If yes, then the server checks that it can satisfy the request
	//    (i.e., can allocate a relayed transport address as described
	//    below).  If the server cannot satisfy the request, then the
	//    server rejects the request with a 508 (Insufficient Capacity)
	//    error.
	var evenPort proto.EvenPort
	if err = evenPort.GetFrom(m); err == nil {
		randomPort := 0
		randomPort, err = r.AllocationManager.GetRandomEvenPort()
		if err != nil {
			return buildAndSendErr(r.Conn, r.SrcAddr, err, insufficentCapacityMsg...)
		}
		requestedPort = randomPort
		reservationToken = randSeq(8)
	}

	// 7. At any point, the server MAY choose to reject the request with a
	//    486 (Allocation Quota Reached) error if it feels the client is
	//    trying to exceed some locally defined allocation quota.  The
	//    server is free to define this allocation quota any way it wishes,
	//    but SHOULD define it based on the username used to authenticate
	//    the request, and not on the client's transport address.

	// 8. Also at any point, the server MAY choose to reject the request
	//    with a 300 (Try Alternate) error if it wishes to redirect the
	//    client to a different server.  The use of this error code and
	//    attribute follow the specification in [RFC5389].
	lifetimeDuration := allocationLifeTime(m)
	a, err := r.AllocationManager.CreateAllocation(
		fiveTuple,
		r.Conn,
		requestedPort,
		lifetimeDuration)
	if err != nil {
		return buildAndSendErr(r.Conn, r.SrcAddr, err, insufficentCapacityMsg...)
	}

	// Once the allocation is created, the server replies with a success
	// response.  The success response contains:
	//   * An XOR-RELAYED-ADDRESS attribute containing the relayed transport
	//     address.
	//   * A LIFETIME attribute containing the current value of the time-to-
	//     expiry timer.
	//   * A RESERVATION-TOKEN attribute (if a second relayed transport
	//     address was reserved).
	//   * An XOR-MAPPED-ADDRESS attribute containing the client's IP address
	//     and port (from the 5-tuple).

	srcIP, srcPort, err := ipnet.AddrIPPort(r.SrcAddr)
	if err != nil {
		return buildAndSendErr(r.Conn, r.SrcAddr, err, badRequestMsg...)
	}

	relayIP, relayPort, err := ipnet.AddrIPPort(a.RelayAddr)
	if err != nil {
		return buildAndSendErr(r.Conn, r.SrcAddr, err, badRequestMsg...)
	}

	responseAttrs := []stun.Setter{
		&proto.RelayedAddress{
			IP:   relayIP,
			Port: relayPort,
		},
		&proto.Lifetime{
			Duration: lifetimeDuration,
		},
		&stun.XORMappedAddress{
			IP:   srcIP,
			Port: srcPort,
		},
	}

	if reservationToken != "" {
		r.AllocationManager.CreateReservation(reservationToken, relayPort)
		responseAttrs = append(responseAttrs, proto.ReservationToken([]byte(reservationToken)))
	}

	msg := buildMsg(m.TransactionID, stun.NewType(stun.MethodAllocate, stun.ClassSuccessResponse), append(responseAttrs, messageIntegrity)...)
	return buildAndSend(r.Conn, r.SrcAddr, msg...)
}

func handleRefreshRequest(r Request, m *stun.Message) error {
	r.Log.Debugf("received RefreshRequest from %s", r.SrcAddr.String())

	messageIntegrity, hasAuth, err := authenticateRequest(r, m, stun.MethodRefresh)
	if !hasAuth {
		return err
	}

	lifetimeDuration := allocationLifeTime(m)
	fiveTuple := &allocation.FiveTuple{
		SrcAddr:  r.SrcAddr,
		DstAddr:  r.Conn.LocalAddr(),
		Protocol: allocation.UDP,
	}

	if lifetimeDuration != 0 {
		a := r.AllocationManager.GetAllocation(fiveTuple)

		if a == nil {
			return fmt.Errorf("%w %v:%v", errNoAllocationFound, r.SrcAddr, r.Conn.LocalAddr())
		}
		a.Refresh(lifetimeDuration)
	} else {
		r.AllocationManager.DeleteAllocation(fiveTuple)
	}

	return buildAndSend(r.Conn, r.SrcAddr, buildMsg(m.TransactionID, stun.NewType(stun.MethodRefresh, stun.ClassSuccessResponse), []stun.Setter{
		&proto.Lifetime{
			Duration: lifetimeDuration,
		},
		messageIntegrity,
	}...)...)
}

func handleCreatePermissionRequest(r Request, m *stun.Message) error {
	r.Log.Debugf("received CreatePermission from %s", r.SrcAddr.String())

	a := r.AllocationManager.GetAllocation(&allocation.FiveTuple{
		SrcAddr:  r.SrcAddr,
		DstAddr:  r.Conn.LocalAddr(),
		Protocol: allocation.UDP,
	})
	if a == nil {
		return fmt.Errorf("%w %v:%v", errNoAllocationFound, r.SrcAddr, r.Conn.LocalAddr())
	}

	messageIntegrity, hasAuth, err := authenticateRequest(r, m, stun.MethodCreatePermission)
	if !hasAuth {
		return err
	}

	addCount := 0

	if err := m.ForEach(stun.AttrXORPeerAddress, func(m *stun.Message) error {
		var peerAddress proto.PeerAddress
		if err := peerAddress.GetFrom(m); err != nil {
			return err
		}

		r.Log.Debugf("adding permission for %s", fmt.Sprintf("%s:%d",
			peerAddress.IP.String(), peerAddress.Port))
		a.AddPermission(allocation.NewPermission(
			&net.UDPAddr{
				IP:   peerAddress.IP,
				Port: peerAddress.Port,
			},
			r.Log,
		))
		addCount++
		return nil
	}); err != nil {
		addCount = 0
	}

	respClass := stun.ClassSuccessResponse
	if addCount == 0 {
		respClass = stun.ClassErrorResponse
	}

	return buildAndSend(r.Conn, r.SrcAddr, buildMsg(m.TransactionID, stun.NewType(stun.MethodCreatePermission, respClass), []stun.Setter{messageIntegrity}...)...)
}

func handleSendIndication(r Request, m *stun.Message) error {
	r.Log.Debugf("received SendIndication from %s", r.SrcAddr.String())
	a := r.AllocationManager.GetAllocation(&allocation.FiveTuple{
		SrcAddr:  r.SrcAddr,
		DstAddr:  r.Conn.LocalAddr(),
		Protocol: allocation.UDP,
	})
	if a == nil {
		return fmt.Errorf("%w %v:%v", errNoAllocationFound, r.SrcAddr, r.Conn.LocalAddr())
	}

	dataAttr := proto.Data{}
	if err := dataAttr.GetFrom(m); err != nil {
		return err
	}

	peerAddress := proto.PeerAddress{}
	if err := peerAddress.GetFrom(m); err != nil {
		return err
	}

	msgDst := &net.UDPAddr{IP: peerAddress.IP, Port: peerAddress.Port}
	if perm := a.GetPermission(msgDst); perm == nil {
		return fmt.Errorf("%w: %v", errNoPermission, msgDst)
	}

	l, err := a.RelaySocket.WriteTo(dataAttr, msgDst)
	if l != len(dataAttr) {
		return fmt.Errorf("%w %d != %d (expected) err: %v", errShortWrite, l, len(dataAttr), err)
	}
	return err
}

func handleChannelBindRequest(r Request, m *stun.Message) error {
	r.Log.Debugf("received ChannelBindRequest from %s", r.SrcAddr.String())

	a := r.AllocationManager.GetAllocation(&allocation.FiveTuple{
		SrcAddr:  r.SrcAddr,
		DstAddr:  r.Conn.LocalAddr(),
		Protocol: allocation.UDP,
	})
	if a == nil {
		return fmt.Errorf("%w %v:%v", errNoAllocationFound, r.SrcAddr, r.Conn.LocalAddr())
	}

	badRequestMsg := buildMsg(m.TransactionID, stun.NewType(stun.MethodChannelBind, stun.ClassErrorResponse), &stun.ErrorCodeAttribute{Code: stun.CodeBadRequest})

	messageIntegrity, hasAuth, err := authenticateRequest(r, m, stun.MethodChannelBind)
	if !hasAuth {
		return err
	}

	var channel proto.ChannelNumber
	if err = channel.GetFrom(m); err != nil {
		return buildAndSendErr(r.Conn, r.SrcAddr, err, badRequestMsg...)
	}

	peerAddr := proto.PeerAddress{}
	if err = peerAddr.GetFrom(m); err != nil {
		return buildAndSendErr(r.Conn, r.SrcAddr, err, badRequestMsg...)
	}

	r.Log.Debugf("binding channel %d to %s",
		channel,
		fmt.Sprintf("%s:%d", peerAddr.IP.String(), peerAddr.Port))
	err = a.AddChannelBind(allocation.NewChannelBind(
		channel,
		&net.UDPAddr{IP: peerAddr.IP, Port: peerAddr.Port},
		r.Log,
	), r.ChannelBindTimeout)
	if err != nil {
		return buildAndSendErr(r.Conn, r.SrcAddr, err, badRequestMsg...)
	}

	return buildAndSend(r.Conn, r.SrcAddr, buildMsg(m.TransactionID, stun.NewType(stun.MethodChannelBind, stun.ClassSuccessResponse), []stun.Setter{messageIntegrity}...)...)
}

func handleChannelData(r Request, c *proto.ChannelData) error {
	r.Log.Debugf("received ChannelData from %s", r.SrcAddr.String())

	a := r.AllocationManager.GetAllocation(&allocation.FiveTuple{
		SrcAddr:  r.SrcAddr,
		DstAddr:  r.Conn.LocalAddr(),
		Protocol: allocation.UDP,
	})
	if a == nil {
		return fmt.Errorf("%w %v:%v", errNoAllocationFound, r.SrcAddr, r.Conn.LocalAddr())
	}

	channel := a.GetChannelByNumber(c.Number)
	if channel == nil {
		return fmt.Errorf("%w %x", errNoSuchChannelBind, uint16(c.Number))
	}

	l, err := a.RelaySocket.WriteTo(c.Data, channel.Peer)
	if err != nil {
		return fmt.Errorf("%w: %s", errFailedWriteSocket, err.Error())
	} else if l != len(c.Data) {
		return fmt.Errorf("%w %d != %d (expected)", errShortWrite, l, len(c.Data))
	}

	return nil
}
