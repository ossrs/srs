// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package server

import (
	"fmt"
	"net"

	"github.com/pion/randutil"
	"github.com/pion/stun/v3"
	"github.com/pion/turn/v4/internal/allocation"
	"github.com/pion/turn/v4/internal/ipnet"
	"github.com/pion/turn/v4/internal/proto"
)

const runesAlpha = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"

// See: https://tools.ietf.org/html/rfc5766#section-6.2
// .
func handleAllocateRequest(req Request, stunMsg *stun.Message) error { //nolint:cyclop
	req.Log.Debugf("Received AllocateRequest from %s", req.SrcAddr)

	// 1. The server MUST require that the request be authenticated.  This
	//    authentication MUST be done using the long-term credential
	//    mechanism of [https://tools.ietf.org/html/rfc5389#section-10.2.2]
	//    unless the client and server agree to use another mechanism through
	//    some procedure outside the scope of this document.
	messageIntegrity, hasAuth, err := authenticateRequest(req, stunMsg, stun.MethodAllocate)
	if !hasAuth {
		return err
	}

	fiveTuple := &allocation.FiveTuple{
		SrcAddr:  req.SrcAddr,
		DstAddr:  req.Conn.LocalAddr(),
		Protocol: allocation.UDP,
	}
	requestedPort := 0
	reservationToken := ""

	badRequestMsg := buildMsg(
		stunMsg.TransactionID,
		stun.NewType(stun.MethodAllocate, stun.ClassErrorResponse),
		&stun.ErrorCodeAttribute{Code: stun.CodeBadRequest},
	)
	insufficientCapacityMsg := buildMsg(
		stunMsg.TransactionID,
		stun.NewType(stun.MethodAllocate, stun.ClassErrorResponse),
		&stun.ErrorCodeAttribute{Code: stun.CodeInsufficientCapacity},
	)

	// 2. The server checks if the 5-tuple is currently in use by an
	//    existing allocation.  If yes, the server rejects the request with
	//    a 437 (Allocation Mismatch) error.
	if alloc := req.AllocationManager.GetAllocation(fiveTuple); alloc != nil {
		id, attrs := alloc.GetResponseCache()
		if id != stunMsg.TransactionID {
			msg := buildMsg(
				stunMsg.TransactionID,
				stun.NewType(stun.MethodAllocate, stun.ClassErrorResponse),
				&stun.ErrorCodeAttribute{Code: stun.CodeAllocMismatch},
			)

			return buildAndSendErr(req.Conn, req.SrcAddr, errRelayAlreadyAllocatedForFiveTuple, msg...)
		}
		// A retry allocation
		msg := buildMsg(
			stunMsg.TransactionID,
			stun.NewType(stun.MethodAllocate, stun.ClassSuccessResponse),
			append(attrs, messageIntegrity)...,
		)

		return buildAndSend(req.Conn, req.SrcAddr, msg...)
	}

	// 3. The server checks if the request contains a REQUESTED-TRANSPORT
	//    attribute.  If the REQUESTED-TRANSPORT attribute is not included
	//    or is malformed, the server rejects the request with a 400 (Bad
	//    Request) error.  Otherwise, if the attribute is included but
	//    specifies a protocol other that UDP/TCP, the server rejects the
	//    request with a 442 (Unsupported Transport Protocol) error.
	var requestedTransport proto.RequestedTransport
	if err = requestedTransport.GetFrom(stunMsg); err != nil {
		return buildAndSendErr(req.Conn, req.SrcAddr, err, badRequestMsg...)
	} else if requestedTransport.Protocol != proto.ProtoUDP && requestedTransport.Protocol != proto.ProtoTCP {
		msg := buildMsg(
			stunMsg.TransactionID,
			stun.NewType(stun.MethodAllocate, stun.ClassErrorResponse),
			&stun.ErrorCodeAttribute{Code: stun.CodeUnsupportedTransProto},
		)

		return buildAndSendErr(req.Conn, req.SrcAddr, errUnsupportedTransportProtocol, msg...)
	}

	// 4. The request may contain a DONT-FRAGMENT attribute.  If it does,
	//    but the server does not support sending UDP datagrams with the DF
	//    bit set to 1 (see Section 12), then the server treats the DONT-
	//    FRAGMENT attribute in the Allocate request as an unknown
	//    comprehension-required attribute.
	if stunMsg.Contains(stun.AttrDontFragment) {
		msg := buildMsg(
			stunMsg.TransactionID,
			stun.NewType(stun.MethodAllocate, stun.ClassErrorResponse),
			&stun.ErrorCodeAttribute{Code: stun.CodeUnknownAttribute},
			&stun.UnknownAttributes{stun.AttrDontFragment},
		)

		return buildAndSendErr(req.Conn, req.SrcAddr, errNoDontFragmentSupport, msg...)
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
	if err = reservationTokenAttr.GetFrom(stunMsg); err == nil {
		var evenPort proto.EvenPort
		if err = evenPort.GetFrom(stunMsg); err == nil {
			return buildAndSendErr(req.Conn, req.SrcAddr, errRequestWithReservationTokenAndEvenPort, badRequestMsg...)
		}
	}

	// 6. The server checks if the request contains an EVEN-PORT attribute.
	//    If yes, then the server checks that it can satisfy the request
	//    (i.e., can allocate a relayed transport address as described
	//    below).  If the server cannot satisfy the request, then the
	//    server rejects the request with a 508 (Insufficient Capacity)
	//    error.
	var evenPort proto.EvenPort
	if err = evenPort.GetFrom(stunMsg); err == nil {
		var randomPort int
		randomPort, err = req.AllocationManager.GetRandomEvenPort()
		if err != nil {
			return buildAndSendErr(req.Conn, req.SrcAddr, err, insufficientCapacityMsg...)
		}
		requestedPort = randomPort
		reservationToken, err = randutil.GenerateCryptoRandomString(8, runesAlpha)
		if err != nil {
			return err
		}
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
	lifetimeDuration := allocationLifeTime(stunMsg)
	alloc, err := req.AllocationManager.CreateAllocation(
		fiveTuple,
		req.Conn,
		requestedPort,
		lifetimeDuration)
	if err != nil {
		return buildAndSendErr(req.Conn, req.SrcAddr, err, insufficientCapacityMsg...)
	}

	// Once the allocation is created, the server replies with a success
	// response.
	// The success response contains:
	//   * An XOR-RELAYED-ADDRESS attribute containing the relayed transport
	//     address.
	//   * A LIFETIME attribute containing the current value of the time-to-
	//     expiry timer.
	//   * A RESERVATION-TOKEN attribute (if a second relayed transport
	//     address was reserved).
	//   * An XOR-MAPPED-ADDRESS attribute containing the client's IP address
	//     and port (from the 5-tuple).

	srcIP, srcPort, err := ipnet.AddrIPPort(req.SrcAddr)
	if err != nil {
		return buildAndSendErr(req.Conn, req.SrcAddr, err, badRequestMsg...)
	}

	relayIP, relayPort, err := ipnet.AddrIPPort(alloc.RelayAddr)
	if err != nil {
		return buildAndSendErr(req.Conn, req.SrcAddr, err, badRequestMsg...)
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
		req.AllocationManager.CreateReservation(reservationToken, relayPort)
		responseAttrs = append(responseAttrs, proto.ReservationToken([]byte(reservationToken)))
	}

	msg := buildMsg(
		stunMsg.TransactionID,
		stun.NewType(stun.MethodAllocate, stun.ClassSuccessResponse),
		append(responseAttrs, messageIntegrity)...,
	)
	alloc.SetResponseCache(stunMsg.TransactionID, responseAttrs)

	return buildAndSend(req.Conn, req.SrcAddr, msg...)
}

func handleRefreshRequest(req Request, stunMsg *stun.Message) error {
	req.Log.Debugf("Received RefreshRequest from %s", req.SrcAddr)

	messageIntegrity, hasAuth, err := authenticateRequest(req, stunMsg, stun.MethodRefresh)
	if !hasAuth {
		return err
	}

	lifetimeDuration := allocationLifeTime(stunMsg)
	fiveTuple := &allocation.FiveTuple{
		SrcAddr:  req.SrcAddr,
		DstAddr:  req.Conn.LocalAddr(),
		Protocol: allocation.UDP,
	}

	if lifetimeDuration != 0 {
		a := req.AllocationManager.GetAllocation(fiveTuple)

		if a == nil {
			return fmt.Errorf("%w %v:%v", errNoAllocationFound, req.SrcAddr, req.Conn.LocalAddr())
		}
		a.Refresh(lifetimeDuration)
	} else {
		req.AllocationManager.DeleteAllocation(fiveTuple)
	}

	return buildAndSend(
		req.Conn,
		req.SrcAddr,
		buildMsg(
			stunMsg.TransactionID,
			stun.NewType(stun.MethodRefresh, stun.ClassSuccessResponse),
			[]stun.Setter{
				&proto.Lifetime{
					Duration: lifetimeDuration,
				},
				messageIntegrity,
			}...,
		)...,
	)
}

func handleCreatePermissionRequest(req Request, stunMsg *stun.Message) error {
	req.Log.Debugf("Received CreatePermission from %s", req.SrcAddr)

	alloc := req.AllocationManager.GetAllocation(&allocation.FiveTuple{
		SrcAddr:  req.SrcAddr,
		DstAddr:  req.Conn.LocalAddr(),
		Protocol: allocation.UDP,
	})
	if alloc == nil {
		return fmt.Errorf("%w %v:%v", errNoAllocationFound, req.SrcAddr, req.Conn.LocalAddr())
	}

	messageIntegrity, hasAuth, err := authenticateRequest(req, stunMsg, stun.MethodCreatePermission)
	if !hasAuth {
		return err
	}

	addCount := 0

	if err := stunMsg.ForEach(stun.AttrXORPeerAddress, func(m *stun.Message) error {
		var peerAddress proto.PeerAddress
		if err := peerAddress.GetFrom(m); err != nil {
			return err
		}

		if err := req.AllocationManager.GrantPermission(req.SrcAddr, peerAddress.IP); err != nil {
			req.Log.Infof("permission denied for client %s to peer %s", req.SrcAddr, peerAddress.IP)

			return err
		}

		req.Log.Debugf("Adding permission for %s", fmt.Sprintf("%s:%d",
			peerAddress.IP, peerAddress.Port))

		alloc.AddPermission(allocation.NewPermission(
			&net.UDPAddr{
				IP:   peerAddress.IP,
				Port: peerAddress.Port,
			},
			req.Log,
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

	return buildAndSend(
		req.Conn,
		req.SrcAddr,
		buildMsg(stunMsg.TransactionID, stun.NewType(stun.MethodCreatePermission, respClass),
			[]stun.Setter{messageIntegrity}...)...,
	)
}

func handleSendIndication(req Request, stunMsg *stun.Message) error {
	req.Log.Debugf("Received SendIndication from %s", req.SrcAddr)
	alloc := req.AllocationManager.GetAllocation(&allocation.FiveTuple{
		SrcAddr:  req.SrcAddr,
		DstAddr:  req.Conn.LocalAddr(),
		Protocol: allocation.UDP,
	})
	if alloc == nil {
		return fmt.Errorf("%w %v:%v", errNoAllocationFound, req.SrcAddr, req.Conn.LocalAddr())
	}

	dataAttr := proto.Data{}
	if err := dataAttr.GetFrom(stunMsg); err != nil {
		return err
	}

	peerAddress := proto.PeerAddress{}
	if err := peerAddress.GetFrom(stunMsg); err != nil {
		return err
	}

	msgDst := &net.UDPAddr{IP: peerAddress.IP, Port: peerAddress.Port}
	if perm := alloc.GetPermission(msgDst); perm == nil {
		return fmt.Errorf("%w: %v", errNoPermission, msgDst)
	}

	l, err := alloc.RelaySocket.WriteTo(dataAttr, msgDst)
	if l != len(dataAttr) {
		return fmt.Errorf("%w %d != %d (expected) err: %v", errShortWrite, l, len(dataAttr), err) //nolint:errorlint
	}

	return err
}

func handleChannelBindRequest(req Request, stunMsg *stun.Message) error {
	req.Log.Debugf("Received ChannelBindRequest from %s", req.SrcAddr)

	alloc := req.AllocationManager.GetAllocation(&allocation.FiveTuple{
		SrcAddr:  req.SrcAddr,
		DstAddr:  req.Conn.LocalAddr(),
		Protocol: allocation.UDP,
	})
	if alloc == nil {
		return fmt.Errorf("%w %v:%v", errNoAllocationFound, req.SrcAddr, req.Conn.LocalAddr())
	}

	badRequestMsg := buildMsg(
		stunMsg.TransactionID,
		stun.NewType(stun.MethodChannelBind, stun.ClassErrorResponse),
		&stun.ErrorCodeAttribute{Code: stun.CodeBadRequest},
	)

	messageIntegrity, hasAuth, err := authenticateRequest(req, stunMsg, stun.MethodChannelBind)
	if !hasAuth {
		return err
	}

	var channel proto.ChannelNumber
	if err = channel.GetFrom(stunMsg); err != nil {
		return buildAndSendErr(req.Conn, req.SrcAddr, err, badRequestMsg...)
	}

	peerAddr := proto.PeerAddress{}
	if err = peerAddr.GetFrom(stunMsg); err != nil {
		return buildAndSendErr(req.Conn, req.SrcAddr, err, badRequestMsg...)
	}

	if err = req.AllocationManager.GrantPermission(req.SrcAddr, peerAddr.IP); err != nil {
		req.Log.Infof("permission denied for client %s to peer %s", req.SrcAddr, peerAddr.IP)

		unauthorizedRequestMsg := buildMsg(stunMsg.TransactionID,
			stun.NewType(stun.MethodChannelBind, stun.ClassErrorResponse),
			&stun.ErrorCodeAttribute{Code: stun.CodeUnauthorized})

		return buildAndSendErr(req.Conn, req.SrcAddr, err, unauthorizedRequestMsg...)
	}

	req.Log.Debugf("Binding channel %d to %s", channel, peerAddr)
	err = alloc.AddChannelBind(allocation.NewChannelBind(
		channel,
		&net.UDPAddr{IP: peerAddr.IP, Port: peerAddr.Port},
		req.Log,
	), req.ChannelBindTimeout)
	if err != nil {
		return buildAndSendErr(req.Conn, req.SrcAddr, err, badRequestMsg...)
	}

	return buildAndSend(
		req.Conn,
		req.SrcAddr,
		buildMsg(stunMsg.TransactionID, stun.NewType(stun.MethodChannelBind, stun.ClassSuccessResponse),
			[]stun.Setter{messageIntegrity}...)...,
	)
}

func handleChannelData(req Request, channelData *proto.ChannelData) error {
	req.Log.Debugf("Received ChannelData from %s", req.SrcAddr)

	alloc := req.AllocationManager.GetAllocation(&allocation.FiveTuple{
		SrcAddr:  req.SrcAddr,
		DstAddr:  req.Conn.LocalAddr(),
		Protocol: allocation.UDP,
	})
	if alloc == nil {
		return fmt.Errorf("%w %v:%v", errNoAllocationFound, req.SrcAddr, req.Conn.LocalAddr())
	}

	channel := alloc.GetChannelByNumber(channelData.Number)
	if channel == nil {
		return fmt.Errorf("%w %x", errNoSuchChannelBind, uint16(channelData.Number))
	}

	l, err := alloc.RelaySocket.WriteTo(channelData.Data, channel.Peer)
	if err != nil {
		return fmt.Errorf("%w: %s", errFailedWriteSocket, err.Error())
	} else if l != len(channelData.Data) {
		return fmt.Errorf("%w %d != %d (expected)", errShortWrite, l, len(channelData.Data))
	}

	return nil
}
