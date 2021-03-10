// The MIT License (MIT)
//
// Copyright (c) 2021 srs-bench(ossrs)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
package srs

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"github.com/pion/transport/vnet"
	"github.com/pion/webrtc/v3"
	"github.com/pion/webrtc/v3/pkg/media/h264reader"
	"io/ioutil"
	"net"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"
)

func apiRtcRequest(ctx context.Context, apiPath, r, offer string) (string, error) {
	u, err := url.Parse(r)
	if err != nil {
		return "", errors.Wrapf(err, "Parse url %v", r)
	}

	// Build api url.
	host := u.Host
	if !strings.Contains(host, ":") {
		host += ":1985"
	}

	api := fmt.Sprintf("http://%v", host)
	if !strings.HasPrefix(apiPath, "/") {
		api += "/"
	}
	api += apiPath

	if !strings.HasSuffix(apiPath, "/") {
		api += "/"
	}
	if u.RawQuery != "" {
		api += "?" + u.RawQuery
	}

	// Build JSON body.
	reqBody := struct {
		Api       string `json:"api"`
		ClientIP  string `json:"clientip"`
		SDP       string `json:"sdp"`
		StreamURL string `json:"streamurl"`
	}{
		api, "", offer, r,
	}

	b, err := json.Marshal(reqBody)
	if err != nil {
		return "", errors.Wrapf(err, "Marshal body %v", reqBody)
	}
	logger.If(ctx, "Request url api=%v with %v", api, string(b))
	logger.Tf(ctx, "Request url api=%v with %v bytes", api, len(b))

	req, err := http.NewRequest("POST", api, strings.NewReader(string(b)))
	if err != nil {
		return "", errors.Wrapf(err, "HTTP request %v", string(b))
	}

	res, err := http.DefaultClient.Do(req.WithContext(ctx))
	if err != nil {
		return "", errors.Wrapf(err, "Do HTTP request %v", string(b))
	}

	b2, err := ioutil.ReadAll(res.Body)
	if err != nil {
		return "", errors.Wrapf(err, "Read response for %v", string(b))
	}
	logger.If(ctx, "Response from %v is %v", api, string(b2))
	logger.Tf(ctx, "Response from %v is %v bytes", api, len(b2))

	resBody := struct {
		Code    int    `json:"code"`
		Session string `json:"sessionid"`
		SDP     string `json:"sdp"`
	}{}
	if err := json.Unmarshal(b2, &resBody); err != nil {
		return "", errors.Wrapf(err, "Marshal %v", string(b2))
	}

	if resBody.Code != 0 {
		return "", errors.Errorf("Server fail code=%v %v", resBody.Code, string(b2))
	}
	logger.If(ctx, "Parse response to code=%v, session=%v, sdp=%v",
		resBody.Code, resBody.Session, escapeSDP(resBody.SDP))
	logger.Tf(ctx, "Parse response to code=%v, session=%v, sdp=%v bytes",
		resBody.Code, resBody.Session, len(resBody.SDP))

	return string(resBody.SDP), nil
}

func escapeSDP(sdp string) string {
	return strings.ReplaceAll(strings.ReplaceAll(sdp, "\r", "\\r"), "\n", "\\n")
}

func packageAsSTAPA(frames ...*h264reader.NAL) *h264reader.NAL {
	first := frames[0]

	buf := bytes.Buffer{}
	buf.WriteByte(
		byte(first.RefIdc<<5)&0x60 | byte(24), // STAP-A
	)

	for _, frame := range frames {
		buf.WriteByte(byte(len(frame.Data) >> 8))
		buf.WriteByte(byte(len(frame.Data)))
		buf.Write(frame.Data)
	}

	return &h264reader.NAL{
		PictureOrderCount: first.PictureOrderCount,
		ForbiddenZeroBit:  false,
		RefIdc:            first.RefIdc,
		UnitType:          h264reader.NalUnitType(24), // STAP-A
		Data:              buf.Bytes(),
	}
}

type wallClock struct {
	start    time.Time
	duration time.Duration
}

func newWallClock() *wallClock {
	return &wallClock{start: time.Now()}
}

func (v *wallClock) Tick(d time.Duration) time.Duration {
	v.duration += d

	wc := time.Now().Sub(v.start)
	re := v.duration - wc
	if re > 30*time.Millisecond {
		return re
	}
	return 0
}

// Set to active, as DTLS client, to start ClientHello.
func testUtilSetupActive(s *webrtc.SessionDescription) error {
	if strings.Contains(s.SDP, "setup:passive") {
		return errors.New("set to active")
	}

	s.SDP = strings.ReplaceAll(s.SDP, "setup:actpass", "setup:active")
	return nil
}

// Set to passive, as DTLS client, to start ClientHello.
func testUtilSetupPassive(s *webrtc.SessionDescription) error {
	if strings.Contains(s.SDP, "setup:active") {
		return errors.New("set to passive")
	}

	s.SDP = strings.ReplaceAll(s.SDP, "setup:actpass", "setup:passive")
	return nil
}

// Parse address from SDP.
// candidate:0 1 udp 2130706431 192.168.3.8 8000 typ host generation 0
func parseAddressOfCandidate(answerSDP string) (*net.UDPAddr, error) {
	answer := webrtc.SessionDescription{Type: webrtc.SDPTypeAnswer, SDP: answerSDP}
	answerObject, err := answer.Unmarshal()
	if err != nil {
		return nil, errors.Wrapf(err, "unmarshal answer %v", answerSDP)
	}

	if len(answerObject.MediaDescriptions) == 0 {
		return nil, errors.New("no media")
	}

	candidate, ok := answerObject.MediaDescriptions[0].Attribute("candidate")
	if !ok {
		return nil, errors.New("no candidate")
	}

	// candidate:0 1 udp 2130706431 192.168.3.8 8000 typ host generation 0
	attrs := strings.Split(candidate, " ")
	if len(attrs) <= 6 {
		return nil, errors.Errorf("no address in %v", candidate)
	}

	// Parse ip and port from answer.
	ip := attrs[4]
	port, err := strconv.Atoi(attrs[5])
	if err != nil {
		return nil, errors.Wrapf(err, "invalid port %v", candidate)
	}

	address := fmt.Sprintf("%v:%v", ip, port)
	addr, err := net.ResolveUDPAddr("udp4", address)
	if err != nil {
		return nil, errors.Wrapf(err, "parse %v", address)
	}

	return addr, nil
}

// Filter the test error, ignore context.Canceled
func filterTestError(errs ...error) error {
	var filteredErrors []error

	for _, err := range errs {
		if err == nil || errors.Cause(err) == context.Canceled {
			continue
		}
		filteredErrors = append(filteredErrors, err)
	}

	if len(filteredErrors) == 0 {
		return nil
	}
	if len(filteredErrors) == 1 {
		return filteredErrors[0]
	}

	var descs []string
	for i, err := range filteredErrors[1:] {
		descs = append(descs, fmt.Sprintf("err #%d, %+v", i, err))
	}
	return errors.Wrapf(filteredErrors[0], "with %v", strings.Join(descs, ","))
}

// For STUN packet, 0x00 is binding request, 0x01 is binding success response.
// @see srs_is_stun of https://github.com/ossrs/srs
func srsIsStun(b []byte) bool {
	return len(b) > 0 && (b[0] == 0 || b[0] == 1)
}

// change_cipher_spec(20), alert(21), handshake(22), application_data(23)
// @see https://tools.ietf.org/html/rfc2246#section-6.2.1
// @see srs_is_dtls of https://github.com/ossrs/srs
func srsIsDTLS(b []byte) bool {
	return (len(b) >= 13 && (b[0] > 19 && b[0] < 64))
}

// For RTP or RTCP, the V=2 which is in the high 2bits, 0xC0 (1100 0000)
// @see srs_is_rtp_or_rtcp of https://github.com/ossrs/srs
func srsIsRTPOrRTCP(b []byte) bool {
	return (len(b) >= 12 && (b[0]&0xC0) == 0x80)
}

// For RTCP, PT is [128, 223] (or without marker [0, 95]).
// Literally, RTCP starts from 64 not 0, so PT is [192, 223] (or without marker [64, 95]).
// @note For RTP, the PT is [96, 127], or [224, 255] with marker.
// @see srs_is_rtcp of https://github.com/ossrs/srs
func srsIsRTCP(b []byte) bool {
	return (len(b) >= 12) && (b[0]&0x80) != 0 && (b[1] >= 192 && b[1] <= 223)
}

type ChunkType int

const (
	ChunkTypeICE ChunkType = iota + 1
	ChunkTypeDTLS
	ChunkTypeRTP
	ChunkTypeRTCP
)

func (v ChunkType) String() string {
	switch v {
	case ChunkTypeICE:
		return "ICE"
	case ChunkTypeDTLS:
		return "DTLS"
	case ChunkTypeRTP:
		return "RTP"
	case ChunkTypeRTCP:
		return "RTCP"
	default:
		return "Unknown"
	}
}

type DTLSContentType int

const (
	DTLSContentTypeHandshake        DTLSContentType = 22
	DTLSContentTypeChangeCipherSpec DTLSContentType = 20
	DTLSContentTypeAlert            DTLSContentType = 21
)

func (v DTLSContentType) String() string {
	switch v {
	case DTLSContentTypeHandshake:
		return "Handshake"
	case DTLSContentTypeChangeCipherSpec:
		return "ChangeCipherSpec"
	default:
		return "Unknown"
	}
}

type DTLSHandshakeType int

const (
	DTLSHandshakeTypeClientHello        DTLSHandshakeType = 1
	DTLSHandshakeTypeServerHello        DTLSHandshakeType = 2
	DTLSHandshakeTypeCertificate        DTLSHandshakeType = 11
	DTLSHandshakeTypeServerKeyExchange  DTLSHandshakeType = 12
	DTLSHandshakeTypeCertificateRequest DTLSHandshakeType = 13
	DTLSHandshakeTypeServerDone         DTLSHandshakeType = 14
	DTLSHandshakeTypeCertificateVerify  DTLSHandshakeType = 15
	DTLSHandshakeTypeClientKeyExchange  DTLSHandshakeType = 16
	DTLSHandshakeTypeFinished           DTLSHandshakeType = 20
)

func (v DTLSHandshakeType) String() string {
	switch v {
	case DTLSHandshakeTypeClientHello:
		return "ClientHello"
	case DTLSHandshakeTypeServerHello:
		return "ServerHello"
	case DTLSHandshakeTypeCertificate:
		return "Certificate"
	case DTLSHandshakeTypeServerKeyExchange:
		return "ServerKeyExchange"
	case DTLSHandshakeTypeCertificateRequest:
		return "CertificateRequest"
	case DTLSHandshakeTypeServerDone:
		return "ServerDone"
	case DTLSHandshakeTypeCertificateVerify:
		return "CertificateVerify"
	case DTLSHandshakeTypeClientKeyExchange:
		return "ClientKeyExchange"
	case DTLSHandshakeTypeFinished:
		return "Finished"
	default:
		return "Unknown"
	}
}

type ChunkMessageType struct {
	chunk     ChunkType
	content   DTLSContentType
	handshake DTLSHandshakeType
}

func (v *ChunkMessageType) String() string {
	if v.chunk == ChunkTypeDTLS {
		return fmt.Sprintf("%v-%v-%v", v.chunk, v.content, v.handshake)
	}
	return fmt.Sprintf("%v", v.chunk)
}

func NewChunkMessageType(c vnet.Chunk) (*ChunkMessageType, bool) {
	b := c.UserData()

	if len(b) == 0 {
		return nil, false
	}

	v := &ChunkMessageType{}

	if srsIsRTPOrRTCP(b) {
		if srsIsRTCP(b) {
			v.chunk = ChunkTypeRTCP
		} else {
			v.chunk = ChunkTypeRTP
		}
		return v, true
	}

	if srsIsStun(b) {
		v.chunk = ChunkTypeICE
		return v, true
	}

	if !srsIsDTLS(b) {
		return nil, false
	}

	v.chunk, v.content = ChunkTypeDTLS, DTLSContentType(b[0])
	if v.content != DTLSContentTypeHandshake {
		return v, true
	}

	if len(b) < 14 {
		return v, false
	}
	v.handshake = DTLSHandshakeType(b[13])
	return v, true
}

func (v *ChunkMessageType) IsHandshake() bool {
	return v.chunk == ChunkTypeDTLS && v.content == DTLSContentTypeHandshake
}

func (v *ChunkMessageType) IsClientHello() bool {
	return v.chunk == ChunkTypeDTLS && v.content == DTLSContentTypeHandshake && v.handshake == DTLSHandshakeTypeClientHello
}

func (v *ChunkMessageType) IsServerHello() bool {
	return v.chunk == ChunkTypeDTLS && v.content == DTLSContentTypeHandshake && v.handshake == DTLSHandshakeTypeServerHello
}

func (v *ChunkMessageType) IsCertificate() bool {
	return v.chunk == ChunkTypeDTLS && v.content == DTLSContentTypeHandshake && v.handshake == DTLSHandshakeTypeCertificate
}

func (v *ChunkMessageType) IsChangeCipherSpec() bool {
	return v.chunk == ChunkTypeDTLS && v.content == DTLSContentTypeChangeCipherSpec
}

type DTLSRecord struct {
	ContentType    DTLSContentType
	Version        uint16
	Epoch          uint16
	SequenceNumber uint64
	Length         uint16
	Data           []byte
}

func NewDTLSRecord(b []byte) (*DTLSRecord, error) {
	v := &DTLSRecord{}
	return v, v.Unmarshal(b)
}

func (v *DTLSRecord) String() string {
	return fmt.Sprintf("epoch=%v, sequence=%v", v.Epoch, v.SequenceNumber)
}

func (v *DTLSRecord) Equals(p *DTLSRecord) bool {
	return v.Epoch == p.Epoch && v.SequenceNumber == p.SequenceNumber
}

func (v *DTLSRecord) Unmarshal(b []byte) error {
	if len(b) < 13 {
		return errors.Errorf("requires 13B only %v", len(b))
	}

	v.ContentType = DTLSContentType(uint8(b[0]))
	v.Version = uint16(b[1])<<8 | uint16(b[2])
	v.Epoch = uint16(b[3])<<8 | uint16(b[4])
	v.SequenceNumber = uint64(b[5])<<40 | uint64(b[6])<<32 | uint64(b[7])<<24 | uint64(b[8])<<16 | uint64(b[9])<<8 | uint64(b[10])
	v.Length = uint16(b[11])<<8 | uint16(b[12])
	v.Data = b[13:]
	return nil
}
