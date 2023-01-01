//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//

#ifndef SRS_APP_TENCENTCLOUD_HPP
#define SRS_APP_TENCENTCLOUD_HPP

#include <srs_core.hpp>
#ifdef SRS_APM

#include <srs_kernel_buffer.hpp>

#include <string>
#include <vector>

class SrsBuffer;
class SrsClsLogGroupList;
class SrsClsLogGroup;
class SrsClsLog;
class SrsOtelSpan;
class SrsApmContext;
class SrsAmf0Object;
class SrsApmSpan;
class SrsOtelResourceSpans;
class SrsOtelResource;
class SrsOtelScopeSpans;
class SrsOtelAttribute;
class SrsOtelAnyValue;
class SrsOtelScope;
class SrsOtelStatus;
class SrsOtelEvent;
class SrsOtelLink;

class SrsClsSugar : public ISrsEncoder
{
private:
    SrsClsLog* log_;
    SrsClsLogGroup* log_group_;
    SrsClsLogGroupList* log_groups_;
public:
    SrsClsSugar();
    virtual ~SrsClsSugar();
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
public:
    bool empty();
    SrsClsSugar* kv(std::string k, std::string v);
};

class SrsClsSugars : public ISrsEncoder
{
private:
    std::vector<SrsClsSugar*> sugars;
public:
    SrsClsSugars();
    virtual ~SrsClsSugars();
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
public:
    SrsClsSugar* create();
    SrsClsSugars* slice(int max_size);
    bool empty();
    int size();
};

class SrsClsClient
{
private:
    bool enabled_;
    bool stat_heartbeat_;
    bool stat_streams_;
    bool debug_logging_;
    int heartbeat_ratio_;
    int streams_ratio_;
    std::string label_;
    std::string tag_;
private:
    std::string secret_id_;
    std::string endpoint_;
    std::string topic_;
private:
    SrsClsSugars* sugars_;
    uint64_t nn_logs_;
public:
    SrsClsClient();
    virtual ~SrsClsClient();
public:
    bool enabled();
    std::string label();
    std::string tag();
    uint64_t nn_logs();
public:
    srs_error_t initialize();
    srs_error_t report();
private:
    srs_error_t do_send_logs(ISrsEncoder* sugar, int count, int total);
    srs_error_t send_logs(SrsClsSugars* sugars);
    srs_error_t dump_summaries(SrsClsSugars* sugars);
    srs_error_t dump_streams(SrsClsSugars* sugars);
};

extern SrsClsClient* _srs_cls;

enum SrsApmKind
{
    SrsApmKindUnspecified = 0,
    SrsApmKindInternal = 1,
    SrsApmKindServer = 2,
    SrsApmKindClient = 3,
    SrsApmKindProducer = 4,
    SrsApmKindConsumer = 5,
};

enum SrsApmStatus
{
    SrsApmStatusUnset = 0,
    SrsApmStatusOk = 1,
    SrsApmStatusError = 2,
};

class SrsOtelExportTraceServiceRequest : public ISrsEncoder
{
private:
    // repeated opentelemetry.proto.trace.v1.ResourceSpans resource_spans = 1;
    std::vector<SrsOtelResourceSpans*> spans_;
public:
    SrsOtelExportTraceServiceRequest();
    virtual ~SrsOtelExportTraceServiceRequest();
public:
    SrsOtelResourceSpans* append();
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
};

class SrsOtelResourceSpans : public ISrsEncoder
{
private:
    // opentelemetry.proto.resource.v1.Resource resource = 1;
    SrsOtelResource* resource_;
    // repeated ScopeSpans scope_spans = 2;
    std::vector<SrsOtelScopeSpans*> spans_;
public:
    SrsOtelResourceSpans();
    virtual ~SrsOtelResourceSpans();
public:
    SrsOtelResource* resource();
    SrsOtelScopeSpans* append();
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
};

class SrsOtelResource : public ISrsEncoder
{
private:
    // repeated opentelemetry.proto.common.v1.KeyValue attributes = 1;
    std::vector<SrsOtelAttribute*> attributes_;
public:
    SrsOtelResource();
    virtual ~SrsOtelResource();
public:
    SrsOtelResource* add_addr(SrsOtelAttribute* v);
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
};

class SrsOtelAttribute : public ISrsEncoder
{
private:
    // string key = 1;
    std::string key_;
    // AnyValue value = 2;
    SrsOtelAnyValue* value_;
private:
    SrsOtelAttribute();
public:
    virtual ~SrsOtelAttribute();
public:
    const std::string& key();
public:
    static SrsOtelAttribute* kv(std::string k, std::string v);
    static SrsOtelAttribute* kvi(std::string k, int64_t v);
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
};

class SrsOtelAnyValue : public ISrsEncoder
{
private:
    // Default to string(1). Please change it if use other value, such as int(3).
    uint8_t used_field_id_;
private:
    // string string_value = 1;
    std::string string_value_;
    // bool bool_value = 2;
    // int64 int_value = 3;
    int64_t int_value_;
    // double double_value = 4;
    // ArrayValue array_value = 5;
    // KeyValueList kvlist_value = 6;
    // bytes bytes_value = 7;
public:
    SrsOtelAnyValue();
    virtual ~SrsOtelAnyValue();
public:
    SrsOtelAnyValue* set_string(const std::string& v);
    SrsOtelAnyValue* set_int(int64_t v);
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
};

class SrsOtelScopeSpans : public ISrsEncoder
{
private:
    // opentelemetry.proto.common.v1.InstrumentationScope scope = 1;
    SrsOtelScope* scope_;
    // repeated Span spans = 2;
    std::vector<SrsOtelSpan*> spans_;
public:
    SrsOtelScopeSpans();
    virtual ~SrsOtelScopeSpans();
public:
    SrsOtelScope* scope();
    SrsOtelScopeSpans* swap(std::vector<SrsOtelSpan*>& spans);
    int size();
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
};

class SrsOtelScope : public ISrsEncoder
{
public:
    //string name = 1;
    std::string name_;
    //string version = 2;
    //repeated KeyValue attributes = 3;
    //uint32 dropped_attributes_count = 4;
public:
    SrsOtelScope();
    virtual ~SrsOtelScope();
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
};

class SrsOtelSpan : public ISrsEncoder
{
public:
    //bytes trace_id = 1; // This field is required.
    std::string trace_id_;
    //bytes span_id = 2; // This field is required.
    std::string span_id_;
    //string trace_state = 3;
    //bytes parent_span_id = 4;
    std::string parent_span_id_;
    //string name = 5; // This field is required.
    std::string name_;
    //SpanKind kind = 6;
    SrsApmKind kind_;
    //fixed64 start_time_unix_nano = 7;
    int64_t start_time_unix_nano_;
    //fixed64 end_time_unix_nano = 8;
    int64_t end_time_unix_nano_;
    //repeated opentelemetry.proto.common.v1.KeyValue attributes = 9;
    std::vector<SrsOtelAttribute*> attributes_;
    //uint32 dropped_attributes_count = 10;
    //repeated Event events = 11;
    std::vector<SrsOtelEvent*> events_;
    //uint32 dropped_events_count = 12;
    //repeated Link links = 13;
    std::vector<SrsOtelLink*> links_;
    //uint32 dropped_links_count = 14;
    //Status status = 15;
    SrsOtelStatus* status_;
public:
    SrsOtelSpan();
    virtual ~SrsOtelSpan();
public:
    SrsOtelAttribute* attr(const std::string k);
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
};

class SrsOtelEvent : public ISrsEncoder
{
public:
    // fixed64 time_unix_nano = 1;
    int64_t time_;
    // string name = 2;
    std::string name_;
    // repeated opentelemetry.proto.common.v1.KeyValue attributes = 3;
    std::vector<SrsOtelAttribute*> attributes_;
    // uint32 dropped_attributes_count = 4;
public:
    SrsOtelEvent();
    virtual ~SrsOtelEvent();
public:
    static SrsOtelEvent* create(std::string v);
    SrsOtelEvent* add_attr(SrsOtelAttribute* v);
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
};

class SrsOtelLink : public ISrsEncoder
{
private:
    // bytes trace_id = 1;
    std::string trace_id_;
    // bytes span_id = 2;
    std::string span_id_;
    // string trace_state = 3;
    // repeated opentelemetry.proto.common.v1.KeyValue attributes = 4;
    // uint32 dropped_attributes_count = 5;
private:
    SrsOtelLink();
public:
    virtual ~SrsOtelLink();
public:
    static SrsOtelLink* create();
    SrsOtelLink* set_id(const std::string& trace_id, const std::string& span_id);
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
};

class SrsOtelStatus : public ISrsEncoder
{
public:
    //string message = 2;
    std::string message_;
    //StatusCode code = 3;
    SrsApmStatus code_;
public:
    SrsOtelStatus();
    virtual ~SrsOtelStatus();
public:
    virtual uint64_t nb_bytes();
    srs_error_t encode(SrsBuffer* b);
};

class SrsApmContext
{
private:
    friend class SrsApmSpan;
private:
    SrsApmSpan* span_;
    SrsApmContext* parent_;
    std::vector<SrsApmContext*> childs_;
    // Encode the id in hex string.
    std::string str_trace_id_;
    std::string str_span_id_;
private:
    std::string name_;
    SrsApmKind kind_;
    std::string trace_id_;
    std::string span_id_;
    // Note that parent span id might not be empty, while parent might be NULL, when extract span from propagator.
    std::string parent_span_id_;
    srs_utime_t start_time_;
    SrsApmStatus status_;
    std::string description_;
    srs_error_t err_;
    std::vector<SrsOtelAttribute*> attributes_;
    std::vector<SrsOtelLink*> links_;
private:
    bool ended_;
private:
    SrsApmContext(const std::string& name);
public:
    virtual ~SrsApmContext();
private:
    // Update the trace id and format it as hex string id.
    void set_trace_id(std::string v);
    // Update the span id and format it as hex string id.
    void set_span_id(std::string v);
public:
    const char* format_trace_id();
    const char* format_span_id();
    SrsApmContext* root();
    void set_parent(SrsApmContext* parent);
    void set_status(SrsApmStatus status, const std::string& description);
    bool all_ended();
    int count_spans();
    void update_trace_id(std::string v);
    void link(SrsApmContext* to);
    void end();
};

class ISrsApmSpan
{
private:
    friend class SrsApmClient;
public:
    ISrsApmSpan() {
    }
    virtual ~ISrsApmSpan() {
    }
public:
    // Get the formatted trace ID in hex string.
    virtual const char* format_trace_id() {
        return "";
    }
    // Get the formatted span ID in hex string.
    virtual const char* format_span_id() {
        return "";
    }
public:
    // Set the name of span.
    virtual ISrsApmSpan* set_name(const std::string& name) {
        return this;
    }
    // Set the kind of span.
    virtual ISrsApmSpan* set_kind(SrsApmKind kind) {
        return this;
    }
    // Set span as child span of parent span.
    virtual ISrsApmSpan* as_child(ISrsApmSpan* parent) {
        return this;
    }
    // Set the status of span, and error description by fmt.
    // Note that ignore description except for error status.
    virtual ISrsApmSpan* set_status(SrsApmStatus status, const std::string& description) {
        return this;
    }
    // RecordError will record err as an exception span event for this span. An
    // additional call to SetStatus is required if the Status of the Span should
    // be set to Error, as this method does not change the Span status. If this
    // span is not being recorded or err is nil then this method does nothing.
    virtual ISrsApmSpan* record_error(srs_error_t err) {
        return this;
    }
    // Add an attribute with all string kv to span.
    virtual ISrsApmSpan* attr(const std::string& k, const std::string& v) {
        return this;
    }
    // Link with another span.
    virtual ISrsApmSpan* link(ISrsApmSpan* span) {
        return this;
    }
    // End the span, snapshot and upload(in a while) a otel span to APM server.
    virtual ISrsApmSpan* end() {
        return this;
    }
};

class SrsApmSpan : public ISrsApmSpan
{
private:
    friend class SrsApmContext;
    friend class SrsApmClient;
private:
    bool child_;
    SrsApmContext* ctx_;
private:
    SrsApmSpan(const std::string& name);
public:
    virtual ~SrsApmSpan();
// Span operations.
public:
    const char* format_trace_id();
    const char* format_span_id();
    ISrsApmSpan* set_name(const std::string& name);
    ISrsApmSpan* set_kind(SrsApmKind kind);
    ISrsApmSpan* as_child(ISrsApmSpan* parent);
    ISrsApmSpan* set_status(SrsApmStatus status, const std::string& description);
    ISrsApmSpan* record_error(srs_error_t err);
    ISrsApmSpan* attr(const std::string& k, const std::string& v);
    ISrsApmSpan* link(ISrsApmSpan* span);
    ISrsApmSpan* end();
// Inject or extract for propagator.
private:
    ISrsApmSpan* extract(SrsAmf0Object* h);
    ISrsApmSpan* inject(SrsAmf0Object* h);
    std::string text_propagator();
// Store or load with coroutine context.
private:
    ISrsApmSpan* store();
    static ISrsApmSpan* load();
};

class SrsApmClient
{
private:
    bool enabled_;
    uint64_t nn_spans_;
    std::string team_;
    std::string token_;
    std::string endpoint_;
    std::string service_name_;
    bool debug_logging_;
    std::vector<SrsOtelSpan*> spans_;
public:
    SrsApmClient();
    virtual ~SrsApmClient();
public:
    srs_error_t initialize();
    srs_error_t report();
private:
    srs_error_t do_report();
public:
    bool enabled();
    uint64_t nn_spans();
public:
    // Create a span with specified name.
    ISrsApmSpan* span(const std::string& name);
    // Create dummy span for default.
    ISrsApmSpan* dummy();
public:
    // Extract and inject for propagator.
    ISrsApmSpan* extract(ISrsApmSpan* v, SrsAmf0Object* h);
    ISrsApmSpan* inject(ISrsApmSpan* v, SrsAmf0Object* h);
    // Store the span to coroutine context.
    void store(ISrsApmSpan* span);
    // Get or load span from coroutine context.
    // Note that a dummy span will be returned if no span in coroutine context.
    // Note that user should never free the returned span.
    ISrsApmSpan* load();
// Internal APIs.
public:
    void snapshot(SrsOtelSpan* span);
};

extern SrsApmClient* _srs_apm;

#endif
#endif

