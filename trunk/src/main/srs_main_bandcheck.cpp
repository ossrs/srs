/*
The MIT License (MIT)

Copyright (c) 2013-2014 wenjiegit

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sstream>
#include <getopt.h>
#include <stdlib.h>

#include <srs_protocol_rtmp.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_kernel_error.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_app_socket.hpp>
#include <srs_app_config.hpp>
#include <srs_app_log.hpp>
#include <srs_app_server.hpp>

// kernel module.
ISrsLog* _srs_log = new SrsFastLog();
ISrsThreadContext* _srs_context = new ISrsThreadContext();
// app module.
SrsConfig* _srs_config = NULL;
SrsServer* _srs_server = NULL;

#include <st.h>

// server play control
#define SRS_BW_CHECK_START_PLAY         "onSrsBandCheckStartPlayBytes"
#define SRS_BW_CHECK_STARTING_PLAY      "onSrsBandCheckStartingPlayBytes"
#define SRS_BW_CHECK_STOP_PLAY          "onSrsBandCheckStopPlayBytes"
#define SRS_BW_CHECK_STOPPED_PLAY       "onSrsBandCheckStoppedPlayBytes"

// server publish control
#define SRS_BW_CHECK_START_PUBLISH      "onSrsBandCheckStartPublishBytes"
#define SRS_BW_CHECK_STARTING_PUBLISH   "onSrsBandCheckStartingPublishBytes"
#define SRS_BW_CHECK_STOP_PUBLISH       "onSrsBandCheckStopPublishBytes"
#define SRS_BW_CHECK_STOPPED_PUBLISH    "onSrsBandCheckStoppedPublishBytes"

// EOF control.
#define SRS_BW_CHECK_FINISHED           "onSrsBandCheckFinished"
#define SRS_BW_CHECK_FLASH_FINAL        "finalClientPacket"

// client only
#define SRS_BW_CHECK_PLAYING            "onSrsBandCheckPlaying"
#define SRS_BW_CHECK_PUBLISHING         "onSrsBandCheckPublishing"

class ISrsProtocolReaderWriter;

/**
*  @brief class of Linux version band check client
*  check play and publish speed.
*/
class SrsBandCheckClient : public SrsRtmpClient
{
public:
    SrsBandCheckClient(ISrsProtocolReaderWriter* io);
    ~SrsBandCheckClient();

public:
    /**
    *  @brief test play
    *
    */
    int check_play();
    /**
    *  @brief test publish
    *
    */
    int check_publish();

private:
    /**
    *  @brief just return success.
    */
    int create_stream(int& stream_id);
    /**
    *  @brief just return success.
    */
    int play(std::string stream, int stream_id);
    /**
    *  @brief just return success.
    */
    int publish(std::string stream, int stream_id);

private:
    int expect_start_play();
    int send_starting_play();
    int expect_stop_play();
    int send_stopped_play();
    int expect_start_pub();
    int send_starting_pub();
    int send_pub_data();
    int expect_stop_pub();
    /**
    *  @brief expect result.
    *   because the core module has no method to decode this packet
    *   so we must get the internal data and decode it here.
    */
    int expect_finished();
    int send_stopped_pub();
    /**
    *  @brief notify server the check procedure is over.
    */
    int send_final();
};

/**
*  @brief class of band check
*  used to check band width with a client @param bandCheck_Client
*/
class SrsBandCheck
{
public:
    SrsBandCheck();
    ~SrsBandCheck();

public:
    /**
    *  @brief band check method
    *
    *   connect to server------>rtmp handshake------>rtmp connect------>play------>publish
    *   @retval ERROR_SUCCESS when success.
    */
    int check(const std::string& app, const std::string& tcUrl);

    /**
    *  @brief set the address and port of test server
    *
    *  @param server server address, domain or ip
    *  @param server listened port ,default is 1935
    */
    void set_server(const std::string& server, int port = 1935);

private:
    int connect_server();
private:
    st_netfd_t stfd;
    ISrsProtocolReaderWriter* skt;
    SrsBandCheckClient* bandCheck_Client;
    std::string server_address;
    int server_port;
};

/**
*  @brief init st lib
*/
static int init_st();
static void print_help(char** argv);
static void print_version();

/**
*   @brief get user option
*   @internal ip    Mandatory arguments
*   @internal key   Mandatory arguments
*   @internal port  default 1935
*   @internal vhost default bandcheck.srs.com
*/
static int get_opt(int argc ,char* argv[]);

/**
*   global var.
*/
static struct option long_options[] =
{
    {"ip",      required_argument,  0, 'i'},
    {"port",    optional_argument,  0, 'p'},
    {"key",     required_argument,  0, 'k'},
    {"vhost",   optional_argument,  0, 'v'},
    {"help",    no_argument,        0, 'h'},
    {"version", no_argument,        0, 'V'},
};

static const char* short_options = "i:p::k:v::hV";

static std::string g_ip;
static int         g_port  = 1935;
static std::string g_key;
static std::string g_vhost = "bandcheck.srs.com";

#define BUILD_VERSION   "srs band check 0.1"

// TODO: FIXME: by winlin, the bandwidth test tool has logic bug.
int main(int argc ,char* argv[])
{
    int ret = ERROR_SUCCESS;
    
    if (argc <= 1) {
        print_help(argv);
        exit(1);
    }

    if ((ret = get_opt(argc, argv)) != ERROR_SUCCESS) {
        return -1;
    }

    // check param
    if (g_ip.empty()) {
        printf("ip address should not be empty.\n");
        return -1;
    }

    if (g_key.empty()) {
        printf("test key should not be empty.\n");
        return -1;
    }

    if ((ret = init_st()) != ERROR_SUCCESS) {
        srs_error("band check init failed. ret=%d", ret);
        return ret;
    }

    std::string app   = "app?key=" + g_key + "&vhost=" + g_vhost;

    char tcUrl_buffer[1024] = {0};
    sprintf(tcUrl_buffer, "rtmp://%s:%d/%s", g_ip.c_str(), g_port, app.c_str());
    std::string tcUrl = tcUrl_buffer;

    SrsBandCheck band_check;
    band_check.set_server(g_ip, g_port);
    if ((ret = band_check.check(app, tcUrl)) != ERROR_SUCCESS) {
        srs_error("band check failed. address=%s  ret=%d", "xx.com", ret);
        return -1;
    }

    return 0;
}

SrsBandCheckClient::SrsBandCheckClient(ISrsProtocolReaderWriter* io)
    : SrsRtmpClient(io)
{
}

SrsBandCheckClient::~SrsBandCheckClient()
{
}

int SrsBandCheckClient::check_play()
{
    int ret = ERROR_SUCCESS;

    if ((ret = expect_start_play()) != ERROR_SUCCESS) {
        srs_error("expect_start_play failed. ret=%d", ret);
        return ret;
    }

    if ((ret = send_starting_play()) != ERROR_SUCCESS) {
        srs_error("send starting play failed. ret=%d", ret);
        return ret;
    }

    if ((ret = expect_stop_play()) != ERROR_SUCCESS) {
        srs_error("expect stop play failed. ret=%d", ret);
        return ret;
    }

    if ((ret = send_stopped_play()) != ERROR_SUCCESS) {
        srs_error("send stopped play failed. ret=%d", ret);
        return ret;
    }

    return ret;
}

int SrsBandCheckClient::check_publish()
{
    int ret = ERROR_SUCCESS;

    if ((ret = expect_start_pub()) != ERROR_SUCCESS) {
        srs_error("expect start pub failed. ret=%d", ret);
        return ret;
    }

    if ((ret = send_starting_pub())!= ERROR_SUCCESS) {
        srs_error("send starting pub failed. ret=%d", ret);
        return ret;
    }

    if ((ret = send_pub_data()) != ERROR_SUCCESS) {
        srs_error("publish data failed. ret=%d", ret);
        return ret;
    }

    if ((ret = send_stopped_pub()) != ERROR_SUCCESS) {
        srs_error("send stopped pub failed. ret=%d", ret);
        return ret;
    }

    if ((ret = expect_finished()) != ERROR_SUCCESS) {
        srs_error("expect finished msg failed. ret=%d", ret);
        return ret;
    }

    if ((ret = send_final()) != ERROR_SUCCESS) {
        srs_error("send final msg failed. ret=%d", ret);
        return ret;
    }

    return ret;
}

int SrsBandCheckClient::create_stream(int &stream_id)
{
    return ERROR_SUCCESS;
}

int SrsBandCheckClient::play(std::string stream, int stream_id)
{
    return ERROR_SUCCESS;
}

int SrsBandCheckClient::publish(std::string stream, int stream_id)
{
    return ERROR_SUCCESS;
}

int SrsBandCheckClient::expect_start_play()
{
    int ret = ERROR_SUCCESS;

    // expect connect _result
    SrsMessage* msg = NULL;
    SrsBandwidthPacket* pkt = NULL;
    if ((ret = __srs_rtmp_expect_message<SrsBandwidthPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
        srs_error("expect bandcheck start play message failed. ret=%d", ret);
        return ret;
    }
    SrsAutoFree(SrsMessage, msg, false);
    SrsAutoFree(SrsBandwidthPacket, pkt, false);
    srs_info("get bandcheck start play message");

    if (pkt->command_name != SRS_BW_CHECK_START_PLAY) {
        srs_error("pkt error. expect=%s, actual=%s", SRS_BW_CHECK_START_PLAY, pkt->command_name.c_str());
        return -1;
    }

    return ret;
}

int SrsBandCheckClient::send_starting_play()
{
    int ret = ERROR_SUCCESS;

    SrsBandwidthPacket* pkt = new SrsBandwidthPacket;
    pkt->command_name = SRS_BW_CHECK_STARTING_PLAY;
    if ((ret = send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
        srs_error("send starting play msg failed. ret=%d", ret);
        return ret;
    }

    return ret;
}

int SrsBandCheckClient::expect_stop_play()
{
    int ret = ERROR_SUCCESS;

    while (true) {
        SrsMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((ret = __srs_rtmp_expect_message<SrsBandwidthPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("expect stop play message failed. ret=%d", ret);
            return ret;
        }
        SrsAutoFree(SrsMessage, msg, false);
        SrsAutoFree(SrsBandwidthPacket, pkt, false);
        srs_info("get bandcheck stop play message");

        if (pkt->command_name == SRS_BW_CHECK_STOP_PLAY) {
            break;
        }
    }

    return ret;
}

int SrsBandCheckClient::send_stopped_play()
{
    int ret = ERROR_SUCCESS;

    SrsBandwidthPacket* pkt = new SrsBandwidthPacket;
    pkt->command_name = SRS_BW_CHECK_STOPPED_PLAY;
    if ((ret = send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
        srs_error("send stopped play msg failed. ret=%d", ret);
        return ret;
    }

    return ret;
}

int SrsBandCheckClient::expect_start_pub()
{
    int ret = ERROR_SUCCESS;

    while (true) {
        SrsMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((ret = __srs_rtmp_expect_message<SrsBandwidthPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("expect start pub message failed. ret=%d", ret);
            return ret;
        }
        SrsAutoFree(SrsMessage, msg, false);
        SrsAutoFree(SrsBandwidthPacket, pkt, false);
        srs_info("get bandcheck start pub message");

        if (pkt->command_name == SRS_BW_CHECK_START_PUBLISH) {
            break;
        }
    }

    return ret;
}

int SrsBandCheckClient::send_starting_pub()
{
    int ret = ERROR_SUCCESS;

    SrsBandwidthPacket* pkt = new SrsBandwidthPacket;
    pkt->command_name = SRS_BW_CHECK_STARTING_PUBLISH;
    if ((ret = send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
        srs_error("send starting play msg failed. ret=%d", ret);
        return ret;
    }
    srs_info("send starting play msg success.");

    return ret;
}

int SrsBandCheckClient::send_pub_data()
{
    int ret = ERROR_SUCCESS;

    int data_count = 100;
    while (true) {
        SrsBandwidthPacket* pkt = new SrsBandwidthPacket;
        pkt->command_name = SRS_BW_CHECK_PUBLISHING;

        for (int i = 0; i < data_count; ++i) {
            std::stringstream seq;
            seq << i;
            std::string play_data = "SrS band check data from client's publishing......";
            pkt->data->set(seq.str(), SrsAmf0Any::str(play_data.c_str()));
        }
        data_count += 100;

        if ((ret = send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
            srs_error("send publish message failed.ret=%d", ret);
            return ret;
        }

        if ((ret = expect_stop_pub()) == ERROR_SUCCESS) {
            break;
        }
    }

    return ret;
}

int SrsBandCheckClient::expect_stop_pub()
{
    int ret = ERROR_SUCCESS;
    
    this->set_recv_timeout(1000 * 1000);
    this->set_send_timeout(1000 * 1000);
    
    SrsMessage* msg;
    SrsBandwidthPacket* pkt;
    if ((ret = __srs_rtmp_expect_message<SrsBandwidthPacket>(this->protocol, &msg, &pkt)) != ERROR_SUCCESS) {
        return ret;
    }
    SrsAutoFree(SrsMessage, msg, false);
    SrsAutoFree(SrsBandwidthPacket, pkt, false);
    if (pkt->command_name == SRS_BW_CHECK_STOP_PUBLISH) {
        return ret;
    }

    return ret;
}

int SrsBandCheckClient::expect_finished()
{
    int ret = ERROR_SUCCESS;

    while (true) {
        SrsMessage* msg = NULL;
        SrsBandwidthPacket* pkt = NULL;
        if ((ret = __srs_rtmp_expect_message<SrsBandwidthPacket>(protocol, &msg, &pkt)) != ERROR_SUCCESS) {
            srs_error("expect finished message failed. ret=%d", ret);
            return ret;
        }
        SrsAutoFree(SrsMessage, msg, false);
        SrsAutoFree(SrsBandwidthPacket, pkt, false);
        srs_info("get bandcheck finished message");

        if (pkt->command_name == SRS_BW_CHECK_FINISHED) {
            SrsStream *stream = new SrsStream;
            SrsAutoFree(SrsStream, stream, false);

            if ((ret = stream->initialize((char*)msg->payload, msg->size)) != ERROR_SUCCESS) {
                srs_error("initialize stream error. ret=%d", ret);
                return ret;
            }

            std::string command_name;
            if ((ret = srs_amf0_read_string(stream, command_name)) != ERROR_SUCCESS) {
                srs_error("amfo read string error. ret=%d", ret);
                return ret;
            }

            double action_id;
            if ((ret = srs_amf0_read_number(stream, action_id)) != ERROR_SUCCESS) {
                srs_error("amfo read number error. ret=%d", ret);
                return ret;
            }

            if ((ret = srs_amf0_read_null(stream)) != ERROR_SUCCESS) {
                srs_error("amfo read number error. ret=%d", ret);
                return ret;
            }

            SrsAmf0Object* object = SrsAmf0Any::object();
            if ((ret = object->read(stream)) != ERROR_SUCCESS) {
                srs_freep(object);
                srs_error("amfo read object error. ret=%d", ret);
                return ret;
            }

            int64_t start_time = 0;
            int64_t end_time = 0;

            SrsAmf0Any* start_time_any = object->get_property("start_time");
            if (start_time_any && start_time_any->is_number()) {
                start_time = start_time_any->to_number();
            }

            SrsAmf0Any* end_time_any = object->get_property("end_time");
            if (end_time_any && end_time_any->is_number()) {
                end_time = end_time_any->to_number();
            }

            int play_kbps = 0;
            int pub_kbps = 0;
            SrsAmf0Any* play_kbp_any = object->get_property("play_kbps");
            if (play_kbp_any && play_kbp_any->is_number()) {
                play_kbps = play_kbp_any->to_number();
            }

            SrsAmf0Any* pub_kbp_any = object->get_property("publish_kbps");
            if (pub_kbp_any && pub_kbp_any->is_number()) {
                pub_kbps = pub_kbp_any->to_number();
            }

            float time_elapsed;
            if (end_time - start_time > 0) {
                time_elapsed = (end_time - start_time) / 1000.00;
            }

            srs_trace("result: play %d kbps, publish %d kbps, check time %.4f S\n"
                   , play_kbps, pub_kbps, time_elapsed);

            break;
        }
    }

    return ret;
}

int SrsBandCheckClient::send_stopped_pub()
{
    int ret = ERROR_SUCCESS;

    SrsBandwidthPacket* pkt = new SrsBandwidthPacket;
    pkt->command_name = SRS_BW_CHECK_STOPPED_PUBLISH;
    if ((ret = send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
        srs_error("send stopped pub msg failed. ret=%d", ret);
        return ret;
    }
    srs_info("send stopped pub msg success.");

    return ret;
}

int SrsBandCheckClient::send_final()
{
    int ret = ERROR_SUCCESS;

    SrsBandwidthPacket* pkt = new SrsBandwidthPacket;
    pkt->command_name = SRS_BW_CHECK_FLASH_FINAL;
    if ((ret = send_and_free_packet(pkt, 0)) != ERROR_SUCCESS) {
        srs_error("send final msg failed. ret=%d", ret);
        return ret;
    }
    srs_info("send final msg success.");

    return ret;
}

SrsBandCheck::SrsBandCheck()
{
    skt = NULL;
    bandCheck_Client = NULL;
    stfd = NULL;
}

SrsBandCheck::~SrsBandCheck()
{
    srs_freep(bandCheck_Client);
    srs_freep(skt);
    srs_close_stfd(stfd);
}

int SrsBandCheck::check(const std::string &app, const std::string &tcUrl)
{
    int ret = ERROR_SUCCESS;

    if ((ret = connect_server()) != ERROR_SUCCESS) {
        srs_error("connect to server failed. ret = %d", ret);
        return ret;
    }

    if ((ret = bandCheck_Client->handshake()) != ERROR_SUCCESS) {
        srs_error("handshake failed. ret = %d", ret);
        return ret;
    }

    if ((ret = bandCheck_Client->connect_app(app, tcUrl)) != ERROR_SUCCESS) {
        srs_error("handshake failed. ret = %d", ret);
        return ret;
    }

    if ((ret = bandCheck_Client->check_play()) != ERROR_SUCCESS) {
        srs_error("band check play failed.");
        return ret;
    }

    if ((ret = bandCheck_Client->check_publish()) != ERROR_SUCCESS) {
        srs_error("band check publish failed.");
        return ret;
    }

    return ret;
}

void SrsBandCheck::set_server(const std::string &server, int port)
{
    server_address = server;
    server_port = port;
}

int SrsBandCheck::connect_server()
{
    int ret = ERROR_SUCCESS;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock == -1){
        ret = ERROR_SOCKET_CREATE;
        srs_error("create socket error. ret=%d", ret);
        return ret;
    }

    stfd = st_netfd_open_socket(sock);
    if(stfd == NULL){
        ret = ERROR_ST_OPEN_SOCKET;
        srs_error("st_netfd_open_socket failed. ret=%d", ret);
        return ret;
    }

    skt = new SrsSocket(stfd);
    bandCheck_Client = new SrsBandCheckClient(skt);

    // connect to server.
    std::string ip = srs_dns_resolve(server_address);
    if (ip.empty()) {
        ret = ERROR_SYSTEM_IP_INVALID;
        srs_error("dns resolve server error, ip empty. ret=%d", ret);
        return ret;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_port);
    addr.sin_addr.s_addr = inet_addr(ip.c_str());

    if (st_connect(stfd, (const struct sockaddr*)&addr, sizeof(sockaddr_in), ST_UTIME_NO_TIMEOUT) == -1){
        ret = ERROR_ST_CONNECT;
        srs_error("connect to server error. ip=%s, port=%d, ret=%d", ip.c_str(), server_port, ret);
        return ret;
    }
    srs_trace("connect to server success. server=%s, ip=%s, port=%d", server_address.c_str(), ip.c_str(), server_port);

    return ret;
}

int init_st()
{
    int ret = ERROR_SUCCESS;

    if (st_set_eventsys(ST_EVENTSYS_ALT) == -1) {
        ret = ERROR_ST_SET_EPOLL;
        srs_error("st_set_eventsys use linux epoll failed. ret=%d", ret);
        return ret;
    }

    if(st_init() != 0){
        ret = ERROR_ST_INITIALIZE;
        srs_error("st_init failed. ret=%d", ret);
        return ret;
    }

    return ret;
}

void print_help(char** argv)
{
    printf(
        "Usage: %s [OPTION]...\n"
        "test band width from client to rtmp server.\n"
        "Mandatory arguments to long options are mandatory for short options too.\n"
        "  -i, --ip                  the ip or domain that to test\n"
        "  -p, --port                the port that server listen \n"
        "  -k, --key                 the key used to test \n"
        "  -v, --vhost               the vhost used to test \n"
        "  -V, --version             output version information and exit \n"
        "  -h, --help                display this help and exit \n"
        "\n"
        "For example:\n"
        "    %s -i 127.0.0.1 -p 1935 -v bandcheck.srs.com -k 35c9b402c12a7246868752e2878f7e0e"
        "\n\n"
        "Exit status:\n"
        "0      if OK,\n"
        "other  if error occured, and the detail should be printed.\n"
        "\n\n"
        "srs home page: <http://blog.chinaunix.net/uid/25006789.html>\n", 
        argv[0], argv[0]);
}

void print_version()
{
    const char *version = ""
            "srs_bandcheck "BUILD_VERSION"\n"
            "Copyright (c) 2013-2014 wenjiegit.\n"
            "License MIT\n"
            "This is free software: you are free to change and redistribute it.\n"
            "There is NO WARRANTY, to the extent permitted by law.\n"
            "\n"
            "Written by wenjie.\n";

    printf("%s", version);
}

int get_opt(int argc, char *argv[])
{
    int ret = ERROR_SUCCESS;

    int c;
    while ((c = getopt_long (argc, argv, short_options, long_options, NULL)) != -1) {
        switch (c) {
        case 'i':
            if (optarg) {
                g_ip = optarg;
            }
            break;
        case 'p':
            if (optarg) {
                g_port = atoi(optarg);
            }
            break;
        case 'k':
            if (optarg) {
                g_key = optarg;
            }
            break;
        case 'v':
            if (optarg) {
                g_vhost = optarg;
            }
            break;
        case 'V':
            print_version();
            exit(0);
            break;
        case 'h':
            print_help(argv);
            exit(0);
            break;
        default:
            printf("see --help or -h\n");
            ret = -1;
        }
    }

    return ret;
}
