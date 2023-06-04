// The MIT License (MIT)
//
// # Copyright (c) 2021 Winlin
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
package janus

import (
	"context"
	"encoding/json"
	"fmt"
	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"io/ioutil"
	"net/http"
	"strings"
	"sync"
	"time"
)

type publisherInfo struct {
	AudioCodec string `json:"audio_codec"`
	Display    string `json:"display"`
	ID         uint64 `json:"id"`
	Talking    bool   `json:"talking"`
	VideoCodec string `json:"video_codec"`
}

func (v publisherInfo) String() string {
	return fmt.Sprintf("%v(codec:%v/%v,id:%v,talk:%v)",
		v.Display, v.VideoCodec, v.AudioCodec, v.ID, v.Talking)
}

type janusReply struct {
	transactionID string
	replies       chan []byte
}

func newJanusReply(tid string) *janusReply {
	return &janusReply{
		transactionID: tid,
		replies:       make(chan []byte, 1),
	}
}

type janusHandle struct {
	api *janusAPI
	// The ID created by API.
	handleID    uint64
	publisherID uint64
}

type janusAPI struct {
	// For example, http://localhost:8088/janus
	r string
	// The ID created by API.
	sessionID uint64 // By Create().
	privateID uint64 // By JoinAsPublisher().
	// The handles, key is handleID, value is *janusHandle
	handles sync.Map
	// The callbacks.
	onDetached    func(sender, sessionID uint64)
	onWebrtcUp    func(sender, sessionID uint64)
	onMedia       func(sender, sessionID uint64, mtype string, receiving bool)
	onSlowLink    func(sender, sessionID uint64, media string, lost uint64, uplink bool)
	onPublisher   func(sender, sessionID uint64, publishers []publisherInfo)
	onUnPublished func(sender, sessionID, id uint64)
	onLeave       func(sender, sessionID, id uint64)
	// The context for polling.
	pollingCtx    context.Context
	pollingCancel context.CancelFunc
	wg            sync.WaitGroup
	// The replies of polling key is transactionID, value is janusReply.
	replies sync.Map
}

func newJanusAPI(r string) *janusAPI {
	v := &janusAPI{r: r}
	if !strings.HasSuffix(r, "/") {
		v.r += "/"
	}
	v.onDetached = func(sender, sessionID uint64) {
	}
	v.onWebrtcUp = func(sender, sessionID uint64) {
	}
	v.onMedia = func(sender, sessionID uint64, mtype string, receiving bool) {
	}
	v.onSlowLink = func(sender, sessionID uint64, media string, lost uint64, uplink bool) {
	}
	v.onPublisher = func(sender, sessionID uint64, publishers []publisherInfo) {
	}
	v.onUnPublished = func(sender, sessionID, id uint64) {
	}
	v.onLeave = func(sender, sessionID, id uint64) {
	}
	return v
}

func (v *janusAPI) Close() error {
	v.pollingCancel()
	v.wg.Wait()
	return nil
}

func (v *janusAPI) Create(ctx context.Context) error {
	v.pollingCtx, v.pollingCancel = context.WithCancel(ctx)

	api := v.r

	reqBody := struct {
		Janus       string `json:"janus"`
		Transaction string `json:"transaction"`
	}{
		"create", newTransactionID(),
	}

	b, err := json.Marshal(reqBody)
	if err != nil {
		return errors.Wrapf(err, "Marshal body %v", reqBody)
	}
	logger.Tf(ctx, "Request url api=%v with %v", api, string(b))

	req, err := http.NewRequest("POST", api, strings.NewReader(string(b)))
	if err != nil {
		return errors.Wrapf(err, "HTTP request %v", string(b))
	}

	res, err := http.DefaultClient.Do(req.WithContext(ctx))
	if err != nil {
		return errors.Wrapf(err, "Do HTTP request %v", string(b))
	}

	b2, err := ioutil.ReadAll(res.Body)
	if err != nil {
		return errors.Wrapf(err, "Read response for %v", string(b))
	}

	s2 := escapeJSON(string(b2))
	logger.Tf(ctx, "Response from %v is %v", api, s2)

	resBody := struct {
		Janus       string `json:"janus"`
		Transaction string `json:"transaction"`
		Data        struct {
			ID uint64 `json:"id"`
		} `json:"data"`
	}{}
	if err := json.Unmarshal([]byte(s2), &resBody); err != nil {
		return errors.Wrapf(err, "Marshal %v", s2)
	}
	if resBody.Janus != "success" {
		return errors.Errorf("Server fail code=%v %v", resBody.Janus, s2)
	}

	v.sessionID = resBody.Data.ID
	logger.Tf(ctx, "Parse create sessionID=%v", v.sessionID)

	v.wg.Add(1)
	go func() {
		defer v.wg.Done()
		defer v.pollingCancel()

		for v.pollingCtx.Err() == nil {
			if err := v.polling(v.pollingCtx); err != nil {
				if v.pollingCtx.Err() != context.Canceled {
					logger.Wf(ctx, "polling err %+v", err)
				}
				break
			}
		}
	}()

	return nil
}

func (v *janusAPI) AttachPlugin(ctx context.Context) (handleID uint64, err error) {
	api := fmt.Sprintf("%v%v", v.r, v.sessionID)

	reqBody := struct {
		Janus       string `json:"janus"`
		OpaqueID    string `json:"opaque_id"`
		Plugin      string `json:"plugin"`
		Transaction string `json:"transaction"`
	}{
		"attach", newTransactionID(),
		"janus.plugin.videoroom", newTransactionID(),
	}

	b, err := json.Marshal(reqBody)
	if err != nil {
		return 0, errors.Wrapf(err, "Marshal body %v", reqBody)
	}
	logger.Tf(ctx, "Request url api=%v with %v", api, string(b))

	req, err := http.NewRequest("POST", api, strings.NewReader(string(b)))
	if err != nil {
		return 0, errors.Wrapf(err, "HTTP request %v", string(b))
	}

	res, err := http.DefaultClient.Do(req.WithContext(ctx))
	if err != nil {
		return 0, errors.Wrapf(err, "Do HTTP request %v", string(b))
	}

	b2, err := ioutil.ReadAll(res.Body)
	if err != nil {
		return 0, errors.Wrapf(err, "Read response for %v", string(b))
	}

	s2 := escapeJSON(string(b2))
	logger.Tf(ctx, "Response from %v is %v", api, s2)

	resBody := struct {
		Janus       string `json:"janus"`
		SessionID   uint64 `json:"session_id"`
		Transaction string `json:"transaction"`
		Data        struct {
			ID uint64 `json:"id"`
		} `json:"data"`
	}{}
	if err := json.Unmarshal([]byte(s2), &resBody); err != nil {
		return 0, errors.Wrapf(err, "Marshal %v", s2)
	}
	if resBody.Janus != "success" {
		return 0, errors.Errorf("Server fail code=%v %v", resBody.Janus, s2)
	}

	h := &janusHandle{}
	h.handleID = resBody.Data.ID
	h.api = v
	v.handles.Store(h.handleID, h)
	logger.Tf(ctx, "Parse create handleID=%v", h.handleID)

	return h.handleID, nil
}

func (v *janusAPI) DetachPlugin(ctx context.Context, handleID uint64) error {
	handler := v.loadHandler(handleID)
	api := fmt.Sprintf("%v%v/%v", v.r, v.sessionID, handler.handleID)

	reqBody := struct {
		Janus       string `json:"janus"`
		Transaction string `json:"transaction"`
	}{
		"detach", newTransactionID(),
	}

	b, err := json.Marshal(reqBody)
	if err != nil {
		return errors.Wrapf(err, "Marshal body %v", reqBody)
	}
	logger.Tf(ctx, "Request url api=%v with %v", api, string(b))

	req, err := http.NewRequest("POST", api, strings.NewReader(string(b)))
	if err != nil {
		return errors.Wrapf(err, "HTTP request %v", string(b))
	}

	res, err := http.DefaultClient.Do(req.WithContext(ctx))
	if err != nil {
		return errors.Wrapf(err, "Do HTTP request %v", string(b))
	}

	b2, err := ioutil.ReadAll(res.Body)
	if err != nil {
		return errors.Wrapf(err, "Read response for %v", string(b))
	}

	s2 := escapeJSON(string(b2))
	logger.Tf(ctx, "Response from %v is %v", api, s2)

	ackBody := struct {
		Janus       string `json:"janus"`
		SessionID   uint64 `json:"session_id"`
		Transaction string `json:"transaction"`
	}{}
	if err := json.Unmarshal([]byte(s2), &ackBody); err != nil {
		return errors.Wrapf(err, "Marshal %v", s2)
	}
	if ackBody.Janus != "success" {
		return errors.Errorf("Server fail code=%v %v", ackBody.Janus, s2)
	}
	logger.Tf(ctx, "Detach tid=%v done", reqBody.Transaction)

	return nil
}

func (v *janusAPI) loadHandler(handleID uint64) *janusHandle {
	if h, ok := v.handles.Load(handleID); !ok {
		return nil
	} else {
		return h.(*janusHandle)
	}
}

func (v *janusAPI) JoinAsPublisher(ctx context.Context, handleID uint64, room int, display string) error {
	handler := v.loadHandler(handleID)
	api := fmt.Sprintf("%v%v/%v", v.r, v.sessionID, handler.handleID)

	reqBodyBody := struct {
		Request string `json:"request"`
		PType   string `json:"ptype"`
		Room    int    `json:"room"`
		Display string `json:"display"`
	}{
		"join", "publisher", room, display,
	}
	reqBody := struct {
		Janus       string      `json:"janus"`
		Transaction string      `json:"transaction"`
		Body        interface{} `json:"body"`
	}{
		"message", newTransactionID(), reqBodyBody,
	}

	reply := newJanusReply(reqBody.Transaction)
	v.replies.Store(reqBody.Transaction, reply)

	b, err := json.Marshal(reqBody)
	if err != nil {
		return errors.Wrapf(err, "Marshal body %v", reqBody)
	}
	logger.Tf(ctx, "Request url api=%v with %v", api, string(b))

	req, err := http.NewRequest("POST", api, strings.NewReader(string(b)))
	if err != nil {
		return errors.Wrapf(err, "HTTP request %v", string(b))
	}

	res, err := http.DefaultClient.Do(req.WithContext(ctx))
	if err != nil {
		return errors.Wrapf(err, "Do HTTP request %v", string(b))
	}

	b2, err := ioutil.ReadAll(res.Body)
	if err != nil {
		return errors.Wrapf(err, "Read response for %v", string(b))
	}

	s2 := escapeJSON(string(b2))
	logger.Tf(ctx, "Response from %v is %v", api, s2)

	ackBody := struct {
		Janus       string `json:"janus"`
		SessionID   uint64 `json:"session_id"`
		Transaction string `json:"transaction"`
	}{}
	if err := json.Unmarshal([]byte(s2), &ackBody); err != nil {
		return errors.Wrapf(err, "Marshal %v", s2)
	}
	if ackBody.Janus != "ack" {
		return errors.Errorf("Server fail code=%v %v", ackBody.Janus, s2)
	}
	logger.Tf(ctx, "Response tid=%v ack", reply.transactionID)

	// Reply from polling.
	var s3 string
	select {
	case <-ctx.Done():
		return ctx.Err()
	case b3 := <-reply.replies:
		s3 = escapeJSON(string(b3))
		logger.Tf(ctx, "Async response tid=%v, reply=%v", reply.transactionID, s3)
	}
	resBody := struct {
		Janus       string `json:"janus"`
		Session     uint64 `json:"session_id"`
		Transaction string `json:"transaction"`
		Sender      uint64 `json:"sender"`
		PluginData  struct {
			Plugin string `json:"plugin"`
			Data   struct {
				VideoRoom   string          `json:"videoroom"`
				Room        int             `json:"room"`
				Description string          `json:"description"`
				ID          uint64          `json:"id"`
				PrivateID   uint64          `json:"private_id"`
				Publishers  []publisherInfo `json:"publishers"`
			} `json:"data"`
		} `json:"plugindata"`
	}{}
	if err := json.Unmarshal([]byte(s3), &resBody); err != nil {
		return errors.Wrapf(err, "Marshal %v", s3)
	}

	plugin := resBody.PluginData.Data
	if resBody.Janus != "event" || plugin.VideoRoom != "joined" {
		return errors.Errorf("Server fail janus=%v, plugin=%v %v", resBody.Janus, plugin.VideoRoom, s3)
	}

	handler.publisherID = plugin.ID
	v.privateID = plugin.PrivateID
	logger.Tf(ctx, "Join as publisher room=%v, display=%v, tid=%v ok, event=%v, plugin=%v, id=%v, private=%v, publishers=%v",
		room, display, reply.transactionID, resBody.Janus, plugin.VideoRoom, handler.publisherID, plugin.PrivateID, len(plugin.Publishers))

	if len(plugin.Publishers) > 0 {
		v.onPublisher(resBody.Sender, resBody.Session, plugin.Publishers)
	}

	return nil
}

func (v *janusAPI) UnPublish(ctx context.Context, handleID uint64) error {
	handler := v.loadHandler(handleID)
	api := fmt.Sprintf("%v%v/%v", v.r, v.sessionID, handler.handleID)

	reqBodyBody := struct {
		Request string `json:"request"`
	}{
		"unpublish",
	}
	reqBody := struct {
		Janus       string      `json:"janus"`
		Transaction string      `json:"transaction"`
		Body        interface{} `json:"body"`
	}{
		"message", newTransactionID(), reqBodyBody,
	}

	b, err := json.Marshal(reqBody)
	if err != nil {
		return errors.Wrapf(err, "Marshal body %v", reqBody)
	}
	logger.Tf(ctx, "Request url api=%v with %v", api, string(b))

	req, err := http.NewRequest("POST", api, strings.NewReader(string(b)))
	if err != nil {
		return errors.Wrapf(err, "HTTP request %v", string(b))
	}

	res, err := http.DefaultClient.Do(req.WithContext(ctx))
	if err != nil {
		return errors.Wrapf(err, "Do HTTP request %v", string(b))
	}

	b2, err := ioutil.ReadAll(res.Body)
	if err != nil {
		return errors.Wrapf(err, "Read response for %v", string(b))
	}

	s2 := escapeJSON(string(b2))
	logger.Tf(ctx, "Response from %v is %v", api, s2)

	ackBody := struct {
		Janus       string `json:"janus"`
		SessionID   uint64 `json:"session_id"`
		Transaction string `json:"transaction"`
	}{}
	if err := json.Unmarshal([]byte(s2), &ackBody); err != nil {
		return errors.Wrapf(err, "Marshal %v", s2)
	}
	if ackBody.Janus != "ack" {
		return errors.Errorf("Server fail code=%v %v", ackBody.Janus, s2)
	}
	logger.Tf(ctx, "UnPublish tid=%v done", reqBody.Transaction)

	return nil
}

func (v *janusAPI) Publish(ctx context.Context, handleID uint64, offer string) (answer string, err error) {
	handler := v.loadHandler(handleID)
	api := fmt.Sprintf("%v%v/%v", v.r, v.sessionID, handler.handleID)

	reqBodyBody := struct {
		Request string `json:"request"`
		Video   bool   `json:"video"`
		Audio   bool   `json:"audio"`
	}{
		"configure", true, true,
	}
	jsepBody := struct {
		Type string `json:"type"`
		SDP  string `json:"sdp"`
	}{
		"offer", offer,
	}
	reqBody := struct {
		Janus       string      `json:"janus"`
		Transaction string      `json:"transaction"`
		Body        interface{} `json:"body"`
		JSEP        interface{} `json:"jsep"`
	}{
		"message", newTransactionID(), reqBodyBody, jsepBody,
	}

	reply := newJanusReply(reqBody.Transaction)
	v.replies.Store(reqBody.Transaction, reply)

	b, err := json.Marshal(reqBody)
	if err != nil {
		return "", errors.Wrapf(err, "Marshal body %v", reqBody)
	}
	logger.Tf(ctx, "Request url api=%v with %v", api, string(b))

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

	s2 := escapeJSON(string(b2))
	logger.Tf(ctx, "Response from %v is %v", api, s2)

	ackBody := struct {
		Janus       string `json:"janus"`
		SessionID   uint64 `json:"session_id"`
		Transaction string `json:"transaction"`
	}{}
	if err := json.Unmarshal([]byte(s2), &ackBody); err != nil {
		return "", errors.Wrapf(err, "Marshal %v", s2)
	}
	if ackBody.Janus != "ack" {
		return "", errors.Errorf("Server fail code=%v %v", ackBody.Janus, s2)
	}
	logger.Tf(ctx, "Response tid=%v ack", reply.transactionID)

	// Reply from polling.
	var s3 string
	select {
	case <-ctx.Done():
		return "", ctx.Err()
	case b3 := <-reply.replies:
		s3 = escapeJSON(string(b3))
		logger.Tf(ctx, "Async response tid=%v, reply=%v", reply.transactionID, s3)
	}
	resBody := struct {
		Janus       string `json:"janus"`
		Session     uint64 `json:"session_id"`
		Transaction string `json:"transaction"`
		Sender      uint64 `json:"sender"`
		PluginData  struct {
			Plugin string `json:"plugin"`
			Data   struct {
				VideoRoom  string `json:"videoroom"`
				Room       int    `json:"room"`
				Configured string `json:"configured"`
				AudioCodec string `json:"audio_codec"`
				VideoCodec string `json:"video_codec"`
			} `json:"data"`
		} `json:"plugindata"`
		JSEP struct {
			Type string `json:"type"`
			SDP  string `json:"sdp"`
		} `json:"jsep"`
	}{}
	if err := json.Unmarshal([]byte(s3), &resBody); err != nil {
		return "", errors.Wrapf(err, "Marshal %v", s3)
	}

	plugin := resBody.PluginData.Data
	jsep := resBody.JSEP
	if resBody.Janus != "event" || plugin.VideoRoom != "event" {
		return "", errors.Errorf("Server fail janus=%v, plugin=%v %v", resBody.Janus, plugin.VideoRoom, s3)
	}
	logger.Tf(ctx, "Configure publisher offer=%vB, tid=%v ok, event=%v, plugin=%v, answer=%vB",
		len(offer), reply.transactionID, resBody.Janus, plugin.VideoRoom, len(jsep.SDP))

	return jsep.SDP, nil
}

func (v *janusAPI) JoinAsSubscribe(ctx context.Context, handleID uint64, room int, publisher *publisherInfo) (offer string, err error) {
	handler := v.loadHandler(handleID)
	api := fmt.Sprintf("%v%v/%v", v.r, v.sessionID, handler.handleID)

	reqBodyBody := struct {
		Request   string `json:"request"`
		PType     string `json:"ptype"`
		Room      int    `json:"room"`
		Feed      uint64 `json:"feed"`
		PrivateID uint64 `json:"private_id"`
	}{
		"join", "subscriber", room, publisher.ID, v.privateID,
	}
	reqBody := struct {
		Janus       string      `json:"janus"`
		Transaction string      `json:"transaction"`
		Body        interface{} `json:"body"`
	}{
		"message", newTransactionID(), reqBodyBody,
	}

	reply := newJanusReply(reqBody.Transaction)
	v.replies.Store(reqBody.Transaction, reply)

	b, err := json.Marshal(reqBody)
	if err != nil {
		return "", errors.Wrapf(err, "Marshal body %v", reqBody)
	}
	logger.Tf(ctx, "Request url api=%v with %v", api, string(b))

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

	s2 := escapeJSON(string(b2))
	logger.Tf(ctx, "Response from %v is %v", api, s2)

	ackBody := struct {
		Janus       string `json:"janus"`
		SessionID   uint64 `json:"session_id"`
		Transaction string `json:"transaction"`
	}{}
	if err := json.Unmarshal([]byte(s2), &ackBody); err != nil {
		return "", errors.Wrapf(err, "Marshal %v", s2)
	}
	if ackBody.Janus != "ack" {
		return "", errors.Errorf("Server fail code=%v %v", ackBody.Janus, s2)
	}
	logger.Tf(ctx, "Response tid=%v ack", reply.transactionID)

	// Reply from polling.
	var s3 string
	select {
	case <-ctx.Done():
		return "", ctx.Err()
	case b3 := <-reply.replies:
		s3 = escapeJSON(string(b3))
		logger.Tf(ctx, "Async response tid=%v, reply=%v", reply.transactionID, s3)
	}
	resBody := struct {
		Janus       string `json:"janus"`
		Session     uint64 `json:"session_id"`
		Transaction string `json:"transaction"`
		Sender      uint64 `json:"sender"`
		PluginData  struct {
			Plugin string `json:"plugin"`
			Data   struct {
				VideoRoom string `json:"videoroom"`
				Room      int    `json:"room"`
				ID        uint64 `json:"id"`
				Display   string `json:"display"`
			} `json:"data"`
		} `json:"plugindata"`
		JSEP struct {
			Type string `json:"type"`
			SDP  string `json:"sdp"`
		} `json:"jsep"`
	}{}
	if err := json.Unmarshal([]byte(s3), &resBody); err != nil {
		return "", errors.Wrapf(err, "Marshal %v", s3)
	}

	plugin := resBody.PluginData.Data
	jsep := resBody.JSEP
	if resBody.Janus != "event" || plugin.VideoRoom != "attached" {
		return "", errors.Errorf("Server fail janus=%v, plugin=%v %v", resBody.Janus, plugin.VideoRoom, s3)
	}
	logger.Tf(ctx, "Join as subscriber room=%v, tid=%v ok, event=%v, plugin=%v, offer=%vB",
		room, reply.transactionID, resBody.Janus, plugin.VideoRoom, len(jsep.SDP))

	return jsep.SDP, nil
}

func (v *janusAPI) Subscribe(ctx context.Context, handleID uint64, room int, answer string) error {
	handler := v.loadHandler(handleID)
	api := fmt.Sprintf("%v%v/%v", v.r, v.sessionID, handler.handleID)

	reqBodyBody := struct {
		Request string `json:"request"`
		Room    int    `json:"room"`
	}{
		"start", room,
	}
	jsepBody := struct {
		Type string `json:"type"`
		SDP  string `json:"sdp"`
	}{
		"answer", answer,
	}
	reqBody := struct {
		Janus       string      `json:"janus"`
		Transaction string      `json:"transaction"`
		Body        interface{} `json:"body"`
		JSEP        interface{} `json:"jsep"`
	}{
		"message", newTransactionID(), reqBodyBody, jsepBody,
	}

	reply := newJanusReply(reqBody.Transaction)
	v.replies.Store(reqBody.Transaction, reply)

	b, err := json.Marshal(reqBody)
	if err != nil {
		return errors.Wrapf(err, "Marshal body %v", reqBody)
	}
	logger.Tf(ctx, "Request url api=%v with %v", api, string(b))

	req, err := http.NewRequest("POST", api, strings.NewReader(string(b)))
	if err != nil {
		return errors.Wrapf(err, "HTTP request %v", string(b))
	}

	res, err := http.DefaultClient.Do(req.WithContext(ctx))
	if err != nil {
		return errors.Wrapf(err, "Do HTTP request %v", string(b))
	}

	b2, err := ioutil.ReadAll(res.Body)
	if err != nil {
		return errors.Wrapf(err, "Read response for %v", string(b))
	}

	s2 := escapeJSON(string(b2))
	logger.Tf(ctx, "Response from %v is %v", api, s2)

	ackBody := struct {
		Janus       string `json:"janus"`
		SessionID   uint64 `json:"session_id"`
		Transaction string `json:"transaction"`
	}{}
	if err := json.Unmarshal([]byte(s2), &ackBody); err != nil {
		return errors.Wrapf(err, "Marshal %v", s2)
	}
	if ackBody.Janus != "ack" {
		return errors.Errorf("Server fail code=%v %v", ackBody.Janus, s2)
	}
	logger.Tf(ctx, "Response tid=%v ack", reply.transactionID)

	// Reply from polling.
	var s3 string
	select {
	case <-ctx.Done():
		return ctx.Err()
	case b3 := <-reply.replies:
		s3 = escapeJSON(string(b3))
		logger.Tf(ctx, "Async response tid=%v, reply=%v", reply.transactionID, s3)
	}
	resBody := struct {
		Janus       string `json:"janus"`
		Session     uint64 `json:"session_id"`
		Transaction string `json:"transaction"`
		Sender      uint64 `json:"sender"`
		PluginData  struct {
			Plugin string `json:"plugin"`
			Data   struct {
				VideoRoom string `json:"videoroom"`
				Room      int    `json:"room"`
				Started   string `json:"started"`
			} `json:"data"`
		} `json:"plugindata"`
	}{}
	if err := json.Unmarshal([]byte(s3), &resBody); err != nil {
		return errors.Wrapf(err, "Marshal %v", s3)
	}

	plugin := resBody.PluginData.Data
	if resBody.Janus != "event" || plugin.VideoRoom != "event" || plugin.Started != "ok" {
		return errors.Errorf("Server fail janus=%v, plugin=%v, started=%v %v", resBody.Janus, plugin.VideoRoom, plugin.Started, s3)
	}
	logger.Tf(ctx, "Start subscribe answer=%vB, tid=%v ok, event=%v, plugin=%v, started=%v",
		len(answer), reply.transactionID, resBody.Janus, plugin.VideoRoom, plugin.Started)

	return nil
}

func (v *janusAPI) polling(ctx context.Context) error {
	api := fmt.Sprintf("%v%v?rid=%v&maxev=1", v.r, v.sessionID,
		uint64(time.Duration(time.Now().UnixNano())/time.Millisecond))
	logger.Tf(ctx, "Polling: Request url api=%v", api)

	req, err := http.NewRequest("GET", api, nil)
	if err != nil {
		return errors.Wrapf(err, "HTTP request %v", api)
	}

	res, err := http.DefaultClient.Do(req.WithContext(ctx))
	if err != nil {
		return errors.Wrapf(err, "Do HTTP request %v", api)
	}

	b2, err := ioutil.ReadAll(res.Body)
	if err != nil {
		return errors.Wrapf(err, "Read response for %v", api)
	}

	s2 := escapeJSON(string(b2))
	logger.Tf(ctx, "Polling: Response from %v is %v", api, s2)

	if len(b2) == 0 {
		return nil
	}

	replyID := struct {
		Janus       string `json:"janus"`
		Transaction string `json:"transaction"`
	}{}
	if err := json.Unmarshal([]byte(s2), &replyID); err != nil {
		return errors.Wrapf(err, "Marshal %v", s2)
	}

	switch replyID.Janus {
	case "event":
		if r, ok := v.replies.Load(replyID.Transaction); !ok {
			if err := v.handleCall(replyID.Janus, s2); err != nil {
				logger.Wf(ctx, "Polling: Handle call %v fail %v, err %+v", replyID.Janus, s2, err)
			}
		} else if r2, ok := r.(*janusReply); !ok {
			logger.Wf(ctx, "Polling: Ignore tid=%v reply %v", replyID.Transaction, s2)
		} else {
			select {
			case <-ctx.Done():
				return ctx.Err()
			case r2.replies <- b2:
				logger.Tf(ctx, "Polling: Reply tid=%v ok, %v", replyID.Transaction, s2)
			}
		}
	case "keepalive":
		return nil
	case "webrtcup", "media", "slowlink", "detached":
		if err := v.handleCall(replyID.Janus, s2); err != nil {
			logger.Wf(ctx, "Polling: Handle call %v fail %v, err %+v", replyID.Janus, s2, err)
		}
	default:
		logger.Wf(ctx, "Polling: Unknown janus=%v %v", replyID.Janus, s2)
	}

	return nil
}

func (v *janusAPI) handleCall(janus string, s string) error {
	type callHeader struct {
		Sender    uint64 `json:"sender"`
		SessionID uint64 `json:"session_id"`
	}

	switch janus {
	case "detached":
		/*{
			"janus": "detached",
			"sender": 4201795482244652,
			"session_id": 373403124722380
		}*/
		r := callHeader{}
		if err := json.Unmarshal([]byte(s), &r); err != nil {
			return err
		}

		v.onDetached(r.Sender, r.SessionID)
	case "webrtcup":
		/*{
			"janus": "webrtcup",
			"sender": 7698695982180732,
			"session_id": 2403223275773854
		}*/
		r := callHeader{}
		if err := json.Unmarshal([]byte(s), &r); err != nil {
			return err
		}

		v.onWebrtcUp(r.Sender, r.SessionID)
	case "media":
		/*{
			"janus": "media",
			"receiving": true,
			"sender": 7698695982180732,
			"session_id": 2403223275773854,
			"type": "audio"
		}*/
		r := struct {
			callHeader
			Type      string `json:"type"`
			Receiving bool   `json:"receiving"`
		}{}
		if err := json.Unmarshal([]byte(s), &r); err != nil {
			return err
		}

		v.onMedia(r.Sender, r.SessionID, r.Type, r.Receiving)
	case "slowlink":
		/*{
			"janus": "slowlink",
			"lost": 4294902988,
			"media": "video",
			"sender": 562229074390269,
			"session_id": 156116325213625,
			"uplink": false
		}*/
		r := struct {
			callHeader
			Lost   uint64 `json:"lost"`
			Media  string `json:"media"`
			Uplink bool   `json:"uplink"`
		}{}
		if err := json.Unmarshal([]byte(s), &r); err != nil {
			return err
		}

		v.onSlowLink(r.Sender, r.SessionID, r.Media, r.Lost, r.Uplink)
	case "event":
		if strings.Contains(s, "publishers") {
			/*{
				"janus": "event",
				"plugindata": {
					"data": {
						"publishers": [{
							"audio_codec": "opus",
							"display": "test",
							"id": 2805536617160145,
							"talking": false,
							"video_codec": "h264"
						}],
						"room": 2345,
						"videoroom": "event"
					},
					"plugin": "janus.plugin.videoroom"
				},
				"sender": 2156044968631669,
				"session_id": 6696376606446844
			}*/
			r := struct {
				callHeader
				PluginData struct {
					Data struct {
						Publishers []publisherInfo `json:"publishers"`
						Room       int             `json:"room"`
						VideoRoom  string          `json:"videoroom"`
					} `json:"data"`
					Plugin string `json:"plugin"`
				} `json:"plugindata"`
			}{}
			if err := json.Unmarshal([]byte(s), &r); err != nil {
				return err
			}

			v.onPublisher(r.Sender, r.SessionID, r.PluginData.Data.Publishers)
		} else if strings.Contains(s, "unpublished") {
			/*{
				"janus": "event",
				"plugindata": {
					"data": {
						"room": 2345,
						"unpublished": 2805536617160145,
						"videoroom": "event"
					},
					"plugin": "janus.plugin.videoroom"
				},
				"sender": 2156044968631669,
				"session_id": 6696376606446844
			}*/
			r := struct {
				callHeader
				PluginData struct {
					Data struct {
						Room        int    `json:"room"`
						UnPublished uint64 `json:"unpublished"`
						VideoRoom   string `json:"videoroom"`
					} `json:"data"`
					Plugin string `json:"plugin"`
				} `json:"plugindata"`
			}{}
			if err := json.Unmarshal([]byte(s), &r); err != nil {
				return err
			}

			v.onUnPublished(r.Sender, r.SessionID, r.PluginData.Data.UnPublished)
		} else if strings.Contains(s, "leaving") {
			/*{
				"janus": "event",
				"plugindata": {
					"data": {
						"leaving": 2805536617160145,
						"room": 2345,
						"videoroom": "event"
					},
					"plugin": "janus.plugin.videoroom"
				},
				"sender": 2156044968631669,
				"session_id": 6696376606446844
			}*/
			r := struct {
				callHeader
				PluginData struct {
					Data struct {
						Leaving   uint64 `json:"leaving"`
						Room      int    `json:"room"`
						VideoRoom string `json:"videoroom"`
					} `json:"data"`
					Plugin string `json:"plugin"`
				} `json:"plugindata"`
			}{}
			if err := json.Unmarshal([]byte(s), &r); err != nil {
				return err
			}

			v.onLeave(r.Sender, r.SessionID, r.PluginData.Data.Leaving)
		}
	}

	return nil
}

func (v *janusAPI) DiscoverPublisher(ctx context.Context, room int, display string, timeout time.Duration) (*publisherInfo, error) {
	var publisher *publisherInfo
	discoverCtx, discoverCancel := context.WithCancel(context.Background())

	ov := v.onPublisher
	defer func() {
		v.onPublisher = ov
	}()
	v.onPublisher = func(sender, sessionID uint64, publishers []publisherInfo) {
		for _, p := range publishers {
			if p.Display == display {
				publisher = &p
				discoverCancel()
				logger.Tf(ctx, "Publisher discovered %v", p)
				return
			}
		}
	}
	go func() {
		if err := func() error {
			publishHandleID, err := v.AttachPlugin(ctx)
			if err != nil {
				return err
			}
			defer v.DetachPlugin(ctx, publishHandleID)

			if err := v.JoinAsPublisher(ctx, publishHandleID, room, fmt.Sprintf("sub-%v", display)); err != nil {
				return err
			}

			<-discoverCtx.Done()
			return nil
		}(); err != nil {
			logger.Ef(ctx, "join err %+v", err)
		}
	}()

	select {
	case <-ctx.Done():
		return nil, ctx.Err()
	case <-discoverCtx.Done():
	case <-time.After(timeout):
		discoverCancel()
	}
	if publisher == nil {
		return nil, errors.Errorf("no publisher for room=%v, display=%v, session=%v",
			room, display, v.sessionID)
	}

	return publisher, nil
}
