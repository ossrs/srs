// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package flexfec

// FecOption can be used to set initial options on Fec encoder interceptors.
type FecOption func(d *FecInterceptor) error

// NumMediaPackets sets the number of media packets to accumulate before generating another FEC packets batch.
func NumMediaPackets(numMediaPackets uint32) FecOption {
	return func(f *FecInterceptor) error {
		f.numMediaPackets = numMediaPackets

		return nil
	}
}

// NumFECPackets sets the number of FEC packets to generate for each batch of media packets.
func NumFECPackets(numFecPackets uint32) FecOption {
	return func(f *FecInterceptor) error {
		f.numFecPackets = numFecPackets

		return nil
	}
}

// FECEncoderFactory sets the custom factory for constructing the FEC Encoders.
func FECEncoderFactory(factory EncoderFactory) FecOption {
	return func(f *FecInterceptor) error {
		f.encoderFactory = factory

		return nil
	}
}
