// Package protocol provides the DTLS wire format
package protocol

// Version enums
var (
	Version1_0 = Version{Major: 0xfe, Minor: 0xff} //nolint:gochecknoglobals
	Version1_2 = Version{Major: 0xfe, Minor: 0xfd} //nolint:gochecknoglobals
)

// Version is the minor/major value in the RecordLayer
// and ClientHello/ServerHello
//
// https://tools.ietf.org/html/rfc4346#section-6.2.1
type Version struct {
	Major, Minor uint8
}

// Equal determines if two protocol versions are equal
func (v Version) Equal(x Version) bool {
	return v.Major == x.Major && v.Minor == x.Minor
}
