// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

import "strings"

// getICEServers side-steps the strict parsing mode of the ice package
// (as defined in https://tools.ietf.org/html/rfc7064) by copying and then
// stripping any erroneous queries from "stun(s):" URLs before parsing.
func (c Configuration) getICEServers() []ICEServer {
	iceServers := append([]ICEServer{}, c.ICEServers...)

	for iceServersIndex := range iceServers {
		iceServers[iceServersIndex].URLs = append([]string{}, iceServers[iceServersIndex].URLs...)

		for urlsIndex, rawURL := range iceServers[iceServersIndex].URLs {
			if strings.HasPrefix(rawURL, "stun") {
				// strip the query from "stun(s):" if present
				parts := strings.Split(rawURL, "?")
				rawURL = parts[0]
			}
			iceServers[iceServersIndex].URLs[urlsIndex] = rawURL
		}
	}
	return iceServers
}
