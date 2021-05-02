// The MIT License (MIT)
//
// Copyright (c) 2021 Winlin
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
package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"net/http"
	"os"
	"path"
	"strings"
	"sync"

	"github.com/ossrs/go-oryx-lib/errors"
	"github.com/ossrs/go-oryx-lib/logger"
	"golang.org/x/net/websocket"
)

type Participant struct {
	Room       *Room       `json:"-"`
	Display    string      `json:"display"`
	Publishing bool        `json:"publishing"`
	Out        chan []byte `json:"-"`
}

func (v *Participant) String() string {
	return fmt.Sprintf("display=%v, room=%v", v.Display, v.Room.Name)
}

type Room struct {
	Name         string         `json:"room"`
	Participants []*Participant `json:"participants"`
	lock         sync.RWMutex   `json:"-"`
}

func (v *Room) String() string {
	return fmt.Sprintf("room=%v, participants=%v", v.Name, len(v.Participants))
}

func (v *Room) Add(p *Participant) error {
	v.lock.Lock()
	defer v.lock.Unlock()

	for _, r := range v.Participants {
		if r.Display == p.Display {
			return errors.Errorf("Participant %v exists in room %v", p.Display, v.Name)
		}
	}

	v.Participants = append(v.Participants, p)
	return nil
}

func (v *Room) Get(display string) *Participant {
	v.lock.RLock()
	defer v.lock.RUnlock()

	for _, r := range v.Participants {
		if r.Display == display {
			return r
		}
	}

	return nil
}

func (v *Room) Remove(p *Participant) {
	v.lock.Lock()
	defer v.lock.Unlock()

	for i, r := range v.Participants {
		if p == r {
			v.Participants = append(v.Participants[:i], v.Participants[i+1:]...)
			return
		}
	}
}

func (v *Room) Notify(ctx context.Context, peer *Participant, event string) {
	var participants []*Participant
	func() {
		v.lock.RLock()
		defer v.lock.RUnlock()
		participants = append(participants, v.Participants...)
	}()

	for _, r := range participants {
		if r == peer {
			continue
		}

		res := struct {
			Action       string         `json:"action"`
			Event        string         `json:"event"`
			Room         string         `json:"room"`
			Self         *Participant   `json:"self"`
			Peer         *Participant   `json:"peer"`
			Participants []*Participant `json:"participants"`
		}{
			"notify", event, v.Name, r, peer, participants,
		}

		b, err := json.Marshal(struct {
			Message interface{} `json:"msg"`
		}{
			res,
		})
		if err != nil {
			return
		}

		select {
		case <-ctx.Done():
			return
		case r.Out <- b:
		}

		logger.Tf(ctx, "Notify %v about %v %v", r, peer, event)
	}
}

func main() {
	var listen string
	flag.StringVar(&listen, "listen", "1989", "The TCP listen port")

	var html string
	flag.StringVar(&html, "root", "./www", "The www web root")

	flag.Usage = func() {
		fmt.Println(fmt.Sprintf("Usage: %v [Options]", os.Args[0]))
		fmt.Println(fmt.Sprintf("Options:"))
		fmt.Println(fmt.Sprintf("    -listen     The TCP listen port. Default: %v", listen))
		fmt.Println(fmt.Sprintf("    -root       The www web root. Default: %v", html))
		fmt.Println(fmt.Sprintf("For example:"))
		fmt.Println(fmt.Sprintf("    %v -listen %v -html %v", os.Args[0], listen, html))
	}
	flag.Parse()

	if !strings.Contains(listen, ":") {
		listen = ":" + listen
	}

	ctx := context.Background()

	home := listen
	if strings.HasPrefix(home, ":") {
		home = "http://localhost" + listen
	}

	if !path.IsAbs(html) && path.IsAbs(os.Args[0]) {
		html = path.Join(path.Dir(os.Args[0]), html)
	}
	logger.Tf(ctx, "Signaling ok, root=%v, home page is %v", html, home)

	http.Handle("/", http.FileServer(http.Dir(html)))

	// Key is name of room, value is Room
	var rooms sync.Map
	http.Handle("/sig/v1/rtc", websocket.Handler(func(c *websocket.Conn) {
		ctx, cancel := context.WithCancel(logger.WithContext(ctx))
		defer cancel()

		r := c.Request()
		logger.Tf(ctx, "Serve client %v at %v", r.RemoteAddr, r.RequestURI)
		defer c.Close()

		var self *Participant
		go func() {
			<-ctx.Done()
			if self != nil {
				self.Room.Remove(self)
				logger.Tf(ctx, "Remove client %v", self)
			}
		}()

		inMessages := make(chan []byte, 0)
		go func() {
			defer cancel()

			buf := make([]byte, 16384)
			for {
				n, err := c.Read(buf)
				if err != nil {
					logger.Wf(ctx, "Ignore err %v for %v", err, r.RemoteAddr)
					break
				}

				select {
				case <-ctx.Done():
				case inMessages <- buf[:n]:
				}
			}
		}()

		outMessages := make(chan []byte, 0)
		go func() {
			defer cancel()

			handleMessage := func(m []byte) error {
				action := struct {
					TID     string `json:"tid"`
					Message struct {
						Action string `json:"action"`
					} `json:"msg"`
				}{}
				if err := json.Unmarshal(m, &action); err != nil {
					return errors.Wrapf(err, "Unmarshal %s", m)
				}

				var res interface{}
				var p *Participant
				if action.Message.Action == "join" {
					obj := struct {
						Message struct {
							Room    string `json:"room"`
							Display string `json:"display"`
						} `json:"msg"`
					}{}
					if err := json.Unmarshal(m, &obj); err != nil {
						return errors.Wrapf(err, "Unmarshal %s", m)
					}

					r, _ := rooms.LoadOrStore(obj.Message.Room, &Room{Name: obj.Message.Room})
					p = &Participant{Room: r.(*Room), Display: obj.Message.Display, Out: outMessages}
					if err := r.(*Room).Add(p); err != nil {
						return errors.Wrapf(err, "join")
					}

					self = p
					logger.Tf(ctx, "Join %v ok", self)

					res = struct {
						Action       string         `json:"action"`
						Room         string         `json:"room"`
						Self         *Participant   `json:"self"`
						Participants []*Participant `json:"participants"`
					}{
						action.Message.Action, obj.Message.Room, p, r.(*Room).Participants,
					}

					go r.(*Room).Notify(ctx, p, action.Message.Action)
				} else if action.Message.Action == "publish" {
					obj := struct {
						Message struct {
							Room    string `json:"room"`
							Display string `json:"display"`
						} `json:"msg"`
					}{}
					if err := json.Unmarshal(m, &obj); err != nil {
						return errors.Wrapf(err, "Unmarshal %s", m)
					}

					r, _ := rooms.LoadOrStore(obj.Message.Room, &Room{Name: obj.Message.Room})
					p := r.(*Room).Get(obj.Message.Display)

					// Now, the peer is publishing.
					p.Publishing = true

					go r.(*Room).Notify(ctx, p, action.Message.Action)
				} else {
					return errors.Errorf("Invalid message %s", m)
				}

				if b, err := json.Marshal(struct {
					TID     string      `json:"tid"`
					Message interface{} `json:"msg"`
				}{
					action.TID, res,
				}); err != nil {
					return errors.Wrapf(err, "marshal")
				} else {
					select {
					case <-ctx.Done():
						return ctx.Err()
					case outMessages <- b:
					}
				}

				return nil
			}

			for m := range inMessages {
				if err := handleMessage(m); err != nil {
					logger.Wf(ctx, "Handle %s err %v", m, err)
					break
				}
			}
		}()

		for m := range outMessages {
			if _, err := c.Write(m); err != nil {
				logger.Wf(ctx, "Ignore err %v for %v", err, r.RemoteAddr)
				break
			}
		}
	}))

	http.ListenAndServe(listen, nil)
}
