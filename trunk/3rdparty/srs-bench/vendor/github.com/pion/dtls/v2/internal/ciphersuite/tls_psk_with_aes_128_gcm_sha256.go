package ciphersuite

import "github.com/pion/dtls/v2/pkg/crypto/clientcertificate"

// TLSPskWithAes128GcmSha256 implements the TLS_PSK_WITH_AES_128_GCM_SHA256 CipherSuite
type TLSPskWithAes128GcmSha256 struct {
	TLSEcdheEcdsaWithAes128GcmSha256
}

// CertificateType returns what type of certificate this CipherSuite exchanges
func (c *TLSPskWithAes128GcmSha256) CertificateType() clientcertificate.Type {
	return clientcertificate.Type(0)
}

// ID returns the ID of the CipherSuite
func (c *TLSPskWithAes128GcmSha256) ID() ID {
	return TLS_PSK_WITH_AES_128_GCM_SHA256
}

func (c *TLSPskWithAes128GcmSha256) String() string {
	return "TLS_PSK_WITH_AES_128_GCM_SHA256"
}

// AuthenticationType controls what authentication method is using during the handshake
func (c *TLSPskWithAes128GcmSha256) AuthenticationType() AuthenticationType {
	return AuthenticationTypePreSharedKey
}
