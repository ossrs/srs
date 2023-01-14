package main

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
)

/*
   for SRS hook: on_dvr
   on_dvr:
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
                 "cwd": "/usr/local/srs",
                 "file": "./objs/nginx/html/live/livestream.1420254068776.flv"
             }
   if valid, the hook must return HTTP code 200(Stauts OK) and response
   an int value specifies the error code(0 corresponding to success):
         0
*/

type DvrMsg struct {
	Action string `json:"action"`
	ClientId string `json:"client_id"`
	Ip string `json:"ip"`
	Vhost string `json:"vhost"`
	App string `json:"app"`
	Stream string `json:"stream"`
	Param string `json:"param"`
	Cwd string `json:"cwd"`
	File string `json:"file"`
}

func (v *DvrMsg) String() string {
	return fmt.Sprintf("srs %v: client id=%v, ip=%v, vhost=%v, app=%v, stream=%v, param=%v, cwd=%v, file=%v", v.Action, v.ClientId, v.Ip, v.Vhost, v.App, v.Stream, v.Param, v.Cwd, v.File)
}

type Dvr struct {}

func (v *Dvr) Parse(body []byte) (se *SrsError) {
	msg := &DvrMsg{}
	if err := json.Unmarshal(body, msg); err != nil {
		return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse dvr msg failed, err is %v", err.Error())}
	}
	log.Println(msg)
	return nil
}

// handle the dvrs requests: dvr stream.
func DvrServe(w http.ResponseWriter, r *http.Request) {
	if r.Method == "GET" {
		res := struct {}{}
		body, _ := json.Marshal(res)
		w.Write(body)
	} else if r.Method == "POST" {
		body, err := ioutil.ReadAll(r.Body)
		if err != nil {
			Response(&SrsError{Code: error_system_read_request, Data: fmt.Sprintf("read request body failed, err is %v", err)}).ServeHTTP(w, r)
			return
		}
		log.Println(fmt.Sprintf("post to dvrs, req=%v", string(body)))
		c := &Dvr{}
		if se := c.Parse(body); se != nil {
			Response(se).ServeHTTP(w, r)
			return
		}
		Response(&SrsError{Code: 0, Data: nil}).ServeHTTP(w, r)
		return
	}
}
