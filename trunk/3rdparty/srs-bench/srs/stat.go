package srs

import (
	"context"
	"encoding/json"
	"github.com/ossrs/go-oryx-lib/logger"
	"net/http"
	"strings"
)

type statRTC struct {
	Publishers struct {
		Expect int `json:"expect"`
		Alive  int `json:"alive"`
	} `json:"publishers"`
	Subscribers struct {
		Expect int `json:"expect"`
		Alive  int `json:"alive"`
	} `json:"subscribers"`
	PeerConnection interface{} `json:"random-pc"`
}

var StatRTC statRTC

func HandleStat(ctx context.Context, mux *http.ServeMux, l string) {
	if strings.HasPrefix(l, ":") {
		l = "127.0.0.1" + l
	}

	logger.Tf(ctx, "Handle http://%v/api/v1/sb/rtc", l)
	mux.HandleFunc("/api/v1/sb/rtc", func(w http.ResponseWriter, r *http.Request) {
		res := &struct {
			Code int         `json:"code"`
			Data interface{} `json:"data"`
		}{
			0, &StatRTC,
		}

		b, err := json.Marshal(res)
		if err != nil {
			logger.Wf(ctx, "marshal %v err %+v", res, err)
			return
		}

		w.Write(b)
	})
}
