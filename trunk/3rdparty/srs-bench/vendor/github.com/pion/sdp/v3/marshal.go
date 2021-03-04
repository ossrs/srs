package sdp

import (
	"strings"
)

// Marshal takes a SDP struct to text
// https://tools.ietf.org/html/rfc4566#section-5
// Session description
//    v=  (protocol version)
//    o=  (originator and session identifier)
//    s=  (session name)
//    i=* (session information)
//    u=* (URI of description)
//    e=* (email address)
//    p=* (phone number)
//    c=* (connection information -- not required if included in
//         all media)
//    b=* (zero or more bandwidth information lines)
//    One or more time descriptions ("t=" and "r=" lines; see below)
//    z=* (time zone adjustments)
//    k=* (encryption key)
//    a=* (zero or more session attribute lines)
//    Zero or more media descriptions
//
// Time description
//    t=  (time the session is active)
//    r=* (zero or more repeat times)
//
// Media description, if present
//    m=  (media name and transport address)
//    i=* (media title)
//    c=* (connection information -- optional if included at
//         session level)
//    b=* (zero or more bandwidth information lines)
//    k=* (encryption key)
//    a=* (zero or more media attribute lines)
func (s *SessionDescription) Marshal() ([]byte, error) {
	m := make(marshaller, 0, 1024)

	m.addKeyValue("v=", s.Version.String())
	m.addKeyValue("o=", s.Origin.String())
	m.addKeyValue("s=", s.SessionName.String())

	if s.SessionInformation != nil {
		m.addKeyValue("i=", s.SessionInformation.String())
	}

	if s.URI != nil {
		m.addKeyValue("u=", s.URI.String())
	}

	if s.EmailAddress != nil {
		m.addKeyValue("e=", s.EmailAddress.String())
	}

	if s.PhoneNumber != nil {
		m.addKeyValue("p=", s.PhoneNumber.String())
	}

	if s.ConnectionInformation != nil {
		m.addKeyValue("c=", s.ConnectionInformation.String())
	}

	for _, b := range s.Bandwidth {
		m.addKeyValue("b=", b.String())
	}

	for _, td := range s.TimeDescriptions {
		m.addKeyValue("t=", td.Timing.String())
		for _, r := range td.RepeatTimes {
			m.addKeyValue("r=", r.String())
		}
	}

	if len(s.TimeZones) > 0 {
		var b strings.Builder
		for i, z := range s.TimeZones {
			if i > 0 {
				b.WriteString(" ")
			}
			b.WriteString(z.String())
		}
		m.addKeyValue("z=", b.String())
	}

	if s.EncryptionKey != nil {
		m.addKeyValue("k=", s.EncryptionKey.String())
	}

	for _, a := range s.Attributes {
		m.addKeyValue("a=", a.String())
	}

	for _, md := range s.MediaDescriptions {
		m.addKeyValue("m=", md.MediaName.String())

		if md.MediaTitle != nil {
			m.addKeyValue("i=", md.MediaTitle.String())
		}

		if md.ConnectionInformation != nil {
			m.addKeyValue("c=", md.ConnectionInformation.String())
		}

		for _, b := range md.Bandwidth {
			m.addKeyValue("b=", b.String())
		}

		if md.EncryptionKey != nil {
			m.addKeyValue("k=", md.EncryptionKey.String())
		}

		for _, a := range md.Attributes {
			m.addKeyValue("a=", a.String())
		}
	}

	return m.bytes(), nil
}

// marshaller contains state during marshaling.
type marshaller []byte

func (m *marshaller) addKeyValue(key, value string) {
	if value == "" {
		return
	}
	*m = append(*m, key...)
	*m = append(*m, value...)
	*m = append(*m, "\r\n"...)
}

func (m *marshaller) bytes() []byte {
	return *m
}
