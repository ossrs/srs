package extension

import "encoding/binary"

const (
	useSRTPHeaderSize = 6
)

// UseSRTP allows a Client/Server to negotiate what SRTPProtectionProfiles
// they both support
//
// https://tools.ietf.org/html/rfc8422
type UseSRTP struct {
	ProtectionProfiles []SRTPProtectionProfile
}

// TypeValue returns the extension TypeValue
func (u UseSRTP) TypeValue() TypeValue {
	return UseSRTPTypeValue
}

// Marshal encodes the extension
func (u *UseSRTP) Marshal() ([]byte, error) {
	out := make([]byte, useSRTPHeaderSize)

	binary.BigEndian.PutUint16(out, uint16(u.TypeValue()))
	binary.BigEndian.PutUint16(out[2:], uint16(2+(len(u.ProtectionProfiles)*2)+ /* MKI Length */ 1))
	binary.BigEndian.PutUint16(out[4:], uint16(len(u.ProtectionProfiles)*2))

	for _, v := range u.ProtectionProfiles {
		out = append(out, []byte{0x00, 0x00}...)
		binary.BigEndian.PutUint16(out[len(out)-2:], uint16(v))
	}

	out = append(out, 0x00) /* MKI Length */
	return out, nil
}

// Unmarshal populates the extension from encoded data
func (u *UseSRTP) Unmarshal(data []byte) error {
	if len(data) <= useSRTPHeaderSize {
		return errBufferTooSmall
	} else if TypeValue(binary.BigEndian.Uint16(data)) != u.TypeValue() {
		return errInvalidExtensionType
	}

	profileCount := int(binary.BigEndian.Uint16(data[4:]) / 2)
	if supportedGroupsHeaderSize+(profileCount*2) > len(data) {
		return errLengthMismatch
	}

	for i := 0; i < profileCount; i++ {
		supportedProfile := SRTPProtectionProfile(binary.BigEndian.Uint16(data[(useSRTPHeaderSize + (i * 2)):]))
		if _, ok := srtpProtectionProfiles()[supportedProfile]; ok {
			u.ProtectionProfiles = append(u.ProtectionProfiles, supportedProfile)
		}
	}
	return nil
}
