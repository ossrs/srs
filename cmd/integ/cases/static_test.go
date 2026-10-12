// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import (
	"bufio"
	"bytes"
	"context"
	"crypto/rand"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"srs-integ/internal/check"
	"srs-integ/internal/server"
)

// staticMimeTypes are the extensions SRS knows and the Content-Type it
// answers each with; any other is application/octet-stream. FLV is not one
// of them: on Common every .flv path is an HTTP-FLV stream of the remux.
var staticMimeTypes = []struct{ ext, mime string }{
	{"html", "text/html"},
	{"js", "text/javascript"},
	{"css", "text/css"},
	{"json", "application/json"},
	{"xml", "text/xml"},
	{"ico", "image/x-icon"},
	{"png", "image/png"},
	{"jpg", "image/jpeg"},
	{"jpeg", "image/jpeg"},
	{"gif", "image/gif"},
	{"mp3", "audio/mpeg"},
	{"aac", "audio/x-aac"},
	{"m4a", "audio/x-m4a"},
	{"ogg", "audio/ogg"},
	{"zip", "application/zip"},
	{"mpd", "application/dash+xml"},
	{"ts", "video/MP2T"},
	{"m4s", "video/iso.segment"},
	{"mp4", "video/mp4"},
}

// TestHttpStatic_Common serves files on the Common server: the HTTP server's
// own dir, ./objs/nginx/html, at every path no vhost mounts, and the default
// vhost's files (http_static, server.CommonStatic) at /static/. Each case
// writes its files in a dir of its own, named like a stream, and asks for
// them; the paths it must not get are files outside both dirs. A case of one
// request is Fast; one of several requests, or of 1 MiB, is Normal, since a
// busy Common shard answers each request in up to 50ms.
func TestHttpStatic_Common(t *testing.T) {
	t.Parallel()

	// ServesFile gets a file: 200, its bytes, and its Content-Length.
	check.Case(t, "ServesFile", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		dir := streamName(c)
		data := []byte("file of " + dir)
		writeStatic(c, filepath.Join(srs.WebRoot(), dir, "a.dat"), data)

		res, body := staticGet(c, srs, "/"+dir+"/a.dat", nil)
		c.CheckEqual(res.StatusCode, 200, "status")
		c.Check(bytes.Equal(body, data), "body %q, want %q", body, data)
		c.CheckEqual(res.ContentLength, int64(len(data)), "Content-Length")
	})

	// LargeFile gets a file of 1 MiB of random bytes, all of them.
	check.Case(t, "LargeFile", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		dir := streamName(c)
		data := make([]byte, 1<<20)
		rand.Read(data)
		writeStatic(c, filepath.Join(srs.WebRoot(), dir, "large.bin"), data)

		res, body := staticGet(c, srs, "/"+dir+"/large.bin", nil)
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(res.ContentLength, int64(len(data)), "Content-Length")
		c.Check(bytes.Equal(body, data), "body of %d bytes differs from the file's %d", len(body), len(data))
	})

	// Mime<Ext> gets a file of each extension SRS knows: its Content-Type.
	for _, m := range staticMimeTypes {
		check.Case(t, "Mime"+strings.ToUpper(m.ext[:1])+m.ext[1:], check.Fast, func(c *check.C) {
			checkStaticType(c, server.Common(c), "f."+m.ext, m.mime)
		})
	}

	// MimeUnknown gets a file of an extension SRS doesn't know, and one with
	// none: application/octet-stream.
	check.Case(t, "MimeUnknown", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		checkStaticType(c, srs, "f.txt", "application/octet-stream")
		checkStaticType(c, srs, "README", "application/octet-stream")
	})

	// QueryIgnored gets a file with a query: the file.
	check.Case(t, "QueryIgnored", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		dir := streamName(c)
		writeStatic(c, filepath.Join(srs.WebRoot(), dir, "a.json"), []byte(`{}`))

		res, body := staticGet(c, srs, "/"+dir+"/a.json?v=1&k=x", nil)
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(string(body), `{}`, "body")
	})

	// IndexOfDir asks for a dir with its slash: its index.html, as HTML.
	check.Case(t, "IndexOfDir", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		dir := streamName(c)
		page := "<p>" + dir + "</p>"
		writeStatic(c, filepath.Join(srs.WebRoot(), dir, "index.html"), []byte(page))

		res, body := staticGet(c, srs, "/"+dir+"/", nil)
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(res.Header.Get("Content-Type"), "text/html", "Content-Type")
		c.CheckEqual(string(body), page, "body")
	})

	// DirWithoutIndex asks for a dir with no index.html: 404, not a listing.
	check.Case(t, "DirWithoutIndex", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		dir := streamName(c)
		writeStatic(c, filepath.Join(srs.WebRoot(), dir, "a.dat"), []byte("a"))

		res, body := staticGet(c, srs, "/"+dir+"/", nil)
		c.CheckEqual(res.StatusCode, 404, "status")
		c.Check(!bytes.Contains(body, []byte("a.dat")), "the 404 lists the dir: %q", body)
	})

	// NotFound asks for a missing file, and a file in a missing dir: 404.
	check.Case(t, "NotFound", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		dir := streamName(c)
		writeStatic(c, filepath.Join(srs.WebRoot(), dir, "a.dat"), []byte("a"))

		for _, p := range []string{"/" + dir + "/b.dat", "/" + dir + "-none/a.dat"} {
			res, body := staticGet(c, srs, p, nil)
			c.CheckEqual(res.StatusCode, 404, p+" status")
			c.CheckEqual(string(body), "Not Found", p+" body")
		}
	})

	// DirWithoutSlash asks for a dir without its slash: an HTTP answer, a
	// redirect to the dir or 404.
	check.Case(t, "DirWithoutSlash", check.Normal, func(c *check.C) {
		check.KnownBug(c, "a dir without its slash gets no answer, SRS fails to read the dir as a file")
		srs := server.Common(c)
		dir := streamName(c)
		writeStatic(c, filepath.Join(srs.WebRoot(), dir, "index.html"), []byte("<p>index</p>"))

		status, _, err := rawStaticGet(c, srs.Addr(server.HTTPServer), "/"+dir)
		if c.CheckNoError(err, "the answer") {
			c.Check(status == 301 || status == 302 || status == 404, "status %d, want a redirect to the dir or 404", status)
		}
		checkStaticServes(c, srs)
	})

	// TraversalNotServed asks for a file outside the dir through "..": SRS
	// doesn't serve it, and serves the next request.
	check.Case(t, "TraversalNotServed", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		secret := writeSecret(c, srs)
		dir := streamName(c)
		writeStatic(c, filepath.Join(srs.WebRoot(), dir, "a.dat"), []byte("a"))

		for _, p := range []string{"/../../../" + secret, "/" + dir + "/../../../../" + secret, "/" + dir + "/./../../../../" + secret} {
			checkNotServed(c, srs, p, secret)
		}
		checkStaticServes(c, srs)
	})

	// TraversalAnswered asks for a path with "..": a 400, which tells the
	// client why.
	check.Case(t, "TraversalAnswered", check.Fast, func(c *check.C) {
		check.KnownBug(c, "a path with .. gets no answer, the connection closed")
		srs := server.Common(c)
		status, _, err := rawStaticGet(c, srs.Addr(server.HTTPServer), "/a/../"+streamName(c)+".dat")
		if c.CheckNoError(err, "the answer") {
			c.CheckEqual(status, 400, "status")
		}
	})

	// EncodedTraversalNotServed asks for a file outside the dir through ".."
	// with its dots or slashes percent-encoded: SRS doesn't serve it.
	check.Case(t, "EncodedTraversalNotServed", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		secret := writeSecret(c, srs)
		for _, p := range []string{
			"/%2e%2e/%2e%2e/%2e%2e/" + secret,
			"/%2E%2E/%2E%2E/%2E%2E/" + secret,
			"/..%2f..%2f..%2f" + secret,
			"/%2e%2e%2f%2e%2e%2f%2e%2e%2f" + secret,
		} {
			checkNotServed(c, srs, p, secret)
		}
		checkStaticServes(c, srs)
	})

	// EscapedFileName gets a file whose name has a space, percent-encoded in
	// the URL as it must be: the file.
	check.Case(t, "EscapedFileName", check.Fast, func(c *check.C) {
		check.KnownBug(c, "the URL path is not percent-decoded, so a file whose name needs escaping is 404")
		srs := server.Common(c)
		dir := streamName(c)
		writeStatic(c, filepath.Join(srs.WebRoot(), dir, "a b.dat"), []byte("spaced"))

		res, body := staticGet(c, srs, "/"+dir+"/a%20b.dat", nil)
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(string(body), "spaced", "body")
	})

	// CorsHeaders gets a file with an Origin header: the CORS headers, as
	// crossdomain is on by default; none without one.
	check.Case(t, "CorsHeaders", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		dir := streamName(c)
		writeStatic(c, filepath.Join(srs.WebRoot(), dir, "a.js"), []byte("x"))

		res, _ := staticGet(c, srs, "/"+dir+"/a.js", originHeader)
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(res.Header.Get("Access-Control-Allow-Origin"), "*", "Access-Control-Allow-Origin")
		c.CheckEqual(res.Header.Get("Access-Control-Allow-Methods"), "*", "Access-Control-Allow-Methods")

		res, _ = staticGet(c, srs, "/"+dir+"/a.js", nil)
		c.CheckEqual(res.Header.Get("Access-Control-Allow-Origin"), "", "Access-Control-Allow-Origin without an Origin")
	})

	// CorsPreflight sends the OPTIONS preflight for a file: 200, the CORS
	// headers and no body.
	check.Case(t, "CorsPreflight", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		dir := streamName(c)
		writeStatic(c, filepath.Join(srs.WebRoot(), dir, "a.js"), []byte("x"))

		res, body := staticDo(c, srs, http.MethodOptions, "/"+dir+"/a.js", originHeader)
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(res.Header.Get("Access-Control-Allow-Origin"), "*", "Access-Control-Allow-Origin")
		c.CheckEqual(len(body), 0, "body bytes")
	})

	// HttpsServesFile gets a file from the HTTPS server, which serves the
	// same dirs.
	check.Case(t, "HttpsServesFile", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		dir := streamName(c)
		writeStatic(c, filepath.Join(srs.WebRoot(), dir, "a.css"), []byte("p{}"))
		writeStatic(c, filepath.Join(server.CommonStatic.DirIn(srs), dir, "b.css"), []byte("b{}"))

		cfg, err := server.ClientTLS()
		c.Must(err, "the client TLS config")
		tr := httpClient.Transport.(*http.Transport).Clone()
		tr.TLSClientConfig = cfg
		client := &http.Client{Transport: tr, Timeout: time.Second}
		c.Cleanup(tr.CloseIdleConnections)
		for path, want := range map[string]string{
			"/" + dir + "/a.css":                           "p{}",
			server.CommonStatic.URLPath() + dir + "/b.css": "b{}",
		} {
			res, err := client.Get("https://" + srs.Addr(server.HTTPSServer) + path)
			c.Must(err, "GET "+path)
			body, err := io.ReadAll(res.Body)
			res.Body.Close()
			c.CheckNoError(err, "read "+path)
			c.CheckEqual(res.StatusCode, 200, path+" status")
			c.CheckEqual(res.Header.Get("Content-Type"), "text/css", path+" Content-Type")
			c.CheckEqual(string(body), want, path+" body")
		}
	})

	// VhostMount gets a file of the vhost's dir, ./static/[vhost] with the
	// default vhost's name, at its mount, [vhost]/static/ served at /static/.
	check.Case(t, "VhostMount", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		dir := streamName(c)
		writeStatic(c, filepath.Join(server.CommonStatic.DirIn(srs), dir, "v.css"), []byte(dir))

		res, body := staticGet(c, srs, server.CommonStatic.URLPath()+dir+"/v.css", nil)
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(res.Header.Get("Content-Type"), "text/css", "Content-Type")
		c.CheckEqual(string(body), dir, "body")
	})

	// VhostMountShadowsRoot puts files at the same path under the HTTP
	// server's dir: the mount serves only its own dir, so a file there is
	// served and one only in the server's dir is 404.
	check.Case(t, "VhostMountShadowsRoot", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		dir := streamName(c)
		rootDir := filepath.Join(srs.WebRoot(), strings.Trim(server.CommonStatic.URLPath(), "/"), dir)
		writeStatic(c, filepath.Join(rootDir, "both.dat"), []byte("root"))
		writeStatic(c, filepath.Join(rootDir, "root-only.dat"), []byte("root"))
		writeStatic(c, filepath.Join(server.CommonStatic.DirIn(srs), dir, "both.dat"), []byte("vhost"))

		res, body := staticGet(c, srs, server.CommonStatic.URLPath()+dir+"/both.dat", nil)
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(string(body), "vhost", "body")
		res, _ = staticGet(c, srs, server.CommonStatic.URLPath()+dir+"/root-only.dat", nil)
		c.CheckEqual(res.StatusCode, 404, "status of a file only in the server's dir")
	})

	// VhostMountIndex asks for a dir of the mount with its slash: its
	// index.html.
	check.Case(t, "VhostMountIndex", check.Fast, func(c *check.C) {
		srs := server.Common(c)
		dir := streamName(c)
		writeStatic(c, filepath.Join(server.CommonStatic.DirIn(srs), dir, "index.html"), []byte("<p>vhost</p>"))

		res, body := staticGet(c, srs, server.CommonStatic.URLPath()+dir+"/", nil)
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(string(body), "<p>vhost</p>", "body")
	})

	// VhostMountTraversalNotServed asks for a file outside the vhost's dir
	// through "..", plain and percent-encoded, under the mount: SRS doesn't
	// serve it.
	check.Case(t, "VhostMountTraversalNotServed", check.Normal, func(c *check.C) {
		srs := server.Common(c)
		secret := writeSecret(c, srs)
		mount := server.CommonStatic.URLPath()
		for _, p := range []string{mount + "../../" + secret, mount + "%2e%2e/%2e%2e/" + secret} {
			checkNotServed(c, srs, p, secret)
		}
		checkStaticServes(c, srs)
	})

	vodMp4Cases(t)
	vodM3u8Cases(t)
}

// staticCrossdomainOffCases adds the static file cases of a server with the
// HTTP server's crossdomain off to its group.
func staticCrossdomainOffCases(t *testing.T, srs *server.Server) {
	// StaticNoCorsHeaders gets a file with an Origin header: no CORS
	// headers; the preflight is 405.
	check.Case(t, "StaticNoCorsHeaders", check.Normal, func(c *check.C) {
		dir := streamName(c)
		writeStatic(c, filepath.Join(srs.WebRoot(), dir, "a.js"), []byte("x"))

		res, body := staticGet(c, srs, "/"+dir+"/a.js", originHeader)
		c.CheckEqual(res.StatusCode, 200, "status")
		c.CheckEqual(string(body), "x", "body")
		c.CheckEqual(res.Header.Get("Access-Control-Allow-Origin"), "", "Access-Control-Allow-Origin")
		res, _ = staticDo(c, srs, http.MethodOptions, "/"+dir+"/a.js", originHeader)
		c.CheckEqual(res.StatusCode, 405, "preflight status")
	})
}

// writeStatic writes a file for the server to serve, with its dirs, and
// removes it when the case ends.
func writeStatic(c *check.C, file string, data []byte) {
	c.Helper()
	c.Must(os.MkdirAll(filepath.Dir(file), 0o755), "make the file's dir")
	c.Must(os.WriteFile(file, data, 0o644), "write the file")
	c.Cleanup(func() { os.Remove(file) })
}

// writeSecret writes a file in the server's directory, outside every dir it
// serves, three levels above the HTTP server's dir, and returns its name.
func writeSecret(c *check.C, srs *server.Server) string {
	c.Helper()
	name := streamName(c) + ".secret"
	writeStatic(c, filepath.Join(srs.Dir, name), []byte("secret of "+name))
	return name
}

// staticGet gets a path of the server's HTTP server, with the header, and
// returns the answer and its body.
func staticGet(c *check.C, srs *server.Server, path string, header http.Header) (*http.Response, []byte) {
	c.Helper()
	return staticDo(c, srs, http.MethodGet, path, header)
}

// staticDo sends a request for a path to the server's HTTP server.
func staticDo(c *check.C, srs *server.Server, method, path string, header http.Header) (*http.Response, []byte) {
	c.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), time.Second)
	defer cancel()
	req, err := http.NewRequestWithContext(ctx, method, "http://"+srs.Addr(server.HTTPServer)+path, nil)
	c.Must(err, "the request")
	for k, v := range header {
		req.Header[k] = v
	}
	res, err := httpClient.Do(req)
	c.Must(err, method+" "+path)
	defer res.Body.Close()
	body, err := io.ReadAll(res.Body)
	c.CheckNoError(err, "read "+path)
	return res, body
}

// rawStaticGet sends a GET of target as it is, which a client library might
// clean, and reads the answer for up to 1s: its status and body, or an error
// when the server closes the connection without one.
func rawStaticGet(c *check.C, addr, target string) (int, []byte, error) {
	c.Helper()
	conn := rawConnTo(c, addr)
	conn.SetDeadline(time.Now().Add(time.Second))
	if _, err := fmt.Fprintf(conn, "GET %s HTTP/1.1\r\nHost: localhost\r\n\r\n", target); err != nil {
		return 0, nil, err
	}
	res, err := http.ReadResponse(bufio.NewReader(conn), nil)
	if err != nil {
		return 0, nil, err
	}
	defer res.Body.Close()
	body, err := io.ReadAll(res.Body)
	return res.StatusCode, body, err
}

// checkStaticType gets a file of the name and checks its Content-Type.
func checkStaticType(c *check.C, srs *server.Server, name, mime string) {
	c.Helper()
	dir := streamName(c)
	writeStatic(c, filepath.Join(srs.WebRoot(), dir, name), []byte("data of "+name))

	res, body := staticGet(c, srs, "/"+dir+"/"+name, nil)
	c.CheckEqual(res.StatusCode, 200, name+" status")
	c.CheckEqual(res.Header.Get("Content-Type"), mime, name+" Content-Type")
	c.CheckEqual(string(body), "data of "+name, name+" body")
}

// checkNotServed sends a GET of target as it is and checks the answer is not
// the secret file: any answer but 200, or the connection closed.
func checkNotServed(c *check.C, srs *server.Server, target, secret string) {
	c.Helper()
	status, body, err := rawStaticGet(c, srs.Addr(server.HTTPServer), target)
	c.Check(!isTimeout(err), "%s: no answer nor close in 1s", target)
	c.Check(err != nil || status != 200, "%s: 200, want it refused", target)
	c.Check(!bytes.Contains(body, []byte("secret of "+secret)), "%s: served the file outside the dir", target)
}

// checkStaticServes checks the server still serves a file.
func checkStaticServes(c *check.C, srs *server.Server) {
	c.Helper()
	dir := streamName(c)
	writeStatic(c, filepath.Join(srs.WebRoot(), dir, "after.dat"), []byte("after"))
	res, body := staticGet(c, srs, "/"+dir+"/after.dat", nil)
	c.CheckEqual(res.StatusCode, 200, "status of a file after")
	c.CheckEqual(string(body), "after", "body of a file after")
}
