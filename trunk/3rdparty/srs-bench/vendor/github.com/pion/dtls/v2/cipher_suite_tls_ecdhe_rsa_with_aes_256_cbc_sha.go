package dtls

type cipherSuiteTLSEcdheRsaWithAes256CbcSha struct {
	cipherSuiteTLSEcdheEcdsaWithAes256CbcSha
}

func (c *cipherSuiteTLSEcdheRsaWithAes256CbcSha) certificateType() clientCertificateType {
	return clientCertificateTypeRSASign
}

func (c *cipherSuiteTLSEcdheRsaWithAes256CbcSha) ID() CipherSuiteID {
	return TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA
}

func (c *cipherSuiteTLSEcdheRsaWithAes256CbcSha) String() string {
	return "TLS_ECDHE_RSA_WITH_AES_256_CBC_SHA"
}
