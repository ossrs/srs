package main

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
)

/*
handle the clients requests: connect/disconnect vhost/app.
    for SRS hook: on_connect/on_close
    on_connect:
        when client connect to vhost/app, call the hook,
        the request in the POST data string is a object encode by json:
              {
                  "action": "on_connect",
                  "client_id": "9308h583",
                  "ip": "192.168.1.10",
    			  "vhost": "video.test.com",
                  "app": "live",
                  "tcUrl": "rtmp://video.test.com/live?key=d2fa801d08e3f90ed1e1670e6e52651a",
                  "pageUrl": "http://www.test.com/live.html"
              }
    on_close:
        when client close/disconnect to vhost/app/stream, call the hook,
        the request in the POST data string is a object encode by json:
              {
                  "action": "on_close",
                  "client_id": "9308h583",
                  "ip": "192.168.1.10",
				  "vhost": "video.test.com",
                  "app": "live",
                  "send_bytes": 10240,
				  "recv_bytes": 10240
              }
    if valid, the hook must return HTTP code 200(Stauts OK) and response
    an int value specifies the error code(0 corresponding to success):
          0
*/
type ClientMsg struct {
	Action string `json:"action"`
	ClientId string `json:"client_id"`
	Ip string `json:"ip"`
	Vhost string `json:"vhost"`
	App string `json:"app"`
}

type ClientOnConnectMsg struct {
	ClientMsg
	TcUrl string `json:"tcUrl"`
	PageUrl string `json:"pageUrl"`
}

func (v *ClientOnConnectMsg) String() string {
	return fmt.Sprintf("srs:%v, client id=%v, ip=%v, vhost=%v, app=%v, tcUrl=%v, pageUrl=%v", v.Action, v.ClientId, v.Ip, v.Vhost, v.App, v.TcUrl, v.PageUrl)
}

type ClientOnCloseMsg struct {
	ClientMsg
	SendBytes int64 `json:"send_bytes"`
	RecvBytes int64 `json:"recv_bytes"`
}

func (v *ClientOnCloseMsg) String() string {
	return fmt.Sprintf("srs:%v, client id=%v, ip=%v, vhost=%v, app=%v, send_bytes=%v, recv_bytes=%v", v.Action, v.ClientId, v.Ip, v.Vhost, v.App, v.SendBytes, v.RecvBytes)
}

type Client struct {}

func (v *Client) Parse(body []byte) (se *SrsError) {
	data := &struct {
		Action string `json:"action"`
	}{}
	if err := json.Unmarshal(body, data); err != nil {
		return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse client action failed, err is %v", err.Error())}
	}

	if data.Action == client_action_on_connect {
		msg := &ClientOnConnectMsg{}
		if err := json.Unmarshal(body, msg); err != nil {
			return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse client %v msg failed, err is %v", client_action_on_connect, err.Error())}
		}
		log.Println(msg)
	} else if data.Action == client_action_on_close {
		msg := &ClientOnCloseMsg{}
		if err := json.Unmarshal(body, msg); err != nil {
			return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse client %v msg failed, err is %v", client_action_on_close, err.Error())}
		}
		log.Println(msg)
	}
	return nil
}

// handle the clients requests: connect/disconnect vhost/app.
func ClientServe(w http.ResponseWriter, r *http.Request)  {
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
		log.Println(fmt.Sprintf("post to clients, req=%v", string(body)))
		c := &Client{}
		if se := c.Parse(body); se != nil {
			Response(se).ServeHTTP(w, r)
			return
		}
		Response(&SrsError{Code: 0, Data: nil}).ServeHTTP(w, r)
		return
	}
}
