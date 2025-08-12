// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package server

import (
	"github.com/pion/stun/v3"
	"github.com/pion/turn/v4/internal/ipnet"
)

func handleBindingRequest(req Request, stunMsg *stun.Message) error {
	req.Log.Debugf("Received BindingRequest from %s", req.SrcAddr)

	ip, port, err := ipnet.AddrIPPort(req.SrcAddr)
	if err != nil {
		return err
	}

	attrs := buildMsg(stunMsg.TransactionID, stun.BindingSuccess, &stun.XORMappedAddress{
		IP:   ip,
		Port: port,
	}, stun.Fingerprint)

	return buildAndSend(req.Conn, req.SrcAddr, attrs...)
}
