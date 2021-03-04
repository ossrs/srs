package dtls

// https://www.iana.org/assignments/tls-parameters/tls-parameters.xhtml#tls-parameters-10
type clientCertificateType byte

const (
	clientCertificateTypeRSASign   clientCertificateType = 1
	clientCertificateTypeECDSASign clientCertificateType = 64
)

func clientCertificateTypes() map[clientCertificateType]bool {
	return map[clientCertificateType]bool{
		clientCertificateTypeRSASign:   true,
		clientCertificateTypeECDSASign: true,
	}
}
