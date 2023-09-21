// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package stun contains ICE specific STUN code
package stun

import (
	"errors"
	"fmt"
	"net"
	"time"

	"github.com/pion/stun"
)

var (
	errGetXorMappedAddrResponse = errors.New("failed to get XOR-MAPPED-ADDRESS response")
	errMismatchUsername         = errors.New("username mismatch")
)

// GetXORMappedAddr initiates a STUN requests to serverAddr using conn, reads the response and returns
// the XORMappedAddress returned by the STUN server.
func GetXORMappedAddr(conn net.PacketConn, serverAddr net.Addr, timeout time.Duration) (*stun.XORMappedAddress, error) {
	if timeout > 0 {
		if err := conn.SetReadDeadline(time.Now().Add(timeout)); err != nil {
			return nil, err
		}

		// Reset timeout after completion
		defer conn.SetReadDeadline(time.Time{}) //nolint:errcheck
	}

	req, err := stun.Build(stun.BindingRequest, stun.TransactionID)
	if err != nil {
		return nil, err
	}

	if _, err = conn.WriteTo(req.Raw, serverAddr); err != nil {
		return nil, err
	}

	const maxMessageSize = 1280
	buf := make([]byte, maxMessageSize)
	n, _, err := conn.ReadFrom(buf)
	if err != nil {
		return nil, err
	}

	res := &stun.Message{Raw: buf[:n]}
	if err = res.Decode(); err != nil {
		return nil, err
	}

	var addr stun.XORMappedAddress
	if err = addr.GetFrom(res); err != nil {
		return nil, fmt.Errorf("%w: %v", errGetXorMappedAddrResponse, err) //nolint:errorlint
	}

	return &addr, nil
}

// AssertUsername checks that the given STUN message m has a USERNAME attribute with a given value
func AssertUsername(m *stun.Message, expectedUsername string) error {
	var username stun.Username
	if err := username.GetFrom(m); err != nil {
		return err
	} else if string(username) != expectedUsername {
		return fmt.Errorf("%w expected(%x) actual(%x)", errMismatchUsername, expectedUsername, string(username))
	}

	return nil
}
