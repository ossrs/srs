//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_ai22.hpp>

using namespace std;

#include <srs_app_dvr.hpp>
#include <srs_app_fragment.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtsp_conn.hpp>
#include <srs_app_rtsp_source.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_flv.hpp>
#include <srs_kernel_mp4.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai16.hpp>
#include <srs_utest_manual_config.hpp>
#include <srs_utest_manual_fmp4.hpp>
#include <srs_utest_manual_kernel.hpp>
#include <srs_utest_manual_protocol.hpp>

// Mock request implementation
MockEdgeRequest::MockEdgeRequest(std::string vhost, std::string app, std::string stream)
{
    vhost_ = vhost;
    app_ = app;
    stream_ = stream;
    host_ = "127.0.0.1";
    port_ = 1935;
    param_ = "";
    schema_ = "rtmp";
    tcUrl_ = "rtmp://127.0.0.1:1935/" + app;
    pageUrl_ = "";
    swfUrl_ = "";
    objectEncoding_ = 0;
    duration_ = -1;
    args_ = NULL;
    protocol_ = "rtmp";
    ip_ = "127.0.0.1";
}

MockEdgeRequest::~MockEdgeRequest()
{
    srs_freep(args_);
}

ISrsRequest *MockEdgeRequest::copy()
{
    MockEdgeRequest *req = new MockEdgeRequest(vhost_, app_, stream_);
    req->tcUrl_ = tcUrl_;
    req->pageUrl_ = pageUrl_;
    req->swfUrl_ = swfUrl_;
    req->objectEncoding_ = objectEncoding_;
    req->schema_ = schema_;
    req->host_ = host_;
    req->port_ = port_;
    req->param_ = param_;
    req->duration_ = duration_;
    req->protocol_ = protocol_;
    req->ip_ = ip_;
    return req;
}

std::string MockEdgeRequest::get_stream_url()
{
    if (vhost_ == "__defaultVhost__" || vhost_.empty()) {
        return "/" + app_ + "/" + stream_;
    } else {
        return vhost_ + "/" + app_ + "/" + stream_;
    }
}

void MockEdgeRequest::update_auth(ISrsRequest *req)
{
}

void MockEdgeRequest::strip()
{
}

ISrsRequest *MockEdgeRequest::as_http()
{
    return this;
}

// MockEdgeConfig implementation
MockEdgeConfig::MockEdgeConfig()
{
    edge_origin_directive_ = NULL;
    edge_transform_vhost_ = "";
    chunk_size_ = 60000;
}

MockEdgeConfig::~MockEdgeConfig()
{
    reset();
}

void MockEdgeConfig::reset()
{
    srs_freep(edge_origin_directive_);
}

SrsConfDirective *MockEdgeConfig::get_vhost_edge_origin(std::string vhost)
{
    return edge_origin_directive_;
}

std::string MockEdgeConfig::get_vhost_edge_transform_vhost(std::string vhost)
{
    return edge_transform_vhost_;
}

int MockEdgeConfig::get_chunk_size(std::string vhost)
{
    return chunk_size_;
}

srs_utime_t MockEdgeConfig::get_vhost_edge_origin_connect_timeout(std::string vhost)
{
    return 3 * SRS_UTIME_SECONDS;
}

srs_utime_t MockEdgeConfig::get_vhost_edge_origin_stream_timeout(std::string vhost)
{
    return 30 * SRS_UTIME_SECONDS;
}

std::string MockEdgeConfig::get_dvr_path(std::string vhost)
{
    return "./[vhost]/[app]/[stream]/[2006]/[01]/[02]/[15].[04].[05].[999].flv";
}

int MockEdgeConfig::get_dvr_time_jitter(std::string vhost)
{
    return 0; // SrsRtmpJitterAlgorithmOFF
}

bool MockEdgeConfig::get_dvr_wait_keyframe(std::string vhost)
{
    return true;
}

bool MockEdgeConfig::get_dvr_enabled(std::string vhost)
{
    return true;
}

srs_utime_t MockEdgeConfig::get_dvr_duration(std::string vhost)
{
    return 30 * SRS_UTIME_SECONDS;
}

SrsConfDirective *MockEdgeConfig::get_dvr_apply(std::string vhost)
{
    return NULL;
}

std::string MockEdgeConfig::get_dvr_plan(std::string vhost)
{
    return "session";
}

// MockEdgeAppFactory implementation
MockEdgeAppFactory::MockEdgeAppFactory()
{
    mock_client_ = NULL;
}

MockEdgeAppFactory::~MockEdgeAppFactory()
{
    // Don't delete mock_client_ here, it will be managed by the test
}

ISrsBasicRtmpClient *MockEdgeAppFactory::create_rtmp_client(std::string url, srs_utime_t cto, srs_utime_t sto)
{
    // Return the pre-configured mock client
    return mock_client_;
}

// Test SrsEdgeRtmpUpstream::connect() - major use scenario
// This test covers the typical edge server connecting to origin server scenario:
// 1. Edge server receives a play request
// 2. Uses load balancer to select an origin server
// 3. Connects to the origin server via RTMP
// 4. Plays the stream from origin
//
// This test uses mocks to avoid needing a real RTMP server.
VOID TEST(EdgeRtmpUpstreamTest, ConnectToOriginWithLoadBalancing)
{
    srs_error_t err;

    // Create mock request for edge pull
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "livestream"));
    req->host_ = "192.168.1.100";
    req->param_ = "?token=abc123";

    // Create mock config with origin servers
    SrsUniquePtr<MockEdgeConfig> config(new MockEdgeConfig());
    config->edge_origin_directive_ = new SrsConfDirective();
    config->edge_origin_directive_->name_ = "origin";
    config->edge_origin_directive_->args_.push_back("192.168.1.10:1935");
    config->edge_origin_directive_->args_.push_back("192.168.1.11:1935");
    config->edge_origin_directive_->args_.push_back("192.168.1.12:1935");
    config->edge_transform_vhost_ = "origin.[vhost]";
    config->chunk_size_ = 60000;

    // Create mock RTMP client - use raw pointer since upstream will manage it
    MockRtmpClient *mock_sdk = new MockRtmpClient();
    mock_sdk->connect_error_ = srs_success;
    mock_sdk->play_error_ = srs_success;

    // Create mock app factory that returns our mock client
    SrsUniquePtr<MockEdgeAppFactory> mock_factory(new MockEdgeAppFactory());
    mock_factory->mock_client_ = mock_sdk;

    // Create load balancer for round-robin selection
    SrsUniquePtr<SrsLbRoundRobin> lb(new SrsLbRoundRobin());

    // Create edge upstream (no redirect)
    SrsUniquePtr<SrsEdgeRtmpUpstream> upstream(new SrsEdgeRtmpUpstream(""));

    // Inject mock dependencies (private members are accessible in utests)
    upstream->config_ = config.get();
    upstream->app_factory_ = mock_factory.get();

    // Test: Connect to origin with load balancing
    err = upstream->connect(req.get(), lb.get());
    HELPER_EXPECT_SUCCESS(err);

    // Verify mock SDK was called
    EXPECT_TRUE(mock_sdk->connect_called_);
    EXPECT_TRUE(mock_sdk->play_called_);

    // Verify load balancer selected first server
    std::string selected_server;
    int selected_port = 0;
    upstream->selected(selected_server, selected_port);
    EXPECT_STREQ("192.168.1.10", selected_server.c_str());
    EXPECT_EQ(1935, selected_port);

    // Verify the stream was played correctly
    EXPECT_STREQ("livestream", mock_sdk->play_stream_.c_str());

    // Note: mock_sdk will be deleted by upstream destructor via srs_freep(sdk_)
}

// MockEdgeHttpClient implementation
MockEdgeHttpClient::MockEdgeHttpClient()
{
    initialize_called_ = false;
    get_called_ = false;
    set_recv_timeout_called_ = false;
    kbps_sample_called_ = false;
    initialize_error_ = srs_success;
    get_error_ = srs_success;
    mock_response_ = NULL;
    schema_ = "";
    host_ = "";
    port_ = 0;
    path_ = "";
    kbps_label_ = "";
    kbps_age_ = 0;
}

MockEdgeHttpClient::~MockEdgeHttpClient()
{
    // Don't free mock_response_ here, it will be managed by the caller
}

srs_error_t MockEdgeHttpClient::initialize(std::string schema, std::string h, int p, srs_utime_t tm)
{
    initialize_called_ = true;
    schema_ = schema;
    host_ = h;
    port_ = p;
    return srs_error_copy(initialize_error_);
}

srs_error_t MockEdgeHttpClient::get(std::string path, std::string req, ISrsHttpMessage **ppmsg)
{
    get_called_ = true;
    path_ = path;
    if (ppmsg && mock_response_) {
        *ppmsg = mock_response_;
    }
    return srs_error_copy(get_error_);
}

srs_error_t MockEdgeHttpClient::post(std::string path, std::string req, ISrsHttpMessage **ppmsg)
{
    return srs_success;
}

void MockEdgeHttpClient::set_recv_timeout(srs_utime_t tm)
{
    set_recv_timeout_called_ = true;
}

void MockEdgeHttpClient::kbps_sample(const char *label, srs_utime_t age)
{
    kbps_sample_called_ = true;
    kbps_label_ = label;
    kbps_age_ = age;
}

// MockEdgeHttpMessage implementation
MockEdgeHttpMessage::MockEdgeHttpMessage()
{
    status_code_ = 200;
    header_ = new SrsHttpHeader();
    body_reader_ = NULL;
}

MockEdgeHttpMessage::~MockEdgeHttpMessage()
{
    srs_freep(header_);
    // Don't free body_reader_ here, it's managed externally
}

uint8_t MockEdgeHttpMessage::message_type()
{
    return 0;
}

uint8_t MockEdgeHttpMessage::method()
{
    return 0;
}

uint16_t MockEdgeHttpMessage::status_code()
{
    return status_code_;
}

std::string MockEdgeHttpMessage::method_str()
{
    return "GET";
}

bool MockEdgeHttpMessage::is_http_get()
{
    return true;
}

bool MockEdgeHttpMessage::is_http_put()
{
    return false;
}

bool MockEdgeHttpMessage::is_http_post()
{
    return false;
}

bool MockEdgeHttpMessage::is_http_delete()
{
    return false;
}

bool MockEdgeHttpMessage::is_http_options()
{
    return false;
}

std::string MockEdgeHttpMessage::uri()
{
    return "";
}

std::string MockEdgeHttpMessage::url()
{
    return "";
}

std::string MockEdgeHttpMessage::host()
{
    return "";
}

std::string MockEdgeHttpMessage::path()
{
    return "";
}

std::string MockEdgeHttpMessage::query()
{
    return "";
}

std::string MockEdgeHttpMessage::ext()
{
    return "";
}

srs_error_t MockEdgeHttpMessage::body_read_all(std::string &body)
{
    return srs_success;
}

ISrsHttpResponseReader *MockEdgeHttpMessage::body_reader()
{
    return body_reader_;
}

int64_t MockEdgeHttpMessage::content_length()
{
    return -1;
}

std::string MockEdgeHttpMessage::query_get(std::string key)
{
    return "";
}

SrsHttpHeader *MockEdgeHttpMessage::header()
{
    return header_;
}

bool MockEdgeHttpMessage::is_jsonp()
{
    return false;
}

bool MockEdgeHttpMessage::is_keep_alive()
{
    return false;
}

std::string MockEdgeHttpMessage::parse_rest_id(std::string pattern)
{
    return "";
}

// MockEdgeFileReader implementation
MockEdgeFileReader::MockEdgeFileReader(const char *data, int size)
{
    data_ = new char[size];
    memcpy(data_, data, size);
    size_ = size;
    pos_ = 0;
}

MockEdgeFileReader::~MockEdgeFileReader()
{
    srs_freepa(data_);
}

srs_error_t MockEdgeFileReader::open(std::string p)
{
    return srs_success;
}

void MockEdgeFileReader::close()
{
}

bool MockEdgeFileReader::is_open()
{
    return true;
}

int64_t MockEdgeFileReader::tellg()
{
    return pos_;
}

void MockEdgeFileReader::skip(int64_t size)
{
    pos_ += size;
}

int64_t MockEdgeFileReader::seek2(int64_t offset)
{
    pos_ = offset;
    return pos_;
}

int64_t MockEdgeFileReader::filesize()
{
    return size_;
}

srs_error_t MockEdgeFileReader::read(void *buf, size_t count, ssize_t *pnread)
{
    int available = size_ - pos_;
    int to_read = (int)count;
    if (to_read > available) {
        to_read = available;
    }

    if (to_read > 0) {
        memcpy(buf, data_ + pos_, to_read);
        pos_ += to_read;
    }

    if (pnread) {
        *pnread = to_read;
    }

    return srs_success;
}

srs_error_t MockEdgeFileReader::lseek(off_t offset, int whence, off_t *seeked)
{
    return srs_success;
}

// MockPublishEdge implementation
MockPublishEdge::MockPublishEdge()
{
}

MockPublishEdge::~MockPublishEdge()
{
}

// MockEdgeFlvDecoder implementation
MockEdgeFlvDecoder::MockEdgeFlvDecoder()
{
    initialize_called_ = false;
    read_header_called_ = false;
    read_previous_tag_size_called_ = false;
}

MockEdgeFlvDecoder::~MockEdgeFlvDecoder()
{
}

srs_error_t MockEdgeFlvDecoder::initialize(ISrsReader *fr)
{
    initialize_called_ = true;
    return srs_success;
}

srs_error_t MockEdgeFlvDecoder::read_header(char header[9])
{
    read_header_called_ = true;
    // Write FLV header: 'FLV' + version(1) + flags(5) + header_size(4)
    header[0] = 'F';
    header[1] = 'L';
    header[2] = 'V';
    header[3] = 0x01; // version
    header[4] = 0x05; // audio + video
    header[5] = 0x00;
    header[6] = 0x00;
    header[7] = 0x00;
    header[8] = 0x09; // header size
    return srs_success;
}

srs_error_t MockEdgeFlvDecoder::read_tag_header(char *ptype, int32_t *pdata_size, uint32_t *ptime)
{
    return srs_success;
}

srs_error_t MockEdgeFlvDecoder::read_tag_data(char *data, int32_t size)
{
    return srs_success;
}

srs_error_t MockEdgeFlvDecoder::read_previous_tag_size(char previous_tag_size[4])
{
    read_previous_tag_size_called_ = true;
    // Write previous tag size as 0
    previous_tag_size[0] = 0x00;
    previous_tag_size[1] = 0x00;
    previous_tag_size[2] = 0x00;
    previous_tag_size[3] = 0x00;
    return srs_success;
}

// MockEdgeFlvAppFactory implementation
MockEdgeFlvAppFactory::MockEdgeFlvAppFactory()
{
    mock_http_client_ = NULL;
    mock_file_reader_ = NULL;
    mock_flv_decoder_ = NULL;
}

MockEdgeFlvAppFactory::~MockEdgeFlvAppFactory()
{
    // Don't delete mock objects here, they will be managed by the test
}

ISrsHttpClient *MockEdgeFlvAppFactory::create_http_client()
{
    return mock_http_client_;
}

ISrsFileReader *MockEdgeFlvAppFactory::create_http_file_reader(ISrsHttpResponseReader *r)
{
    return mock_file_reader_;
}

ISrsFlvDecoder *MockEdgeFlvAppFactory::create_flv_decoder()
{
    return mock_flv_decoder_;
}

// Test SrsEdgeFlvUpstream::connect() - major use scenario
// This test covers the typical edge server connecting to origin server via HTTP-FLV:
// 1. Edge server receives a play request
// 2. Uses load balancer to select an origin server
// 3. Connects to the origin server via HTTP
// 4. Gets the FLV stream from origin
// 5. Initializes FLV decoder to read the stream
//
// This test uses mocks to avoid needing a real HTTP server.
VOID TEST(EdgeFlvUpstreamTest, ConnectToOriginWithHttpFlv)
{
    srs_error_t err;

    // Create mock request for edge pull
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "livestream"));
    req->host_ = "192.168.1.100";
    req->param_ = "?token=abc123";

    // Create mock config with origin servers
    SrsUniquePtr<MockEdgeConfig> config(new MockEdgeConfig());
    config->edge_origin_directive_ = new SrsConfDirective();
    config->edge_origin_directive_->name_ = "origin";
    config->edge_origin_directive_->args_.push_back("192.168.1.10:8080");
    config->edge_origin_directive_->args_.push_back("192.168.1.11:8080");

    // Create mock HTTP response message
    MockEdgeHttpMessage *mock_response = new MockEdgeHttpMessage();
    mock_response->status_code_ = 200;

    // Create mock HTTP client
    MockEdgeHttpClient *mock_http_client = new MockEdgeHttpClient();
    mock_http_client->initialize_error_ = srs_success;
    mock_http_client->get_error_ = srs_success;
    mock_http_client->mock_response_ = mock_response;

    // Create mock file reader
    MockEdgeFileReader *mock_file_reader = new MockEdgeFileReader("", 0);

    // Create mock FLV decoder
    MockEdgeFlvDecoder *mock_flv_decoder = new MockEdgeFlvDecoder();

    // Create mock app factory that returns our mock objects
    SrsUniquePtr<MockEdgeFlvAppFactory> mock_factory(new MockEdgeFlvAppFactory());
    mock_factory->mock_http_client_ = mock_http_client;
    mock_factory->mock_file_reader_ = mock_file_reader;
    mock_factory->mock_flv_decoder_ = mock_flv_decoder;

    // Create load balancer for round-robin selection
    SrsUniquePtr<SrsLbRoundRobin> lb(new SrsLbRoundRobin());

    // Create edge FLV upstream with http schema
    SrsUniquePtr<SrsEdgeFlvUpstream> upstream(new SrsEdgeFlvUpstream("http"));

    // Inject mock dependencies (private members are accessible in utests)
    upstream->config_ = config.get();
    upstream->app_factory_ = mock_factory.get();

    // Test: Connect to origin with load balancing
    err = upstream->connect(req.get(), lb.get());
    HELPER_EXPECT_SUCCESS(err);

    // Verify mock HTTP client was called
    EXPECT_TRUE(mock_http_client->initialize_called_);
    EXPECT_TRUE(mock_http_client->get_called_);
    EXPECT_STREQ("http", mock_http_client->schema_.c_str());
    EXPECT_STREQ("192.168.1.10", mock_http_client->host_.c_str());
    EXPECT_EQ(8080, mock_http_client->port_);
    EXPECT_STREQ("/live/livestream.flv?token=abc123", mock_http_client->path_.c_str());

    // Verify FLV decoder was initialized and read header
    EXPECT_TRUE(mock_flv_decoder->initialize_called_);
    EXPECT_TRUE(mock_flv_decoder->read_header_called_);
    EXPECT_TRUE(mock_flv_decoder->read_previous_tag_size_called_);

    // Verify load balancer selected first server
    std::string selected_server;
    int selected_port = 0;
    upstream->selected(selected_server, selected_port);
    EXPECT_STREQ("192.168.1.10", selected_server.c_str());
    EXPECT_EQ(8080, selected_port);

    // Clean up - set to NULL to avoid double-free
    upstream->sdk_ = NULL;
    upstream->hr_ = NULL;
    upstream->reader_ = NULL;
    upstream->decoder_ = NULL;

    srs_freep(mock_http_client);
    srs_freep(mock_response);
    srs_freep(mock_file_reader);
    srs_freep(mock_flv_decoder);
}

// Mock FLV decoder that returns actual FLV tag data for testing recv_message
class MockFlvDecoderWithData : public ISrsFlvDecoder
{
public:
    char tag_type_;
    int32_t tag_size_;
    uint32_t tag_time_;
    char *tag_data_;
    bool should_fail_;

public:
    MockFlvDecoderWithData()
    {
        tag_type_ = SrsFrameTypeScript;
        tag_size_ = 0;
        tag_time_ = 0;
        tag_data_ = NULL;
        should_fail_ = false;
    }

    virtual ~MockFlvDecoderWithData()
    {
        // Don't free tag_data_ - it will be managed by the message
    }

    virtual srs_error_t initialize(ISrsReader *fr)
    {
        return srs_success;
    }

    virtual srs_error_t read_header(char header[9])
    {
        return srs_success;
    }

    virtual srs_error_t read_tag_header(char *ptype, int32_t *pdata_size, uint32_t *ptime)
    {
        if (should_fail_) {
            return srs_error_new(ERROR_SYSTEM_IO_INVALID, "mock read tag header failed");
        }
        *ptype = tag_type_;
        *pdata_size = tag_size_;
        *ptime = tag_time_;
        return srs_success;
    }

    virtual srs_error_t read_tag_data(char *data, int32_t size)
    {
        if (should_fail_) {
            return srs_error_new(ERROR_SYSTEM_IO_INVALID, "mock read tag data failed");
        }
        if (tag_data_ && size > 0) {
            memcpy(data, tag_data_, size);
        }
        return srs_success;
    }

    virtual srs_error_t read_previous_tag_size(char previous_tag_size[4])
    {
        if (should_fail_) {
            return srs_error_new(ERROR_SYSTEM_IO_INVALID, "mock read pts failed");
        }
        previous_tag_size[0] = 0x00;
        previous_tag_size[1] = 0x00;
        previous_tag_size[2] = 0x00;
        previous_tag_size[3] = 0x00;
        return srs_success;
    }
};

// Test SrsEdgeFlvUpstream::recv_message() and decode_message() - major use scenario
// This test covers the typical edge server receiving FLV messages from origin:
// 1. Edge server reads FLV tag header (type, size, timestamp)
// 2. Reads FLV tag data
// 3. Reads previous tag size
// 4. Creates RTMP message from FLV tag
// 5. Decodes metadata message if it's onMetaData
VOID TEST(EdgeFlvUpstreamTest, RecvAndDecodeMetadataMessage)
{
    srs_error_t err;

    // Create onMetaData packet data
    // AMF0 string: "onMetaData"
    // AMF0 object: {width: 1920, height: 1080}
    SrsUniquePtr<SrsAmf0Any> name(SrsAmf0Any::str(SRS_CONSTS_RTMP_ON_METADATA));
    SrsUniquePtr<SrsAmf0Object> metadata(SrsAmf0Any::object());
    metadata->set("width", SrsAmf0Any::number(1920));
    metadata->set("height", SrsAmf0Any::number(1080));

    int metadata_size = name->total_size() + metadata->total_size();
    char *metadata_data = new char[metadata_size];
    SrsBuffer metadata_buf(metadata_data, metadata_size);
    HELPER_EXPECT_SUCCESS(name->write(&metadata_buf));
    HELPER_EXPECT_SUCCESS(metadata->write(&metadata_buf));

    // Create mock FLV decoder with metadata tag
    MockFlvDecoderWithData *mock_decoder = new MockFlvDecoderWithData();
    mock_decoder->tag_type_ = SrsFrameTypeScript; // 18 = script data
    mock_decoder->tag_size_ = metadata_size;
    mock_decoder->tag_time_ = 1000; // 1 second (note: script messages always have timestamp 0)
    mock_decoder->tag_data_ = metadata_data;

    // Create edge FLV upstream
    SrsUniquePtr<SrsEdgeFlvUpstream> upstream(new SrsEdgeFlvUpstream("http"));
    upstream->decoder_ = mock_decoder;

    // Test: Receive message from FLV stream
    SrsRtmpCommonMessage *msg = NULL;
    err = upstream->recv_message(&msg);
    HELPER_EXPECT_SUCCESS(err);
    ASSERT_TRUE(msg != NULL);

    // Verify message properties
    EXPECT_EQ(SrsFrameTypeScript, msg->header_.message_type_);
    EXPECT_EQ(metadata_size, msg->size());
    // Note: Script messages always have timestamp 0 by design in initialize_amf0_script()
    EXPECT_EQ(0, (int)msg->header_.timestamp_);

    // Test: Decode the metadata message
    SrsRtmpCommand *packet = NULL;
    err = upstream->decode_message(msg, &packet);
    HELPER_EXPECT_SUCCESS(err);
    ASSERT_TRUE(packet != NULL);

    // Verify decoded metadata packet
    SrsOnMetaDataPacket *meta_packet = dynamic_cast<SrsOnMetaDataPacket *>(packet);
    ASSERT_TRUE(meta_packet != NULL);
    EXPECT_STREQ(SRS_CONSTS_RTMP_ON_METADATA, meta_packet->name_.c_str());

    // Verify metadata properties
    SrsAmf0Any *width = meta_packet->metadata_->get_property("width");
    ASSERT_TRUE(width != NULL);
    EXPECT_TRUE(width->is_number());
    EXPECT_EQ(1920, (int)width->to_number());

    SrsAmf0Any *height = meta_packet->metadata_->get_property("height");
    ASSERT_TRUE(height != NULL);
    EXPECT_TRUE(height->is_number());
    EXPECT_EQ(1080, (int)height->to_number());

    // Clean up
    srs_freep(msg);
    srs_freep(packet);
    upstream->decoder_ = NULL;
    srs_freep(mock_decoder);
}

// Test SrsEdgeFlvUpstream utility methods - major use scenario
// This test covers the typical edge server operations:
// 1. Query selected origin server (selected())
// 2. Set receive timeout for reading from origin (set_recv_timeout())
// 3. Sample bandwidth statistics (kbps_sample())
// 4. Clean up resources when connection closes (close())
VOID TEST(EdgeFlvUpstreamTest, UtilityMethodsAndCleanup)
{

    // Create mock HTTP client
    MockEdgeHttpClient *mock_http_client = new MockEdgeHttpClient();
    mock_http_client->set_recv_timeout_called_ = false;
    mock_http_client->kbps_sample_called_ = false;

    // Create edge FLV upstream
    SrsUniquePtr<SrsEdgeFlvUpstream> upstream(new SrsEdgeFlvUpstream("http"));

    // Inject mock HTTP client
    upstream->sdk_ = mock_http_client;

    // Set selected server info (simulating successful connection)
    upstream->selected_ip_ = "192.168.1.100";
    upstream->selected_port_ = 8080;

    // Test 1: Query selected origin server
    std::string server;
    int port = 0;
    upstream->selected(server, port);
    EXPECT_STREQ("192.168.1.100", server.c_str());
    EXPECT_EQ(8080, port);

    // Test 2: Set receive timeout (typical edge ingester timeout)
    srs_utime_t timeout = 30 * SRS_UTIME_SECONDS;
    upstream->set_recv_timeout(timeout);
    EXPECT_TRUE(mock_http_client->set_recv_timeout_called_);

    // Test 3: Sample bandwidth statistics
    upstream->kbps_sample("edge-pull", 5 * SRS_UTIME_SECONDS);
    EXPECT_TRUE(mock_http_client->kbps_sample_called_);
    EXPECT_STREQ("edge-pull", mock_http_client->kbps_label_.c_str());
    EXPECT_EQ(5 * SRS_UTIME_SECONDS, mock_http_client->kbps_age_);

    // Create additional resources to test cleanup
    MockEdgeHttpMessage *mock_response = new MockEdgeHttpMessage();
    MockEdgeFileReader *mock_file_reader = new MockEdgeFileReader(NULL, 0);
    MockEdgeFlvDecoder *mock_flv_decoder = new MockEdgeFlvDecoder();
    MockEdgeRequest *mock_request = new MockEdgeRequest("test.vhost", "live", "stream1");

    upstream->hr_ = mock_response;
    upstream->reader_ = mock_file_reader;
    upstream->decoder_ = mock_flv_decoder;
    upstream->req_ = mock_request;

    // Test 4: Close and cleanup all resources
    upstream->close();

    // Verify all resources are freed (pointers should be NULL after close)
    EXPECT_TRUE(upstream->sdk_ == NULL);
    EXPECT_TRUE(upstream->hr_ == NULL);
    EXPECT_TRUE(upstream->reader_ == NULL);
    EXPECT_TRUE(upstream->decoder_ == NULL);
    EXPECT_TRUE(upstream->req_ == NULL);
}

// MockPlayEdge implementation
MockPlayEdge::MockPlayEdge()
{
    on_ingest_play_count_ = 0;
    on_ingest_play_error_ = srs_success;
}

MockPlayEdge::~MockPlayEdge()
{
    srs_freep(on_ingest_play_error_);
}

srs_error_t MockPlayEdge::on_ingest_play()
{
    on_ingest_play_count_++;
    return srs_error_copy(on_ingest_play_error_);
}

void MockPlayEdge::reset()
{
    on_ingest_play_count_ = 0;
    srs_freep(on_ingest_play_error_);
}

VOID TEST(EdgeIngesterTest, Initialize)
{
    srs_error_t err;

    // Create mock dependencies
    SrsUniquePtr<MockEdgeRequest> mock_req(new MockEdgeRequest("test.vhost", "live", "stream1"));
    SrsUniquePtr<MockPlayEdge> mock_edge(new MockPlayEdge());

    // Create mock source - use raw pointer for shared_ptr
    MockLiveSource *raw_source = new MockLiveSource();
    SrsSharedPtr<SrsLiveSource> source_ptr(raw_source);

    // Create SrsEdgeIngester instance
    SrsUniquePtr<SrsEdgeIngester> ingester(new SrsEdgeIngester());

    // Test: Initialize the ingester with source, edge, and request
    HELPER_EXPECT_SUCCESS(ingester->initialize(source_ptr, mock_edge.get(), mock_req.get()));

    // Verify: All fields are properly initialized
    EXPECT_TRUE(ingester->source_ == raw_source);
    EXPECT_TRUE(ingester->edge_ == mock_edge.get());
    EXPECT_TRUE(ingester->req_ == mock_req.get());

    // Clean up: Set fields to NULL to avoid double-free
    ingester->source_ = NULL;
    ingester->edge_ = NULL;
    ingester->req_ = NULL;
}

// MockEdgeUpstreamForIngester implementation
MockEdgeUpstreamForIngester::MockEdgeUpstreamForIngester()
{
    decode_message_called_ = false;
    decode_message_packet_ = NULL;
    decode_message_error_ = srs_success;
}

MockEdgeUpstreamForIngester::~MockEdgeUpstreamForIngester()
{
    srs_freep(decode_message_packet_);
    srs_freep(decode_message_error_);
}

srs_error_t MockEdgeUpstreamForIngester::connect(ISrsRequest *r, ISrsLbRoundRobin *lb)
{
    return srs_success;
}

srs_error_t MockEdgeUpstreamForIngester::recv_message(SrsRtmpCommonMessage **pmsg)
{
    return srs_success;
}

srs_error_t MockEdgeUpstreamForIngester::decode_message(SrsRtmpCommonMessage *msg, SrsRtmpCommand **ppacket)
{
    decode_message_called_ = true;
    if (decode_message_packet_ != NULL) {
        *ppacket = decode_message_packet_;
        decode_message_packet_ = NULL;
    }
    return srs_error_copy(decode_message_error_);
}

void MockEdgeUpstreamForIngester::close()
{
}

void MockEdgeUpstreamForIngester::selected(std::string &server, int &port)
{
}

void MockEdgeUpstreamForIngester::set_recv_timeout(srs_utime_t tm)
{
}

void MockEdgeUpstreamForIngester::kbps_sample(const char *label, srs_utime_t age)
{
}

void MockEdgeUpstreamForIngester::reset()
{
    decode_message_called_ = false;
    srs_freep(decode_message_packet_);
    srs_freep(decode_message_error_);
}

VOID TEST(EdgeIngesterTest, ProcessPublishMessageAudioVideo)
{
    srs_error_t err;

    // Create mock dependencies
    SrsUniquePtr<MockEdgeRequest> mock_req(new MockEdgeRequest("test.vhost", "live", "stream1"));
    SrsUniquePtr<MockPlayEdge> mock_edge(new MockPlayEdge());

    // Create mock source using existing MockLiveSource
    MockLiveSource *raw_source = new MockLiveSource();
    SrsSharedPtr<SrsLiveSource> source_ptr(raw_source);

    // Create mock upstream (use raw pointer since ingester will manage it)
    MockEdgeUpstreamForIngester *mock_upstream = new MockEdgeUpstreamForIngester();

    // Create SrsEdgeIngester instance
    SrsUniquePtr<SrsEdgeIngester> ingester(new SrsEdgeIngester());

    // Initialize the ingester (this sets source_ from the shared_ptr)
    HELPER_EXPECT_SUCCESS(ingester->initialize(source_ptr, mock_edge.get(), mock_req.get()));

    // Verify source_ was set correctly by initialize
    EXPECT_TRUE(ingester->source_ == raw_source);

    // Inject mock upstream (free the original one first, then set our mock)
    srs_freep(ingester->upstream_);
    ingester->upstream_ = mock_upstream;

    // Test 1: Process audio message with valid AAC codec
    {
        SrsUniquePtr<SrsRtmpCommonMessage> audio_msg(new SrsRtmpCommonMessage());
        audio_msg->header_.message_type_ = RTMP_MSG_AudioMessage;
        audio_msg->header_.payload_length_ = 10;
        audio_msg->header_.timestamp_ = 1000;
        audio_msg->create_payload(10);

        // Set valid AAC audio data (codec ID 10)
        char *payload = audio_msg->payload();
        payload[0] = 0xAF; // AAC codec (ID=10), 44kHz, 16-bit, stereo
        payload[1] = 0x01; // AAC raw data (not sequence header)
        for (int i = 2; i < 10; i++) {
            payload[i] = 0x00; // Sample AAC data
        }

        std::string redirect;
        HELPER_EXPECT_SUCCESS(ingester->process_publish_message(audio_msg.get(), redirect));
    }

    // Test 2: Process video message
    {
        SrsUniquePtr<SrsRtmpCommonMessage> video_msg(new SrsRtmpCommonMessage());
        video_msg->header_.message_type_ = RTMP_MSG_VideoMessage;
        video_msg->header_.payload_length_ = 20;
        video_msg->header_.timestamp_ = 2000;
        video_msg->create_payload(20);

        std::string redirect;
        HELPER_EXPECT_SUCCESS(ingester->process_publish_message(video_msg.get(), redirect));
    }

    // Test 3: Process aggregate message
    {
        SrsUniquePtr<SrsRtmpCommonMessage> aggregate_msg(new SrsRtmpCommonMessage());
        aggregate_msg->header_.message_type_ = RTMP_MSG_AggregateMessage;
        aggregate_msg->header_.payload_length_ = 30;
        aggregate_msg->header_.timestamp_ = 3000;
        aggregate_msg->create_payload(30);

        std::string redirect;
        HELPER_EXPECT_SUCCESS(ingester->process_publish_message(aggregate_msg.get(), redirect));
    }

    // Clean up: Set fields to NULL to avoid double-free
    // Note: source_ is managed by source_ptr shared pointer, so we just set to NULL
    // upstream_ will be freed by the ingester destructor (it calls srs_freep(upstream_))
    ingester->source_ = NULL;
    ingester->edge_ = NULL;
    ingester->req_ = NULL;
    // Don't set upstream_ to NULL - let the destructor free it
}

VOID TEST(EdgeForwarderTest, InitializeAndSetQueueSize)
{
    srs_error_t err;

    // Create mock objects
    SrsSharedPtr<SrsLiveSource> source_ptr(new MockLiveSource());
    MockPublishEdge mock_edge;
    SrsUniquePtr<MockEdgeRequest> mock_req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create SrsEdgeForwarder
    SrsUniquePtr<SrsEdgeForwarder> forwarder(new SrsEdgeForwarder());

    // Test initialize method
    HELPER_EXPECT_SUCCESS(forwarder->initialize(source_ptr, &mock_edge, mock_req.get()));

    // Verify that fields are set correctly
    EXPECT_EQ(forwarder->source_, source_ptr.get());
    EXPECT_EQ(forwarder->edge_, &mock_edge);
    EXPECT_EQ(forwarder->req_, mock_req.get());

    // Test set_queue_size method
    srs_utime_t queue_size = 10 * SRS_UTIME_SECONDS;
    forwarder->set_queue_size(queue_size);

    // Verify queue size is set (we can't directly check the queue, but the call should not crash)
    // The actual verification would be done by checking if the queue behaves correctly with the new size

    // Clean up: Set fields to NULL to avoid double-free
    forwarder->source_ = NULL;
    forwarder->edge_ = NULL;
    forwarder->req_ = NULL;
}

VOID TEST(EdgeForwarderTest, ProxyVideoMessage)
{
    srs_error_t err;

    // Create mock objects
    SrsSharedPtr<SrsLiveSource> source_ptr(new MockLiveSource());
    MockPublishEdge mock_edge;
    SrsUniquePtr<MockEdgeRequest> mock_req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create SrsEdgeForwarder
    SrsUniquePtr<SrsEdgeForwarder> forwarder(new SrsEdgeForwarder());

    // Initialize the forwarder
    HELPER_EXPECT_SUCCESS(forwarder->initialize(source_ptr, &mock_edge, mock_req.get()));

    // Create a simple RTMP client for testing (won't actually connect)
    // We need this because proxy() calls sdk_->sid()
    SrsSimpleRtmpClient *sdk = new SrsSimpleRtmpClient("rtmp://127.0.0.1:1935/live/test", 3 * SRS_UTIME_SECONDS, 9 * SRS_UTIME_SECONDS);
    srs_freep(forwarder->sdk_);
    forwarder->sdk_ = sdk;

    // Create a video message to proxy
    SrsUniquePtr<SrsRtmpCommonMessage> video_msg(new SrsRtmpCommonMessage());
    video_msg->header_.message_type_ = RTMP_MSG_VideoMessage;
    video_msg->header_.timestamp_ = 1000;
    video_msg->header_.stream_id_ = 1;
    video_msg->header_.payload_length_ = 100;

    // Create payload for the message
    char *payload = new char[100];
    memset(payload, 0x17, 100); // Video keyframe marker
    HELPER_EXPECT_SUCCESS(video_msg->create(&video_msg->header_, payload, 100));

    // Test proxy method - should successfully enqueue the message
    HELPER_EXPECT_SUCCESS(forwarder->proxy(video_msg.get()));

    // Verify the message was enqueued (queue size should be 1)
    EXPECT_EQ(1, forwarder->queue_->size());

    // Clean up: Set fields to NULL to avoid double-free
    forwarder->source_ = NULL;
    forwarder->edge_ = NULL;
    forwarder->req_ = NULL;
}

MockEdgeIngester::MockEdgeIngester()
{
    initialize_called_ = false;
    start_called_ = false;
    stop_called_ = false;
    initialize_error_ = srs_success;
    start_error_ = srs_success;
}

MockEdgeIngester::~MockEdgeIngester()
{
    srs_freep(initialize_error_);
    srs_freep(start_error_);
}

srs_error_t MockEdgeIngester::initialize(SrsSharedPtr<SrsLiveSource> s, ISrsPlayEdge *e, ISrsRequest *r)
{
    initialize_called_ = true;
    return srs_error_copy(initialize_error_);
}

srs_error_t MockEdgeIngester::start()
{
    start_called_ = true;
    return srs_error_copy(start_error_);
}

void MockEdgeIngester::stop()
{
    stop_called_ = true;
}

void MockEdgeIngester::reset()
{
    initialize_called_ = false;
    start_called_ = false;
    stop_called_ = false;
    srs_freep(initialize_error_);
    srs_freep(start_error_);
}

MockEdgeForwarder::MockEdgeForwarder()
{
    initialize_called_ = false;
    start_called_ = false;
    stop_called_ = false;
    set_queue_size_called_ = false;
    proxy_called_ = false;
    queue_size_ = 0;
    initialize_error_ = srs_success;
    start_error_ = srs_success;
    proxy_error_ = srs_success;
}

MockEdgeForwarder::~MockEdgeForwarder()
{
    srs_freep(initialize_error_);
    srs_freep(start_error_);
    srs_freep(proxy_error_);
}

void MockEdgeForwarder::set_queue_size(srs_utime_t queue_size)
{
    set_queue_size_called_ = true;
    queue_size_ = queue_size;
}

srs_error_t MockEdgeForwarder::initialize(SrsSharedPtr<SrsLiveSource> s, ISrsPublishEdge *e, ISrsRequest *r)
{
    initialize_called_ = true;
    return srs_error_copy(initialize_error_);
}

srs_error_t MockEdgeForwarder::start()
{
    start_called_ = true;
    return srs_error_copy(start_error_);
}

void MockEdgeForwarder::stop()
{
    stop_called_ = true;
}

srs_error_t MockEdgeForwarder::proxy(SrsRtmpCommonMessage *msg)
{
    proxy_called_ = true;
    return srs_error_copy(proxy_error_);
}

void MockEdgeForwarder::reset()
{
    initialize_called_ = false;
    start_called_ = false;
    stop_called_ = false;
    set_queue_size_called_ = false;
    proxy_called_ = false;
    queue_size_ = 0;
    srs_freep(initialize_error_);
    srs_freep(start_error_);
    srs_freep(proxy_error_);
}

VOID TEST(PlayEdgeTest, ClientPlayLifecycle)
{
    srs_error_t err;

    // Create mock dependencies
    SrsSharedPtr<SrsLiveSource> source_ptr(new MockLiveSource());
    SrsUniquePtr<MockEdgeRequest> mock_req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create SrsPlayEdge
    SrsUniquePtr<SrsPlayEdge> play_edge(new SrsPlayEdge());

    // Replace the real ingester with mock ingester
    MockEdgeIngester *mock_ingester = new MockEdgeIngester();
    srs_freep(play_edge->ingester_);
    play_edge->ingester_ = mock_ingester;

    // Test initialize method
    HELPER_EXPECT_SUCCESS(play_edge->initialize(source_ptr, mock_req.get()));
    EXPECT_TRUE(mock_ingester->initialize_called_);

    // Test on_client_play method - should start ingester when in init state
    HELPER_EXPECT_SUCCESS(play_edge->on_client_play());
    EXPECT_TRUE(mock_ingester->start_called_);
    EXPECT_EQ(SrsEdgeStatePlay, play_edge->state_);

    // Test on_all_client_stop method - should stop ingester and reset state
    play_edge->on_all_client_stop();
    EXPECT_TRUE(mock_ingester->stop_called_);
    EXPECT_EQ(SrsEdgeStateInit, play_edge->state_);

    // Clean up: ingester_ will be freed by play_edge destructor
}

VOID TEST(PlayEdgeTest, IngestPlayStateTransition)
{
    srs_error_t err;

    // Create mock dependencies
    SrsSharedPtr<SrsLiveSource> source_ptr(new MockLiveSource());
    SrsUniquePtr<MockEdgeRequest> mock_req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create SrsPlayEdge
    SrsUniquePtr<SrsPlayEdge> play_edge(new SrsPlayEdge());

    // Replace the real ingester with mock ingester
    MockEdgeIngester *mock_ingester = new MockEdgeIngester();
    srs_freep(play_edge->ingester_);
    play_edge->ingester_ = mock_ingester;

    // Initialize the play edge
    HELPER_EXPECT_SUCCESS(play_edge->initialize(source_ptr, mock_req.get()));

    // Client starts playing - state should transition to SrsEdgeStatePlay
    HELPER_EXPECT_SUCCESS(play_edge->on_client_play());
    EXPECT_EQ(SrsEdgeStatePlay, play_edge->state_);

    // Ingester connects and starts playing - state should transition to SrsEdgeStateIngestConnected
    HELPER_EXPECT_SUCCESS(play_edge->on_ingest_play());
    EXPECT_EQ(SrsEdgeStateIngestConnected, play_edge->state_);

    // Call on_ingest_play again when already connected - should return success and remain in same state
    HELPER_EXPECT_SUCCESS(play_edge->on_ingest_play());
    EXPECT_EQ(SrsEdgeStateIngestConnected, play_edge->state_);

    // All clients stop - state should transition back to SrsEdgeStateInit
    play_edge->on_all_client_stop();
    EXPECT_EQ(SrsEdgeStateInit, play_edge->state_);

    // Clean up: ingester_ will be freed by play_edge destructor
}

VOID TEST(PublishEdgeTest, InitializeSetQueueSizeAndCanPublish)
{
    srs_error_t err;

    // Create mock dependencies
    SrsSharedPtr<SrsLiveSource> source_ptr(new MockLiveSource());
    SrsUniquePtr<MockEdgeRequest> mock_req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create SrsPublishEdge
    SrsUniquePtr<SrsPublishEdge> publish_edge(new SrsPublishEdge());

    // Replace the real forwarder with mock forwarder
    MockEdgeForwarder *mock_forwarder = new MockEdgeForwarder();
    srs_freep(publish_edge->forwarder_);
    publish_edge->forwarder_ = mock_forwarder;

    // Test initial state - should be able to publish
    EXPECT_TRUE(publish_edge->can_publish());
    EXPECT_EQ(SrsEdgeStateInit, publish_edge->state_);

    // Test set_queue_size method - should delegate to forwarder
    srs_utime_t queue_size = 10 * SRS_UTIME_SECONDS;
    publish_edge->set_queue_size(queue_size);
    EXPECT_TRUE(mock_forwarder->set_queue_size_called_);
    EXPECT_EQ(queue_size, mock_forwarder->queue_size_);

    // Test initialize method - should initialize forwarder
    HELPER_EXPECT_SUCCESS(publish_edge->initialize(source_ptr, mock_req.get()));
    EXPECT_TRUE(mock_forwarder->initialize_called_);

    // After initialization, should still be able to publish (state is still Init)
    EXPECT_TRUE(publish_edge->can_publish());
    EXPECT_EQ(SrsEdgeStateInit, publish_edge->state_);

    // Clean up: forwarder_ will be freed by publish_edge destructor
}

VOID TEST(PublishEdgeTest, ClientPublishProxyAndUnpublish)
{
    srs_error_t err;

    // Create mock dependencies
    SrsSharedPtr<SrsLiveSource> source_ptr(new MockLiveSource());
    SrsUniquePtr<MockEdgeRequest> mock_req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create SrsPublishEdge
    SrsUniquePtr<SrsPublishEdge> publish_edge(new SrsPublishEdge());

    // Replace the real forwarder with mock forwarder
    MockEdgeForwarder *mock_forwarder = new MockEdgeForwarder();
    srs_freep(publish_edge->forwarder_);
    publish_edge->forwarder_ = mock_forwarder;

    // Initialize the publish edge
    HELPER_EXPECT_SUCCESS(publish_edge->initialize(source_ptr, mock_req.get()));
    EXPECT_EQ(SrsEdgeStateInit, publish_edge->state_);

    // Test on_client_publish - should start forwarder and transition to Publish state
    HELPER_EXPECT_SUCCESS(publish_edge->on_client_publish());
    EXPECT_TRUE(mock_forwarder->start_called_);
    EXPECT_EQ(SrsEdgeStatePublish, publish_edge->state_);

    // Test on_proxy_publish - should proxy message to forwarder
    SrsUniquePtr<SrsRtmpCommonMessage> msg(new SrsRtmpCommonMessage());
    HELPER_EXPECT_SUCCESS(publish_edge->on_proxy_publish(msg.get()));
    EXPECT_TRUE(mock_forwarder->proxy_called_);

    // Test on_proxy_unpublish - should stop forwarder and transition back to Init state
    publish_edge->on_proxy_unpublish();
    EXPECT_TRUE(mock_forwarder->stop_called_);
    EXPECT_EQ(SrsEdgeStateInit, publish_edge->state_);

    // Clean up: forwarder_ will be freed by publish_edge destructor
}

// MockStatisticForRtspPlayStream implementation
MockStatisticForRtspPlayStream::MockStatisticForRtspPlayStream()
{
    on_client_count_ = 0;
    on_client_error_ = srs_success;
}

MockStatisticForRtspPlayStream::~MockStatisticForRtspPlayStream()
{
    srs_freep(on_client_error_);
}

void MockStatisticForRtspPlayStream::on_disconnect(std::string id, srs_error_t err)
{
}

srs_error_t MockStatisticForRtspPlayStream::on_client(std::string id, ISrsRequest *req, ISrsExpire *conn, SrsRtmpConnType type)
{
    on_client_count_++;
    return srs_error_copy(on_client_error_);
}

srs_error_t MockStatisticForRtspPlayStream::on_video_info(ISrsRequest *req, SrsVideoCodecId vcodec, int avc_profile, int avc_level, int width, int height)
{
    return srs_success;
}

srs_error_t MockStatisticForRtspPlayStream::on_audio_info(ISrsRequest *req, SrsAudioCodecId acodec, SrsAudioSampleRate asample_rate, SrsAudioChannels asound_type, SrsAacObjectType aac_object)
{
    return srs_success;
}

void MockStatisticForRtspPlayStream::on_stream_publish(ISrsRequest *req, std::string publisher_id)
{
}

void MockStatisticForRtspPlayStream::on_stream_close(ISrsRequest *req)
{
}

void MockStatisticForRtspPlayStream::kbps_add_delta(std::string id, ISrsKbpsDelta *delta)
{
}

void MockStatisticForRtspPlayStream::kbps_sample()
{
}

srs_error_t MockStatisticForRtspPlayStream::on_video_frames(ISrsRequest *req, int nb_frames)
{
    return srs_success;
}

std::string MockStatisticForRtspPlayStream::server_id()
{
    return "mock_server_id";
}

std::string MockStatisticForRtspPlayStream::service_id()
{
    return "mock_service_id";
}

std::string MockStatisticForRtspPlayStream::service_pid()
{
    return "mock_service_pid";
}

SrsStatisticVhost *MockStatisticForRtspPlayStream::find_vhost_by_id(std::string vid)
{
    return NULL;
}

SrsStatisticStream *MockStatisticForRtspPlayStream::find_stream(std::string sid)
{
    return NULL;
}

SrsStatisticStream *MockStatisticForRtspPlayStream::find_stream_by_url(std::string url)
{
    return NULL;
}

SrsStatisticClient *MockStatisticForRtspPlayStream::find_client(std::string client_id)
{
    return NULL;
}

srs_error_t MockStatisticForRtspPlayStream::dumps_vhosts(SrsJsonArray *arr)
{
    return srs_success;
}

srs_error_t MockStatisticForRtspPlayStream::dumps_streams(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockStatisticForRtspPlayStream::dumps_clients(SrsJsonArray *arr, int start, int count)
{
    return srs_success;
}

srs_error_t MockStatisticForRtspPlayStream::dumps_metrics(int64_t &send_bytes, int64_t &recv_bytes, int64_t &nstreams, int64_t &nclients, int64_t &total_nclients, int64_t &nerrs)
{
    send_bytes = 0;
    recv_bytes = 0;
    nstreams = 0;
    nclients = 0;
    total_nclients = 0;
    nerrs = 0;
    return srs_success;
}

void MockStatisticForRtspPlayStream::reset()
{
    on_client_count_ = 0;
    srs_freep(on_client_error_);
}

// MockRtspSourceManager implementation
MockRtspSourceManager::MockRtspSourceManager()
{
    fetch_or_create_count_ = 0;
    fetch_or_create_error_ = srs_success;
    mock_source_ = SrsSharedPtr<SrsRtspSource>(new SrsRtspSource());
}

MockRtspSourceManager::~MockRtspSourceManager()
{
    srs_freep(fetch_or_create_error_);
}

srs_error_t MockRtspSourceManager::initialize()
{
    return srs_success;
}

srs_error_t MockRtspSourceManager::fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtspSource> &pps)
{
    fetch_or_create_count_++;
    if (fetch_or_create_error_ != srs_success) {
        return srs_error_copy(fetch_or_create_error_);
    }
    pps = mock_source_;
    return srs_success;
}

SrsSharedPtr<SrsRtspSource> MockRtspSourceManager::fetch(ISrsRequest *r)
{
    return mock_source_;
}

void MockRtspSourceManager::reset()
{
    fetch_or_create_count_ = 0;
    srs_freep(fetch_or_create_error_);
}

// MockRtspSendTrack implementation
MockRtspSendTrack::MockRtspSendTrack(std::string track_id, SrsRtcTrackDescription *desc)
{
    track_id_ = track_id;
    track_desc_ = desc->copy();
    track_status_ = false;
    on_rtp_count_ = 0;
    last_ssrc_ = 0;
    last_sequence_ = 0;
}

MockRtspSendTrack::~MockRtspSendTrack()
{
    srs_freep(track_desc_);
}

bool MockRtspSendTrack::set_track_status(bool active)
{
    bool previous = track_status_;
    track_status_ = active;
    return previous;
}

std::string MockRtspSendTrack::get_track_id()
{
    return track_id_;
}

SrsRtcTrackDescription *MockRtspSendTrack::track_desc()
{
    return track_desc_;
}

srs_error_t MockRtspSendTrack::on_rtp(SrsRtpPacket *pkt)
{
    on_rtp_count_++;
    last_ssrc_ = pkt->header_.get_ssrc();
    last_sequence_ = pkt->header_.get_sequence();
    return srs_success;
}

void MockRtspSendTrack::reset()
{
    on_rtp_count_ = 0;
    last_ssrc_ = 0;
    last_sequence_ = 0;
}

// MockRtspStack implementation
MockRtspStack::MockRtspStack()
{
    send_message_called_ = false;
    last_response_seq_ = 0;
    last_response_session_ = "";
    last_response_type_ = "";
    send_message_error_ = srs_success;
}

MockRtspStack::~MockRtspStack()
{
    srs_freep(send_message_error_);
}

srs_error_t MockRtspStack::recv_message(SrsRtspRequest **preq)
{
    return srs_success;
}

srs_error_t MockRtspStack::send_message(SrsRtspResponse *res)
{
    send_message_called_ = true;
    last_response_seq_ = (int)res->seq_;
    last_response_session_ = res->session_;

    // Determine response type by dynamic_cast
    if (dynamic_cast<SrsRtspOptionsResponse *>(res)) {
        last_response_type_ = "OPTIONS";
    } else if (dynamic_cast<SrsRtspDescribeResponse *>(res)) {
        last_response_type_ = "DESCRIBE";
    } else if (dynamic_cast<SrsRtspSetupResponse *>(res)) {
        last_response_type_ = "SETUP";
    } else {
        last_response_type_ = "GENERIC";
    }

    return srs_error_copy(send_message_error_);
}

void MockRtspStack::reset()
{
    send_message_called_ = false;
    last_response_seq_ = 0;
    last_response_session_ = "";
    last_response_type_ = "";
    srs_freep(send_message_error_);
}

// MockRtspPlayStream implementation
MockRtspPlayStream::MockRtspPlayStream()
{
    initialize_called_ = false;
    start_called_ = false;
    stop_called_ = false;
    set_all_tracks_status_called_ = false;
    set_all_tracks_status_value_ = false;
    initialize_error_ = srs_success;
    start_error_ = srs_success;
}

MockRtspPlayStream::~MockRtspPlayStream()
{
    srs_freep(initialize_error_);
    srs_freep(start_error_);
}

srs_error_t MockRtspPlayStream::initialize(ISrsRequest *request, std::map<uint32_t, SrsRtcTrackDescription *> sub_relations)
{
    initialize_called_ = true;
    return srs_error_copy(initialize_error_);
}

srs_error_t MockRtspPlayStream::start()
{
    start_called_ = true;
    return srs_error_copy(start_error_);
}

void MockRtspPlayStream::stop()
{
    stop_called_ = true;
}

void MockRtspPlayStream::set_all_tracks_status(bool status)
{
    set_all_tracks_status_called_ = true;
    set_all_tracks_status_value_ = status;
}

void MockRtspPlayStream::reset()
{
    initialize_called_ = false;
    start_called_ = false;
    stop_called_ = false;
    set_all_tracks_status_called_ = false;
    set_all_tracks_status_value_ = false;
    srs_freep(initialize_error_);
    srs_freep(start_error_);
}

// MockAppFactoryForRtspPlayStream implementation
MockAppFactoryForRtspPlayStream::MockAppFactoryForRtspPlayStream()
{
    create_rtsp_audio_send_track_count_ = 0;
    create_rtsp_video_send_track_count_ = 0;
}

MockAppFactoryForRtspPlayStream::~MockAppFactoryForRtspPlayStream()
{
}

ISrsRtspSendTrack *MockAppFactoryForRtspPlayStream::create_rtsp_audio_send_track(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc)
{
    create_rtsp_audio_send_track_count_++;
    return new MockRtspSendTrack("audio_track", track_desc);
}

ISrsRtspSendTrack *MockAppFactoryForRtspPlayStream::create_rtsp_video_send_track(ISrsRtspConnection *session, SrsRtcTrackDescription *track_desc)
{
    create_rtsp_video_send_track_count_++;
    return new MockRtspSendTrack("video_track", track_desc);
}

void MockAppFactoryForRtspPlayStream::reset()
{
    create_rtsp_audio_send_track_count_ = 0;
    create_rtsp_video_send_track_count_ = 0;
}

VOID TEST(RtspPlayStreamTest, InitializeWithAudioAndVideoTracks)
{
    srs_error_t err;

    // Create mock dependencies - these must outlive play_stream
    MockRtspConnection mock_session;
    MockStatisticForRtspPlayStream mock_stat;
    MockRtspSourceManager mock_rtsp_sources;
    MockAppFactoryForRtspPlayStream mock_app_factory;
    SrsUniquePtr<MockEdgeRequest> mock_req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create SrsRtspPlayStream
    SrsContextId cid;
    SrsUniquePtr<SrsRtspPlayStream> play_stream(new SrsRtspPlayStream(&mock_session, cid));

    // Inject mock dependencies
    play_stream->stat_ = &mock_stat;
    play_stream->rtsp_sources_ = &mock_rtsp_sources;
    play_stream->app_factory_ = &mock_app_factory;

    // Create track descriptions for audio and video
    SrsUniquePtr<SrsRtcTrackDescription> audio_desc(new SrsRtcTrackDescription());
    audio_desc->type_ = "audio";
    audio_desc->id_ = "audio_track_1";
    audio_desc->ssrc_ = 1001;

    SrsUniquePtr<SrsRtcTrackDescription> video_desc(new SrsRtcTrackDescription());
    video_desc->type_ = "video";
    video_desc->id_ = "video_track_1";
    video_desc->ssrc_ = 2001;

    // Create sub_relations map
    std::map<uint32_t, SrsRtcTrackDescription *> sub_relations;
    sub_relations[1001] = audio_desc.get();
    sub_relations[2001] = video_desc.get();

    // Test initialize method
    HELPER_EXPECT_SUCCESS(play_stream->initialize(mock_req.get(), sub_relations));

    // Verify that stat->on_client was called
    EXPECT_EQ(1, mock_stat.on_client_count_);

    // Verify that rtsp_sources->fetch_or_create was called
    EXPECT_EQ(1, mock_rtsp_sources.fetch_or_create_count_);

    // Verify that audio and video tracks were created
    EXPECT_EQ(1, mock_app_factory.create_rtsp_audio_send_track_count_);
    EXPECT_EQ(1, mock_app_factory.create_rtsp_video_send_track_count_);

    // Verify that tracks were added to the maps
    EXPECT_EQ(1, (int)play_stream->audio_tracks_.size());
    EXPECT_EQ(1, (int)play_stream->video_tracks_.size());

    // Verify the audio track
    EXPECT_TRUE(play_stream->audio_tracks_.find(1001) != play_stream->audio_tracks_.end());
    ISrsRtspSendTrack *audio_track = play_stream->audio_tracks_[1001];
    EXPECT_TRUE(audio_track != NULL);
    EXPECT_EQ("audio_track", audio_track->get_track_id());

    // Verify the video track
    EXPECT_TRUE(play_stream->video_tracks_.find(2001) != play_stream->video_tracks_.end());
    ISrsRtspSendTrack *video_track = play_stream->video_tracks_[2001];
    EXPECT_TRUE(video_track != NULL);
    EXPECT_EQ("video_track", video_track->get_track_id());

    // Note: play_stream will be destroyed before mocks go out of scope
    // The destructor calls stat_->on_disconnect(), so stat_ must remain valid
}

VOID TEST(RtspPlayStreamTest, OnStreamChange)
{
    srs_error_t err = srs_success;

    // Create mock dependencies
    MockStatisticForRtspPlayStream mock_stat;
    MockRtspSourceManager mock_rtsp_sources;
    MockAppFactoryForRtspPlayStream mock_app_factory;

    // Create a mock RTSP source
    mock_rtsp_sources.mock_source_ = SrsSharedPtr<SrsRtspSource>(new SrsRtspSource());

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> mock_req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create SrsRtspPlayStream instance
    SrsContextId cid;
    SrsUniquePtr<SrsRtspPlayStream> play_stream(new SrsRtspPlayStream(NULL, cid));

    // Inject mock dependencies
    play_stream->stat_ = &mock_stat;
    play_stream->rtsp_sources_ = &mock_rtsp_sources;
    play_stream->app_factory_ = &mock_app_factory;

    // Create initial audio and video track descriptions with original SSRCs and PTs
    SrsUniquePtr<SrsRtcTrackDescription> audio_desc(new SrsRtcTrackDescription());
    audio_desc->type_ = "audio";
    audio_desc->id_ = "audio_track";
    audio_desc->ssrc_ = 1001;
    audio_desc->media_ = new SrsAudioPayload(111, "opus", 48000, 2);
    audio_desc->media_->pt_of_publisher_ = 111;
    audio_desc->red_ = new SrsRedPayload(63, "red", 48000, 2);
    audio_desc->red_->pt_of_publisher_ = 63;

    SrsUniquePtr<SrsRtcTrackDescription> video_desc(new SrsRtcTrackDescription());
    video_desc->type_ = "video";
    video_desc->id_ = "video_track";
    video_desc->ssrc_ = 2001;
    video_desc->media_ = new SrsVideoPayload(102, "H264", 90000);
    video_desc->media_->pt_of_publisher_ = 102;
    video_desc->red_ = new SrsCodecPayload(100, "rtx", 90000);
    video_desc->red_->pt_of_publisher_ = 100;

    // Initialize with sub_relations
    std::map<uint32_t, SrsRtcTrackDescription *> sub_relations;
    sub_relations[1001] = audio_desc.get();
    sub_relations[2001] = video_desc.get();

    HELPER_EXPECT_SUCCESS(play_stream->initialize(mock_req.get(), sub_relations));

    // Verify initial state
    EXPECT_EQ(1, (int)play_stream->audio_tracks_.size());
    EXPECT_EQ(1, (int)play_stream->video_tracks_.size());

    // Get the tracks
    ISrsRtspSendTrack *audio_track = play_stream->audio_tracks_[1001];
    ISrsRtspSendTrack *video_track = play_stream->video_tracks_[2001];
    EXPECT_TRUE(audio_track != NULL);
    EXPECT_TRUE(video_track != NULL);

    // Verify initial PT values
    EXPECT_EQ(111, audio_track->track_desc()->media_->pt_of_publisher_);
    EXPECT_EQ(63, audio_track->track_desc()->red_->pt_of_publisher_);
    EXPECT_EQ(102, video_track->track_desc()->media_->pt_of_publisher_);
    EXPECT_EQ(100, video_track->track_desc()->red_->pt_of_publisher_);

    // Create a new stream description with changed SSRCs and PTs (simulating stream change)
    SrsUniquePtr<SrsRtcSourceDescription> new_desc(new SrsRtcSourceDescription());

    // Create new audio track description with different SSRC and PT
    new_desc->audio_track_desc_ = new SrsRtcTrackDescription();
    new_desc->audio_track_desc_->type_ = "audio";
    new_desc->audio_track_desc_->ssrc_ = 1002;                                        // Changed SSRC
    new_desc->audio_track_desc_->media_ = new SrsAudioPayload(112, "opus", 48000, 2); // Changed PT
    new_desc->audio_track_desc_->media_->pt_ = 112;
    new_desc->audio_track_desc_->red_ = new SrsRedPayload(64, "red", 48000, 2); // Changed PT
    new_desc->audio_track_desc_->red_->pt_ = 64;

    // Create new video track description with different SSRC and PT
    SrsRtcTrackDescription *new_video_desc = new SrsRtcTrackDescription();
    new_video_desc->type_ = "video";
    new_video_desc->ssrc_ = 2002;                                     // Changed SSRC
    new_video_desc->media_ = new SrsVideoPayload(103, "H264", 90000); // Changed PT
    new_video_desc->media_->pt_ = 103;
    new_video_desc->red_ = new SrsCodecPayload(101, "rtx", 90000); // Changed PT
    new_video_desc->red_->pt_ = 101;
    new_desc->video_track_descs_.push_back(new_video_desc);

    // Call on_stream_change
    play_stream->on_stream_change(new_desc.get());

    // Verify that audio track map was updated with new SSRC
    EXPECT_EQ(1, (int)play_stream->audio_tracks_.size());
    EXPECT_TRUE(play_stream->audio_tracks_.find(1001) == play_stream->audio_tracks_.end()); // Old SSRC removed
    EXPECT_TRUE(play_stream->audio_tracks_.find(1002) != play_stream->audio_tracks_.end()); // New SSRC added

    // Verify that video track map was updated with new SSRC
    EXPECT_EQ(1, (int)play_stream->video_tracks_.size());
    EXPECT_TRUE(play_stream->video_tracks_.find(2001) == play_stream->video_tracks_.end()); // Old SSRC removed
    EXPECT_TRUE(play_stream->video_tracks_.find(2002) != play_stream->video_tracks_.end()); // New SSRC added

    // Verify that the track objects are the same (not recreated)
    EXPECT_EQ(audio_track, play_stream->audio_tracks_[1002]);
    EXPECT_EQ(video_track, play_stream->video_tracks_[2002]);

    // Verify that PT values were updated
    EXPECT_EQ(112, audio_track->track_desc()->media_->pt_of_publisher_);
    EXPECT_EQ(64, audio_track->track_desc()->red_->pt_of_publisher_);
    EXPECT_EQ(103, video_track->track_desc()->media_->pt_of_publisher_);
    EXPECT_EQ(101, video_track->track_desc()->red_->pt_of_publisher_);

    // Test with NULL desc (should return early without error)
    play_stream->on_stream_change(NULL);
    EXPECT_EQ(1, (int)play_stream->audio_tracks_.size());
    EXPECT_EQ(1, (int)play_stream->video_tracks_.size());
}

VOID TEST(RtspPlayStreamTest, SendPacketWithCacheAndTrackLookup)
{
    srs_error_t err = srs_success;

    // Create mock dependencies
    MockRtspConnection mock_session;
    MockStatisticForRtspPlayStream mock_stat;
    MockRtspSourceManager mock_rtsp_sources;
    MockAppFactoryForRtspPlayStream mock_app_factory;
    SrsUniquePtr<MockEdgeRequest> mock_req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create SrsRtspPlayStream
    SrsContextId cid;
    SrsUniquePtr<SrsRtspPlayStream> play_stream(new SrsRtspPlayStream(&mock_session, cid));

    // Inject mock dependencies
    play_stream->stat_ = &mock_stat;
    play_stream->rtsp_sources_ = &mock_rtsp_sources;
    play_stream->app_factory_ = &mock_app_factory;

    // Create track descriptions for audio and video
    SrsUniquePtr<SrsRtcTrackDescription> audio_desc(new SrsRtcTrackDescription());
    audio_desc->type_ = "audio";
    audio_desc->id_ = "audio_track_1";
    audio_desc->ssrc_ = 1001;

    SrsUniquePtr<SrsRtcTrackDescription> video_desc(new SrsRtcTrackDescription());
    video_desc->type_ = "video";
    video_desc->id_ = "video_track_1";
    video_desc->ssrc_ = 2001;

    // Create sub_relations map
    std::map<uint32_t, SrsRtcTrackDescription *> sub_relations;
    sub_relations[1001] = audio_desc.get();
    sub_relations[2001] = video_desc.get();

    // Initialize play stream
    HELPER_EXPECT_SUCCESS(play_stream->initialize(mock_req.get(), sub_relations));

    // Get references to the created tracks
    MockRtspSendTrack *audio_track = dynamic_cast<MockRtspSendTrack *>(play_stream->audio_tracks_[1001]);
    MockRtspSendTrack *video_track = dynamic_cast<MockRtspSendTrack *>(play_stream->video_tracks_[2001]);
    EXPECT_TRUE(audio_track != NULL);
    EXPECT_TRUE(video_track != NULL);

    // Verify cache is initially empty
    EXPECT_EQ(0u, play_stream->cache_ssrc0_);
    EXPECT_EQ(0u, play_stream->cache_ssrc1_);
    EXPECT_EQ(0u, play_stream->cache_ssrc2_);
    EXPECT_TRUE(play_stream->cache_track0_ == NULL);
    EXPECT_TRUE(play_stream->cache_track1_ == NULL);
    EXPECT_TRUE(play_stream->cache_track2_ == NULL);

    // Create audio RTP packet with SSRC 1001
    SrsUniquePtr<SrsRtpPacket> audio_pkt(new SrsRtpPacket());
    audio_pkt->header_.set_ssrc(1001);
    audio_pkt->header_.set_sequence(100);
    audio_pkt->frame_type_ = SrsFrameTypeAudio;

    // Send audio packet - should build cache for slot 0
    SrsRtpPacket *audio_pkt_ptr = audio_pkt.get();
    HELPER_EXPECT_SUCCESS(play_stream->send_packet(audio_pkt_ptr));

    // Verify audio track received the packet
    EXPECT_EQ(1, audio_track->on_rtp_count_);
    EXPECT_EQ(1001u, audio_track->last_ssrc_);
    EXPECT_EQ(100, audio_track->last_sequence_);

    // Verify cache was built for audio track in slot 0
    EXPECT_EQ(1001u, play_stream->cache_ssrc0_);
    EXPECT_EQ(audio_track, play_stream->cache_track0_);
    EXPECT_EQ(0u, play_stream->cache_ssrc1_);
    EXPECT_EQ(0u, play_stream->cache_ssrc2_);

    // Create video RTP packet with SSRC 2001
    SrsUniquePtr<SrsRtpPacket> video_pkt(new SrsRtpPacket());
    video_pkt->header_.set_ssrc(2001);
    video_pkt->header_.set_sequence(200);
    video_pkt->frame_type_ = SrsFrameTypeVideo;

    // Send video packet - should build cache for slot 1
    SrsRtpPacket *video_pkt_ptr = video_pkt.get();
    HELPER_EXPECT_SUCCESS(play_stream->send_packet(video_pkt_ptr));

    // Verify video track received the packet
    EXPECT_EQ(1, video_track->on_rtp_count_);
    EXPECT_EQ(2001u, video_track->last_ssrc_);
    EXPECT_EQ(200, video_track->last_sequence_);

    // Verify cache was built for video track in slot 1
    EXPECT_EQ(1001u, play_stream->cache_ssrc0_);
    EXPECT_EQ(audio_track, play_stream->cache_track0_);
    EXPECT_EQ(2001u, play_stream->cache_ssrc1_);
    EXPECT_EQ(video_track, play_stream->cache_track1_);
    EXPECT_EQ(0u, play_stream->cache_ssrc2_);

    // Send another audio packet - should hit cache slot 0
    audio_track->reset();
    SrsUniquePtr<SrsRtpPacket> audio_pkt2(new SrsRtpPacket());
    audio_pkt2->header_.set_ssrc(1001);
    audio_pkt2->header_.set_sequence(101);
    audio_pkt2->frame_type_ = SrsFrameTypeAudio;

    SrsRtpPacket *audio_pkt2_ptr = audio_pkt2.get();
    HELPER_EXPECT_SUCCESS(play_stream->send_packet(audio_pkt2_ptr));

    // Verify audio track received the second packet
    EXPECT_EQ(1, audio_track->on_rtp_count_);
    EXPECT_EQ(1001u, audio_track->last_ssrc_);
    EXPECT_EQ(101, audio_track->last_sequence_);

    // Send another video packet - should hit cache slot 1
    video_track->reset();
    SrsUniquePtr<SrsRtpPacket> video_pkt2(new SrsRtpPacket());
    video_pkt2->header_.set_ssrc(2001);
    video_pkt2->header_.set_sequence(201);
    video_pkt2->frame_type_ = SrsFrameTypeVideo;

    SrsRtpPacket *video_pkt2_ptr = video_pkt2.get();
    HELPER_EXPECT_SUCCESS(play_stream->send_packet(video_pkt2_ptr));

    // Verify video track received the second packet
    EXPECT_EQ(1, video_track->on_rtp_count_);
    EXPECT_EQ(2001u, video_track->last_ssrc_);
    EXPECT_EQ(201, video_track->last_sequence_);

    // Test packet with unknown SSRC - should be dropped silently
    SrsUniquePtr<SrsRtpPacket> unknown_pkt(new SrsRtpPacket());
    unknown_pkt->header_.set_ssrc(9999);
    unknown_pkt->header_.set_sequence(999);
    unknown_pkt->frame_type_ = SrsFrameTypeAudio;

    SrsRtpPacket *unknown_pkt_ptr = unknown_pkt.get();
    HELPER_EXPECT_SUCCESS(play_stream->send_packet(unknown_pkt_ptr));

    // Verify tracks did not receive the unknown packet
    EXPECT_EQ(1, audio_track->on_rtp_count_);
    EXPECT_EQ(1, video_track->on_rtp_count_);
}

VOID TEST(RtspPlayStreamTest, SetAllTracksStatus)
{
    // Create mock dependencies
    MockRtspConnection mock_session;
    MockStatisticForRtspPlayStream mock_stat;
    MockRtspSourceManager mock_source_manager;
    MockAppFactoryForRtspPlayStream mock_factory;

    // Create SrsRtspPlayStream
    SrsContextId cid;
    SrsUniquePtr<SrsRtspPlayStream> play_stream(new SrsRtspPlayStream(&mock_session, cid));

    // Inject mock dependencies
    play_stream->stat_ = &mock_stat;
    play_stream->rtsp_sources_ = &mock_source_manager;
    play_stream->app_factory_ = &mock_factory;

    // Create mock track descriptions
    SrsUniquePtr<SrsRtcTrackDescription> audio_desc1(new SrsRtcTrackDescription());
    audio_desc1->type_ = "audio";
    audio_desc1->id_ = "audio-track-1";
    audio_desc1->ssrc_ = 1001;

    SrsUniquePtr<SrsRtcTrackDescription> audio_desc2(new SrsRtcTrackDescription());
    audio_desc2->type_ = "audio";
    audio_desc2->id_ = "audio-track-2";
    audio_desc2->ssrc_ = 1002;

    SrsUniquePtr<SrsRtcTrackDescription> video_desc1(new SrsRtcTrackDescription());
    video_desc1->type_ = "video";
    video_desc1->id_ = "video-track-1";
    video_desc1->ssrc_ = 2001;

    SrsUniquePtr<SrsRtcTrackDescription> video_desc2(new SrsRtcTrackDescription());
    video_desc2->type_ = "video";
    video_desc2->id_ = "video-track-2";
    video_desc2->ssrc_ = 2002;

    // Create mock tracks
    MockRtspSendTrack *audio_track1 = new MockRtspSendTrack("audio-track-1", audio_desc1.get());
    MockRtspSendTrack *audio_track2 = new MockRtspSendTrack("audio-track-2", audio_desc2.get());
    MockRtspSendTrack *video_track1 = new MockRtspSendTrack("video-track-1", video_desc1.get());
    MockRtspSendTrack *video_track2 = new MockRtspSendTrack("video-track-2", video_desc2.get());

    // Add tracks to play_stream
    play_stream->audio_tracks_[1001] = audio_track1;
    play_stream->audio_tracks_[1002] = audio_track2;
    play_stream->video_tracks_[2001] = video_track1;
    play_stream->video_tracks_[2002] = video_track2;

    // Verify initial status is false (default from MockRtspSendTrack constructor)
    EXPECT_FALSE(audio_track1->track_status_);
    EXPECT_FALSE(audio_track2->track_status_);
    EXPECT_FALSE(video_track1->track_status_);
    EXPECT_FALSE(video_track2->track_status_);

    // Set all tracks to active
    play_stream->set_all_tracks_status(true);

    // Verify all tracks are now active
    EXPECT_TRUE(audio_track1->track_status_);
    EXPECT_TRUE(audio_track2->track_status_);
    EXPECT_TRUE(video_track1->track_status_);
    EXPECT_TRUE(video_track2->track_status_);

    // Set all tracks to inactive
    play_stream->set_all_tracks_status(false);

    // Verify all tracks are now inactive
    EXPECT_FALSE(audio_track1->track_status_);
    EXPECT_FALSE(audio_track2->track_status_);
    EXPECT_FALSE(video_track1->track_status_);
    EXPECT_FALSE(video_track2->track_status_);

    // Note: play_stream will be destroyed before mocks go out of scope
    // The destructor will free the tracks in the maps and call stat_->on_disconnect()
}

// Test SrsRtspConnection::on_rtsp_request() - major use scenario
// This test covers the complete RTSP play flow which is the most common scenario:
// 1. Client sends OPTIONS request to query server capabilities
// 2. Client sends DESCRIBE request to get stream SDP information
// 3. Client sends SETUP request to configure transport for a track
// 4. Client sends PLAY request to start streaming
// 5. Client sends TEARDOWN request to stop streaming
//
// This test uses mocks to avoid needing real network connections and RTSP sources.
VOID TEST(RtspConnectionTest, OnRtspRequestCompletePlayFlow)
{
    srs_error_t err;

    // Create mock RTSP stack
    MockRtspStack *mock_rtsp = new MockRtspStack();

    // Create RTSP connection (use minimal constructor parameters)
    SrsUniquePtr<SrsRtspConnection> conn(new SrsRtspConnection(NULL, NULL, "127.0.0.1", 8554));

    // Inject mock RTSP stack
    conn->rtsp_ = mock_rtsp;

    // Initialize session_id for testing
    conn->session_id_ = "test_session_123";

    // Test 1: OPTIONS request
    {
        SrsRtspRequest *req = new SrsRtspRequest();
        req->method_ = "OPTIONS";
        req->uri_ = "rtsp://127.0.0.1:8554/live/stream";
        req->seq_ = 1;

        mock_rtsp->reset();
        err = conn->on_rtsp_request(req);
        HELPER_EXPECT_SUCCESS(err);

        // Verify OPTIONS response was sent
        EXPECT_TRUE(mock_rtsp->send_message_called_);
        EXPECT_EQ(1, mock_rtsp->last_response_seq_);
        EXPECT_STREQ("OPTIONS", mock_rtsp->last_response_type_.c_str());
    }

    // Test 2: DESCRIBE request
    {
        SrsRtspRequest *req = new SrsRtspRequest();
        req->method_ = "DESCRIBE";
        req->uri_ = "rtsp://127.0.0.1:8554/live/stream";
        req->seq_ = 2;

        mock_rtsp->reset();
        err = conn->on_rtsp_request(req);
        HELPER_EXPECT_SUCCESS(err);

        // Verify DESCRIBE response was sent
        EXPECT_TRUE(mock_rtsp->send_message_called_);
        EXPECT_EQ(2, mock_rtsp->last_response_seq_);
        EXPECT_STREQ("DESCRIBE", mock_rtsp->last_response_type_.c_str());
        EXPECT_STREQ("test_session_123", mock_rtsp->last_response_session_.c_str());
    }

    // Test 3: SETUP request
    {
        SrsRtspRequest *req = new SrsRtspRequest();
        req->method_ = "SETUP";
        req->uri_ = "rtsp://127.0.0.1:8554/live/stream/trackID=0";
        req->seq_ = 3;
        req->stream_id_ = 0;
        req->transport_ = new SrsRtspTransport();
        req->transport_->transport_ = "RTP";
        req->transport_->profile_ = "AVP";
        req->transport_->lower_transport_ = "UDP";
        req->transport_->client_port_min_ = 50000;
        req->transport_->client_port_max_ = 50001;

        mock_rtsp->reset();
        err = conn->on_rtsp_request(req);
        HELPER_EXPECT_SUCCESS(err);

        // Verify SETUP response was sent
        EXPECT_TRUE(mock_rtsp->send_message_called_);
        EXPECT_EQ(3, mock_rtsp->last_response_seq_);
        EXPECT_STREQ("SETUP", mock_rtsp->last_response_type_.c_str());
        EXPECT_STREQ("test_session_123", mock_rtsp->last_response_session_.c_str());
    }

    // Test 4: PLAY request
    {
        SrsRtspRequest *req = new SrsRtspRequest();
        req->method_ = "PLAY";
        req->uri_ = "rtsp://127.0.0.1:8554/live/stream";
        req->seq_ = 4;
        req->session_ = "test_session_123";

        mock_rtsp->reset();
        err = conn->on_rtsp_request(req);
        HELPER_EXPECT_SUCCESS(err);

        // Verify PLAY response was sent
        EXPECT_TRUE(mock_rtsp->send_message_called_);
        EXPECT_EQ(4, mock_rtsp->last_response_seq_);
        EXPECT_STREQ("GENERIC", mock_rtsp->last_response_type_.c_str());
        EXPECT_STREQ("test_session_123", mock_rtsp->last_response_session_.c_str());
    }

    // Test 5: TEARDOWN request
    {
        SrsRtspRequest *req = new SrsRtspRequest();
        req->method_ = "TEARDOWN";
        req->uri_ = "rtsp://127.0.0.1:8554/live/stream";
        req->seq_ = 5;
        req->session_ = "test_session_123";

        mock_rtsp->reset();
        err = conn->on_rtsp_request(req);
        HELPER_EXPECT_SUCCESS(err);

        // Verify TEARDOWN response was sent
        EXPECT_TRUE(mock_rtsp->send_message_called_);
        EXPECT_EQ(5, mock_rtsp->last_response_seq_);
        EXPECT_STREQ("GENERIC", mock_rtsp->last_response_type_.c_str());
        EXPECT_STREQ("test_session_123", mock_rtsp->last_response_session_.c_str());
    }

    // Clean up: Set to NULL to avoid double-free
    conn->rtsp_ = NULL;
    srs_freep(mock_rtsp);
}

// Test SrsRtspConnection lifecycle and session management - major use scenario
// This test covers the complete lifecycle of an RTSP connection including:
// 1. Session timeout management (is_alive/alive methods)
// 2. Resource disposal lifecycle (on_before_dispose/on_disposing methods)
// 3. Context management (switch_to_context/context_id methods)
//
// This represents the typical lifecycle of an RTSP connection from creation
// through active session management to disposal.
VOID TEST(RtspConnectionTest, SessionLifecycleAndDisposal)
{
    // Create RTSP connection
    SrsUniquePtr<SrsRtspConnection> conn(new SrsRtspConnection(NULL, NULL, "127.0.0.1", 8554));

    // Test 1: Context management
    {
        // Get the context ID
        const SrsContextId &cid = conn->context_id();
        EXPECT_FALSE(cid.empty());

        // Switch to context should set the global context
        conn->switch_to_context();
        EXPECT_TRUE(_srs_context->get_id().compare(cid) == 0);
    }

    // Test 2: Session timeout management
    {
        // Set session timeout to 5 seconds
        conn->session_timeout = 5 * SRS_UTIME_SECONDS;

        // Initially, connection should not be alive (last_stun_time is 0)
        EXPECT_FALSE(conn->is_alive());

        // Mark connection as alive
        conn->alive();

        // Now connection should be alive
        EXPECT_TRUE(conn->is_alive());

        // Simulate time passing (but within timeout)
        srs_utime_t original_time = conn->last_stun_time;
        conn->last_stun_time = original_time - 3 * SRS_UTIME_SECONDS;
        EXPECT_TRUE(conn->is_alive());

        // Simulate timeout expiration
        conn->last_stun_time = original_time - 6 * SRS_UTIME_SECONDS;
        EXPECT_FALSE(conn->is_alive());

        // Refresh the session
        conn->alive();
        EXPECT_TRUE(conn->is_alive());
    }

    // Test 3: Resource disposal lifecycle
    {
        // Initially, disposing flag should be false
        EXPECT_FALSE(conn->disposing_);

        // Simulate on_before_dispose being called with this connection
        conn->on_before_dispose(conn.get());

        // After on_before_dispose, disposing flag should be true
        EXPECT_TRUE(conn->disposing_);

        // Calling on_before_dispose again should be safe (early return)
        conn->on_before_dispose(conn.get());
        EXPECT_TRUE(conn->disposing_);

        // Calling on_disposing should be safe (early return due to disposing_ flag)
        conn->on_disposing(conn.get());
        EXPECT_TRUE(conn->disposing_);
    }

    // Test 4: on_before_dispose with different resource (not self)
    {
        // Create another connection to test disposal of different resource
        SrsUniquePtr<SrsRtspConnection> other_conn(new SrsRtspConnection(NULL, NULL, "127.0.0.2", 8555));

        // Reset disposing flag for testing
        conn->disposing_ = false;

        // Call on_before_dispose with a different resource
        conn->on_before_dispose(other_conn.get());

        // disposing_ should remain false (not disposing self)
        EXPECT_FALSE(conn->disposing_);

        // Call on_disposing with a different resource
        conn->on_disposing(other_conn.get());

        // disposing_ should still be false
        EXPECT_FALSE(conn->disposing_);
    }
}

VOID TEST(RtspConnectionTest, DoDescribeWithAudioAndVideo)
{
    srs_error_t err = srs_success;

    // Create mock objects
    MockEdgeConfig mock_config;
    MockSecurity mock_security;
    MockHttpHooks mock_hooks;
    MockRtspSourceManager mock_rtsp_sources;

    // Create a mock RTSP source with audio and video track descriptions
    SrsSharedPtr<SrsRtspSource> mock_source(new SrsRtspSource());

    // Create audio track description with AAC codec
    SrsRtcTrackDescription *audio_desc = new SrsRtcTrackDescription();
    audio_desc->type_ = "audio";
    audio_desc->ssrc_ = 1001;
    SrsAudioPayload *audio_payload = new SrsAudioPayload(97, "MPEG4-GENERIC", 48000, 2);
    audio_payload->aac_config_hex_ = "1190"; // AAC config hex
    audio_desc->media_ = audio_payload;
    mock_source->audio_desc_ = audio_desc;

    // Create video track description with H264 codec
    SrsRtcTrackDescription *video_desc = new SrsRtcTrackDescription();
    video_desc->type_ = "video";
    video_desc->ssrc_ = 2001;
    video_desc->media_ = new SrsVideoPayload(96, "H264", 90000);
    mock_source->video_desc_ = video_desc;

    // Configure mock source manager to return the mock source
    mock_rtsp_sources.mock_source_ = mock_source;
    mock_rtsp_sources.fetch_or_create_error_ = srs_success;

    // Create RTSP connection
    SrsUniquePtr<SrsRtspConnection> conn(new SrsRtspConnection(NULL, NULL, "127.0.0.1", 8554));

    // Inject mock dependencies
    conn->config_ = &mock_config;
    conn->security_ = &mock_security;
    conn->hooks_ = &mock_hooks;
    conn->rtsp_sources_ = &mock_rtsp_sources;

    // Create RTSP request with URI
    SrsUniquePtr<SrsRtspRequest> req(new SrsRtspRequest());
    req->uri_ = "rtsp://127.0.0.1:8554/live/stream";

    // Call do_describe
    std::string sdp;
    HELPER_EXPECT_SUCCESS(conn->do_describe(req.get(), sdp));

    // Verify SDP is not empty
    EXPECT_FALSE(sdp.empty());

    // Verify SDP contains expected session information
    EXPECT_TRUE(sdp.find("v=0") != std::string::npos);
    EXPECT_TRUE(sdp.find("s=Play") != std::string::npos);
    EXPECT_TRUE(sdp.find("c=IN IP4 0.0.0.0") != std::string::npos);

    // Verify SDP contains audio media description
    EXPECT_TRUE(sdp.find("m=audio") != std::string::npos);
    EXPECT_TRUE(sdp.find("RTP/AVP") != std::string::npos);
    EXPECT_TRUE(sdp.find("MPEG4-GENERIC") != std::string::npos);
    EXPECT_TRUE(sdp.find("a=rtpmap:97 MPEG4-GENERIC/48000/2") != std::string::npos);

    // Verify AAC fmtp line is present with config
    EXPECT_TRUE(sdp.find("a=fmtp:97") != std::string::npos);
    EXPECT_TRUE(sdp.find("config=1190") != std::string::npos);
    EXPECT_TRUE(sdp.find("streamtype=5") != std::string::npos);
    EXPECT_TRUE(sdp.find("mode=AAC-hbr") != std::string::npos);

    // Verify SDP contains video media description
    EXPECT_TRUE(sdp.find("m=video") != std::string::npos);
    EXPECT_TRUE(sdp.find("H264") != std::string::npos);
    EXPECT_TRUE(sdp.find("a=rtpmap:96 H264/90000") != std::string::npos);

    // Verify control URLs are present
    EXPECT_TRUE(sdp.find("a=control:") != std::string::npos);
    EXPECT_TRUE(sdp.find("trackID=0") != std::string::npos);
    EXPECT_TRUE(sdp.find("trackID=1") != std::string::npos);

    // Verify recvonly attribute
    EXPECT_TRUE(sdp.find("a=recvonly") != std::string::npos);

    // Verify tracks were stored in connection
    EXPECT_EQ(2, (int)conn->tracks_.size());
    EXPECT_TRUE(conn->tracks_.find(1001) != conn->tracks_.end());
    EXPECT_TRUE(conn->tracks_.find(2001) != conn->tracks_.end());

    // Clean up injected mocks to avoid double-free
    conn->config_ = NULL;
    conn->security_ = NULL;
    conn->hooks_ = NULL;
    conn->rtsp_sources_ = NULL;
}

// Test SrsRtspConnection::do_play() and do_teardown() - major use scenario
// This test covers the typical RTSP play flow:
// 1. Client sends PLAY request to start streaming
// 2. Server creates SrsRtspPlayStream, initializes it, sets track status, and starts it
// 3. Client sends TEARDOWN request to stop streaming
// 4. Server stops and frees the player
//
// Note: In production code, SrsRtspConnection creates a real SrsRtspPlayStream.
// However, we cannot easily mock the player creation since it's done with 'new' inside do_play().
// Therefore, this test verifies the flow by checking that a real player is created and properly managed.
VOID TEST(RtspConnectionTest, DoPlayAndTeardown)
{
    srs_error_t err = srs_success;

    // Create RTSP connection
    SrsUniquePtr<SrsRtspConnection> conn(new SrsRtspConnection(NULL, NULL, "127.0.0.1", 8554));

    // Create mock dependencies
    SrsUniquePtr<MockEdgeConfig> mock_config(new MockEdgeConfig());
    SrsUniquePtr<MockStatisticForRtspPlayStream> mock_stat(new MockStatisticForRtspPlayStream());
    SrsUniquePtr<MockRtspSourceManager> mock_rtsp_sources(new MockRtspSourceManager());

    // Inject mock dependencies into connection
    conn->config_ = mock_config.get();
    conn->stat_ = mock_stat.get();
    conn->rtsp_sources_ = mock_rtsp_sources.get();

    // Initialize request with stream information
    conn->request_->vhost_ = "test.vhost";
    conn->request_->app_ = "live";
    conn->request_->stream_ = "stream1";

    // Create track descriptions for testing
    SrsRtcTrackDescription *audio_desc = new SrsRtcTrackDescription();
    audio_desc->type_ = "audio";
    audio_desc->id_ = "0";
    audio_desc->ssrc_ = 1001;
    conn->tracks_[1001] = audio_desc;

    SrsRtcTrackDescription *video_desc = new SrsRtcTrackDescription();
    video_desc->type_ = "video";
    video_desc->id_ = "1";
    video_desc->ssrc_ = 2001;
    conn->tracks_[2001] = video_desc;

    // Create RTSP PLAY request
    SrsUniquePtr<SrsRtspRequest> play_req(new SrsRtspRequest());
    play_req->method_ = "PLAY";
    play_req->uri_ = "rtsp://127.0.0.1:8554/live/stream1";
    play_req->seq_ = 4;

    // Test do_play: This will create a real SrsRtspPlayStream
    // We verify that the player is created and the flow completes successfully
    HELPER_EXPECT_SUCCESS(conn->do_play(play_req.get(), conn.get()));

    // Verify player was created
    EXPECT_TRUE(conn->player_ != NULL);

    // Test do_teardown: This should stop and free the player
    HELPER_EXPECT_SUCCESS(conn->do_teardown());

    // Verify player was freed
    EXPECT_TRUE(conn->player_ == NULL);

    // Clean up injected mocks to avoid double-free
    conn->config_ = NULL;
    conn->stat_ = NULL;
    conn->rtsp_sources_ = NULL;
}

// Test SrsRtspConnection::do_setup() - major use scenario
// This test covers the successful SETUP request with TCP transport which is the most common scenario:
// 1. Client sends SETUP request with TCP/interleaved transport
// 2. Server looks up track by stream_id to get SSRC
// 3. Server creates SrsRtspTcpNetwork for the track
// 4. Server returns the SSRC to caller
VOID TEST(RtspConnectionTest, DoSetupWithTcpTransport)
{
    srs_error_t err = srs_success;

    // Create RTSP connection
    SrsUniquePtr<SrsRtspConnection> conn(new SrsRtspConnection(NULL, NULL, "127.0.0.1", 8554));

    // Create a track description with known stream_id and ssrc
    SrsRtcTrackDescription *video_desc = new SrsRtcTrackDescription();
    video_desc->type_ = "video";
    video_desc->id_ = "0"; // stream_id will be 0
    video_desc->ssrc_ = 12345;

    // Add track to connection's tracks map
    conn->tracks_[12345] = video_desc;

    // Create RTSP SETUP request with TCP transport
    SrsUniquePtr<SrsRtspRequest> req(new SrsRtspRequest());
    req->method_ = "SETUP";
    req->stream_id_ = 0; // Matches track id_ = "0"

    // Configure TCP transport (interleaved mode)
    req->transport_ = new SrsRtspTransport();
    req->transport_->transport_ = "RTP";
    req->transport_->profile_ = "AVP";
    req->transport_->lower_transport_ = "TCP";
    req->transport_->interleaved_min_ = 0;
    req->transport_->interleaved_max_ = 1;

    // Call do_setup
    uint32_t ssrc = 0;
    HELPER_EXPECT_SUCCESS(conn->do_setup(req.get(), &ssrc));

    // Verify SSRC was returned correctly
    EXPECT_EQ(12345, (int)ssrc);

    // Verify network was created for this SSRC
    EXPECT_EQ(1, (int)conn->networks_.size());
    EXPECT_TRUE(conn->networks_.find(12345) != conn->networks_.end());

    // Clean up: Free the network object
    ISrsStreamWriter *network = conn->networks_[12345];
    srs_freep(network);
    conn->networks_.clear();
}

// Test SrsRtspConnection::http_hooks_on_play() to verify HTTP hooks are called correctly
// when playing RTSP streams. This covers the major use scenario where HTTP hooks are enabled
// and multiple hook URLs are configured for on_play events.
VOID TEST(SrsRtspConnectionTest, HttpHooksOnPlaySuccess)
{
    srs_error_t err = srs_success;

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> mock_request(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create RTSP connection (we don't need real transport for this test)
    SrsUniquePtr<SrsRtspConnection> conn(new SrsRtspConnection(NULL, NULL, "127.0.0.1", 554));

    // Create mock config with HTTP hooks enabled
    MockAppConfigForHttpHooksOnPlay *mock_config = new MockAppConfigForHttpHooksOnPlay();
    mock_config->http_hooks_enabled_ = true;

    // Create on_play directive with two hook URLs
    mock_config->on_play_directive_ = new SrsConfDirective();
    mock_config->on_play_directive_->name_ = "on_play";
    mock_config->on_play_directive_->args_.push_back("http://127.0.0.1:8085/api/v1/rtsp/play");
    mock_config->on_play_directive_->args_.push_back("http://localhost:8085/api/v1/rtsp/play");

    // Create mock hooks
    MockHttpHooksForOnPlay *mock_hooks = new MockHttpHooksForOnPlay();

    // Inject mocks into connection
    conn->config_ = mock_config;
    conn->hooks_ = mock_hooks;

    // Test the major use scenario: http_hooks_on_play() with hooks enabled
    // This should:
    // 1. Check if HTTP hooks are enabled (they are)
    // 2. Get the on_play directive from config
    // 3. Copy the hook URLs from the directive
    // 4. Call hooks_->on_play() for each URL
    HELPER_EXPECT_SUCCESS(conn->http_hooks_on_play(mock_request.get()));

    // Verify that on_play was called twice (once for each URL)
    EXPECT_EQ(2, mock_hooks->on_play_count_);
    EXPECT_EQ(2, (int)mock_hooks->on_play_calls_.size());

    // Verify the first call
    EXPECT_STREQ("http://127.0.0.1:8085/api/v1/rtsp/play", mock_hooks->on_play_calls_[0].first.c_str());
    EXPECT_TRUE(mock_hooks->on_play_calls_[0].second == mock_request.get());

    // Verify the second call
    EXPECT_STREQ("http://localhost:8085/api/v1/rtsp/play", mock_hooks->on_play_calls_[1].first.c_str());
    EXPECT_TRUE(mock_hooks->on_play_calls_[1].second == mock_request.get());

    // Cleanup: restore to NULL to avoid accessing freed memory
    conn->config_ = NULL;
    conn->hooks_ = NULL;
    srs_freep(mock_config);
    srs_freep(mock_hooks);
}

// Test SrsRtspConnection::get_ssrc_by_stream_id() - major use scenario
// This test covers the typical scenario where RTSP SETUP request needs to map
// stream_id to SSRC to identify which track the client wants to setup.
// The function searches through tracks_ map to find a track whose id_ matches
// the stream_id (converted to string), then returns the corresponding SSRC.
VOID TEST(RtspConnectionTest, GetSsrcByStreamIdSuccess)
{
    srs_error_t err = srs_success;

    // Create RTSP connection
    SrsUniquePtr<SrsRtspConnection> conn(new SrsRtspConnection(NULL, NULL, "127.0.0.1", 8554));

    // Create multiple track descriptions with different stream IDs
    SrsRtcTrackDescription *audio_desc = new SrsRtcTrackDescription();
    audio_desc->type_ = "audio";
    audio_desc->id_ = "0"; // stream_id 0
    audio_desc->ssrc_ = 1001;

    SrsRtcTrackDescription *video_desc = new SrsRtcTrackDescription();
    video_desc->type_ = "video";
    video_desc->id_ = "1"; // stream_id 1
    video_desc->ssrc_ = 2001;

    // Add tracks to connection's tracks map (key is SSRC)
    conn->tracks_[1001] = audio_desc;
    conn->tracks_[2001] = video_desc;

    // Test successful lookup for audio track (stream_id 0)
    uint32_t ssrc = 0;
    HELPER_EXPECT_SUCCESS(conn->get_ssrc_by_stream_id(0, &ssrc));
    EXPECT_EQ(1001, (int)ssrc);

    // Test successful lookup for video track (stream_id 1)
    ssrc = 0;
    HELPER_EXPECT_SUCCESS(conn->get_ssrc_by_stream_id(1, &ssrc));
    EXPECT_EQ(2001, (int)ssrc);

    // Test failure case: stream_id not found
    HELPER_EXPECT_FAILED(conn->get_ssrc_by_stream_id(999, &ssrc));
}

VOID TEST(RtspTcpNetworkTest, WriteRtpPacket)
{
    srs_error_t err;

    // Create mock socket with buffer
    SrsUniquePtr<MockBufferIO> mock_skt(new MockBufferIO());

    // Create SrsRtspTcpNetwork with channel 0
    int channel = 0;
    SrsUniquePtr<SrsRtspTcpNetwork> tcp_network(new SrsRtspTcpNetwork(mock_skt.get(), channel));

    // Prepare test RTP packet data
    const int kRtpPacketSize = 100;
    char rtp_packet[kRtpPacketSize];
    memset(rtp_packet, 0xAB, kRtpPacketSize);

    // Write RTP packet
    ssize_t nwrite = 0;
    HELPER_EXPECT_SUCCESS(tcp_network->write(rtp_packet, kRtpPacketSize, &nwrite));

    // Verify total bytes written (4 byte header + 100 byte payload)
    EXPECT_EQ(104, (int)nwrite);

    // Verify the output buffer contains correct data
    EXPECT_EQ(104, mock_skt->out_length());

    // Get the output buffer data
    char *output = mock_skt->out_buffer.bytes();
    ASSERT_TRUE(output != NULL);

    // Verify magic byte '$' (0x24)
    EXPECT_EQ(0x24, (uint8_t)output[0]);

    // Verify channel number
    EXPECT_EQ(0, (uint8_t)output[1]);

    // Verify packet size in network order (big-endian)
    uint16_t size = ((uint8_t)output[2] << 8) | (uint8_t)output[3];
    EXPECT_EQ(100, (int)size);

    // Verify the payload data (starts at offset 4)
    EXPECT_EQ(0, memcmp(rtp_packet, output + 4, kRtpPacketSize));
}

// MockDvrPlan implementation
MockDvrPlan::MockDvrPlan()
{
    on_publish_called_ = false;
    on_unpublish_called_ = false;
    on_publish_error_ = srs_success;
}

MockDvrPlan::~MockDvrPlan()
{
}

srs_error_t MockDvrPlan::initialize(ISrsOriginHub *h, ISrsDvrSegmenter *s, ISrsRequest *r)
{
    return srs_success;
}

srs_error_t MockDvrPlan::on_publish(ISrsRequest *r)
{
    on_publish_called_ = true;
    return on_publish_error_;
}

void MockDvrPlan::on_unpublish()
{
    on_unpublish_called_ = true;
}

srs_error_t MockDvrPlan::on_meta_data(SrsMediaPacket *shared_metadata)
{
    return srs_success;
}

srs_error_t MockDvrPlan::on_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
{
    return srs_success;
}

srs_error_t MockDvrPlan::on_video(SrsMediaPacket *shared_video, SrsFormat *format)
{
    return srs_success;
}

srs_error_t MockDvrPlan::on_reap_segment()
{
    return srs_success;
}

// MockFlvTransmuxer implementation
MockFlvTransmuxer::MockFlvTransmuxer()
{
    write_header_called_ = false;
    write_metadata_called_ = false;
    metadata_type_ = 0;
    metadata_size_ = 0;
    write_metadata_error_ = srs_success;
}

MockFlvTransmuxer::~MockFlvTransmuxer()
{
}

srs_error_t MockFlvTransmuxer::initialize(ISrsWriter *fw)
{
    return srs_success;
}

void MockFlvTransmuxer::set_drop_if_not_match(bool v)
{
}

bool MockFlvTransmuxer::drop_if_not_match()
{
    return false;
}

srs_error_t MockFlvTransmuxer::write_header(bool has_video, bool has_audio)
{
    write_header_called_ = true;
    return srs_success;
}

srs_error_t MockFlvTransmuxer::write_header(char flv_header[9])
{
    write_header_called_ = true;
    return srs_success;
}

srs_error_t MockFlvTransmuxer::write_metadata(char type, char *data, int size)
{
    write_metadata_called_ = true;
    metadata_type_ = type;
    metadata_size_ = size;
    return write_metadata_error_;
}

srs_error_t MockFlvTransmuxer::write_audio(int64_t timestamp, char *data, int size)
{
    return srs_success;
}

srs_error_t MockFlvTransmuxer::write_video(int64_t timestamp, char *data, int size)
{
    return srs_success;
}

srs_error_t MockFlvTransmuxer::write_tags(SrsMediaPacket **msgs, int count)
{
    return srs_success;
}

// MockMp4Encoder implementation
MockMp4Encoder::MockMp4Encoder()
{
    initialize_called_ = false;
    write_sample_called_ = false;
    flush_called_ = false;
    set_audio_codec_called_ = false;
    last_handler_type_ = SrsMp4HandlerTypeForbidden;
    last_frame_type_ = 0;
    last_codec_type_ = 0;
    last_dts_ = 0;
    last_pts_ = 0;
    last_sample_size_ = 0;
    last_audio_codec_ = SrsAudioCodecIdForbidden;
    last_audio_sample_rate_ = SrsAudioSampleRateForbidden;
    last_audio_sound_bits_ = SrsAudioSampleBitsForbidden;
    last_audio_channels_ = SrsAudioChannelsForbidden;
}

MockMp4Encoder::~MockMp4Encoder()
{
}

srs_error_t MockMp4Encoder::initialize(ISrsWriteSeeker *ws)
{
    initialize_called_ = true;
    return srs_success;
}

srs_error_t MockMp4Encoder::write_sample(SrsFormat *format, SrsMp4HandlerType ht, uint16_t ft, uint16_t ct,
                                         uint32_t dts, uint32_t pts, uint8_t *sample, uint32_t nb_sample)
{
    write_sample_called_ = true;
    last_handler_type_ = ht;
    last_frame_type_ = ft;
    last_codec_type_ = ct;
    last_dts_ = dts;
    last_pts_ = pts;
    last_sample_size_ = nb_sample;
    return srs_success;
}

srs_error_t MockMp4Encoder::flush()
{
    flush_called_ = true;
    return srs_success;
}

void MockMp4Encoder::set_audio_codec(SrsAudioCodecId vcodec, SrsAudioSampleRate sample_rate, SrsAudioSampleBits sound_bits, SrsAudioChannels channels)
{
    set_audio_codec_called_ = true;
    last_audio_codec_ = vcodec;
    last_audio_sample_rate_ = sample_rate;
    last_audio_sound_bits_ = sound_bits;
    last_audio_channels_ = channels;
}

void MockMp4Encoder::reset()
{
    initialize_called_ = false;
    write_sample_called_ = false;
    flush_called_ = false;
    set_audio_codec_called_ = false;
    last_handler_type_ = SrsMp4HandlerTypeForbidden;
    last_frame_type_ = 0;
    last_codec_type_ = 0;
    last_dts_ = 0;
    last_pts_ = 0;
    last_sample_size_ = 0;
    last_audio_codec_ = SrsAudioCodecIdForbidden;
    last_audio_sample_rate_ = SrsAudioSampleRateForbidden;
    last_audio_sound_bits_ = SrsAudioSampleBitsForbidden;
    last_audio_channels_ = SrsAudioChannelsForbidden;
}

// MockDvrAppFactory implementation
MockDvrAppFactory::MockDvrAppFactory()
{
    mock_mp4_encoder_ = NULL;
}

MockDvrAppFactory::~MockDvrAppFactory()
{
    // Note: mock_mp4_encoder_ is NOT owned by this factory - it's freed by the caller
    // We just keep a reference to it for testing purposes
}

ISrsMp4Encoder *MockDvrAppFactory::create_mp4_encoder()
{
    // Create a new mock encoder and save reference for testing
    MockMp4Encoder *encoder = new MockMp4Encoder();
    mock_mp4_encoder_ = encoder;
    return encoder;
}

ISrsDvrSegmenter *MockDvrAppFactory::create_dvr_flv_segmenter()
{
    SrsDvrFlvSegmenter *segmenter = new SrsDvrFlvSegmenter();
    segmenter->assemble();
    return segmenter;
}

ISrsDvrSegmenter *MockDvrAppFactory::create_dvr_mp4_segmenter()
{
    SrsDvrMp4Segmenter *segmenter = new SrsDvrMp4Segmenter();
    segmenter->assemble();
    return segmenter;
}

VOID TEST(DvrSegmenterTest, OpenTypicalScenario)
{
    srs_error_t err;

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create mock plan
    SrsUniquePtr<MockDvrPlan> plan(new MockDvrPlan());

    // Create SrsDvrFlvSegmenter instance
    SrsUniquePtr<SrsDvrFlvSegmenter> segmenter(new SrsDvrFlvSegmenter());
    segmenter->assemble();

    // Initialize the segmenter
    HELPER_EXPECT_SUCCESS(segmenter->initialize(plan.get(), req.get()));

    // Replace the file writer with mock to avoid real file operations
    srs_freep(segmenter->fs_);
    SrsUniquePtr<MockSrsFileWriter> mock_fs(new MockSrsFileWriter());
    segmenter->fs_ = mock_fs.get();

    // Test open() - should succeed
    HELPER_EXPECT_SUCCESS(segmenter->open());

    // Verify file writer was opened
    EXPECT_TRUE(mock_fs->is_open());

    // Verify jitter was created
    EXPECT_TRUE(segmenter->jitter_ != NULL);

    // Test open() again - should return success immediately (already open)
    HELPER_EXPECT_SUCCESS(segmenter->open());

    // Clean up - set to NULL to avoid double-free
    segmenter->fs_ = NULL;
}

VOID TEST(DvrSegmenterTest, WriteAudioTypicalScenario)
{
    srs_error_t err;

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create mock plan
    SrsUniquePtr<MockDvrPlan> plan(new MockDvrPlan());

    // Create SrsDvrFlvSegmenter instance
    SrsUniquePtr<SrsDvrFlvSegmenter> segmenter(new SrsDvrFlvSegmenter());
    segmenter->assemble();

    // Initialize the segmenter
    HELPER_EXPECT_SUCCESS(segmenter->initialize(plan.get(), req.get()));

    // Replace the file writer with mock to avoid real file operations
    srs_freep(segmenter->fs_);
    SrsUniquePtr<MockSrsFileWriter> mock_fs(new MockSrsFileWriter());
    segmenter->fs_ = mock_fs.get();

    // Open the segmenter to initialize jitter and encoder
    HELPER_EXPECT_SUCCESS(segmenter->open());

    // Create audio format with AAC codec using MockSrsFormat
    SrsUniquePtr<MockSrsFormat> format(new MockSrsFormat());

    // Create first audio packet with timestamp 0
    SrsUniquePtr<MockSrsMediaPacket> audio_packet1(new MockSrsMediaPacket(false, 0));

    // Test write_audio() - should succeed
    HELPER_EXPECT_SUCCESS(segmenter->write_audio(audio_packet1.get(), format.get()));

    // Verify file writer has data written
    EXPECT_TRUE(mock_fs->filesize() > 0);

    // Create second audio packet with timestamp 1000ms to establish duration
    SrsUniquePtr<MockSrsMediaPacket> audio_packet2(new MockSrsMediaPacket(false, 1000));

    // Write second audio packet
    HELPER_EXPECT_SUCCESS(segmenter->write_audio(audio_packet2.get(), format.get()));

    // Verify fragment duration was updated (should be > 0 after writing packets with different timestamps)
    // Note: Exact duration may vary due to jitter correction
    EXPECT_TRUE(segmenter->fragment_->duration() > 0);

    // Clean up - set to NULL to avoid double-free
    segmenter->fs_ = NULL;
}

VOID TEST(DvrSegmenterTest, WriteVideoTypicalScenario)
{
    srs_error_t err;

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create mock plan
    SrsUniquePtr<MockDvrPlan> plan(new MockDvrPlan());

    // Create SrsDvrFlvSegmenter instance
    SrsUniquePtr<SrsDvrFlvSegmenter> segmenter(new SrsDvrFlvSegmenter());
    segmenter->assemble();

    // Initialize the segmenter
    HELPER_EXPECT_SUCCESS(segmenter->initialize(plan.get(), req.get()));

    // Replace the file writer with mock to avoid real file operations
    srs_freep(segmenter->fs_);
    SrsUniquePtr<MockSrsFileWriter> mock_fs(new MockSrsFileWriter());
    segmenter->fs_ = mock_fs.get();

    // Open the segmenter to initialize jitter and encoder
    HELPER_EXPECT_SUCCESS(segmenter->open());

    // Create video format with H.264 codec using MockSrsFormat
    SrsUniquePtr<MockSrsFormat> format(new MockSrsFormat());

    // Create first video packet with timestamp 0
    SrsUniquePtr<MockSrsMediaPacket> video_packet1(new MockSrsMediaPacket(true, 0));

    // Test write_video() - should succeed
    HELPER_EXPECT_SUCCESS(segmenter->write_video(video_packet1.get(), format.get()));

    // Verify file writer has data written
    EXPECT_TRUE(mock_fs->filesize() > 0);

    // Create second video packet with timestamp 1000ms to establish duration
    SrsUniquePtr<MockSrsMediaPacket> video_packet2(new MockSrsMediaPacket(true, 1000));

    // Write second video packet
    HELPER_EXPECT_SUCCESS(segmenter->write_video(video_packet2.get(), format.get()));

    // Verify fragment duration was updated (should be > 0 after writing packets with different timestamps)
    // Note: Exact duration may vary due to jitter correction
    EXPECT_TRUE(segmenter->fragment_->duration() > 0);

    // Clean up - set to NULL to avoid double-free
    segmenter->fs_ = NULL;
}

VOID TEST(DvrSegmenterTest, CloseTypicalScenario)
{
    srs_error_t err;

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create mock plan
    SrsUniquePtr<MockDvrPlan> plan(new MockDvrPlan());

    // Create SrsDvrFlvSegmenter instance using real file writer for complete flow
    SrsUniquePtr<SrsDvrFlvSegmenter> segmenter(new SrsDvrFlvSegmenter());
    segmenter->assemble();

    // Initialize the segmenter
    HELPER_EXPECT_SUCCESS(segmenter->initialize(plan.get(), req.get()));

    // Open the segmenter - this creates the file
    HELPER_EXPECT_SUCCESS(segmenter->open());

    // Verify file is open
    EXPECT_TRUE(segmenter->fs_->is_open());

    // Test close() - should succeed (closes encoder, closes file, renames, calls plan callback)
    HELPER_EXPECT_SUCCESS(segmenter->close());

    // Verify file writer was closed
    EXPECT_FALSE(segmenter->fs_->is_open());

    // Test close() again - should return success immediately (already closed)
    HELPER_EXPECT_SUCCESS(segmenter->close());

    // Clean up the created file
    segmenter->fragment_->unlink_file();
}

VOID TEST(DvrSegmenterTest, GeneratePathTypicalScenario)
{
    srs_error_t err;

    // Create mock request with specific vhost, app, and stream values
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create mock plan
    SrsUniquePtr<MockDvrPlan> plan(new MockDvrPlan());

    // Create SrsDvrFlvSegmenter instance
    SrsUniquePtr<SrsDvrFlvSegmenter> segmenter(new SrsDvrFlvSegmenter());
    segmenter->assemble();

    // Initialize the segmenter
    HELPER_EXPECT_SUCCESS(segmenter->initialize(plan.get(), req.get()));

    // Test generate_path() - should replace placeholders with actual values
    std::string path = segmenter->generate_path();

    // Verify all placeholders are replaced (no brackets remain)
    EXPECT_TRUE(path.find("[vhost]") == std::string::npos);
    EXPECT_TRUE(path.find("[app]") == std::string::npos);
    EXPECT_TRUE(path.find("[stream]") == std::string::npos);
    EXPECT_TRUE(path.find("[timestamp]") == std::string::npos);
    EXPECT_TRUE(path.find("[2006]") == std::string::npos);
    EXPECT_TRUE(path.find("[01]") == std::string::npos);
    EXPECT_TRUE(path.find("[02]") == std::string::npos);
    EXPECT_TRUE(path.find("[15]") == std::string::npos);
    EXPECT_TRUE(path.find("[04]") == std::string::npos);
    EXPECT_TRUE(path.find("[05]") == std::string::npos);
    EXPECT_TRUE(path.find("[999]") == std::string::npos);

    // Verify path contains actual values from request
    EXPECT_TRUE(path.find("live") != std::string::npos);
    EXPECT_TRUE(path.find("stream1") != std::string::npos);

    // Verify path ends with .flv extension
    EXPECT_TRUE(srs_strings_ends_with(path, ".flv"));

    // Verify path is not empty and has reasonable structure
    EXPECT_TRUE(path.length() > 0);
    EXPECT_TRUE(path.find("/") != std::string::npos);
}

VOID TEST(DvrSegmenterTest, OnUpdateDurationTypicalScenario)
{
    srs_error_t err;

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create mock plan
    SrsUniquePtr<MockDvrPlan> plan(new MockDvrPlan());

    // Create SrsDvrFlvSegmenter instance
    SrsUniquePtr<SrsDvrFlvSegmenter> segmenter(new SrsDvrFlvSegmenter());
    segmenter->assemble();

    // Initialize the segmenter
    HELPER_EXPECT_SUCCESS(segmenter->initialize(plan.get(), req.get()));

    // Create media packets with different timestamps to test duration tracking
    SrsUniquePtr<SrsMediaPacket> msg1(new SrsMediaPacket());
    msg1->timestamp_ = 1000; // 1000ms

    SrsUniquePtr<SrsMediaPacket> msg2(new SrsMediaPacket());
    msg2->timestamp_ = 2000; // 2000ms

    SrsUniquePtr<SrsMediaPacket> msg3(new SrsMediaPacket());
    msg3->timestamp_ = 3500; // 3500ms

    // Verify initial fragment duration is 0
    EXPECT_EQ(0, srsu2msi(segmenter->fragment_->duration()));

    // Call on_update_duration with first packet
    HELPER_EXPECT_SUCCESS(segmenter->on_update_duration(msg1.get()));

    // After first packet, duration should still be 0 (start_dts is set)
    EXPECT_EQ(0, srsu2msi(segmenter->fragment_->duration()));

    // Call on_update_duration with second packet
    HELPER_EXPECT_SUCCESS(segmenter->on_update_duration(msg2.get()));

    // Duration should be 1000ms (2000 - 1000)
    EXPECT_EQ(1000, srsu2msi(segmenter->fragment_->duration()));

    // Call on_update_duration with third packet
    HELPER_EXPECT_SUCCESS(segmenter->on_update_duration(msg3.get()));

    // Duration should be 2500ms (3500 - 1000)
    EXPECT_EQ(2500, srsu2msi(segmenter->fragment_->duration()));
}

VOID TEST(DvrFlvSegmenterTest, RefreshMetadataTypicalScenario)
{
    srs_error_t err;

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create mock plan
    SrsUniquePtr<MockDvrPlan> plan(new MockDvrPlan());

    // Create SrsDvrFlvSegmenter instance
    SrsUniquePtr<SrsDvrFlvSegmenter> segmenter(new SrsDvrFlvSegmenter());
    segmenter->assemble();

    // Initialize the segmenter
    HELPER_EXPECT_SUCCESS(segmenter->initialize(plan.get(), req.get()));

    // Replace the real file writer with a mock file writer
    srs_freep(segmenter->fs_);
    MockSrsFileWriter *mock_fs = new MockSrsFileWriter();
    segmenter->fs_ = mock_fs;

    // Open the mock file
    HELPER_EXPECT_SUCCESS(mock_fs->open("test.flv"));

    // Simulate writing some initial data to the file (e.g., FLV header and metadata)
    // Write 100 bytes of dummy data to simulate file content
    char dummy_data[100];
    memset(dummy_data, 0, sizeof(dummy_data));
    HELPER_EXPECT_SUCCESS(mock_fs->write(dummy_data, sizeof(dummy_data), NULL));

    // Set the duration and filesize offsets (simulate where metadata fields are in the file)
    segmenter->duration_offset_ = 20; // Duration field at offset 20
    segmenter->filesize_offset_ = 40; // Filesize field at offset 40

    // Set up the fragment with a duration (5.5 seconds = 5500ms = 5500000us)
    SrsUniquePtr<SrsMediaPacket> msg1(new SrsMediaPacket());
    msg1->timestamp_ = 1000;
    HELPER_EXPECT_SUCCESS(segmenter->on_update_duration(msg1.get()));

    SrsUniquePtr<SrsMediaPacket> msg2(new SrsMediaPacket());
    msg2->timestamp_ = 6500; // 5500ms duration
    HELPER_EXPECT_SUCCESS(segmenter->on_update_duration(msg2.get()));

    // Verify fragment duration is 5500ms
    EXPECT_EQ(5500, srsu2msi(segmenter->fragment_->duration()));

    // Get current file position before refresh
    int64_t pos_before = mock_fs->tellg();
    EXPECT_EQ(100, pos_before); // Should be at end of dummy data

    // Call refresh_metadata() - this is the method under test
    HELPER_EXPECT_SUCCESS(segmenter->refresh_metadata());

    // Verify file position is restored to original position
    int64_t pos_after = mock_fs->tellg();
    EXPECT_EQ(pos_before, pos_after);

    // Verify the filesize was written correctly at filesize_offset_
    mock_fs->seek2(segmenter->filesize_offset_);
    int amf0_number_size = SrsAmf0Size::number();
    char filesize_buf[9]; // AMF0 number is always 9 bytes (1 byte marker + 8 bytes double)
    ssize_t nread = 0;
    HELPER_EXPECT_SUCCESS(mock_fs->uf->read(filesize_buf, amf0_number_size, &nread));
    EXPECT_EQ(amf0_number_size, nread);

    // Parse the filesize value
    SrsBuffer filesize_stream(filesize_buf, amf0_number_size);
    SrsUniquePtr<SrsAmf0Any> filesize_value(SrsAmf0Any::number());
    HELPER_EXPECT_SUCCESS(filesize_value->read(&filesize_stream));
    EXPECT_TRUE(filesize_value->is_number());
    EXPECT_EQ(100.0, filesize_value->to_number()); // Should match file size

    // Verify the duration was written correctly at duration_offset_
    mock_fs->seek2(segmenter->duration_offset_);
    char duration_buf[9]; // AMF0 number is always 9 bytes (1 byte marker + 8 bytes double)
    nread = 0;
    HELPER_EXPECT_SUCCESS(mock_fs->uf->read(duration_buf, amf0_number_size, &nread));
    EXPECT_EQ(amf0_number_size, nread);

    // Parse the duration value
    SrsBuffer duration_stream(duration_buf, amf0_number_size);
    SrsUniquePtr<SrsAmf0Any> duration_value(SrsAmf0Any::number());
    HELPER_EXPECT_SUCCESS(duration_value->read(&duration_stream));
    EXPECT_TRUE(duration_value->is_number());
    EXPECT_EQ(5.5, duration_value->to_number()); // Should be 5.5 seconds

    // Clean up - set to NULL to avoid double-free
    segmenter->fs_ = NULL;
    srs_freep(mock_fs);
}

// Test SrsDvrFlvSegmenter::encode_metadata() - major use scenario
// This test covers the typical scenario where DVR writes metadata to FLV file.
// The encode_metadata method:
// 1. Reads AMF0 metadata (name string + object) from the input packet
// 2. Removes existing "duration" and "filesize" properties
// 3. Adds new properties: "service", "filesize" (0), "duration" (0)
// 4. Calculates offsets for duration and filesize fields in the FLV file
// 5. Writes the modified metadata to the FLV encoder
VOID TEST(DvrFlvSegmenterTest, EncodeMetadataTypicalScenario)
{
    srs_error_t err;

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create mock plan
    SrsUniquePtr<MockDvrPlan> plan(new MockDvrPlan());

    // Create SrsDvrFlvSegmenter instance
    SrsUniquePtr<SrsDvrFlvSegmenter> segmenter(new SrsDvrFlvSegmenter());
    segmenter->assemble();

    // Initialize the segmenter
    HELPER_EXPECT_SUCCESS(segmenter->initialize(plan.get(), req.get()));

    // Replace the real file writer with a mock file writer
    srs_freep(segmenter->fs_);
    MockSrsFileWriter *mock_fs = new MockSrsFileWriter();
    segmenter->fs_ = mock_fs;

    // Open the mock file
    HELPER_EXPECT_SUCCESS(mock_fs->open("test.flv"));

    // Replace the FLV encoder with a mock encoder
    srs_freep(segmenter->enc_);
    MockFlvTransmuxer *mock_enc = new MockFlvTransmuxer();
    segmenter->enc_ = mock_enc;

    // Create metadata packet with typical properties
    // AMF0 format: string "onMetaData" + object {width: 1920, height: 1080, duration: 120, filesize: 1000000}
    SrsUniquePtr<SrsAmf0Any> name(SrsAmf0Any::str(SRS_CONSTS_RTMP_ON_METADATA));
    SrsUniquePtr<SrsAmf0Object> metadata_obj(SrsAmf0Any::object());
    metadata_obj->set("width", SrsAmf0Any::number(1920));
    metadata_obj->set("height", SrsAmf0Any::number(1080));
    metadata_obj->set("duration", SrsAmf0Any::number(120));     // Should be removed and replaced
    metadata_obj->set("filesize", SrsAmf0Any::number(1000000)); // Should be removed and replaced

    // Serialize metadata to bytes
    int metadata_size = name->total_size() + metadata_obj->total_size();
    char *metadata_data = new char[metadata_size];
    SrsBuffer metadata_buf(metadata_data, metadata_size);
    HELPER_EXPECT_SUCCESS(name->write(&metadata_buf));
    HELPER_EXPECT_SUCCESS(metadata_obj->write(&metadata_buf));

    // Create SrsMediaPacket with metadata
    SrsUniquePtr<SrsMediaPacket> metadata_packet(new SrsMediaPacket());
    metadata_packet->wrap(metadata_data, metadata_size);
    metadata_packet->message_type_ = SrsFrameTypeScript;

    // Verify initial state - offsets should be 0
    EXPECT_EQ(0, segmenter->duration_offset_);
    EXPECT_EQ(0, segmenter->filesize_offset_);

    // Call encode_metadata() - this is the method under test
    HELPER_EXPECT_SUCCESS(segmenter->encode_metadata(metadata_packet.get()));

    // Verify the mock encoder's write_metadata was called
    EXPECT_TRUE(mock_enc->write_metadata_called_);
    EXPECT_EQ(18, (int)mock_enc->metadata_type_); // Type should be 18 (script data)
    EXPECT_TRUE(mock_enc->metadata_size_ > 0);    // Should have written some data

    // Verify duration_offset_ and filesize_offset_ were calculated
    EXPECT_TRUE(segmenter->duration_offset_ > 0);
    EXPECT_TRUE(segmenter->filesize_offset_ > 0);
    EXPECT_TRUE(segmenter->filesize_offset_ < segmenter->duration_offset_); // filesize comes before duration

    // Verify calling encode_metadata again is ignored (metadata already written)
    int64_t saved_duration_offset = segmenter->duration_offset_;
    int64_t saved_filesize_offset = segmenter->filesize_offset_;
    mock_enc->write_metadata_called_ = false;

    HELPER_EXPECT_SUCCESS(segmenter->encode_metadata(metadata_packet.get()));

    // Should not call write_metadata again
    EXPECT_FALSE(mock_enc->write_metadata_called_);
    // Offsets should remain unchanged
    EXPECT_EQ(saved_duration_offset, segmenter->duration_offset_);
    EXPECT_EQ(saved_filesize_offset, segmenter->filesize_offset_);

    // Clean up - set to NULL to avoid double-free
    segmenter->fs_ = NULL;
    segmenter->enc_ = NULL;
    srs_freep(mock_fs);
    srs_freep(mock_enc);
}

// Test SrsDvrMp4Segmenter::encode_audio() and encode_video() - major use scenario
// This test covers the typical scenario where DVR writes audio and video samples to MP4 file.
// The encode_audio method:
// 1. Extracts audio codec information from format (codec id, sample rate, sample bits, channels)
// 2. Sets audio codec on encoder when receiving sequence header
// 3. Writes audio sample to MP4 encoder with timestamp and raw data
// The encode_video method:
// 1. Extracts video codec information from format (frame type, codec id, CTS)
// 2. Sets video codec on encoder when receiving sequence header
// 3. Calculates PTS from DTS + CTS
// 4. Writes video sample to MP4 encoder with timestamps and raw data
VOID TEST(DvrMp4SegmenterTest, EncodeAudioVideoTypicalScenario)
{
    srs_error_t err;

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create mock plan
    SrsUniquePtr<MockDvrPlan> plan(new MockDvrPlan());

    // Create mock app factory
    SrsUniquePtr<MockDvrAppFactory> mock_factory(new MockDvrAppFactory());

    // Create SrsDvrMp4Segmenter instance
    SrsUniquePtr<SrsDvrMp4Segmenter> segmenter(new SrsDvrMp4Segmenter());

    // Inject mock factory
    segmenter->app_factory_ = mock_factory.get();

    // Initialize the segmenter
    HELPER_EXPECT_SUCCESS(segmenter->initialize(plan.get(), req.get()));

    // Create mock file writer
    MockSrsFileWriter *mock_fs = new MockSrsFileWriter();
    segmenter->fs_ = mock_fs;

    // Open the encoder (this will create the mock MP4 encoder via factory)
    HELPER_EXPECT_SUCCESS(segmenter->open_encoder());

    // Get reference to the mock encoder created by factory
    MockMp4Encoder *mock_enc = mock_factory->mock_mp4_encoder_;
    EXPECT_TRUE(mock_enc != NULL);
    EXPECT_TRUE(mock_enc->initialize_called_);

    // Test encode_audio with AAC sequence header
    // Create audio format with AAC codec
    SrsUniquePtr<SrsFormat> audio_format(new SrsFormat());
    audio_format->acodec_ = new SrsAudioCodecConfig();
    audio_format->acodec_->id_ = SrsAudioCodecIdAAC;
    audio_format->acodec_->sound_rate_ = SrsAudioSampleRate44100;
    audio_format->acodec_->sound_size_ = SrsAudioSampleBits16bit;
    audio_format->acodec_->sound_type_ = SrsAudioChannelsStereo;
    audio_format->audio_ = new SrsParsedAudioPacket();
    audio_format->audio_->aac_packet_type_ = SrsAudioAacFrameTraitSequenceHeader;

    // Create audio sample data
    char audio_data[10] = {0x12, 0x10}; // AAC sequence header
    audio_format->raw_ = audio_data;
    audio_format->nb_raw_ = 2;

    // Create audio packet
    SrsUniquePtr<SrsMediaPacket> audio_packet(new SrsMediaPacket());
    audio_packet->timestamp_ = 1000;

    // Call encode_audio() - this is the method under test
    HELPER_EXPECT_SUCCESS(segmenter->encode_audio(audio_packet.get(), audio_format.get()));

    // Verify set_audio_codec was called for sequence header
    EXPECT_TRUE(mock_enc->set_audio_codec_called_);
    EXPECT_EQ(SrsAudioCodecIdAAC, mock_enc->last_audio_codec_);
    EXPECT_EQ(SrsAudioSampleRate44100, mock_enc->last_audio_sample_rate_);
    EXPECT_EQ(SrsAudioSampleBits16bit, mock_enc->last_audio_sound_bits_);
    EXPECT_EQ(SrsAudioChannelsStereo, mock_enc->last_audio_channels_);

    // Verify write_sample was called with correct parameters
    EXPECT_TRUE(mock_enc->write_sample_called_);
    EXPECT_EQ(SrsMp4HandlerTypeSOUN, mock_enc->last_handler_type_);
    EXPECT_EQ(0x00, (int)mock_enc->last_frame_type_);
    EXPECT_EQ(SrsAudioAacFrameTraitSequenceHeader, (int)mock_enc->last_codec_type_);
    EXPECT_EQ(1000, (int)mock_enc->last_dts_);
    EXPECT_EQ(1000, (int)mock_enc->last_pts_); // For audio, PTS = DTS
    EXPECT_EQ(2, (int)mock_enc->last_sample_size_);

    // Reset mock encoder for video test
    mock_enc->reset();

    // Test encode_video with H.264 sequence header
    // Create video format with H.264 codec
    SrsUniquePtr<SrsFormat> video_format(new SrsFormat());
    video_format->vcodec_ = new SrsVideoCodecConfig();
    video_format->vcodec_->id_ = SrsVideoCodecIdAVC;
    video_format->video_ = new SrsParsedVideoPacket();
    video_format->video_->frame_type_ = SrsVideoAvcFrameTypeKeyFrame;
    video_format->video_->avc_packet_type_ = SrsVideoAvcFrameTraitSequenceHeader;
    video_format->video_->cts_ = 0;

    // Create video sample data (SPS/PPS)
    char video_data[20] = {0x01, 0x42, 0x00, 0x1e};
    video_format->raw_ = video_data;
    video_format->nb_raw_ = 4;

    // Create video packet
    SrsUniquePtr<SrsMediaPacket> video_packet(new SrsMediaPacket());
    video_packet->timestamp_ = 2000;

    // Call encode_video() - this is the method under test
    HELPER_EXPECT_SUCCESS(segmenter->encode_video(video_packet.get(), video_format.get()));

    // Verify vcodec_ was set for sequence header
    EXPECT_EQ(SrsVideoCodecIdAVC, mock_enc->vcodec_);

    // Verify write_sample was called with correct parameters
    EXPECT_TRUE(mock_enc->write_sample_called_);
    EXPECT_EQ(SrsMp4HandlerTypeVIDE, mock_enc->last_handler_type_);
    EXPECT_EQ(SrsVideoAvcFrameTypeKeyFrame, (int)mock_enc->last_frame_type_);
    EXPECT_EQ(SrsVideoAvcFrameTraitSequenceHeader, (int)mock_enc->last_codec_type_);
    EXPECT_EQ(2000, (int)mock_enc->last_dts_);
    EXPECT_EQ(2000, (int)mock_enc->last_pts_); // PTS = DTS + CTS (2000 + 0)
    EXPECT_EQ(4, (int)mock_enc->last_sample_size_);

    // Reset mock encoder for regular video frame test
    mock_enc->reset();

    // Test encode_video with regular video frame (with CTS)
    video_format->video_->avc_packet_type_ = SrsVideoAvcFrameTraitNALU;
    video_format->video_->cts_ = 40; // 40ms CTS
    video_packet->timestamp_ = 3000;

    // Call encode_video() again
    HELPER_EXPECT_SUCCESS(segmenter->encode_video(video_packet.get(), video_format.get()));

    // Verify write_sample was called with correct PTS calculation
    EXPECT_TRUE(mock_enc->write_sample_called_);
    EXPECT_EQ(SrsMp4HandlerTypeVIDE, mock_enc->last_handler_type_);
    EXPECT_EQ(SrsVideoAvcFrameTraitNALU, (int)mock_enc->last_codec_type_);
    EXPECT_EQ(3000, (int)mock_enc->last_dts_);
    EXPECT_EQ(3040, (int)mock_enc->last_pts_); // PTS = DTS + CTS (3000 + 40)

    // Clean up - set to NULL to avoid double-free
    segmenter->fs_ = NULL;
    segmenter->enc_ = NULL;
    segmenter->app_factory_ = NULL;
    srs_freep(mock_fs);
    // Note: mock_enc is freed when segmenter is destroyed
}

VOID TEST(DvrMp4SegmenterTest, CloseEncoderFlushSuccess)
{
    srs_error_t err;

    // Create mock factory
    MockDvrAppFactory *mock_factory = new MockDvrAppFactory();

    // Create SrsDvrMp4Segmenter instance
    SrsUniquePtr<SrsDvrMp4Segmenter> segmenter(new SrsDvrMp4Segmenter());

    // Inject mock factory
    segmenter->app_factory_ = mock_factory;

    // Create mock file writer
    MockSrsFileWriter *mock_fs = new MockSrsFileWriter();
    segmenter->fs_ = mock_fs;

    // Open the encoder (this will create the mock MP4 encoder via factory)
    HELPER_EXPECT_SUCCESS(segmenter->open_encoder());

    // Get reference to the mock encoder created by factory
    MockMp4Encoder *mock_enc = mock_factory->mock_mp4_encoder_;
    EXPECT_TRUE(mock_enc != NULL);
    EXPECT_TRUE(mock_enc->initialize_called_);

    // Reset the mock encoder state
    mock_enc->reset();

    // Call close_encoder() - this should call flush() on the encoder
    HELPER_EXPECT_SUCCESS(segmenter->close_encoder());

    // Verify flush was called
    EXPECT_TRUE(mock_enc->flush_called_);

    // Clean up - set to NULL to avoid double-free
    segmenter->fs_ = NULL;
    segmenter->enc_ = NULL;
    segmenter->app_factory_ = NULL;
    srs_freep(mock_fs);
    srs_freep(mock_factory);
}

// Mock ISrsHttpHooks for testing SrsDvrAsyncCallOnDvr
MockHttpHooksForDvrAsyncCall::MockHttpHooksForDvrAsyncCall()
{
    on_dvr_count_ = 0;
    on_dvr_error_ = srs_success;
}

MockHttpHooksForDvrAsyncCall::~MockHttpHooksForDvrAsyncCall()
{
}

srs_error_t MockHttpHooksForDvrAsyncCall::on_connect(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooksForDvrAsyncCall::on_close(std::string url, ISrsRequest *req, int64_t send_bytes, int64_t recv_bytes)
{
}

srs_error_t MockHttpHooksForDvrAsyncCall::on_publish(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooksForDvrAsyncCall::on_unpublish(std::string url, ISrsRequest *req)
{
}

srs_error_t MockHttpHooksForDvrAsyncCall::on_play(std::string url, ISrsRequest *req)
{
    return srs_success;
}

void MockHttpHooksForDvrAsyncCall::on_stop(std::string url, ISrsRequest *req)
{
}

srs_error_t MockHttpHooksForDvrAsyncCall::on_dvr(SrsContextId cid, std::string url, ISrsRequest *req, std::string file)
{
    on_dvr_count_++;
    OnDvrCall call;
    call.cid_ = cid;
    call.url_ = url;
    call.req_ = req;
    call.file_ = file;
    on_dvr_calls_.push_back(call);
    return srs_error_copy(on_dvr_error_);
}

srs_error_t MockHttpHooksForDvrAsyncCall::on_hls(SrsContextId cid, std::string url, ISrsRequest *req, std::string file, std::string ts_url,
                                                 std::string m3u8, std::string m3u8_url, int sn, srs_utime_t duration)
{
    return srs_success;
}

srs_error_t MockHttpHooksForDvrAsyncCall::on_hls_notify(SrsContextId cid, std::string url, ISrsRequest *req, std::string ts_url, int nb_notify)
{
    return srs_success;
}

srs_error_t MockHttpHooksForDvrAsyncCall::discover_co_workers(std::string url, std::string &host, int &port)
{
    return srs_success;
}

srs_error_t MockHttpHooksForDvrAsyncCall::on_forward_backend(std::string url, ISrsRequest *req, std::vector<std::string> &rtmp_urls)
{
    return srs_success;
}

void MockHttpHooksForDvrAsyncCall::reset()
{
    on_dvr_calls_.clear();
    on_dvr_count_ = 0;
    srs_freep(on_dvr_error_);
}

VOID TEST(DvrAsyncCallOnDvrTest, CallWithMultipleHooks)
{
    srs_error_t err;

    // Create mock HTTP hooks
    SrsUniquePtr<MockHttpHooksForDvrAsyncCall> mock_hooks(new MockHttpHooksForDvrAsyncCall());

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Parse a minimal config with DVR hooks enabled
    std::string conf_str =
        "rtmp{listen 1935;} "
        "vhost test.vhost {\n"
        "    http_hooks {\n"
        "        enabled on;\n"
        "        on_dvr http://localhost:8080/dvr/callback1 http://localhost:8080/dvr/callback2;\n"
        "    }\n"
        "}\n";

    // Create temporary config for this test
    SrsUniquePtr<MockSrsConfig> mock_config(new MockSrsConfig());
    HELPER_EXPECT_SUCCESS(mock_config->mock_parse(conf_str));

    // Create SrsDvrAsyncCallOnDvr instance
    SrsContextId cid = _srs_context->get_id();
    std::string dvr_path = "/path/to/dvr/file.flv";
    SrsUniquePtr<SrsDvrAsyncCallOnDvr> async_call(new SrsDvrAsyncCallOnDvr(cid, req.get(), dvr_path));

    // Inject mock dependencies into member fields
    async_call->hooks_ = mock_hooks.get();
    async_call->config_ = mock_config.get();

    // Call the method under test
    HELPER_EXPECT_SUCCESS(async_call->call());

    // Verify that on_dvr was called twice (once for each URL)
    EXPECT_EQ(2, mock_hooks->on_dvr_count_);
    EXPECT_EQ(2, (int)mock_hooks->on_dvr_calls_.size());

    // Verify first callback
    EXPECT_EQ("http://localhost:8080/dvr/callback1", mock_hooks->on_dvr_calls_[0].url_);
    EXPECT_EQ(dvr_path, mock_hooks->on_dvr_calls_[0].file_);
    EXPECT_EQ(req->vhost_, mock_hooks->on_dvr_calls_[0].req_->vhost_);

    // Verify second callback
    EXPECT_EQ("http://localhost:8080/dvr/callback2", mock_hooks->on_dvr_calls_[1].url_);
    EXPECT_EQ(dvr_path, mock_hooks->on_dvr_calls_[1].file_);
    EXPECT_EQ(req->vhost_, mock_hooks->on_dvr_calls_[1].req_->vhost_);

    // Verify to_string() method
    std::string str = async_call->to_string();
    EXPECT_TRUE(str.find("vhost=test.vhost") != std::string::npos);
    EXPECT_TRUE(str.find("file=/path/to/dvr/file.flv") != std::string::npos);

    // Clean up injected dependencies to avoid double-free
    async_call->hooks_ = NULL;
    async_call->config_ = NULL;
}

// MockDvrSegmenter implementation
MockDvrSegmenter::MockDvrSegmenter()
{
    write_metadata_called_ = false;
    write_audio_called_ = false;
    write_video_called_ = false;
    fragment_ = NULL;
}

void MockDvrSegmenter::assemble()
{
}

MockDvrSegmenter::~MockDvrSegmenter()
{
    srs_freep(fragment_);
}

srs_error_t MockDvrSegmenter::initialize(ISrsDvrPlan *p, ISrsRequest *r)
{
    return srs_success;
}

SrsFragment *MockDvrSegmenter::current()
{
    return fragment_;
}

srs_error_t MockDvrSegmenter::open()
{
    return srs_success;
}

srs_error_t MockDvrSegmenter::write_metadata(SrsMediaPacket *metadata)
{
    write_metadata_called_ = true;
    return srs_success;
}

srs_error_t MockDvrSegmenter::write_audio(SrsMediaPacket *shared_audio, SrsFormat *format)
{
    write_audio_called_ = true;
    return srs_success;
}

srs_error_t MockDvrSegmenter::write_video(SrsMediaPacket *shared_video, SrsFormat *format)
{
    write_video_called_ = true;
    return srs_success;
}

srs_error_t MockDvrSegmenter::close()
{
    return srs_success;
}

VOID TEST(DvrPlanTest, WriteMediaPacketsTypicalScenario)
{
    srs_error_t err;

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create mock segmenter
    MockDvrSegmenter *mock_segmenter = new MockDvrSegmenter();
    mock_segmenter->assemble();

    // Create SrsDvrPlan instance
    SrsUniquePtr<SrsDvrPlan> plan(new SrsDvrPlan());

    // Initialize the plan with mock segmenter
    HELPER_EXPECT_SUCCESS(plan->initialize(NULL, mock_segmenter, req.get()));

    // Enable DVR by setting dvr_enabled_ flag
    plan->dvr_enabled_ = true;

    // Create media packets for testing
    SrsUniquePtr<SrsMediaPacket> metadata(new SrsMediaPacket());
    char *metadata_data = new char[10];
    memset(metadata_data, 0x01, 10);
    metadata->wrap(metadata_data, 10);
    metadata->message_type_ = SrsFrameTypeScript;

    SrsUniquePtr<SrsMediaPacket> audio(new SrsMediaPacket());
    char *audio_data = new char[10];
    memset(audio_data, 0x02, 10);
    audio->wrap(audio_data, 10);
    audio->message_type_ = SrsFrameTypeAudio;

    SrsUniquePtr<SrsMediaPacket> video(new SrsMediaPacket());
    char *video_data = new char[10];
    memset(video_data, 0x03, 10);
    video->wrap(video_data, 10);
    video->message_type_ = SrsFrameTypeVideo;

    // Create format object
    SrsUniquePtr<SrsFormat> format(new SrsFormat());

    // Test on_meta_data() - should call segmenter's write_metadata
    HELPER_EXPECT_SUCCESS(plan->on_meta_data(metadata.get()));
    EXPECT_TRUE(mock_segmenter->write_metadata_called_);

    // Test on_audio() - should call segmenter's write_audio
    HELPER_EXPECT_SUCCESS(plan->on_audio(audio.get(), format.get()));
    EXPECT_TRUE(mock_segmenter->write_audio_called_);

    // Test on_video() - should call segmenter's write_video
    HELPER_EXPECT_SUCCESS(plan->on_video(video.get(), format.get()));
    EXPECT_TRUE(mock_segmenter->write_video_called_);

    // Test with DVR disabled - should not call segmenter methods
    plan->dvr_enabled_ = false;
    mock_segmenter->write_metadata_called_ = false;
    mock_segmenter->write_audio_called_ = false;
    mock_segmenter->write_video_called_ = false;

    HELPER_EXPECT_SUCCESS(plan->on_meta_data(metadata.get()));
    EXPECT_FALSE(mock_segmenter->write_metadata_called_);

    HELPER_EXPECT_SUCCESS(plan->on_audio(audio.get(), format.get()));
    EXPECT_FALSE(mock_segmenter->write_audio_called_);

    HELPER_EXPECT_SUCCESS(plan->on_video(video.get(), format.get()));
    EXPECT_FALSE(mock_segmenter->write_video_called_);
}

VOID TEST(DvrPlanTest, CreatePlanTypicalScenario)
{
    srs_error_t err;

    // Test segment plan
    SrsUniquePtr<MockSrsConfig> segment_config(new MockSrsConfig());
    HELPER_EXPECT_SUCCESS(segment_config->mock_parse(_MIN_OK_CONF "vhost test.vhost { dvr { enabled on; dvr_plan segment; } }"));

    ISrsDvrPlan *segment_plan = NULL;
    HELPER_EXPECT_SUCCESS(SrsDvrPlan::create_plan(segment_config.get(), "test.vhost", &segment_plan));
    EXPECT_TRUE(segment_plan != NULL);
    EXPECT_TRUE(dynamic_cast<SrsDvrSegmentPlan *>(segment_plan) != NULL);
    srs_freep(segment_plan);

    // Test session plan
    SrsUniquePtr<MockSrsConfig> session_config(new MockSrsConfig());
    HELPER_EXPECT_SUCCESS(session_config->mock_parse(_MIN_OK_CONF "vhost test.vhost { dvr { enabled on; dvr_plan session; } }"));

    ISrsDvrPlan *session_plan = NULL;
    HELPER_EXPECT_SUCCESS(SrsDvrPlan::create_plan(session_config.get(), "test.vhost", &session_plan));
    EXPECT_TRUE(session_plan != NULL);
    EXPECT_TRUE(dynamic_cast<SrsDvrSessionPlan *>(session_plan) != NULL);
    srs_freep(session_plan);

    // Test illegal plan
    SrsUniquePtr<MockSrsConfig> illegal_config(new MockSrsConfig());
    HELPER_EXPECT_SUCCESS(illegal_config->mock_parse(_MIN_OK_CONF "vhost test.vhost { dvr { enabled on; dvr_plan invalid; } }"));

    ISrsDvrPlan *illegal_plan = NULL;
    HELPER_EXPECT_FAILED(SrsDvrPlan::create_plan(illegal_config.get(), "test.vhost", &illegal_plan));
    EXPECT_TRUE(illegal_plan == NULL);
}

VOID TEST(DvrPlanTest, OnReapSegmentExecutesAsyncTask)
{
    srs_error_t err;

    // Create mock async worker
    SrsUniquePtr<MockAsyncCallWorker> mock_async(new MockAsyncCallWorker());

    // Create mock segmenter with a fragment
    SrsUniquePtr<MockDvrSegmenter> mock_segmenter(new MockDvrSegmenter());
    SrsFragment *fragment = new SrsFragment();
    fragment->set_path("/tmp/dvr_segment.flv");
    mock_segmenter->fragment_ = fragment;

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create SrsDvrPlan instance
    SrsUniquePtr<SrsDvrPlan> plan(new SrsDvrPlan());
    plan->req_ = req.get();

    // Inject mock dependencies
    plan->segment_ = mock_segmenter.get();
    plan->async_ = mock_async.get();

    // Call on_reap_segment
    HELPER_EXPECT_SUCCESS(plan->on_reap_segment());

    // Verify async worker execute was called
    EXPECT_EQ(1, mock_async->execute_count_);
    EXPECT_EQ(1, (int)mock_async->tasks_.size());

    // Clean up injected dependencies to avoid double-free
    plan->segment_ = NULL;
    plan->async_ = NULL;
    plan->req_ = NULL;
}

// Test SrsDvrSessionPlan::on_publish and on_unpublish - major use scenario
// This test covers the typical scenario where DVR session plan handles publish/unpublish lifecycle.
// The on_publish method:
// 1. Calls parent SrsDvrPlan::on_publish() to initialize base functionality
// 2. Checks if DVR is already enabled (supports multiple publish)
// 3. Checks if DVR is enabled in config for the vhost
// 4. Closes any existing segment
// 5. Opens a new segment
// 6. Sets dvr_enabled_ flag to true
// The on_unpublish method:
// 1. Checks if DVR is enabled (supports multiple publish)
// 2. Closes the current segment (ignores errors)
// 3. Sets dvr_enabled_ flag to false
// 4. Calls parent SrsDvrPlan::on_unpublish() to cleanup
VOID TEST(DvrSessionPlanTest, PublishUnpublishTypicalScenario)
{
    srs_error_t err;

    // Create mock config that enables DVR
    SrsUniquePtr<MockEdgeConfig> mock_config(new MockEdgeConfig());

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create mock segmenter
    MockDvrSegmenter *mock_segmenter = new MockDvrSegmenter();
    mock_segmenter->assemble();

    // Create SrsDvrSessionPlan instance
    SrsUniquePtr<SrsDvrSessionPlan> plan(new SrsDvrSessionPlan());

    // Inject mock config
    plan->config_ = mock_config.get();

    // Initialize the plan with mock segmenter
    HELPER_EXPECT_SUCCESS(plan->initialize(NULL, mock_segmenter, req.get()));

    // Verify initial state - DVR should be disabled
    EXPECT_FALSE(plan->dvr_enabled_);

    // Test on_publish() - should enable DVR and open segment
    HELPER_EXPECT_SUCCESS(plan->on_publish(req.get()));

    // Verify DVR is now enabled
    EXPECT_TRUE(plan->dvr_enabled_);

    // Test on_publish() again - should return success immediately (multiple publish support)
    HELPER_EXPECT_SUCCESS(plan->on_publish(req.get()));

    // DVR should still be enabled
    EXPECT_TRUE(plan->dvr_enabled_);

    // Test on_unpublish() - should close segment and disable DVR
    plan->on_unpublish();

    // Verify DVR is now disabled
    EXPECT_FALSE(plan->dvr_enabled_);

    // Test on_unpublish() again - should return immediately (multiple publish support)
    plan->on_unpublish();

    // DVR should still be disabled
    EXPECT_FALSE(plan->dvr_enabled_);

    // Clean up injected dependencies to avoid double-free
    plan->segment_ = NULL;
    plan->config_ = NULL;
}

// Test SrsDvrSegmentPlan::initialize, on_publish and on_unpublish - major use scenario
// This test covers the typical scenario where DVR segment plan handles publish/unpublish lifecycle.
// The initialize method:
// 1. Calls parent SrsDvrPlan::initialize() to initialize base functionality
// 2. Reads wait_keyframe configuration from config
// 3. Reads duration configuration from config
// The on_publish method:
// 1. Calls parent SrsDvrPlan::on_publish() to initialize base functionality
// 2. Checks if DVR is already enabled (supports multiple publish)
// 3. Checks if DVR is enabled in config for the vhost
// 4. Closes any existing segment
// 5. Opens a new segment
// 6. Sets dvr_enabled_ flag to true
// The on_unpublish method:
// 1. Closes the current segment (ignores errors)
// 2. Sets dvr_enabled_ flag to false
// 3. Calls parent SrsDvrPlan::on_unpublish() to cleanup
VOID TEST(DvrSegmentPlanTest, PublishUnpublishTypicalScenario)
{
    srs_error_t err;

    // Create mock config that enables DVR
    SrsUniquePtr<MockEdgeConfig> mock_config(new MockEdgeConfig());

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create mock segmenter
    MockDvrSegmenter *mock_segmenter = new MockDvrSegmenter();
    mock_segmenter->assemble();

    // Create SrsDvrSegmentPlan instance
    SrsUniquePtr<SrsDvrSegmentPlan> plan(new SrsDvrSegmentPlan());

    // Inject mock config
    plan->config_ = mock_config.get();

    // Initialize the plan with mock segmenter
    HELPER_EXPECT_SUCCESS(plan->initialize(NULL, mock_segmenter, req.get()));

    // Verify initial state - DVR should be disabled
    EXPECT_FALSE(plan->dvr_enabled_);

    // Test on_publish() - should enable DVR and open segment
    HELPER_EXPECT_SUCCESS(plan->on_publish(req.get()));

    // Verify DVR is now enabled
    EXPECT_TRUE(plan->dvr_enabled_);

    // Test on_publish() again - should return success immediately (multiple publish support)
    HELPER_EXPECT_SUCCESS(plan->on_publish(req.get()));

    // DVR should still be enabled
    EXPECT_TRUE(plan->dvr_enabled_);

    // Test on_unpublish() - should close segment and disable DVR
    plan->on_unpublish();

    // Verify DVR is now disabled
    EXPECT_FALSE(plan->dvr_enabled_);

    // Test on_unpublish() again - should return immediately (multiple publish support)
    plan->on_unpublish();

    // DVR should still be disabled
    EXPECT_FALSE(plan->dvr_enabled_);

    // Clean up injected dependencies to avoid double-free
    plan->segment_ = NULL;
    plan->config_ = NULL;
}

// Test SrsDvrSegmentPlan::on_video with segment reaping when duration exceeds limit
// This test covers the major use scenario:
// 1. Segment duration exceeds configured limit (cduration_)
// 2. A keyframe arrives (wait_keyframe_ is enabled)
// 3. Segment is reaped (closed and reopened)
// 4. Sequence header is requested from origin hub
VOID TEST(DvrSegmentPlanTest, OnVideoReapSegmentWhenDurationExceeds)
{
    srs_error_t err;

    // Create mock config that enables DVR with segment duration
    SrsUniquePtr<MockEdgeConfig> mock_config(new MockEdgeConfig());

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create mock segmenter with a fragment
    MockDvrSegmenter *mock_segmenter = new MockDvrSegmenter();
    mock_segmenter->assemble();
    SrsFragment *fragment = new SrsFragment();
    fragment->set_path("/tmp/dvr_segment.flv");
    mock_segmenter->fragment_ = fragment;

    // Create mock origin hub
    SrsUniquePtr<MockOriginHub> mock_hub(new MockOriginHub());

    // Create SrsDvrSegmentPlan instance
    SrsUniquePtr<SrsDvrSegmentPlan> plan(new SrsDvrSegmentPlan());

    // Inject mock config
    plan->config_ = mock_config.get();

    // Initialize the plan with mock segmenter and hub
    HELPER_EXPECT_SUCCESS(plan->initialize(mock_hub.get(), mock_segmenter, req.get()));

    // Enable DVR
    plan->dvr_enabled_ = true;

    // Set segment duration to 30 seconds (30 * 1000 * 1000 microseconds)
    plan->cduration_ = 30 * SRS_UTIME_SECONDS;

    // Set wait_keyframe to true (typical configuration)
    plan->wait_keyframe_ = true;

    // Create video format with H.264 codec
    SrsUniquePtr<MockSrsFormat> format(new MockSrsFormat());

    // Simulate fragment duration exceeding the configured limit
    // Append frames to build up duration to 31 seconds (exceeds 30 second limit)
    fragment->append(0);     // Start at 0ms
    fragment->append(31000); // End at 31000ms (31 seconds)

    // Create H.264 keyframe video packet (not sequence header)
    // H.264 keyframe format: 0x17 = (1 << 4) | 7 = keyframe + H.264
    // AVC packet type: 0x01 = NALU (not sequence header which is 0x00)
    SrsUniquePtr<SrsMediaPacket> video_keyframe(new SrsMediaPacket());
    char *keyframe_data = new char[10];
    keyframe_data[0] = 0x17; // Keyframe + H.264 codec
    keyframe_data[1] = 0x01; // AVC NALU (not sequence header)
    memset(keyframe_data + 2, 0, 8);
    video_keyframe->wrap(keyframe_data, 10);
    video_keyframe->message_type_ = SrsFrameTypeVideo;

    // Call on_video() - should trigger segment reaping
    HELPER_EXPECT_SUCCESS(plan->on_video(video_keyframe.get(), format.get()));

    // Verify that on_dvr_request_sh was called (sequence header requested after reaping)
    EXPECT_EQ(1, mock_hub->on_dvr_request_sh_count_);

    // Verify that write_video was called on the segmenter
    EXPECT_TRUE(mock_segmenter->write_video_called_);

    // Clean up injected dependencies to avoid double-free
    plan->segment_ = NULL;
    plan->config_ = NULL;
}

// Test SrsDvr::initialize() method
// This test covers the major use scenario:
// 1. Creates SrsDvr instance with mocked dependencies
// 2. Calls initialize() with mock hub and request
// 3. Verifies that DVR plan is created based on configuration
// 4. Verifies that appropriate segmenter (FLV or MP4) is created based on path extension
// 5. Verifies that plan is initialized with the segmenter
VOID TEST(DvrTest, InitializeTypicalScenario)
{
    srs_error_t err;

    // Create mock config that enables DVR
    SrsUniquePtr<MockEdgeConfig> mock_config(new MockEdgeConfig());

    // Create mock app factory
    SrsUniquePtr<MockDvrAppFactory> mock_factory(new MockDvrAppFactory());

    // Create mock origin hub
    SrsUniquePtr<MockOriginHub> mock_hub(new MockOriginHub());

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create SrsDvr instance
    SrsUniquePtr<SrsDvr> dvr(new SrsDvr());
    dvr->assemble();

    // Inject mock dependencies
    dvr->config_ = mock_config.get();
    dvr->app_factory_ = mock_factory.get();

    // Test: Initialize with FLV path (default)
    HELPER_EXPECT_SUCCESS(dvr->initialize(mock_hub.get(), req.get()));

    // Verify that plan was created
    EXPECT_TRUE(dvr->plan_ != NULL);

    // Verify that request was copied
    EXPECT_TRUE(dvr->req_ != NULL);
    EXPECT_EQ("test.vhost", dvr->req_->vhost_);
    EXPECT_EQ("live", dvr->req_->app_);
    EXPECT_EQ("stream1", dvr->req_->stream_);

    // Verify that hub was set
    EXPECT_EQ(mock_hub.get(), dvr->hub_);

    // Clean up injected dependencies to avoid double-free
    // Note: Don't set config_ to NULL before destruction because destructor needs it for unsubscribe
    dvr->app_factory_ = NULL;
    srs_freep(dvr->req_);
}

// Test SrsDvr::on_publish and on_unpublish - major use scenario
// This test covers the typical scenario where DVR handles publish/unpublish lifecycle.
// The on_publish method:
// 1. Returns early if DVR is not activated (actived_ = false)
// 2. Calls plan_->on_publish() to delegate to the DVR plan
// 3. Copies the request to req_ member field
// The on_unpublish method:
// 1. Calls plan_->on_unpublish() to delegate to the DVR plan
VOID TEST(DvrTest, OnPublishUnpublishTypicalScenario)
{
    srs_error_t err;

    // Create mock config
    SrsUniquePtr<MockEdgeConfig> mock_config(new MockEdgeConfig());

    // Create mock app factory
    SrsUniquePtr<MockDvrAppFactory> mock_factory(new MockDvrAppFactory());

    // Create mock origin hub
    SrsUniquePtr<MockOriginHub> mock_hub(new MockOriginHub());

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create SrsDvr instance
    SrsUniquePtr<SrsDvr> dvr(new SrsDvr());
    dvr->assemble();

    // Inject mock dependencies
    dvr->config_ = mock_config.get();
    dvr->app_factory_ = mock_factory.get();

    // Initialize DVR (this creates the plan)
    HELPER_EXPECT_SUCCESS(dvr->initialize(mock_hub.get(), req.get()));

    // Replace the real plan with a mock plan for testing
    MockDvrPlan *mock_plan = new MockDvrPlan();
    srs_freep(dvr->plan_);
    dvr->plan_ = mock_plan;

    // Set actived_ to true to enable DVR
    dvr->actived_ = true;

    // Test on_publish() - should call plan's on_publish and copy request
    HELPER_EXPECT_SUCCESS(dvr->on_publish(req.get()));

    // Verify that plan's on_publish was called
    EXPECT_TRUE(mock_plan->on_publish_called_);

    // Verify that request was copied
    EXPECT_TRUE(dvr->req_ != NULL);
    EXPECT_EQ("test.vhost", dvr->req_->vhost_);
    EXPECT_EQ("live", dvr->req_->app_);
    EXPECT_EQ("stream1", dvr->req_->stream_);

    // Test on_unpublish() - should call plan's on_unpublish
    dvr->on_unpublish();

    // Verify that plan's on_unpublish was called
    EXPECT_TRUE(mock_plan->on_unpublish_called_);

    // Clean up injected dependencies to avoid double-free
    dvr->plan_ = NULL;
    dvr->app_factory_ = NULL;
    srs_freep(mock_plan);
}

// Test SrsDvr media packet handling methods (on_meta_data, on_audio, on_video)
// These methods check the actived_ flag and delegate to plan_ when DVR is active.
// When DVR is not active, they return success without calling plan_.
VOID TEST(DvrTest, OnMediaPacketsTypicalScenario)
{
    srs_error_t err;

    // Create mock config
    SrsUniquePtr<MockEdgeConfig> mock_config(new MockEdgeConfig());

    // Create mock app factory
    SrsUniquePtr<MockDvrAppFactory> mock_factory(new MockDvrAppFactory());

    // Create mock origin hub
    SrsUniquePtr<MockOriginHub> mock_hub(new MockOriginHub());

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create SrsDvr instance
    SrsUniquePtr<SrsDvr> dvr(new SrsDvr());
    dvr->assemble();

    // Inject mock dependencies
    dvr->config_ = mock_config.get();
    dvr->app_factory_ = mock_factory.get();

    // Initialize DVR
    HELPER_EXPECT_SUCCESS(dvr->initialize(mock_hub.get(), req.get()));

    // Create mock plan to track method calls
    MockDvrPlan *mock_plan = new MockDvrPlan();
    srs_freep(dvr->plan_);
    dvr->plan_ = mock_plan;

    // Create test media packets
    SrsUniquePtr<SrsMediaPacket> metadata(new SrsMediaPacket());
    SrsUniquePtr<SrsMediaPacket> audio(new SrsMediaPacket());
    SrsUniquePtr<SrsMediaPacket> video(new SrsMediaPacket());
    SrsUniquePtr<SrsFormat> format(new SrsFormat());

    // Test 1: When DVR is not activated, methods should return success without calling plan
    dvr->actived_ = false;

    HELPER_EXPECT_SUCCESS(dvr->on_meta_data(metadata.get()));
    HELPER_EXPECT_SUCCESS(dvr->on_audio(audio.get(), format.get()));
    HELPER_EXPECT_SUCCESS(dvr->on_video(video.get(), format.get()));

    // Test 2: When DVR is activated, methods should delegate to plan
    dvr->actived_ = true;

    HELPER_EXPECT_SUCCESS(dvr->on_meta_data(metadata.get()));
    HELPER_EXPECT_SUCCESS(dvr->on_audio(audio.get(), format.get()));
    HELPER_EXPECT_SUCCESS(dvr->on_video(video.get(), format.get()));

    // Verify all methods successfully delegated to plan when active
    // (MockDvrPlan returns srs_success for all methods)

    // Clean up injected dependencies to avoid double-free
    dvr->plan_ = NULL;
    dvr->app_factory_ = NULL;
    srs_freep(mock_plan);

    // Note: Keep config_ set so destructor can call unsubscribe
}

// Test SrsDvrSegmentPlan::on_audio - major use scenario
// This test covers the typical scenario where DVR segment plan handles audio packets.
// The on_audio method:
// 1. Calls update_duration() to check if segment needs reaping based on duration
// 2. Calls parent SrsDvrPlan::on_audio() to write audio to segmenter
// 3. Returns success if both operations succeed
VOID TEST(DvrSegmentPlanTest, OnAudioTypicalScenario)
{
    srs_error_t err;

    // Create mock config that enables DVR
    SrsUniquePtr<MockEdgeConfig> mock_config(new MockEdgeConfig());

    // Create mock request
    SrsUniquePtr<MockEdgeRequest> req(new MockEdgeRequest("test.vhost", "live", "stream1"));

    // Create mock segmenter with a fragment
    MockDvrSegmenter *mock_segmenter = new MockDvrSegmenter();
    mock_segmenter->assemble();
    SrsFragment *fragment = new SrsFragment();
    fragment->set_path("/tmp/dvr_audio.flv");
    mock_segmenter->fragment_ = fragment;

    // Create mock origin hub
    SrsUniquePtr<MockOriginHub> mock_hub(new MockOriginHub());

    // Create SrsDvrSegmentPlan instance
    SrsUniquePtr<SrsDvrSegmentPlan> plan(new SrsDvrSegmentPlan());

    // Inject mock config
    plan->config_ = mock_config.get();

    // Initialize the plan with mock segmenter and hub
    HELPER_EXPECT_SUCCESS(plan->initialize(mock_hub.get(), mock_segmenter, req.get()));

    // Enable DVR
    plan->dvr_enabled_ = true;

    // Set segment duration to 30 seconds (typical configuration)
    plan->cduration_ = 30 * SRS_UTIME_SECONDS;

    // Create audio format
    SrsUniquePtr<MockSrsFormat> format(new MockSrsFormat());

    // Create AAC audio packet
    // AAC audio format: 0xAF = (10 << 4) | 15 = AAC + 44kHz + 16bit + stereo
    // AAC packet type: 0x01 = AAC raw (not sequence header which is 0x00)
    SrsUniquePtr<SrsMediaPacket> audio(new SrsMediaPacket());
    char *audio_data = new char[10];
    audio_data[0] = 0xAF; // AAC codec
    audio_data[1] = 0x01; // AAC raw (not sequence header)
    memset(audio_data + 2, 0, 8);
    audio->wrap(audio_data, 10);
    audio->message_type_ = SrsFrameTypeAudio;
    audio->timestamp_ = 1000; // 1 second

    // Append timestamp to fragment to simulate duration tracking
    fragment->append(0);    // Start at 0ms
    fragment->append(1000); // Current at 1000ms (1 second, well below 30 second limit)

    // Call on_audio() - should succeed without triggering segment reaping
    HELPER_EXPECT_SUCCESS(plan->on_audio(audio.get(), format.get()));

    // Verify that write_audio was called on the segmenter (parent SrsDvrPlan::on_audio)
    EXPECT_TRUE(mock_segmenter->write_audio_called_);

    // Verify that segment was NOT reaped (duration not exceeded, so on_dvr_request_sh not called)
    EXPECT_EQ(0, mock_hub->on_dvr_request_sh_count_);

    // Clean up injected dependencies to avoid double-free
    plan->segment_ = NULL;
    plan->config_ = NULL;
}
