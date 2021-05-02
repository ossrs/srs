/*
The MIT License (MIT)

Copyright (c) 2019 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

package main

import (
	"crypto/tls"
	"fmt"
	oe "github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/https"
)

type certsManager struct {
	// Key is hostname.
	certs map[string]https.Manager
}

func NewCertsManager(domains, keys, certs []string) (m https.Manager, err error) {
	v := &certsManager{
		certs: make(map[string]https.Manager),
	}

	for i := 0; i < len(domains); i++ {
		domain, key, cert := domains[i], keys[i], certs[i]

		if m, err = https.NewSelfSignManager(cert, key); err != nil {
			return nil, oe.Wrapf(err, "create cert for %v by %v, %v", domain, cert, key)
		} else {
			v.certs[domain] = m
		}
	}

	return v, nil
}

func (v *certsManager) GetCertificate(clientHello *tls.ClientHelloInfo) (*tls.Certificate, error) {
	if cert, ok := v.certs[clientHello.ServerName]; ok {
		return cert.GetCertificate(clientHello)
	}

	return nil, fmt.Errorf("no cert for %v", clientHello.ServerName)
}
