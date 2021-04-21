package dtls

import "encoding/binary"

const (
	extensionUseSRTPHeaderSize = 6
)

// https://tools.ietf.org/html/rfc8422
type extensionUseSRTP struct {
	protectionProfiles []SRTPProtectionProfile
}

func (e extensionUseSRTP) extensionValue() extensionValue {
	return extensionUseSRTPValue
}

func (e *extensionUseSRTP) Marshal() ([]byte, error) {
	out := make([]byte, extensionUseSRTPHeaderSize)

	binary.BigEndian.PutUint16(out, uint16(e.extensionValue()))
	binary.BigEndian.PutUint16(out[2:], uint16(2+(len(e.protectionProfiles)*2)+ /* MKI Length */ 1))
	binary.BigEndian.PutUint16(out[4:], uint16(len(e.protectionProfiles)*2))

	for _, v := range e.protectionProfiles {
		out = append(out, []byte{0x00, 0x00}...)
		binary.BigEndian.PutUint16(out[len(out)-2:], uint16(v))
	}

	out = append(out, 0x00) /* MKI Length */
	return out, nil
}

func (e *extensionUseSRTP) Unmarshal(data []byte) error {
	if len(data) <= extensionUseSRTPHeaderSize {
		return errBufferTooSmall
	} else if extensionValue(binary.BigEndian.Uint16(data)) != e.extensionValue() {
		return errInvalidExtensionType
	}

	profileCount := int(binary.BigEndian.Uint16(data[4:]) / 2)
	if extensionSupportedGroupsHeaderSize+(profileCount*2) > len(data) {
		return errLengthMismatch
	}

	for i := 0; i < profileCount; i++ {
		supportedProfile := SRTPProtectionProfile(binary.BigEndian.Uint16(data[(extensionUseSRTPHeaderSize + (i * 2)):]))
		if _, ok := srtpProtectionProfiles()[supportedProfile]; ok {
			e.protectionProfiles = append(e.protectionProfiles, supportedProfile)
		}
	}
	return nil
}
