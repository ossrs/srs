// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package protocol

// ApplicationData messages are carried by the record layer and are
// fragmented, compressed, and encrypted based on the current connection
// state.  The messages are treated as transparent data to the record
// layer.
// https://tools.ietf.org/html/rfc5246#section-10
type ApplicationData struct {
	Data []byte
}

// ContentType returns the ContentType of this content
func (a ApplicationData) ContentType() ContentType {
	return ContentTypeApplicationData
}

// Marshal encodes the ApplicationData to binary
func (a *ApplicationData) Marshal() ([]byte, error) {
	return append([]byte{}, a.Data...), nil
}

// Unmarshal populates the ApplicationData from binary
func (a *ApplicationData) Unmarshal(data []byte) error {
	a.Data = append([]byte{}, data...)
	return nil
}
