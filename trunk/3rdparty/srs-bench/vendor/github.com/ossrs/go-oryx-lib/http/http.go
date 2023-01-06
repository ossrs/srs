// The MIT License (MIT)
//
// Copyright (c) 2013-2017 Oryx(ossrs)
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

// The oryx http package provides standard request and response in json.
//			Error, when error, use this handler.
//			CplxError, for complex error, use this handler.
//			Data, when no error, use this handler.
//			SystemError, application level error code.
//			SetHeader, for direclty response the raw stream.
// The standard server response:
//			code, an int error code.
//			data, specifies the data.
// The api for simple api:
//			WriteVersion, to directly response the version.
//			WriteData, to directly write the data in json.
//			WriteError, to directly write the error.
//			WriteCplxError, to directly write the complex error.
// The global variables:
//			oh.Server, to set the response header["Server"].
package http

import (
	"encoding/json"
	"fmt"
	ol "github.com/ossrs/go-oryx-lib/logger"
	"net/http"
	"os"
	"strconv"
	"strings"
)

// header["Content-Type"] in response.
const (
	HttpJson       = "application/json"
	HttpJavaScript = "application/javascript"
)

// header["Server"] in response.
var Server = "Oryx"

// system int error.
type SystemError int

func (v SystemError) Error() string {
	return fmt.Sprintf("System error=%d", int(v))
}

// system conplex error.
type SystemComplexError struct {
	// the system error code.
	Code SystemError `json:"code"`
	// the description for this error.
	Message string `json:"data"`
}

func (v SystemComplexError) Error() string {
	return fmt.Sprintf("%v, %v", v.Code.Error(), v.Message)
}

// application level, with code.
type AppError interface {
	Code() int
	error
}

// HTTP Status Code
type HTTPStatus interface {
	Status() int
}

// http standard error response.
// @remark for not SystemError, we will use logger.E to print it.
// @remark user can use WriteError() for simple api.
func Error(ctx ol.Context, err error) http.Handler {
	// for complex error, use code instead.
	if v, ok := err.(SystemComplexError); ok {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			jsonHandler(ctx, FilterCplxSystemError(ctx, w, r, v)).ServeHTTP(w, r)
		})
	}

	// for int error, use code instead.
	if v, ok := err.(SystemError); ok {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			jsonHandler(ctx, FilterSystemError(ctx, w, r, v)).ServeHTTP(w, r)
		})
	}

	// for application error, use code instead.
	if v, ok := err.(AppError); ok {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			jsonHandler(ctx, FilterAppError(ctx, w, r, v)).ServeHTTP(w, r)
		})
	}

	// unknown error, log and response detail
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		SetHeader(w)
		w.Header().Set("Content-Type", HttpJson)

		status := http.StatusInternalServerError
		if v, ok := err.(HTTPStatus); ok {
			status = v.Status()
		}

		http.Error(w, FilterError(ctx, w, r, err), status)
	})
}

// Wrapper for complex error use Error(ctx, SystemComplexError{})
// @remark user can use WriteCplxError() for simple api.
func CplxError(ctx ol.Context, code SystemError, message string) http.Handler {
	return Error(ctx, SystemComplexError{code, message})
}

// http normal response.
// @remark user can use nil v to response success, which data is null.
// @remark user can use WriteData() for simple api.
func Data(ctx ol.Context, v interface{}) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		jsonHandler(ctx, FilterData(ctx, w, r, v)).ServeHTTP(w, r)
	})
}

// set http header, for directly use the w,
// for example, user want to directly write raw text.
func SetHeader(w http.ResponseWriter) {
	w.Header().Set("Server", Server)
}

// response json directly.
func jsonHandler(ctx ol.Context, rv interface{}) http.Handler {
	var err error
	var b []byte
	if b, err = json.Marshal(rv); err != nil {
		return Error(ctx, err)
	}

	status := http.StatusOK
	if v, ok := rv.(HTTPStatus); ok {
		status = v.Status()
	}

	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		SetHeader(w)

		q := r.URL.Query()
		if cb := q.Get("callback"); cb != "" {
			w.Header().Set("Content-Type", HttpJavaScript)
			if status != http.StatusOK {
				w.WriteHeader(status)
			}

			// TODO: Handle error.
			fmt.Fprintf(w, "%s(%s)", cb, string(b))
		} else {
			w.Header().Set("Content-Type", HttpJson)
			if status != http.StatusOK {
				w.WriteHeader(status)
			}
			// TODO: Handle error.
			w.Write(b)
		}
	})
}

// response the standard version info:
// 	{code, server, data} where server is the server pid, and data is below object:
//	{major, minor, revision, extra, version, signature}
// @param version in {major.minor.revision-extra}, where -extra is optional,
//	for example: 1.0.0 or 1.0.0-0 or 1.0.0-1
func WriteVersion(w http.ResponseWriter, r *http.Request, version string) {
	var major, minor, revision, extra int

	versions := strings.Split(version, "-")
	if len(versions) > 1 {
		extra, _ = strconv.Atoi(versions[1])
	}

	versions = strings.Split(versions[0], ".")
	if len(versions) > 0 {
		major, _ = strconv.Atoi(versions[0])
	}
	if len(versions) > 1 {
		minor, _ = strconv.Atoi(versions[1])
	}
	if len(versions) > 2 {
		revision, _ = strconv.Atoi(versions[2])
	}

	Data(nil, map[string]interface{}{
		"major":     major,
		"minor":     minor,
		"revision":  revision,
		"extra":     extra,
		"version":   version,
		"signature": Server,
	}).ServeHTTP(w, r)
}

// Directly write json data, a wrapper for Data().
// @remark user can use Data() for group of complex apis.
func WriteData(ctx ol.Context, w http.ResponseWriter, r *http.Request, v interface{}) {
	Data(ctx, v).ServeHTTP(w, r)
}

// Directly write success json response, same to WriteData(ctx, w, r, nil).
func Success(ctx ol.Context, w http.ResponseWriter, r *http.Request) {
	WriteData(ctx, w, r, nil)
}

// Directly write error, a wrapper for Error().
// @remark user can use Error() for group of complex apis.
func WriteError(ctx ol.Context, w http.ResponseWriter, r *http.Request, err error) {
	Error(ctx, err).ServeHTTP(w, r)
}

// Directly write complex error, a wrappter for CplxError().
// @remark user can use CplxError() for group of complex apis.
func WriteCplxError(ctx ol.Context, w http.ResponseWriter, r *http.Request, code SystemError, message string) {
	CplxError(ctx, code, message).ServeHTTP(w, r)
}

// for hijack to define the response structure.
// user can redefine these functions for special response.
var FilterCplxSystemError = func(ctx ol.Context, w http.ResponseWriter, r *http.Request, o SystemComplexError) interface{} {
	ol.Ef(ctx, "Serve %v failed, err is %+v", r.URL, o)
	return o
}
var FilterSystemError = func(ctx ol.Context, w http.ResponseWriter, r *http.Request, o SystemError) interface{} {
	ol.Ef(ctx, "Serve %v failed, err is %+v", r.URL, o)
	return map[string]int{"code": int(o)}
}
var FilterAppError = func(ctx ol.Context, w http.ResponseWriter, r *http.Request, err AppError) interface{} {
	ol.Ef(ctx, "Serve %v failed, err is %+v", r.URL, err)
	return map[string]interface{}{"code": err.Code(), "data": err.Error()}
}
var FilterError = func(ctx ol.Context, w http.ResponseWriter, r *http.Request, err error) string {
	ol.Ef(ctx, "Serve %v failed, err is %+v", r.URL, err)
	return err.Error()
}
var FilterData = func(ctx ol.Context, w http.ResponseWriter, r *http.Request, o interface{}) interface{} {
	rv := map[string]interface{}{
		"code":   0,
		"server": os.Getpid(),
		"data":   o,
	}

	// for string, directly use it without convert,
	// for the type covert by golang maybe modify the content.
	if v, ok := o.(string); ok {
		rv["data"] = v
	}

	return rv
}
