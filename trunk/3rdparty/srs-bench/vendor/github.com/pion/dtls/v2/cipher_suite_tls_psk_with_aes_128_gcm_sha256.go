package dtls

type cipherSuiteTLSPskWithAes128GcmSha256 struct {
	cipherSuiteTLSEcdheEcdsaWithAes128GcmSha256
}

func (c *cipherSuiteTLSPskWithAes128GcmSha256) certificateType() clientCertificateType {
	return clientCertificateType(0)
}

func (c *cipherSuiteTLSPskWithAes128GcmSha256) ID() CipherSuiteID {
	return TLS_PSK_WITH_AES_128_GCM_SHA256
}

func (c *cipherSuiteTLSPskWithAes128GcmSha256) String() string {
	return "TLS_PSK_WITH_AES_128_GCM_SHA256"
}

func (c *cipherSuiteTLSPskWithAes128GcmSha256) isPSK() bool {
	return true
}
