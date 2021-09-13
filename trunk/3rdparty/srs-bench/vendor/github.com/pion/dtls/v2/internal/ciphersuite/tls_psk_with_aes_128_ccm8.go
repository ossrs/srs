package ciphersuite

import (
	"github.com/pion/dtls/v2/pkg/crypto/ciphersuite"
	"github.com/pion/dtls/v2/pkg/crypto/clientcertificate"
)

// NewTLSPskWithAes128Ccm8 returns the TLS_PSK_WITH_AES_128_CCM_8 CipherSuite
func NewTLSPskWithAes128Ccm8() *Aes128Ccm {
	return newAes128Ccm(clientcertificate.Type(0), TLS_PSK_WITH_AES_128_CCM_8, true, ciphersuite.CCMTagLength8)
}
