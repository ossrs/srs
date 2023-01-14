package main

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"sync"
	"time"
)

/*
# object fields:
# id: an int value indicates the id of user.
# username: a str indicates the user name.
# url: a str indicates the url of user stream.
# agent: a str indicates the agent of user.
# join_date: a number indicates the join timestamp in seconds.
# join_date_str: a str specifies the formated friendly time.
# heartbeat: a number indicates the heartbeat timestamp in seconds.
# vcodec: a dict indicates the video codec info.
# acodec: a dict indicates the audio codec info.

# dead time in seconds, if exceed, remove the chat.
*/

type Chat struct {
	Id int `json:"id"`
	Username string `json:"username"`
	Url string `json:"url"`
	JoinDate int64 `json:"join_date"`
	JoinDateStr string `json:"join_date_str"`
	Heartbeat int64 `json:"heartbeat"`
}

type ChatManager struct {
	globalId int
	chats *sync.Map
	deadTime int
}

func NewChatManager() *ChatManager {
	v := &ChatManager{
		globalId: 100,
		// key is globalId, value is chat
		chats: new(sync.Map),
		deadTime: 15,
	}
	return v
}

func (v *ChatManager) List() (chats []*Chat) {
	chats = []*Chat{}
	v.chats.Range(func(key, value any) bool {
		_, chat := key.(int), value.(*Chat)
		if (time.Now().Unix() - chat.Heartbeat) > int64(v.deadTime) {
			v.chats.Delete(key)
			return true
		}
		chats = append(chats, chat)
		return true
	})
	return
}

func (v *ChatManager) Update(id int) (se *SrsError) {
	value, ok := v.chats.Load(id)
	if !ok {
		return &SrsError{Code: error_chat_id_not_exist, Data: fmt.Sprintf("cannot find id:%v", id)}
	}
	c := value.(*Chat)
	c.Heartbeat = time.Now().Unix()
	log.Println(fmt.Sprintf("heartbeat chat success, id=%v", id))
	return nil
}

func (v *ChatManager) Delete(id int) (se *SrsError) {
	if _, ok := v.chats.Load(id); !ok {
		return &SrsError{Code: error_chat_id_not_exist, Data: fmt.Sprintf("cannot find id:%v", id)}
	}
	v.chats.Delete(id)
	log.Println(fmt.Sprintf("delete chat success, id=%v", id))
	return
}

func (v *ChatManager) Add(c *Chat) {
	c.Id = v.globalId
	now := time.Now()
	c.JoinDate, c.Heartbeat = now.Unix(), now.Unix()
	c.JoinDateStr = now.Format("2006-01-02 15:04:05")
	v.globalId += 1
	v.chats.Store(c.Id, c)
}

// the chat streams, public chat room.
func ChatServe(w http.ResponseWriter, r *http.Request) {
	log.Println(fmt.Sprintf("got a chat req, uPath=%v", r.URL.Path))
	if r.Method == "GET" {
		chats := cm.List()
		Response(&SrsError{Code: 0, Data: chats}).ServeHTTP(w, r)
	} else if r.Method == "POST" {
		body, err := ioutil.ReadAll(r.Body)
		if err != nil {
			Response(&SrsError{Code: error_system_read_request, Data: fmt.Sprintf("read request body failed, err is %v", err)}).ServeHTTP(w, r)
			return
		}
		c := &Chat{}
		if err := json.Unmarshal(body, c); err != nil {
			Response(&SrsError{Code: error_system_parse_json, Data: fmt.Sprintf("parse body to chat json failed, err is %v", err)})
			return
		}
		cm.Add(c)
		log.Println(fmt.Sprintf("create chat success, id=%v", c.Id))
		Response(&SrsError{Code: 0, Data: nil}).ServeHTTP(w, r)
	} else if r.Method == "PUT" {
		// TODO: parse id?
		Response(cm.Update(0)).ServeHTTP(w, r)
	} else if r.Method == "DELETE" {
		// TODO: parse id?
		Response(cm.Delete(0)).ServeHTTP(w, r)
	}
}
