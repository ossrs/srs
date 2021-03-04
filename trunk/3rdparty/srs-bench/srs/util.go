package srs

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"github.com/pion/webrtc/v3/pkg/media/h264reader"
	"io/ioutil"
	"net/http"
	"net/url"
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
