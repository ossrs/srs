package main

import (
	"bytes"
	"context"
	"fmt"
	"github.com/golang/protobuf/proto"
	"go.opentelemetry.io/otel"
	"go.opentelemetry.io/otel/attribute"
	"go.opentelemetry.io/otel/codes"
	"go.opentelemetry.io/otel/exporters/otlp/otlptrace"
	"go.opentelemetry.io/otel/propagation"
	"go.opentelemetry.io/otel/sdk/resource"
	sdktrace "go.opentelemetry.io/otel/sdk/trace"
	"go.opentelemetry.io/otel/trace"
	coltracepb "go.opentelemetry.io/proto/otlp/collector/trace/v1"
	tracepb "go.opentelemetry.io/proto/otlp/trace/v1"
	"net/http"
	"os"
	"time"
)

type httpRawClientForAPM struct {
	endpoint, urlPath string
}

func (v *httpRawClientForAPM) Start(ctx context.Context) error {
	fmt.Println("http1: Transport start")
	return nil
}

func (v *httpRawClientForAPM) Stop(ctx context.Context) error {
	fmt.Println("http1: Transport stop")
	return nil
}

func (v *httpRawClientForAPM) UploadTraces(ctx context.Context, protoSpans []*tracepb.ResourceSpans) error {
	// Wrap with request message.
	m := &coltracepb.ExportTraceServiceRequest{
		ResourceSpans: protoSpans,
	}

	// Marshal to bytes.
	b, err := proto.Marshal(m)
	if err != nil {
		return err
	}

	// Upload by HTTP/1
	url := fmt.Sprintf("http://%v%v", v.endpoint, v.urlPath)
	r, err := http.NewRequest("POST", url, bytes.NewReader(b))
	if err != nil {
		return err
	}
	r.Header.Set("Content-Type", "application/x-protobuf") // For http

	res, err := http.DefaultClient.Do(r)
	if err != nil {
		return err
	}
	defer res.Body.Close()

	var nn int
	for _, span := range protoSpans {
		for _, span2 := range span.ScopeSpans {
			nn += len(span2.Spans)
		}
	}

	fmt.Println(fmt.Sprintf("http1: Upload %v/%v spans to %v%v, response is %v", len(protoSpans), nn, v.endpoint, v.urlPath, res.Status))
	return nil
}

func main() {
	if os.Getenv("TOKEN") == "" {
		panic("no env TOKEN, see https://console.cloud.tencent.com/apm/monitor/access")
	}

	// The gRPC client defaults to https://localhost:4317 and the HTTP client https://localhost:4318.
	// See https://github.com/open-telemetry/opentelemetry-go/tree/main/exporters/otlp/otlptrace
	endpoint := "ap-guangzhou.apm.tencentcs.com:55681"
	urlPath := "/v1/traces"
	service := "srs-server"
	fmt.Println(fmt.Sprintf("main: APM endpoint=%v, urlPath=%v, token=%vB, service=%v", endpoint, urlPath, len(os.Getenv("TOKEN")), service))

	// Create exporter to upload spans over HTTP/1.1
	ctx := context.Background()
	exporter, err := otlptrace.New(ctx, &httpRawClientForAPM{endpoint, urlPath})
	if err != nil {
		panic(err)
	}

	// Create resource for tracer provider.
	r, err := resource.New(ctx, []resource.Option{
		resource.WithAttributes(attribute.KeyValue{
			Key: "token", Value: attribute.StringValue(os.Getenv("TOKEN")),
		}),
		resource.WithAttributes(attribute.KeyValue{
			Key: "service.name", Value: attribute.StringValue(service),
		}),
	}...)
	if err != nil {
		panic(err)
	}

	// Create tracer provider for tracing.
	tp := sdktrace.NewTracerProvider(
		sdktrace.WithSampler(sdktrace.AlwaysSample()),
		sdktrace.WithBatcher(exporter),
		sdktrace.WithResource(r),
	)
	defer tp.Shutdown(ctx)

	otel.SetTracerProvider(tp)
	otel.SetTextMapPropagator(propagation.NewCompositeTextMapPropagator(propagation.TraceContext{}, propagation.Baggage{}))
	fmt.Println("main: Init ok")

	// Create global tracer.
	tracer := otel.GetTracerProvider().Tracer("app")
	ctx, span := tracer.Start(context.Background(), "main", trace.WithSpanKind(trace.SpanKindServer))
	time.Sleep(100 * time.Millisecond)
	defer span.End()

	ctx, span2 := tracer.Start(ctx, "sub", trace.WithSpanKind(trace.SpanKindInternal))
	time.Sleep(70 * time.Millisecond)
	span2.End()

	sc := span.SpanContext()
	fmt.Println(fmt.Sprintf("main: Done, trace=%v, span=%v", sc.TraceID().String(), sc.SpanID().String()))

	foo := func() {
		_, span5 := tracer.Start(ctx, "failed")
		span5.RecordError(fmt.Errorf("default err service=%v", service), trace.WithStackTrace(true))
		span5.SetStatus(codes.Error, fmt.Sprintf("failed span endpoint=%v", endpoint))
		span5.End()
	}
	foo()

	// Link for span.
	_, span6 := tracer.Start(context.Background(), "linker", trace.WithSpanKind(trace.SpanKindServer),
		trace.WithNewRoot(), trace.WithLinks(trace.LinkFromContext(ctx)))
	defer span6.End()

	// Propagator over HTTP header, start a client span and inject HTTP header.
	ctx, span3 := tracer.Start(ctx, "call", trace.WithSpanKind(trace.SpanKindClient))
	defer span3.End()

	header := make(http.Header)
	propagators := otel.GetTextMapPropagator()
	propagators.Inject(ctx, propagation.HeaderCarrier(header))
	fmt.Println(fmt.Sprintf("main: Save to header %v, span=%v", header, span3.SpanContext().SpanID().String()))

	// Mock another service.
	mockServer(endpoint, urlPath, "origin", header)
}

func mockServer(endpoint, urlPath, service string, header http.Header) {
	// Create exporter to upload spans over HTTP/1.1
	ctx := context.Background()
	exporter, err := otlptrace.New(ctx, &httpRawClientForAPM{endpoint, urlPath})
	if err != nil {
		panic(err)
	}

	// Create resource for tracer provider.
	r, err := resource.New(ctx, []resource.Option{
		resource.WithAttributes(attribute.KeyValue{
			Key: "token", Value: attribute.StringValue(os.Getenv("TOKEN")),
		}),
		resource.WithAttributes(attribute.KeyValue{
			Key: "service.name", Value: attribute.StringValue(service),
		}),
	}...)
	if err != nil {
		panic(err)
	}

	// Create tracer provider for tracing.
	tp := sdktrace.NewTracerProvider(
		sdktrace.WithSampler(sdktrace.AlwaysSample()),
		sdktrace.WithBatcher(exporter),
		sdktrace.WithResource(r),
	)
	defer tp.Shutdown(ctx)

	otel.SetTracerProvider(tp)
	otel.SetTextMapPropagator(propagation.NewCompositeTextMapPropagator(propagation.TraceContext{}, propagation.Baggage{}))
	fmt.Println("main: Init ok")

	tracer := otel.GetTracerProvider().Tracer("app")
	propagators := otel.GetTextMapPropagator()

	ctx = propagators.Extract(context.Background(), propagation.HeaderCarrier(header))
	ctx, span4 := tracer.Start(ctx, "callee", trace.WithSpanKind(trace.SpanKindServer))
	defer span4.End()
	fmt.Println(fmt.Sprintf("main: Restore from header %v, span=%v", header, span4.SpanContext().SpanID().String()))
}
