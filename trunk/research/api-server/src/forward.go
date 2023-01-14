package main

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
)

/*
handle the forward requests: dynamic forward url.
for SRS hook: on_forward
on_forward:
	when srs reap a dvr file, call the hook,
	the request in the POST data string is a object encode by json:
		  {
			  "action": "on_forward",
			  "server_id": "server_test",
			  "client_id": 1985,
			  "ip": "192.168.1.10",
			  "vhost": "video.test.com",
			  "app": "live",
			  "tcUrl": "rtmp://video.test.com/live?key=d2fa801d08e3f90ed1e1670e6e52651a",
			  "stream": "livestream",
			  "param":"?token=xxx&salt=yyy"
		  }
if valid, the hook must return HTTP code 200(Stauts OK) and response
an int value specifies the error code(0 corresponding to success):
	  0
*/

type ForwardMsg struct {
	Action string `json:"action"`
	ServerId string `json:"server_id"`
	ClientId string `json:"client_id"`
	Ip string `json:"ip"`
	Vhost string `json:"vhost"`
	App string `json:"app"`
	TcUrl string `json:"tc_url"`
	Stream string `json:"stream"`
	Param string `json:"param"`
}

func (v *ForwardMsg) String() string {
	return fmt.Sprintf("srs %v: client id=%v, ip=%v, vhost=%v, app=%v, tcUrl=%v, stream=%v, param=%v", v.Action, v.ClientId, v.Ip, v.Vhost, v.App, v.TcUrl, v.Stream, v.Param)
}

type Forward struct {}

/*
backend service config description:
   support multiple rtmp urls(custom addresses or third-party cdn service),
   url's host is slave service.
For example:
   ["rtmp://127.0.0.1:19350/test/teststream", "rtmp://127.0.0.1:19350/test/teststream?token=xxxx"]
*/
func (v *Forward) Parse(body []byte) (se *SrsError) {
	msg := &DvrMsg{}
	if err := json.Unmarshal(body, msg); err != nil {
		return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse forward msg failed, err is %v", err.Error())}
	}
	if msg.Action == "on_forward" {
		log.Println(msg)
		res := &struct {
			Urls []string `json:"urls"`
		}{
			Urls: []string{"rtmp://127.0.0.1:19350/test/teststream"},
		}
		return &SrsError{Code: 0, Data: res}
	} else {
		return &SrsError{Code: error_request_invalid_action, Data: fmt.Sprintf("invalid action:%v", msg.Action)}
	}
	return
}

func ForwardServe(w http.ResponseWriter, r *http.Request)  {
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
		log.Println(fmt.Sprintf("post to forward, req=%v", string(body)))
		c := &Forward{}
		if se := c.Parse(body); se != nil {
			Response(se).ServeHTTP(w, r)
			return
		}
		Response(&SrsError{Code: 0, Data: nil}).ServeHTTP(w, r)
		return
	}
}
