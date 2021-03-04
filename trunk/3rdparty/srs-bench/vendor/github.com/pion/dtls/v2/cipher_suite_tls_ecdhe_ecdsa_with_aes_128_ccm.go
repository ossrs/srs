package dtls

func newCipherSuiteTLSEcdheEcdsaWithAes128Ccm() *cipherSuiteAes128Ccm {
	return newCipherSuiteAes128Ccm(clientCertificateTypeECDSASign, TLS_ECDHE_ECDSA_WITH_AES_128_CCM, false, cryptoCCMTagLength)
}
