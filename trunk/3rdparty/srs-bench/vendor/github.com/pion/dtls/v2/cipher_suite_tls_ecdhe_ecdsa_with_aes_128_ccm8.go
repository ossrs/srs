package dtls

func newCipherSuiteTLSEcdheEcdsaWithAes128Ccm8() *cipherSuiteAes128Ccm {
	return newCipherSuiteAes128Ccm(clientCertificateTypeECDSASign, TLS_ECDHE_ECDSA_WITH_AES_128_CCM_8, false, cryptoCCM8TagLength)
}
