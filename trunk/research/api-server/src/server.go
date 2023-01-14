package main

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"
)

/*
the server list
*/

type ServerMsg struct {
	Ip string `json:"ip"`
	DeviceId string `json:"device_id"`
	Summaries interface{} `json:"summaries"`
	Devices interface{} `json:"devices"` //not used now
}

type ArmServer struct {
	Id string `json:"id"`
	ServerMsg
	PublicIp string `json:"public_ip"`
	Heartbeat int64 `json:"heartbeat"`
	HeartbeatH string `json:"heartbeat_h"`
	Api string `json:"api"`
	Console string `json:"console"`
}

func (v *ArmServer) Dead() bool {
	deadTimeSeconds := int64(20)
	if time.Now().Unix() - v.Heartbeat > deadTimeSeconds {
		return true
	}
	return false
}

type ServerManager struct {
	globalArmServerId int
	nodes *sync.Map // key is deviceId
	lastUpdateAt time.Time
}

func NewServerManager() *ServerManager {
	sm := &ServerManager{
		globalArmServerId: os.Getpid(),
		nodes: new(sync.Map),
		lastUpdateAt: time.Now(),
	}
	return sm
}

func (v *ServerManager) List(id string) (nodes []*ArmServer) {
	nodes = []*ArmServer{}
	// list nodes, remove dead node
	v.nodes.Range(func(key, value any) bool {
		node, _ := value.(*ArmServer)
		if node.Dead() {
			v.nodes.Delete(key)
			return true
		}
		if len(id) == 0 {
			nodes = append(nodes, node)
			return true
		}
		if id == node.Id || id == node.DeviceId {
			nodes = append(nodes, node)
			return true
		}
		return true
	})
	return
}

func (v *ServerManager) Parse(body []byte, r *http.Request) (se *SrsError) {
	msg := &ServerMsg{}
	if err := json.Unmarshal(body, msg); err != nil {
		return &SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse server msg failed, err is %v", err.Error())}
	}

	var node *ArmServer
	value, ok := v.nodes.Load(msg.DeviceId)
	if !ok {
		node = &ArmServer{}
		node.ServerMsg = *msg
		node.Id = strconv.Itoa(v.globalArmServerId)
		v.globalArmServerId += 1
	} else {
		node = value.(*ArmServer)
		if msg.Summaries != nil {
			node.Summaries = msg.Summaries
		}
		if msg.Devices != nil {
			node.Devices = msg.Devices
		}
	}
	node.PublicIp = r.RemoteAddr
	now := time.Now()
	node.Heartbeat = now.Unix()
	node.HeartbeatH = now.Format("2006-01-02 15:04:05")
	v.nodes.Store(msg.DeviceId, node)
	return nil
}

func ServerServe(w http.ResponseWriter, r *http.Request)  {
	uPath := r.URL.Path
	if r.Method == "GET" {
		index := strings.Index(uPath, "/api/v1/servers/")
		if index == -1 {
			Response(&SrsError{Code: 0, Data: sm.List("")}).ServeHTTP(w, r)
		} else {
			id := uPath[(index + len("/api/v1/servers/")):]
			Response(&SrsError{Code: 0, Data: sm.List(id)}).ServeHTTP(w, r)
		}
	} else if r.Method == "POST" {
		body, err := ioutil.ReadAll(r.Body)
		if err != nil {
			Response(&SrsError{Code: error_system_read_request, Data: fmt.Sprintf("read request body failed, err is %v", err)}).ServeHTTP(w, r)
			return
		}
		log.Println(fmt.Sprintf("post to nodes, req=%v", string(body)))

		if se := sm.Parse(body, r); se != nil {
			Response(se).ServeHTTP(w, r)
			return
		}
		Response(&SrsError{Code: 0, Data: nil}).ServeHTTP(w, r)
		return
	}

}