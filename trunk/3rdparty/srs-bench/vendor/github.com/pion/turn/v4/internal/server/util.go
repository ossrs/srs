// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package server

import (
	"errors"
	"fmt"
	"net"
	"time"

	"github.com/pion/stun/v3"
	"github.com/pion/turn/v4/internal/proto"
)

const (
	maximumAllocationLifetime = time.Hour // See: https://tools.ietf.org/html/rfc5766#section-6.2 defines 3600 seconds recommendation
)

func buildAndSend(conn net.PacketConn, dst net.Addr, attrs ...stun.Setter) error {
	msg, err := stun.Build(attrs...)
	if err != nil {
		return err
	}
	_, err = conn.WriteTo(msg.Raw, dst)
	if errors.Is(err, net.ErrClosed) {
		return nil
	}

	return err
}

// Send a STUN packet and return the original error to the caller
func buildAndSendErr(conn net.PacketConn, dst net.Addr, err error, attrs ...stun.Setter) error {
	if sendErr := buildAndSend(conn, dst, attrs...); sendErr != nil {
		err = fmt.Errorf("%w %v %v", errFailedToSendError, sendErr, err) //nolint:errorlint
	}
	return err
}

func buildMsg(transactionID [stun.TransactionIDSize]byte, msgType stun.MessageType, additional ...stun.Setter) []stun.Setter {
	return append([]stun.Setter{&stun.Message{TransactionID: transactionID}, msgType}, additional...)
}

func authenticateRequest(r Request, m *stun.Message, callingMethod stun.Method) (stun.MessageIntegrity, bool, error) {
	respondWithNonce := func(responseCode stun.ErrorCode) (stun.MessageIntegrity, bool, error) {
		nonce, err := r.NonceHash.Generate()
		if err != nil {
			return nil, false, err
		}

		return nil, false, buildAndSend(r.Conn, r.SrcAddr, buildMsg(m.TransactionID,
			stun.NewType(callingMethod, stun.ClassErrorResponse),
			&stun.ErrorCodeAttribute{Code: responseCode},
			stun.NewNonce(nonce),
			stun.NewRealm(r.Realm),
		)...)
	}

	if !m.Contains(stun.AttrMessageIntegrity) {
		return respondWithNonce(stun.CodeUnauthorized)
	}

	nonceAttr := &stun.Nonce{}
	usernameAttr := &stun.Username{}
	realmAttr := &stun.Realm{}
	badRequestMsg := buildMsg(m.TransactionID, stun.NewType(callingMethod, stun.ClassErrorResponse), &stun.ErrorCodeAttribute{Code: stun.CodeBadRequest})

	// No Auth handler is set, server is running in STUN only mode
	// Respond with 400 so clients don't retry
	if r.AuthHandler == nil {
		sendErr := buildAndSend(r.Conn, r.SrcAddr, badRequestMsg...)
		return nil, false, sendErr
	}

	if err := nonceAttr.GetFrom(m); err != nil {
		return nil, false, buildAndSendErr(r.Conn, r.SrcAddr, err, badRequestMsg...)
	}

	// Assert Nonce is signed and is not expired
	if err := r.NonceHash.Validate(nonceAttr.String()); err != nil {
		return respondWithNonce(stun.CodeStaleNonce)
	}

	if err := realmAttr.GetFrom(m); err != nil {
		return nil, false, buildAndSendErr(r.Conn, r.SrcAddr, err, badRequestMsg...)
	} else if err := usernameAttr.GetFrom(m); err != nil {
		return nil, false, buildAndSendErr(r.Conn, r.SrcAddr, err, badRequestMsg...)
	}

	ourKey, ok := r.AuthHandler(usernameAttr.String(), realmAttr.String(), r.SrcAddr)
	if !ok {
		return nil, false, buildAndSendErr(r.Conn, r.SrcAddr, fmt.Errorf("%w %s", errNoSuchUser, usernameAttr.String()), badRequestMsg...)
	}

	if err := stun.MessageIntegrity(ourKey).Check(m); err != nil {
		return nil, false, buildAndSendErr(r.Conn, r.SrcAddr, err, badRequestMsg...)
	}

	return stun.MessageIntegrity(ourKey), true, nil
}

func allocationLifeTime(m *stun.Message) time.Duration {
	lifetimeDuration := proto.DefaultLifetime

	var lifetime proto.Lifetime
	if err := lifetime.GetFrom(m); err == nil {
		if lifetime.Duration < maximumAllocationLifetime {
			lifetimeDuration = lifetime.Duration
		}
	}

	return lifetimeDuration
}
