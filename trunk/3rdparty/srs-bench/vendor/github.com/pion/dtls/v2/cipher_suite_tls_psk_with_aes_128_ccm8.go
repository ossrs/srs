package dtls

func newCipherSuiteTLSPskWithAes128Ccm8() *cipherSuiteAes128Ccm {
	return newCipherSuiteAes128Ccm(clientCertificateType(0), TLS_PSK_WITH_AES_128_CCM_8, true, cryptoCCM8TagLength)
}
