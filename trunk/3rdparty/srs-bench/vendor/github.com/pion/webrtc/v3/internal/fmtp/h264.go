// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package fmtp

import (
	"encoding/hex"
)

func profileLevelIDMatches(a, b string) bool {
	aa, err := hex.DecodeString(a)
	if err != nil || len(aa) < 2 {
		return false
	}
	bb, err := hex.DecodeString(b)
	if err != nil || len(bb) < 2 {
		return false
	}
	return aa[0] == bb[0] && aa[1] == bb[1]
}

type h264FMTP struct {
	parameters map[string]string
}

func (h *h264FMTP) MimeType() string {
	return "video/h264"
}

// Match returns true if h and b are compatible fmtp descriptions
// Based on RFC6184 Section 8.2.2:
//
//	The parameters identifying a media format configuration for H.264
//	are profile-level-id and packetization-mode.  These media format
//	configuration parameters (except for the level part of profile-
//	level-id) MUST be used symmetrically; that is, the answerer MUST
//	either maintain all configuration parameters or remove the media
//	format (payload type) completely if one or more of the parameter
//	values are not supported.
//	  Informative note: The requirement for symmetric use does not
//	  apply for the level part of profile-level-id and does not apply
//	  for the other stream properties and capability parameters.
func (h *h264FMTP) Match(b FMTP) bool {
	c, ok := b.(*h264FMTP)
	if !ok {
		return false
	}

	// test packetization-mode
	hpmode, hok := h.parameters["packetization-mode"]
	if !hok {
		return false
	}
	cpmode, cok := c.parameters["packetization-mode"]
	if !cok {
		return false
	}

	if hpmode != cpmode {
		return false
	}

	// test profile-level-id
	hplid, hok := h.parameters["profile-level-id"]
	if !hok {
		return false
	}

	cplid, cok := c.parameters["profile-level-id"]
	if !cok {
		return false
	}

	if !profileLevelIDMatches(hplid, cplid) {
		return false
	}

	return true
}

func (h *h264FMTP) Parameter(key string) (string, bool) {
	v, ok := h.parameters[key]
	return v, ok
}
