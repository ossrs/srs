package main

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
)

/*
   for SRS hook: on_publish/on_unpublish
   on_publish:
       when client(encoder) publish to vhost/app/stream, call the hook,
       the request in the POST data string is a object encode by json:
             {
                 "action": "on_publish",
                 "client_id": "9308h583",
                 "ip": "192.168.1.10",
				 "vhost": "video.test.com",
				 "app": "live",
                 "stream": "livestream",
				 "param":"?token=xxx&salt=yyy"
             }
   on_unpublish:
       when client(encoder) stop publish to vhost/app/stream, call the hook,
       the request in the POST data string is a object encode by json:
             {
                 "action": "on_unpublish",
                 "client_id": "9308h583",
                 "ip": "192.168.1.10",
				 "vhost": "video.test.com",
                 "app": "live",
                 "stream": "livestream",
				 "param":"?token=xxx&salt=yyy"
             }
   if valid, the hook must return HTTP code 200(Stauts OK) and response
   an int value specifies the error code(0 corresponding to success):
         0
*/
type StreamMsg struct {
	Action string `json:"action"`
	ClientId string `json:"client_id"`
	Ip string `json:"ip"`
	Vhost string `json:"vhost"`
	App string `json:"app"`
	Stream string `json:"stream"`
	Param string `json:"param"`
}

func (v *StreamMsg) String() string {
	return fmt.Sprintf("srs %v: client id=%v, ip=%v, vhost=%v, app=%v, stream=%v, param=%v", v.Action, v.ClientId, v.Ip, v.Vhost, v.App, v.Stream, v.Param)
}

type Stream struct {}

func (v *Stream) Parse(body []byte) (se *SrsError) {
	msg := &StreamMsg{}
	if err := json.Unmarshal(body, msg); err != nil {
		return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse stream msg failed, err is %v", err.Error())}
	}
	log.Println(msg)
	return nil
}

// handle the streams requests: publish/unpublish stream.

func StreamServe(w http.ResponseWriter, r *http.Request) {
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
		log.Println(fmt.Sprintf("post to streams, req=%v", string(body)))
		c := &Stream{}
		if se := c.Parse(body); se != nil {
			Response(se).ServeHTTP(w, r)
			return
		}
		Response(&SrsError{Code: 0, Data: nil}).ServeHTTP(w, r)
		return
	}
}
