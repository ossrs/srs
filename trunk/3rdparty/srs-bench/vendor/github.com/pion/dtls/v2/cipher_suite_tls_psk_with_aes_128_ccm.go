package dtls

func newCipherSuiteTLSPskWithAes128Ccm() *cipherSuiteAes128Ccm {
	return newCipherSuiteAes128Ccm(clientCertificateType(0), TLS_PSK_WITH_AES_128_CCM, true, cryptoCCMTagLength)
}
