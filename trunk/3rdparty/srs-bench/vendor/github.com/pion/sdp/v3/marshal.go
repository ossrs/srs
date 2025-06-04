// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sdp

// Marshal takes a SDP struct to text
// https://tools.ietf.org/html/rfc4566#section-5
// Session description
//
//	v=  (protocol version)
//	o=  (originator and session identifier)
//	s=  (session name)
//	i=* (session information)
//	u=* (URI of description)
//	e=* (email address)
//	p=* (phone number)
//	c=* (connection information -- not required if included in
//	     all media)
//	b=* (zero or more bandwidth information lines)
//	One or more time descriptions ("t=" and "r=" lines; see below)
//	z=* (time zone adjustments)
//	k=* (encryption key)
//	a=* (zero or more session attribute lines)
//	Zero or more media descriptions
//
// Time description
//
//	t=  (time the session is active)
//	r=* (zero or more repeat times)
//
// Media description, if present
//
//	m=  (media name and transport address)
//	i=* (media title)
//	c=* (connection information -- optional if included at
//	     session level)
//	b=* (zero or more bandwidth information lines)
//	k=* (encryption key)
//	a=* (zero or more media attribute lines)
func (s *SessionDescription) Marshal() ([]byte, error) { //nolint:cyclop
	marsh := make(marshaller, 0, s.MarshalSize())

	marsh.addKeyValue("v=", s.Version.marshalInto)
	marsh.addKeyValue("o=", s.Origin.marshalInto)
	marsh.addKeyValue("s=", s.SessionName.marshalInto)

	if s.SessionInformation != nil {
		marsh.addKeyValue("i=", s.SessionInformation.marshalInto)
	}

	if s.URI != nil {
		marsh = append(marsh, "u="...)
		marsh = append(marsh, s.URI.String()...)
		marsh = append(marsh, "\r\n"...)
	}

	if s.EmailAddress != nil {
		marsh.addKeyValue("e=", s.EmailAddress.marshalInto)
	}

	if s.PhoneNumber != nil {
		marsh.addKeyValue("p=", s.PhoneNumber.marshalInto)
	}

	if s.ConnectionInformation != nil {
		marsh.addKeyValue("c=", s.ConnectionInformation.marshalInto)
	}

	for _, b := range s.Bandwidth {
		marsh.addKeyValue("b=", b.marshalInto)
	}

	for _, td := range s.TimeDescriptions {
		marsh.addKeyValue("t=", td.Timing.marshalInto)
		for _, r := range td.RepeatTimes {
			marsh.addKeyValue("r=", r.marshalInto)
		}
	}

	if len(s.TimeZones) > 0 {
		marsh = append(marsh, "z="...)
		for i, z := range s.TimeZones {
			if i > 0 {
				marsh = append(marsh, ' ')
			}
			marsh = z.marshalInto(marsh)
		}
		marsh = append(marsh, "\r\n"...)
	}

	if s.EncryptionKey != nil {
		marsh.addKeyValue("k=", s.EncryptionKey.marshalInto)
	}

	for _, a := range s.Attributes {
		marsh.addKeyValue("a=", a.marshalInto)
	}

	for _, md := range s.MediaDescriptions {
		marsh.addKeyValue("m=", md.MediaName.marshalInto)

		if md.MediaTitle != nil {
			marsh.addKeyValue("i=", md.MediaTitle.marshalInto)
		}

		if md.ConnectionInformation != nil {
			marsh.addKeyValue("c=", md.ConnectionInformation.marshalInto)
		}

		for _, b := range md.Bandwidth {
			marsh.addKeyValue("b=", b.marshalInto)
		}

		if md.EncryptionKey != nil {
			marsh.addKeyValue("k=", md.EncryptionKey.marshalInto)
		}

		for _, a := range md.Attributes {
			marsh.addKeyValue("a=", a.marshalInto)
		}
	}

	return marsh, nil
}

// `$type=` and CRLF size.
const lineBaseSize = 4

// MarshalSize returns the size of the SessionDescription once marshaled.
func (s *SessionDescription) MarshalSize() (marshalSize int) { //nolint:cyclop
	marshalSize += lineBaseSize + s.Version.marshalSize()
	marshalSize += lineBaseSize + s.Origin.marshalSize()
	marshalSize += lineBaseSize + s.SessionName.marshalSize()

	if s.SessionInformation != nil {
		marshalSize += lineBaseSize + s.SessionInformation.marshalSize()
	}

	if s.URI != nil {
		marshalSize += lineBaseSize + len(s.URI.String())
	}

	if s.EmailAddress != nil {
		marshalSize += lineBaseSize + s.EmailAddress.marshalSize()
	}

	if s.PhoneNumber != nil {
		marshalSize += lineBaseSize + s.PhoneNumber.marshalSize()
	}

	if s.ConnectionInformation != nil {
		marshalSize += lineBaseSize + s.ConnectionInformation.marshalSize()
	}

	for _, b := range s.Bandwidth {
		marshalSize += lineBaseSize + b.marshalSize()
	}

	for _, td := range s.TimeDescriptions {
		marshalSize += lineBaseSize + td.Timing.marshalSize()
		for _, r := range td.RepeatTimes {
			marshalSize += lineBaseSize + r.marshalSize()
		}
	}

	if len(s.TimeZones) > 0 {
		marshalSize += lineBaseSize

		for i, z := range s.TimeZones {
			if i > 0 {
				marshalSize++
			}
			marshalSize += z.marshalSize()
		}
	}

	if s.EncryptionKey != nil {
		marshalSize += lineBaseSize + s.EncryptionKey.marshalSize()
	}

	for _, a := range s.Attributes {
		marshalSize += lineBaseSize + a.marshalSize()
	}

	for _, md := range s.MediaDescriptions {
		marshalSize += lineBaseSize + md.MediaName.marshalSize()
		if md.MediaTitle != nil {
			marshalSize += lineBaseSize + md.MediaTitle.marshalSize()
		}
		if md.ConnectionInformation != nil {
			marshalSize += lineBaseSize + md.ConnectionInformation.marshalSize()
		}

		for _, b := range md.Bandwidth {
			marshalSize += lineBaseSize + b.marshalSize()
		}

		if md.EncryptionKey != nil {
			marshalSize += lineBaseSize + md.EncryptionKey.marshalSize()
		}

		for _, a := range md.Attributes {
			marshalSize += lineBaseSize + a.marshalSize()
		}
	}

	return marshalSize
}

// marshaller contains state during marshaling.
type marshaller []byte

func (m *marshaller) addKeyValue(key string, value func([]byte) []byte) {
	*m = append(*m, key...)
	*m = value(*m)
	*m = append(*m, "\r\n"...)
}

func lenUint(i uint64) (count int) {
	if i == 0 {
		return 1
	}

	for i != 0 {
		i /= 10
		count++
	}

	return
}

func lenInt(i int64) (count int) {
	if i < 0 {
		return lenUint(uint64(-i)) + 1
	}

	return lenUint(uint64(i))
}

func stringFromMarshal(marshalFunc func([]byte) []byte, sizeFunc func() int) string {
	return string(marshalFunc(make([]byte, 0, sizeFunc())))
}
