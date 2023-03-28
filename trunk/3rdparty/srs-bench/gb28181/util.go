// The MIT License (MIT)
//
// # Copyright (c) 2022 Winlin
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
package gb28181

import (
	"bufio"
	"context"
	"flag"
	"fmt"
	"github.com/ghettovoice/gosip/sip"
	"github.com/ossrs/go-oryx-lib/aac"
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/yapingcat/gomedia/mpeg2"
	"io"
	"net"
	"net/url"
	"os"
	"path"
	"strings"
	"time"
)

var srsLog *bool

var srsTimeout *int
var srsPublishVideoFps *int

var srsSipAddr *string
var srsSipUser *string
var srsSipRandomID *int
var srsSipDomain *string
var srsSipSvrID *string

var srsMediaTimeout *int
var srsReinviteTimeout *int
var srsPublishAudio *string
var srsPublishVideo *string

func prepareTest() (err error) {
	srsSipAddr = flag.String("srs-sip", "tcp://127.0.0.1:5060", "The SRS GB server to connect to")
	srsSipUser = flag.String("srs-stream", "3402000000", "The GB user/stream to publish")
	srsSipRandomID = flag.Int("srs-random", 10, "The GB user/stream random suffix to publish")
	srsSipDomain = flag.String("srs-domain", "3402000000", "The GB SIP domain")
	srsSipSvrID = flag.String("srs-server", "34020000002000000001", "The GB server ID for SIP")
	srsLog = flag.Bool("srs-log", false, "Whether enable the detail log")
	srsTimeout = flag.Int("srs-timeout", 11000, "For each case, the timeout in ms")
	srsMediaTimeout = flag.Int("srs-media-timeout", 2100, "PS media disconnect timeout in ms")
	srsReinviteTimeout = flag.Int("srs-reinvite-timeout", 1200, "When disconnect, SIP re-invite timeout in ms")
	srsPublishAudio = flag.String("srs-publish-audio", "avatar.aac", "The audio file for publisher.")
	srsPublishVideo = flag.String("srs-publish-video", "avatar.h264", "The video file for publisher. Note that *.h264 is for AVC, *.h265 is for HEVC.")
	srsPublishVideoFps = flag.Int("srs-publish-video-fps", 25, "The video fps for publisher.")

	// Should parse it first.
	flag.Parse()

	// Check file.
	tryOpenFile := func(filename string) (string, error) {
		if filename == "" {
			return filename, nil
		}

		f, err := os.Open(filename)
		if err != nil {
			nfilename := path.Join("../", filename)
			f2, err := os.Open(nfilename)
			if err != nil {
				return filename, errors.Wrapf(err, "No video file at %v or %v", filename, nfilename)
			}
			defer f2.Close()

			return nfilename, nil
		}
		defer f.Close()

		return filename, nil
	}

	if *srsPublishVideo, err = tryOpenFile(*srsPublishVideo); err != nil {
		return err
	}

	if *srsPublishAudio, err = tryOpenFile(*srsPublishAudio); err != nil {
		return err
	}

	return nil
}

type GBTestSession struct {
	session *GBSession
}

func NewGBTestSession() *GBTestSession {
	sipConfig := SIPConfig{
		addr:   *srsSipAddr,
		domain: *srsSipDomain,
		user:   *srsSipUser,
		random: *srsSipRandomID,
		server: *srsSipSvrID,
	}
	return &GBTestSession{
		session: NewGBSession(&GBSessionConfig{
			regTimeout:    time.Duration(*srsTimeout) * 5 * time.Minute,
			inviteTimeout: time.Duration(*srsTimeout) * 5 * time.Minute,
		}, &sipConfig),
	}
}

func (v *GBTestSession) Close() error {
	v.session.Close()
	return nil
}

func (v *GBTestSession) Run(ctx context.Context) (err error) {
	if err = v.session.Connect(ctx); err != nil {
		return errors.Wrap(err, "connect")
	}
	if err = v.session.Register(ctx); err != nil {
		return errors.Wrap(err, "register")
	}
	if err = v.session.Invite(ctx); err != nil {
		return errors.Wrap(err, "invite")
	}

	return nil
}

type GBTestPublisher struct {
	session  *GBSession
	ingester *PSIngester
}

func NewGBTestPublisher() *GBTestPublisher {
	sipConfig := SIPConfig{
		addr:   *srsSipAddr,
		domain: *srsSipDomain,
		user:   *srsSipUser,
		random: *srsSipRandomID,
		server: *srsSipSvrID,
	}
	psConfig := PSConfig{
		video: *srsPublishVideo,
		fps:   *srsPublishVideoFps,
		audio: *srsPublishAudio,
	}
	return &GBTestPublisher{
		session: NewGBSession(&GBSessionConfig{
			regTimeout:    time.Duration(*srsTimeout) * 5 * time.Minute,
			inviteTimeout: time.Duration(*srsTimeout) * 5 * time.Minute,
		}, &sipConfig),
		ingester: NewPSIngester(&IngesterConfig{
			psConfig: psConfig,
		}),
	}
}

func (v *GBTestPublisher) Close() error {
	v.ingester.Close()
	v.session.Close()
	return nil
}

func (v *GBTestPublisher) Run(ctx context.Context) (err error) {
	if err = v.session.Connect(ctx); err != nil {
		return errors.Wrap(err, "connect")
	}
	if err = v.session.Register(ctx); err != nil {
		return errors.Wrap(err, "register")
	}
	if err = v.session.Invite(ctx); err != nil {
		return errors.Wrap(err, "invite")
	}

	serverAddr, err := utilBuildMediaAddr(v.session.sip.conf.addr, v.session.out.mediaPort)
	if err != nil {
		return errors.Wrap(err, "parse")
	}
	v.ingester.conf.serverAddr = serverAddr

	v.ingester.conf.ssrc = uint32(v.session.out.ssrc)
	v.ingester.conf.clockRate = v.session.out.clockRate
	v.ingester.conf.payloadType = uint8(v.session.out.payloadType)

	if err := v.ingester.Ingest(ctx); err != nil {
		return errors.Wrap(err, "ingest")
	}

	return nil
}

// Filter the test error, ignore context.Canceled
func filterTestError(errs ...error) error {
	var filteredErrors []error

	for _, err := range errs {
		if err == nil || errors.Cause(err) == context.Canceled {
			continue
		}

		// If url error, server maybe error, do not print the detail log.
		if r0 := errors.Cause(err); r0 != nil {
			if r1, ok := r0.(*url.Error); ok {
				err = r1
			}
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

func sipGetCallID(m sip.Message) string {
	if v, ok := m.CallID(); !ok {
		return ""
	} else {
		return v.Value()
	}
}

func utilBuildMediaAddr(addr string, mediaPort int64) (string, error) {
	if u, err := url.Parse(addr); err != nil {
		return "", errors.Wrapf(err, "parse %v", addr)
	} else if addr, err := net.ResolveTCPAddr(u.Scheme, u.Host); err != nil {
		return "", errors.Wrapf(err, "parse %v scheme=%v, host=%v", addr, u.Scheme, u.Host)
	} else {
		return fmt.Sprintf("%v://%v:%v",
			u.Scheme, addr.IP.String(), mediaPort,
		), nil
	}
}

// See SrsMpegPES::decode
func utilUpdatePesPacketLength(pes *mpeg2.PesPacket) {
	var nb_required int
	if pes.PTS_DTS_flags == 0x2 {
		nb_required += 5
	}
	if pes.PTS_DTS_flags == 0x3 {
		nb_required += 10
	}
	if pes.ESCR_flag > 0 {
		nb_required += 6
	}
	if pes.ES_rate_flag > 0 {
		nb_required += 3
	}
	if pes.DSM_trick_mode_flag > 0 {
		nb_required += 1
	}
	if pes.Additional_copy_info_flag > 0 {
		nb_required += 1
	}
	if pes.PES_CRC_flag > 0 {
		nb_required += 2
	}
	if pes.PES_extension_flag > 0 {
		nb_required += 1
	}

	// Size before PES_header_data_length.
	const fixed = uint16(3)
	// Size after PES_header_data_length.
	pes.PES_header_data_length = uint8(nb_required)
	// Size after PES_packet_length
	pes.PES_packet_length = uint16(len(pes.Pes_payload)) + fixed + uint16(pes.PES_header_data_length)
}

type AACReader struct {
	codec aac.ADTS
	r     *bufio.Reader
}

func NewAACReader(f io.Reader) (*AACReader, error) {
	v := &AACReader{}

	var err error
	if v.codec, err = aac.NewADTS(); err != nil {
		return nil, err
	}

	v.r = bufio.NewReaderSize(f, 4096)
	b, err := v.r.Peek(7 + 1024)
	if err != nil {
		return nil, err
	}

	if _, _, err = v.codec.Decode(b); err != nil {
		return nil, err
	}

	return v, nil
}

func (v *AACReader) NextADTSFrame() ([]byte, error) {
	b, err := v.r.Peek(7 + 1024)
	if err != nil {
		return nil, err
	}

	_, left, err := v.codec.Decode(b)
	if err != nil {
		return nil, err
	}

	adts := b[:len(b)-len(left)]
	if _, err = v.r.Discard(len(adts)); err != nil {
		return nil, err
	}

	return adts, nil
}
