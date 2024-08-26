package main

import (
	"context"
	"srs-proxy/log"
)

func main() {
	ctx := log.WithContext(context.Background())
	log.If(ctx, "SRS Proxy server started")
}
