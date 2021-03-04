package dtls

// https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml#tls-parameters-10
type ellipticCurveType byte

const (
	ellipticCurveTypeNamedCurve ellipticCurveType = 0x03
)

func ellipticCurveTypes() map[ellipticCurveType]bool {
	return map[ellipticCurveType]bool{
		ellipticCurveTypeNamedCurve: true,
	}
}
