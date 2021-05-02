/*
The MIT License (MIT)

Copyright (c) 2019 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

/*
 This the main entrance of https-proxy, proxy to api or other http server.
*/
package main

import (
	"context"
	"crypto/tls"
	"flag"
	"fmt"
	oe "github.com/ossrs/go-oryx-lib/errors"
	oh "github.com/ossrs/go-oryx-lib/http"
	"github.com/ossrs/go-oryx-lib/https"
	ol "github.com/ossrs/go-oryx-lib/logger"
	"log"
	"net"
	"net/http"
	"net/http/httputil"
	"net/url"
	"os"
	"path"
	"strconv"
	"strings"
	"sync"
)

type Strings []string

func (v *Strings) String() string {
	return fmt.Sprintf("strings [%v]", strings.Join(*v, ","))
}

func (v *Strings) Set(value string) error {
	*v = append(*v, value)
	return nil
}

func shouldProxyURL(srcPath, proxyPath string) bool {
	if !strings.HasSuffix(srcPath, "/") {
		// /api to /api/
		// /api.js to /api.js/
		// /api/100 to /api/100/
		srcPath += "/"
	}

	if !strings.HasSuffix(proxyPath, "/") {
		// /api/ to /api/
		// to match /api/ or /api/100
		// and not match /api.js/
		proxyPath += "/"
	}

	return strings.HasPrefix(srcPath, proxyPath)
}

func NewComplexProxy(ctx context.Context, proxyUrl *url.URL, originalRequest *http.Request) http.Handler {
	proxy := &httputil.ReverseProxy{}

	// Create a proxy which attach a isolate logger.
	elogger := log.New(os.Stderr, fmt.Sprintf("%v ", originalRequest.RemoteAddr), log.LstdFlags)
	proxy.ErrorLog = elogger

	proxy.Director = func(r *http.Request) {
		// about the x-real-schema, we proxy to backend to identify the client schema.
		if rschema := r.Header.Get("X-Real-Schema"); rschema == "" {
			if r.TLS == nil {
				r.Header.Set("X-Real-Schema", "http")
			} else {
				r.Header.Set("X-Real-Schema", "https")
			}
		}

		// about x-real-ip and x-forwarded-for or
		// about X-Real-IP and X-Forwarded-For or
		// https://segmentfault.com/q/1010000002409659
		// https://distinctplace.com/2014/04/23/story-behind-x-forwarded-for-and-x-real-ip-headers/
		// @remark http proxy will set the X-Forwarded-For.
		if rip := r.Header.Get("X-Real-IP"); rip == "" {
			if rip, _, err := net.SplitHostPort(r.RemoteAddr); err == nil {
				r.Header.Set("X-Real-IP", rip)
			}
		}

		r.URL.Scheme = proxyUrl.Scheme
		r.URL.Host = proxyUrl.Host

		ra, url, rip := r.RemoteAddr, r.URL.String(), r.Header.Get("X-Real-Ip")
		ol.Tf(ctx, "proxy http rip=%v, addr=%v %v %v with headers %v", rip, ra, r.Method, url, r.Header)
	}

	proxy.ModifyResponse = func(w *http.Response) error {
		// we already added this header, it will cause chrome failed when duplicated.
		if w.Header.Get("Access-Control-Allow-Origin") == "*" {
			w.Header.Del("Access-Control-Allow-Origin")
		}

		return nil
	}

	return proxy
}

func run(ctx context.Context) error {
	oh.Server = fmt.Sprintf("%v/%v", Signature(), Version())
	fmt.Println(oh.Server, "HTTP/HTTPS static server with API proxy.")

	var httpPorts Strings
	flag.Var(&httpPorts, "t", "http listen")
	flag.Var(&httpPorts, "http", "http listen at. 0 to disable http.")

	var httpsPorts Strings
	flag.Var(&httpsPorts, "s", "https listen")
	flag.Var(&httpsPorts, "https", "https listen at. 0 to disable https. 443 to serve. ")

	var httpsDomains string
	flag.StringVar(&httpsDomains, "d", "", "https the allow domains")
	flag.StringVar(&httpsDomains, "domains", "", "https the allow domains, empty to allow all. for example: ossrs.net,www.ossrs.net")

	var html string
	flag.StringVar(&html, "r", "./html", "the www web root")
	flag.StringVar(&html, "root", "./html", "the www web root. support relative dir to argv[0].")

	var cacheFile string
	flag.StringVar(&cacheFile, "e", "./letsencrypt.cache", "https the cache for letsencrypt")
	flag.StringVar(&cacheFile, "cache", "./letsencrypt.cache", "https the cache for letsencrypt. support relative dir to argv[0].")

	var useLetsEncrypt bool
	flag.BoolVar(&useLetsEncrypt, "l", false, "whether use letsencrypt CA")
	flag.BoolVar(&useLetsEncrypt, "lets", false, "whether use letsencrypt CA. self sign if not.")

	var ssKey string
	flag.StringVar(&ssKey, "k", "", "https self-sign key")
	flag.StringVar(&ssKey, "ssk", "", "https self-sign key")

	var ssCert string
	flag.StringVar(&ssCert, "c", "", `https self-sign cert`)
	flag.StringVar(&ssCert, "ssc", "", `https self-sign cert`)

	var oproxies Strings
	flag.Var(&oproxies, "p", "proxy ruler")
	flag.Var(&oproxies, "proxy", "one or more proxy the matched path to backend, for example, -proxy http://127.0.0.1:8888/api/webrtc")

	var sdomains, skeys, scerts Strings
	flag.Var(&sdomains, "sdomain", "the SSL hostname")
	flag.Var(&skeys, "skey", "the SSL key for domain")
	flag.Var(&scerts, "scert", "the SSL cert for domain")

	flag.Usage = func() {
		fmt.Println(fmt.Sprintf("Usage: %v -t http -s https -d domains -r root -e cache -l lets -k ssk -c ssc -p proxy", os.Args[0]))
		fmt.Println(fmt.Sprintf("	"))
		fmt.Println(fmt.Sprintf("Options:"))
		fmt.Println(fmt.Sprintf("	-t, -http string"))
		fmt.Println(fmt.Sprintf("			Listen at port for HTTP server. Default: 0, disable HTTP."))
		fmt.Println(fmt.Sprintf("	-s, -https string"))
		fmt.Println(fmt.Sprintf("			Listen at port for HTTPS server. Default: 0, disable HTTPS."))
		fmt.Println(fmt.Sprintf("	-r, -root string"))
		fmt.Println(fmt.Sprintf("			The www root path. Supports relative to argv[0]=%v. Default: ./html", path.Dir(os.Args[0])))
		fmt.Println(fmt.Sprintf("	-p, -proxy string"))
		fmt.Println(fmt.Sprintf("			Proxy path to backend. For example: http://127.0.0.1:8888/api/webrtc"))
		fmt.Println(fmt.Sprintf("Options for HTTPS(letsencrypt cert):"))
		fmt.Println(fmt.Sprintf("	-l, -lets=bool"))
		fmt.Println(fmt.Sprintf("			Whether use letsencrypt CA. Default: false"))
		fmt.Println(fmt.Sprintf("	-e, -cache string"))
		fmt.Println(fmt.Sprintf("			The letsencrypt cache. Default: ./letsencrypt.cache"))
		fmt.Println(fmt.Sprintf("	-d, -domains string"))
		fmt.Println(fmt.Sprintf("			Set the validate HTTPS domain. For example: ossrs.net,www.ossrs.net"))
		fmt.Println(fmt.Sprintf("Options for HTTPS(file-based cert):"))
		fmt.Println(fmt.Sprintf("	-k, -ssk string"))
		fmt.Println(fmt.Sprintf("			The self-sign or validate file-based key file."))
		fmt.Println(fmt.Sprintf("	-c, -ssc string"))
		fmt.Println(fmt.Sprintf("			The self-sign or validate file-based cert file."))
		fmt.Println(fmt.Sprintf("	-sdomain string"))
		fmt.Println(fmt.Sprintf("			For multiple HTTPS site, the domain name. For example: ossrs.net"))
		fmt.Println(fmt.Sprintf("	-skey string"))
		fmt.Println(fmt.Sprintf("			For multiple HTTPS site, the key file."))
		fmt.Println(fmt.Sprintf("	-scert string"))
		fmt.Println(fmt.Sprintf("			For multiple HTTPS site, the cert file."))
		fmt.Println(fmt.Sprintf("For example:"))
		fmt.Println(fmt.Sprintf("	%v -t 8080 -s 9443 -r ./html", os.Args[0]))
		fmt.Println(fmt.Sprintf("	%v -t 8080 -s 9443 -r ./html -p http://ossrs.net:1985/api/v1/versions", os.Args[0]))
		fmt.Println(fmt.Sprintf("Generate cert for self-sign HTTPS:"))
		fmt.Println(fmt.Sprintf("	openssl genrsa -out server.key 2048"))
		fmt.Println(fmt.Sprintf(`	openssl req -new -x509 -key server.key -out server.crt -days 365 -subj "/C=CN/ST=Beijing/L=Beijing/O=Me/OU=Me/CN=me.org"`))
		fmt.Println(fmt.Sprintf("For example:"))
		fmt.Println(fmt.Sprintf("	%v -s 9443 -r ./html -sdomain ossrs.net -skey ossrs.net.key -scert ossrs.net.pem", os.Args[0]))
	}
	flag.Parse()

	if useLetsEncrypt && len(httpsPorts) == 0 {
		return oe.Errorf("for letsencrypt, https=%v must be 0(disabled) or 443(enabled)", httpsPorts)
	}
	if len(httpPorts) == 0 && len(httpsPorts) == 0 {
		flag.Usage()
		os.Exit(-1)
	}

	var proxyUrls []*url.URL
	proxies := make(map[string]*url.URL)
	for _, oproxy := range []string(oproxies) {
		if oproxy == "" {
			return oe.Errorf("empty proxy in %v", oproxies)
		}

		proxyUrl, err := url.Parse(oproxy)
		if err != nil {
			return oe.Wrapf(err, "parse proxy %v", oproxy)
		}

		if _, ok := proxies[proxyUrl.Path]; ok {
			return oe.Errorf("proxy %v duplicated", proxyUrl.Path)
		}

		proxyUrls = append(proxyUrls, proxyUrl)
		proxies[proxyUrl.Path] = proxyUrl
		ol.Tf(ctx, "Proxy %v to %v", proxyUrl.Path, oproxy)
	}

	if !path.IsAbs(cacheFile) && path.IsAbs(os.Args[0]) {
		cacheFile = path.Join(path.Dir(os.Args[0]), cacheFile)
	}
	if !path.IsAbs(html) && path.IsAbs(os.Args[0]) {
		html = path.Join(path.Dir(os.Args[0]), html)
	}

	fs := http.FileServer(http.Dir(html))
	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		oh.SetHeader(w)

		if o := r.Header.Get("Origin"); len(o) > 0 {
			w.Header().Set("Access-Control-Allow-Origin", "*")
			w.Header().Set("Access-Control-Allow-Methods", "GET, POST, HEAD, PUT, DELETE, OPTIONS")
			w.Header().Set("Access-Control-Expose-Headers", "Server,range,Content-Length,Content-Range")
			w.Header().Set("Access-Control-Allow-Headers", "origin,range,accept-encoding,referer,Cache-Control,X-Proxy-Authorization,X-Requested-With,Content-Type")
		}

		// For matched OPTIONS, directly return without response.
		if r.Method == "OPTIONS" {
			return
		}

		if proxyUrls == nil {
			if r.URL.Path == "/httpx/v1/versions" {
				oh.WriteVersion(w, r, Version())
				return
			}

			fs.ServeHTTP(w, r)
			return
		}

		for _, proxyUrl := range proxyUrls {
			if !shouldProxyURL(r.URL.Path, proxyUrl.Path) {
				continue
			}

			if proxy, ok := proxies[proxyUrl.Path]; ok {
				p := NewComplexProxy(ctx, proxy, r)
				p.ServeHTTP(w, r)
				return
			}
		}

		fs.ServeHTTP(w, r)
	})

	var protos []string
	if len(httpPorts) > 0 {
		protos = append(protos, fmt.Sprintf("http(:%v)", strings.Join(httpPorts, ",")))
	}
	for _, httpsPort := range httpsPorts {
		if httpsPort == "0" {
			continue
		}

		s := httpsDomains
		if httpsDomains == "" {
			s = "all domains"
		}

		if useLetsEncrypt {
			protos = append(protos, fmt.Sprintf("https(:%v, %v, %v)", httpsPort, s, cacheFile))
		} else {
			protos = append(protos, fmt.Sprintf("https(:%v)", httpsPort))
		}

		if useLetsEncrypt {
			protos = append(protos, "letsencrypt")
		} else if ssKey != "" {
			protos = append(protos, fmt.Sprintf("self-sign(%v, %v)", ssKey, ssCert))
		} else if len(sdomains) == 0 {
			return oe.New("no ssl config")
		}

		for i := 0; i < len(sdomains); i++ {
			sdomain, skey, scert := sdomains[i], skeys[i], scerts[i]
			if f, err := os.Open(scert); err != nil {
				return oe.Wrapf(err, "open cert %v for %v err %+v", scert, sdomain, err)
			} else {
				f.Close()
			}

			if f, err := os.Open(skey); err != nil {
				return oe.Wrapf(err, "open key %v for %v err %+v", skey, sdomain, err)
			} else {
				f.Close()
			}
			protos = append(protos, fmt.Sprintf("ssl(%v,%v,%v)", sdomain, skey, scert))
		}
	}
	ol.Tf(ctx, "%v html root at %v", strings.Join(protos, ", "), string(html))

	if len(httpsPorts) > 0 && !useLetsEncrypt && ssKey != "" {
		if f, err := os.Open(ssCert); err != nil {
			return oe.Wrapf(err, "open cert %v err %+v", ssCert, err)
		} else {
			f.Close()
		}

		if f, err := os.Open(ssKey); err != nil {
			return oe.Wrapf(err, "open key %v err %+v", ssKey, err)
		} else {
			f.Close()
		}
	}

	wg := sync.WaitGroup{}
	ctx, cancel := context.WithCancel(ctx)
	defer cancel()

	var httpServers []*http.Server

	for _, v := range httpPorts {
		httpPort, err := strconv.ParseInt(v, 10, 64)
		if err != nil {
			return oe.Wrapf(err, "parse %v", v)
		}

		wg.Add(1)
		go func(httpPort int) {
			defer wg.Done()

			ctx = ol.WithContext(ctx)
			if httpPort == 0 {
				ol.W(ctx, "http server disabled")
				return
			}

			defer cancel()
			hs := &http.Server{Addr: fmt.Sprintf(":%v", httpPort), Handler: nil}
			httpServers = append(httpServers, hs)
			ol.Tf(ctx, "http serve at %v", httpPort)

			if err := hs.ListenAndServe(); err != nil {
				ol.Ef(ctx, "http serve err %+v", err)
				return
			}
			ol.T("http server ok")
		}(int(httpPort))
	}

	for _, v := range httpsPorts {
		httpsPort, err := strconv.ParseInt(v, 10, 64)
		if err != nil {
			return oe.Wrapf(err, "parse %v", v)
		}

		wg.Add(1)
		go func() {
			defer wg.Done()

			ctx = ol.WithContext(ctx)
			if httpsPort == 0 {
				ol.W(ctx, "https server disabled")
				return
			}

			defer cancel()

			var err error
			var m https.Manager
			if useLetsEncrypt {
				var domains []string
				if httpsDomains != "" {
					domains = strings.Split(httpsDomains, ",")
				}

				if m, err = https.NewLetsencryptManager("", domains, cacheFile); err != nil {
					ol.Ef(ctx, "create letsencrypt manager err %+v", err)
					return
				}
			} else if ssKey != "" {
				if m, err = https.NewSelfSignManager(ssCert, ssKey); err != nil {
					ol.Ef(ctx, "create self-sign manager err %+v", err)
					return
				}
			} else if len(sdomains) > 0 {
				if m, err = NewCertsManager(sdomains, skeys, scerts); err != nil {
					ol.Ef(ctx, "create ssl managers err %+v", err)
					return
				}
			}

			hss := &http.Server{
				Addr: fmt.Sprintf(":%v", httpsPort),
				TLSConfig: &tls.Config{
					GetCertificate: m.GetCertificate,
				},
			}
			httpServers = append(httpServers, hss)
			ol.Tf(ctx, "https serve at %v", httpsPort)

			if err = hss.ListenAndServeTLS("", ""); err != nil {
				ol.Ef(ctx, "https serve err %+v", err)
				return
			}
			ol.T("https serve ok")
		}()
	}

	select {
	case <-ctx.Done():
		for _, server := range httpServers {
			server.Close()
		}
	}
	wg.Wait()

	return nil
}

func main() {
	ctx := ol.WithContext(context.Background())
	if err := run(ctx); err != nil {
		ol.Ef(ctx, "run err %+v", err)
		os.Exit(-1)
	}
}
