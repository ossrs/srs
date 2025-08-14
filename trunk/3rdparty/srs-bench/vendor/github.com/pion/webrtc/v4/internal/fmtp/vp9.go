// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package fmtp

type vp9FMTP struct {
	parameters map[string]string
}

func (h *vp9FMTP) MimeType() string {
	return "video/vp9"
}

func (h *vp9FMTP) Match(b FMTP) bool {
	c, ok := b.(*vp9FMTP)
	if !ok {
		return false
	}

	// RTP Payload Format for VP9 Video - draft-ietf-payload-vp9-16
	// https://datatracker.ietf.org/doc/html/draft-ietf-payload-vp9-16
	// If no profile-id is present, Profile 0 MUST be inferred
	hProfileID, ok := h.parameters["profile-id"]
	if !ok {
		hProfileID = "0"
	}
	cProfileID, ok := c.parameters["profile-id"]
	if !ok {
		cProfileID = "0"
	}
	if hProfileID != cProfileID {
		return false
	}

	return true
}

func (h *vp9FMTP) Parameter(key string) (string, bool) {
	v, ok := h.parameters[key]

	return v, ok
}
