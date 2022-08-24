package main

import (
	"context"
	"fmt"
	"go.opentelemetry.io/otel"
	"go.opentelemetry.io/otel/attribute"
	"go.opentelemetry.io/otel/exporters/otlp/otlptrace/otlptracehttp"
	"go.opentelemetry.io/otel/propagation"
	"go.opentelemetry.io/otel/sdk/resource"
	sdktrace "go.opentelemetry.io/otel/sdk/trace"
	"go.opentelemetry.io/otel/trace"
	"os"
	"time"
)

func initTraceProvider(ctx context.Context, endpoint, token string) (*sdktrace.TracerProvider, error) {
	opts := []otlptracehttp.Option{
		otlptracehttp.WithEndpoint(endpoint),
		otlptracehttp.WithInsecure(),
	}
	exporter, err := otlptracehttp.New(ctx, opts...)
	if err != nil {
		return nil, err
	}

	r, err := resource.New(ctx, []resource.Option{
		resource.WithAttributes(attribute.KeyValue{
			Key: "token", Value: attribute.StringValue(token),
		}),
		resource.WithAttributes(attribute.KeyValue{
			Key: "service.name", Value: attribute.StringValue("srs-server"),
		}),
	}...)
	if err != nil {
		return nil, err
	}

	tp := sdktrace.NewTracerProvider(
		sdktrace.WithSampler(sdktrace.AlwaysSample()),
		sdktrace.WithBatcher(exporter),
		sdktrace.WithResource(r),
	)
	otel.SetTracerProvider(tp)
	otel.SetTextMapPropagator(propagation.NewCompositeTextMapPropagator(propagation.TraceContext{}, propagation.Baggage{}))
	return tp, nil
}

func main() {
	if os.Getenv("TOKEN") == "" {
		panic("no env TOKEN, see https://console.cloud.tencent.com/apm/monitor/access")
	}

	// The gRPC client defaults to https://localhost:4317 and the HTTP client https://localhost:4318.
	// See https://github.com/open-telemetry/opentelemetry-go/tree/main/exporters/otlp/otlptrace
	endpoint := "ap-guangzhou.apm.tencentcs.com:55681"
	fmt.Println(fmt.Sprintf("APM endpoint=%v, token=%vB", endpoint, len(os.Getenv("TOKEN"))))

	ctx := context.Background()
	if tp, err := initTraceProvider(ctx, endpoint, os.Getenv("TOKEN")); err != nil {
		panic(err)
	} else {
		defer tp.Shutdown(ctx)
	}

	fmt.Println("init ok")

	////////////////////////////////////////////////
	// Create global tracer.
	tracer := otel.Tracer("app")
	ctx, span := tracer.Start(context.Background(), "main", trace.WithSpanKind(trace.SpanKindServer))
	time.Sleep(100 * time.Millisecond)
	defer span.End()

	_, span2 := tracer.Start(ctx, "sub")
	time.Sleep(70 * time.Millisecond)
	span2.End()

	fmt.Println("done")
}
