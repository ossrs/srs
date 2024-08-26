// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import "os"

// setEnvDefault set env key=value if not set.
func setEnvDefault(key, value string) {
	if os.Getenv(key) == "" {
		os.Setenv(key, value)
	}
}

func envHttpAPI() string {
	return os.Getenv("PROXY_HTTP_API")
}

func envGoPprof() string {
	return os.Getenv("GO_PPROF")
}
