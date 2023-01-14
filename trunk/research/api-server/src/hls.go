package main

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"net/url"
	"strings"
)

/*
   for SRS hook: on_hls_notify
   on_hls_notify:
       when srs reap a ts file of hls, call this hook,
       used to push file to cdn network, by get the ts file from cdn network.
       so we use HTTP GET and use the variable following:
             [app], replace with the app.
             [stream], replace with the stream.
             [param], replace with the param.
             [ts_url], replace with the ts url.
       ignore any return data of server.

    for SRS hook: on_hls
    on_hls:
        when srs reap a dvr file, call the hook,
        the request in the POST data string is a object encode by json:
              {
                  "action": "on_dvr",
                  "client_id": "9308h583",
                  "ip": "192.168.1.10",
                  "vhost": "video.test.com",
                  "app": "live",
                  "stream": "livestream",
				  "param":"?token=xxx&salt=yyy",
                  "duration": 9.68, // in seconds
                  "cwd": "/usr/local/srs",
                  "file": "./objs/nginx/html/live/livestream.1420254068776-100.ts",
                  "seq_no": 100
              }
    if valid, the hook must return HTTP code 200(Stauts OK) and response
    an int value specifies the error code(0 corresponding to success):
          0
*/

type HlsMsg struct {
	Action string `json:"action"`
	ClientId string `json:"client_id"`
	Ip string `json:"ip"`
	Vhost string `json:"vhost"`
	App string `json:"app"`
	Stream string `json:"stream"`
	Param string `json:"param"`
	Duration float64 `json:"duration"`
	Cwd string `json:"cwd"`
	File string `json:"file"`
	SeqNo int `json:"seq_no"`
}

func (v *HlsMsg) String() string {
	return fmt.Sprintf("srs %v: client id=%v, ip=%v, vhost=%v, app=%v, stream=%v, param=%v, duration=%v, cwd=%v, file=%v, seq_no=%v", v.Action, v.ClientId, v.Ip, v.Vhost, v.App, v.Stream, v.Param, v.Duration, v.Cwd, v.File, v.SeqNo)
}

type Hls struct {}

func (v *Hls) Parse(body []byte) (se *SrsError) {
	msg := &HlsMsg{}
	if err := json.Unmarshal(body, msg); err != nil {
		return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse hls msg failed, err is %v", err.Error())}
	}
	log.Println(msg)
	return nil
}

// handle the hls requests: hls stream.
func HlsServe(w http.ResponseWriter, r *http.Request) {
	log.Println(fmt.Sprintf("hls serve, uPath=%v", r.URL.Path))
	if r.Method == "GET" {
		subPath := r.URL.Path[len("/api/v1/hls/"):]
		res := struct {
			Args []string `json:"args"`
			KwArgs url.Values `json:"kwargs"`
		}{
			Args: strings.Split(subPath, "/"),
			KwArgs: r.URL.Query(),
		}
		body, _ := json.Marshal(res)
		w.Write(body)
	} else if r.Method == "POST" {
		body, err := ioutil.ReadAll(r.Body)
		if err != nil {
			Response(&SrsError{Code: error_system_read_request, Data: fmt.Sprintf("read request body failed, err is %v", err)}).ServeHTTP(w, r)
			return
		}
		log.Println(fmt.Sprintf("post to hls, req=%v", string(body)))
		c := &Hls{}
		if se := c.Parse(body); se != nil {
			Response(se).ServeHTTP(w, r)
			return
		}
		Response(&SrsError{Code: 0, Data: nil}).ServeHTTP(w, r)
		return
	}
}
