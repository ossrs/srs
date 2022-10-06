package transport

import (
	"net"

	"github.com/ghettovoice/gosip/log"
	"github.com/ghettovoice/gosip/sip"
)

// TODO migrate other factories to functional arguments
type Options struct {
	MessageMapper sip.MessageMapper
	Logger        log.Logger
}

type LayerOption interface {
	ApplyLayer(opts *LayerOptions)
}

type LayerOptions struct {
	Options
	DNSResolver *net.Resolver
}

type ProtocolOption interface {
	ApplyProtocol(opts *ProtocolOptions)
}

type ProtocolOptions struct {
	Options
}

func WithMessageMapper(mapper sip.MessageMapper) interface {
	LayerOption
	ProtocolOption
} {
	return withMessageMapper{mapper}
}

type withMessageMapper struct {
	mapper sip.MessageMapper
}

func (o withMessageMapper) ApplyLayer(opts *LayerOptions) {
	opts.MessageMapper = o.mapper
}

func (o withMessageMapper) ApplyProtocol(opts *ProtocolOptions) {
	opts.MessageMapper = o.mapper
}

func WithLogger(logger log.Logger) interface {
	LayerOption
	ProtocolOption
} {
	return withLogger{logger}
}

type withLogger struct {
	logger log.Logger
}

func (o withLogger) ApplyLayer(opts *LayerOptions) {
	opts.Logger = o.logger
}

func (o withLogger) ApplyProtocol(opts *ProtocolOptions) {
	opts.Logger = o.logger
}

func WithDNSResolver(resolver *net.Resolver) LayerOption {
	return withDnsResolver{resolver}
}

type withDnsResolver struct {
	resolver *net.Resolver
}

func (o withDnsResolver) ApplyLayer(opts *LayerOptions) {
	opts.DNSResolver = o.resolver
}

// Listen method options
type ListenOption interface {
	ApplyListen(opts *ListenOptions)
}

type ListenOptions struct {
	TLSConfig TLSConfig
}
