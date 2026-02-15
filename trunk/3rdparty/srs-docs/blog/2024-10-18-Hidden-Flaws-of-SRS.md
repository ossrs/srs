# The Hidden Flaws of SRS: What You Need to Know

In open-source streaming server projects, SRS is widely used and has been maintained for a long time, starting in 2013. Over the years, various issues have been addressed, but some remain challenging to solve. This article summarizes and shares these insights.

Some people think SRS has many features and is complete, but it is far from a perfect open-source project and never will be. Anyone can contribute to the SRS project at any time, and participation is always welcome.

## Cluster
The cluster capability is a significant weakness of SRS. There have been ongoing debates and design revisions regarding the types of clusters it actually supports.

SRS has supported Edge clusters since version 2.0 in 2015, claiming to be capable of large-scale RTMP/FLV distribution. However, after a decade of development, it was found that very few users actually utilize Edge, especially considering the lack of support for protocols like SRT and WebRTC. There is a notable disparity in capabilities between open-source project users and what video cloud services require in terms of media streaming clusters.

In my assessment, SRS doesn’t necessarily need to support large-scale distribution but rather focus on small to medium-sized clusters. However, even in smaller clusters, it’s essential to support thousands or tens of thousands of concurrent connections. Moreover, it’s crucial to have robust cluster capabilities for mainstream protocols, not just limited to RTMP/FLV.

When discussing media streaming clusters, apart from the media processing server SRS, there are also scheduling and operational systems involved. Even with RTMP URL access systems, there is still DNS resolution and scheduling. While WebRTC signaling typically uses HTTPS, separate from media transport, clusters inherently involve scheduling and distribution capabilities.

Firstly, SRS lacks cluster scheduling capabilities as it’s primarily an open-source media server. To build a million-concurrent-user media streaming cluster, one needs to address scheduling independently. CDNs typically use DNS or HTTP-DNS scheduling. Scheduling essentially determines the system’s capacity utilization and operational costs.

Secondly, the cluster architecture could be tree-based (source-edge) or graph-based (forwarding or cascading) but there isn’t a definitive answer. In China, due to multiple operators, tree architecture is common for handling massive playback issues in live streaming. However, for building a global network or scenarios with massive cold streams (like network conferences), a tree structure is inadequate.

Lastly, load balancing for streaming servers, including load collection and overload protection, is usually balanced internally within the system. While scheduling acts as the first line of load balancing defense, internal load balancing is crucial. Although SRS has implemented circuit breaking, it hasn’t yet implemented load collection and balancing, like redirecting overloaded traffic to other servers through 302 redirects. For more on load balancing, refer to the Load Balancing Streaming Servers summary.

Below is the source-edge architecture of SRS, where multiple Edge servers retrieve streams from the Origin server. Each stream is fetched from the Origin only once per Edge, and Edges can also fetch streams from other Edges, creating a multilevel Edge structure to support massive playback:

```text
SRS(Edge)
SRS(Origin) --RTMP/FLV---> SRS(Edge) --RTMP/FLV--> Massive Playback
                           SRS(Edge)
```

> Note: This Edge architecture was initially derived from Adobe AMS. However, AMS used a modified RTMP protocol with custom messages for cluster management, while SRS uses a standard RTMP protocol, achieving a standard RTMP client implementation.

* Supports only RTMP/FLV protocols, not SRT/WebRTC/HLS. WebRTC protocol, in particular, requires significant cluster support for handling more concurrency due to its performance differences.
* Typically, Nginx is needed for HLS distribution and supporting HTTPS-FLV protocol. SRS’s performance in HTTPS and HLS distribution is low and may lead to stuttering issues due to disk I/O blocking.
* Source retrieval is a fixed configuration, forming a tree-like distribution network that can be challenging to adjust. For multiple source retrievals, source server clusters or retrieval based on vhost are required.

Next is the source server cluster architecture of SRS, where multiple source servers connect in a mesh, exchanging stream information. They use RTMP 302 redirection to direct clients to the target source server, dispersing streams to multiple sources to support more streams:

```text
SRS(Origin)
   +(MESH)  ---RTMP---> SRS(Edge) --RTMP/FLV--> Streaming or Playback
SRS(Origin)
```

* Source servers connect in a mesh, limiting the number of supported source servers.
* If a source server has no stream, RTMP 302 is used to direct the client to the target source server, relying on Edge to handle RTMP 302 redirection messages.
* When streams migrate between source servers, HLS recovery and generation issues are not handled well, leading to interruptions in HLS streams.

In reality, this source server cluster isn’t an ideal solution, especially for the WebRTC protocol. Since SRS is single-threaded and WebRTC protocol performance is notably low, each SRS source server can support around 300 WebRTC connections. Enabling WebRTC and RTMP protocol conversion may support around 40 connections (with audio transcoding). While live streaming protocols may support 3000–5000 concurrent users, WebRTC is on a different scale, necessitating new source server cluster solutions.

The new source server cluster architecture of SRS uses Proxy servers to forward traffic to backend source servers. Proxies are stateless and horizontally scalable, addressing many streaming scenarios:

```text
SRS(Origin)
SRS(Origin) ---> SRS(Proxy) --> Streaming or Playback
SRS(Origin)
```

* Source servers are not interdependent and register with the Proxy. Proxies are stateless and synchronize status through Redis, making deployment in K8s straightforward.
* Proxies support all protocols: RTMP/FLV/HLS/SRT/WebRTC, proxying API and media traffic to source servers, supporting other media servers besides SRS, offering high versatility.
* Despite the new cluster architecture, HLS recovery issues remain unresolved.

Considering the WebRTC protocol, using Proxy to connect 100 SRS Origins, even with WebRTC to RTMP protocol conversion, can support up to 4000 streams, with each stream accommodating up to 300 viewers. This setup can handle a maximum of 4000 streams or support 100 streams with 30K viewers each.

Note: SRS also supports Dynamic Forwarding, actively forwarding streams to different backend services, essentially acting as a proxy. The difference lies in SRS Proxy being implemented in Go, relying on Redis for statelessness, facilitating system integration and various business scheduling logic. On the other hand, SRS Forwarding is a feature implemented by SRS, supporting only the RTMP protocol, and making modifications can be complex.

The new source server cluster can be used in conjunction with Edge servers, still supporting numerous RTMP streams and massive playback for each stream:

```text
SRS(Origin)                     SRS(Edge)
SRS(Origin) ---> SRS(Proxy) --> SRS(Edge) --> Streaming or Playback
SRS(Origin)                     SRS(Edge)
```

* Edge servers continue to support only RTMP/FLV protocols, but future support for WebRTC protocol is planned to enable massive playback for live streaming and RTC scenarios.
* Proxies and Origins are typically regionally deployed. To deploy clusters in different regions, scheduling is usually based on vhost, assigning different vhosts to different regions.
* Monitoring and full traceability of clusters are not fully supported by Proxy. Additionally, HLS stream recovery remains inadequate.

Apart from top-tier video clouds and CDNs requiring millions of streaming and playback capabilities, in general applications, handling thousands of streams and playback is the scale most business scenarios can achieve. For smaller applications, WebRTC relies on clusters to address performance bottlenecks, while other protocols focus on avoiding single points of failure to improve availability.

Cluster implementation for RTMP/FLV protocols is relatively straightforward. For Edge servers, RTMP/FLV is stateless, so if issues arise, switching to the next source server is usually sufficient. Clients may experience minor content shifts or repetitions, but these are generally imperceptible. When saying stateless, it means these states can be discarded, and errors can be resolved through retries. If an Edge server encounters issues, clients simply retry, relying on client retries for error recovery.

Implementing a cluster for HLS protocol is challenging, especially in handling stream switching. HLS protocol can handle client stream reconnections correctly, using DISCONTINUITY tags to ensure continuous segments. However, if a source server crashes or restarts, SRS currently lacks persistent storage and recovery of segment information, leading to M3U8 content regeneration and client disruptions. Similar issues arise when streams migrate between source servers. HLS clusters need to address these exceptional scenarios.

Another challenging aspect of HLS protocol is data statistics like connection numbers. Since HLS is segment-based and lacks a concept of connections, SRS introduced HLS_CTX to achieve connection statistics through QueryString. However, for HLS clusters, Edge servers need to implement HLS distribution and connection statistics, a feature not yet available in SRS. If using Nginx for Edge HLS distribution, connection statistics support needs to be added to Nginx.

For SRT and WebRTC, SRS currently only supports Proxy, enabling stream scheduling to different Origins, but Edge servers do not support these two protocols. This limitation means that if a stream has a large number of viewers, SRT or WebRTC protocols cannot be used. Addressing how to achieve comprehensive protocol stack support on Edge servers is a challenging aspect of clusters.

Typically, K8s or HELM are used to manage clusters, including deployment, upgrades, scaling, shrinking, and rollbacks. SRS clusters consider K8s support, with a critical capability being Gracefully Quit, where ports are closed upon exit, and services are restarted only after existing connections gradually close, enabling smooth upgrades. Gracefully Quit is easily implemented for TCP protocols, but UDP protocol support is still pending.

For end-to-end tracing, SRS passes connection IDs and other information in the RTMP protocol and supports the OpenTelemetry standard protocol, integrating with Tencent Cloud’s APM. However, APM lacks an open-source platform like Prometheus, and integrating with different APM cloud services requires some effort. Additionally, SRS Proxy does not yet support APM instrumentation extensively. SRS’s own APM instrumentation is limited, providing only essential information and not comprehensive coverage.

Using Go to implement Proxy poses potential performance bottlenecks. As Go handles media traffic, it may not achieve the same high performance as C++, especially on multi-core CPUs, potentially leading to a 30% performance loss. While this issue may not be significant in small clusters, most scenarios do not require such high performance levels. RUST could be an alternative option, but maintaining C++, Go, and RUST simultaneously increases community maintenance complexity and incurs high costs.

Regarding clusters, there are considerations around multi-process or multi-threading issues.

## Multi-processing
Multi-core processing or multi-threading is essential for servers as they typically support multiple CPU cores, enabling a single machine to offer more services externally. From a technical standpoint, there are various solutions:

* Multi-process: For instance, Nginx operates as a multi-process system, where each process is single-threaded. Due to Nginx’s influence, most servers of its era adopt this architecture. The challenge with this architecture lies in handling long-lived connections, such as those in live media streaming, where two connections need to be dispatched to a single process. In essence, multi-processing isn’t an ideal architecture for a media streaming server, which explains why this architecture is rarely seen in modern media servers — it’s challenging to maintain.
* Multi-threading: Servers predating Nginx, like Adobe AMS, Real Helix, Wowza, Janus, used a multi-threaded architecture. However, this architecture is outdated due to performance losses from thread switching and the potential for thread and data race issues leading to crashes and deadlocks. Over the last decade, multi-threading has evolved into a thread-local architecture, seen in systems like Envoy and ZLM. Thread-local architecture is essentially multi-process, but with the advantage of shared data between processes. This architecture is the most appealing evolution direction for SRS, but it hasn’t fully convinced SRS to adopt it yet — it’s not quite good enough, as I’ll explain later.
* Cluster architecture: K8s falls under this category, functioning as a distributed architecture that virtualizes a single machine into a K8s cluster. Each Pod can be a single process, and multiple Pods and Services form a cluster to provide services externally. SRS’s Proxy-Origin-Edge cluster operates on this architecture. While this architecture introduces a Proxy element that may impact performance, it offers advantages in maintenance and scalability. K8s not only addresses media distribution issues but also handles deployment, scaling, monitoring, updates, and more.

SRS has traditionally been single-process, single-threaded, akin to a single-process version of Nginx, with the addition of coroutines for concurrent processing. Coroutines are implemented using the StateThreads library, which has been modified to support thread-local functionality for operation in a multi-threaded environment. Despite experimenting and analyzing thread-local handling for a media architecture over the years, SRS has not adopted a thread-local approach but rather a different multi-threaded architecture that is still in the planning stage:

* Stream processing occurs on a single thread, while blocking operations like logging, file writing, and DNS resolution are handled by separate threads. In essence, SRS uses multi-threading to address blocking issues. If Linux supports fully asynchronous I/O in the future, multi-threading may not be necessary, as seen in liburing.
* StateThreads multi-threading faces issues with Windows C++ exception handling. Windows’ exception mechanism differs from Linux, causing compatibility problems when StateThreads implements setjmp and longjmp, as discussed in SEH.
* Challenges with multi-thread scheduling and load balancing. While thread-local multi-threading addresses multi-core utilization, it still limits the need for streaming and playback to a single thread, preventing complete load balancing across multiple threads. Without thread-local functionality, serious locking and competition issues arise. Essentially, it’s like running multiple K8s Pods within a single process and handling scheduling, monitoring, and load balancing internally, which can be quite complex.

In SRS 5.0, StateThreads were restructured to support thread-local functionality and initiated a main thread and subthreads to transition the architecture into a multi-threaded model. However, various issues arose during subsequent stages, leading to a default return to a single-threaded architecture in SRS 6.0. The future may see the removal of multi-threading capabilities as they become less critical with the new Proxy and Edge architectures. If Proxy continues to enhance its capabilities, encompassing various protocols and Edge functionality, it will gradually evolve into a Proxy+Origin cluster, fully resolving the multi-threading challenge.

Additionally, we explored another potential architecture where specific capabilities are distributed across different threads, like using separate threads for WebRTC encryption and decryption. However, this approach transforms into a typical multi-threaded program rather than a thread-local architecture, resulting in performance overhead from locks and reduced stability — not an ideal direction.

In essence, Go can be considered a traditional multi-threaded architecture, not a thread-local architecture, which is why Go also requires locks, contributing to its lower performance. For media streaming clusters, Go may not be the best language as performance losses increase costs for media servers. While C++ is prone to crashing issues, which can’t be fundamentally resolved, RUST might be a potential option, albeit requiring research and experimentation.

The stability issues in medium to large C++ projects are indeed a pain point, especially when introducing SharedPtr for Source object release.

## Smart Pointer
The memory leakage issue in SRS took approximately 10 years to resolve, as detailed in the Source cleanup discussion.

In reality, it wasn’t exactly a leak but rather a failure to release caches. Initially, SRS focused on handling a small number of RTMP streams with a large number of viewers, prioritizing the release of client connections over Stream Source objects. This approach was chosen because most memory was consumed by client connections, while Stream Source objects occupied minimal memory. By not releasing Stream Source objects, the system was simplified, avoiding many memory issues and ensuring high stability.

However, if there were frequent URL changes for streams or a high volume of publishing with few viewers, rapid memory escalation became evident.

Releasing Stream Source objects requires addressing various object references to Stream Source. Since SRS supports protocols like RTMP, HTTP-FLV, HLS, SRT, WebRTC, DASH, GB28181, and their conversions, scenarios involving mutual references arise.

Consider an RTMP client pushing to SRS, creating a Stream Source for each URL:

```cpp
srs_error_t SrsRtmpConn::stream_service_cycle() {
    SrsLiveSource* source = NULL;
    _srs_sources->fetch_or_create(req, server, &source);
}
```

When starting a stream, an HTTP mount is created to enable HTTP FLV playback:

```cpp
srs_error_t SrsHttpStreamServer::http_mount(SrsLiveSource* s, SrsRequest* r) {
    entry = new SrsLiveEntry(mount);
    entry->source = s;
    entry->stream = new SrsLiveStream(s, r, entry->cache);
    mux.handle(mount, entry->stream);
}

srs_error_t SrsLiveStream::do_serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r) {
    SrsLiveConsumer* consumer = NULL;
    source->create_consumer(consumer);
}
```

If the Source remains valid, business logic can be easily implemented. However, if cleanup is needed, various scenarios must be considered, significantly increasing the risk of dangling pointers:

* During unpublish, HTTP Streams must be cleaned up, requiring clients to be disconnected first, and Source can only be released after the connection is destroyed.
* Since SRS allows playback before publishing, all HTTP Streams must be disconnected before Source cleanup.
* In Edge mode, playing an HTTP Stream creates a Source, triggering a fetch from the Origin to publish. Cleanup is triggered when the last client disconnects, posing a self-destruction risk for HTTP Streams.

In practice, C++ 11’s shared pointer, with some advanced features, not only manages memory through reference counting but also includes:

* shared_from_this: This common ability returns a shared pointer from a raw pointer. However, since the raw pointer is managed by a shared pointer, creating a new shared pointer directly could lead to double releases. This feature isn't essential, so SRS hasn't implemented it.
* inheritance and compare: Involving inheritance and comparison, these scenarios deal with smart pointer inheritance and comparison, which aren't necessary for SRS.
* weak pointer: If there's a circular reference, shared pointer reference counting may fail, necessitating the use of weak pointers to avoid circular references. Weak pointers are similar to raw pointers but provide a function to check if the shared pointer is still valid. In SRS's context, circular references can be avoided, so weak pointers aren't implemented.

After implementing simplified smart pointers, Stream Source is managed using timers for checking and releasing. This standard pointer usage involves releasing the Source smart pointer when the reference count reaches zero:

```cpp
srs_error_t SrsLiveSourceManager::notify(int event, srs_utime_t interval, srs_utime_t tick) {
    std::map< std::string, SrsSharedPtr<SrsLiveSource> >::iterator it;
    for (it = pool.begin(); it != pool.end();) {
        SrsSharedPtr<SrsLiveSource>& source = it->second;
        source->cycle();

        if (source->stream_is_dead()) {
            pool.erase(it++); // Free source smart ptr
}
```

To address circular reference issues, comments are used to avoid circular references. Directly using raw pointers can help, for instance:

```cpp
// Source holds and releases hub
class SrsLiveSource {
    SrsOriginHub* hub;
}

class SrsOriginHub : public ISrsReloadHandler {
    // Because source references to this object, so we should directly use the source ptr.
    SrsLiveSource* source_;
}
```

Another scenario involves two objects triggering destruction, but their lifecycles aren’t independent. For instance, in the RTC context, a session like SrsRtcConnection contains SrsRtcTcpConn. When the session times out, the TCP connection must be closed. Conversely, when the TCP connection closes, it must trigger session destruction:

```cpp
// Session contains the TCP connection
class SrsRtcConnection {
    SrsRtcNetworks* networks_;
}
class SrsRtcTcpNetwork: public ISrsRtcNetwork {
    SrsSharedResource<SrsRtcTcpConn> owner_;
}

// TCP connection directly uses the session's raw pointer, as the session's lifecycle is shorter
class SrsRtcTcpConn {
    // Because session references to this object, we should directly use the session ptr.
    SrsRtcConnection* session_;
}

// When TCP disconnects, trigger session expiration and destruction
srs_error_t SrsRtcTcpConn::cycle() {
    // Only remove session when network is established, because client might use other UDP network.
    if(session_ && session_->tcp()->is_establelished()) {
        session_->tcp()->set_state(SrsRtcNetworkStateClosed);
        session_->expire();
    }
}
```

Dealing with GB28181 is more complex, where a session includes both Sip and Media components. Although there are no circular reference issues, the session may be created from Sip, and Sip and Media may need to update the session:

```cpp
class SrsGbSession {
    SrsSharedResource<SrsGbSipTcpConn> sip_;
    SrsSharedResource<SrsGbMediaTcpConn> media_;
}

class SrsGbSipTcpConn {
    // The owner session object; we use the raw pointer and avoid freeing it.
    SrsGbSession* session_;
}

class SrsGbMediaTcpConn {
    // The owner session object; we use the raw pointer and avoid freeing it.
    SrsGbSession* session_;
}

srs_error_t SrsGbSipTcpConn::bind_session(SrsSipMessage* msg, SrsGbSession** psession) {
    SrsSharedResource<SrsGbSession>* session = dynamic_cast<SrsSharedResource<SrsGbSession>*>(_srs_gb_manager->find_by_id(device));
    // Create a Session object from the SIP channel
    if (!session) {
        raw_session = new SrsGbSession();
        session = new SrsSharedResource<SrsGbSession>(raw_session);
        _srs_gb_manager->add_with_id(device, session);
    }
    // Update the SIP channel object in the Session.
    raw_session->on_sip_transport(*wrapper_);
}

srs_error_t SrsGbMediaTcpConn::bind_session(uint32_t ssrc, SrsGbSession** psession) {
    SrsSharedResource<SrsGbSession>* session = dynamic_cast<SrsSharedResource<SrsGbSession>*>(_srs_gb_manager->find_by_fast_id(ssrc));
    SrsGbSession* raw_session = (*session).get();
    // Update the Media channel object in the Session.
    raw_session->on_media_transport(*wrapper_);
}
```

In contrast to RTC, for GB, the SIP and Media channels, when released, only free their connection objects and don’t trigger GB Session destruction. Instead, GB Session is destroyed through timeouts.

In practical applications, SRS’s smart pointers are used in limited scenarios, without applying advanced features or syntactic sugar. This approach enhances the maintainability of the solution.

## Error vs Logging
In most cases, errors will eventually occur in areas prone to issues, making error handling crucial for system operation. Errors and logging are often intertwined, with a common practice being to log error information in the logs. However, errors and logging are two distinct matters.

Logging involves recording system actions and can be used to troubleshoot issues. Errors represent a specific type of behavior, and the relationship between errors and logs includes:

* Logs are categorized, enabling the opening of more detailed logs for investigating challenging issues. Errors do not have levels, but errors can be written to logs.
* Errors should provide comprehensive stack traces since the call path is critical when an error occurs. Errors typically occur in lower-level functions, making understanding and resolving errors dependent on different call paths.
* In C++, errors can be expressed using error codes and exceptions. Error codes are more commonly used and facilitate proper error handling.

Errors may not always occur, but issues like irregular frame rates can still arise. Therefore, logs typically need to indicate critical paths without including errors to assess system normalcy:

```text
[2024-09-27 10:32:46.245][INFO][58467][f951w1of] RTMP client ip=127.0.0.1:57591, fd=13
[2024-09-27 10:32:46.246][INFO][58467][f951w1of] complex handshake success
[2024-09-27 10:32:46.247][INFO][58467][f951w1of] connect app, tcUrl=rtmp://localhost:1935/live, pageUrl=, swfUrl=, schema=rtmp, vhost=localhost, port=1935, app=live, args=null
[2024-09-27 10:32:46.247][INFO][58467][f951w1of] protocol in.buffer=0, in.ack=0, out.ack=0, in.chunk=128, out.chunk=128
[2024-09-27 10:32:46.247][INFO][58467][f951w1of] client identified, type=fmle-publish, vhost=localhost, app=live, stream=livestream, param=, duration=0ms
[2024-09-27 10:32:46.247][INFO][58467][f951w1of] connected stream, tcUrl=rtmp://localhost:1935/live, pageUrl=, swfUrl=, schema=rtmp, vhost=__defaultVhost__, port=1935, app=live, stream=livestream, param=, args=null
[2024-09-27 10:32:46.247][INFO][58467][f951w1of] new live source, stream_url=/live/livestream
[2024-09-27 10:32:46.248][INFO][58467][f951w1of] source url=/live/livestream, ip=127.0.0.1, cache=1/2500, is_edge=0, source_id=/
[2024-09-27 10:32:46.250][INFO][58467][f951w1of] start publish mr=0/350, p1stpt=20000, pnt=5000, tcp_nodelay=0
[2024-09-27 10:32:46.251][INFO][58467][f951w1of] got metadata, width=768, height=320, vcodec=7, acodec=10
[2024-09-27 10:32:46.251][INFO][58467][f951w1of] 46B video sh, codec(7, profile=High, level=3.2, 768x320, 0kbps, 0.0fps, 0.0s)
[2024-09-27 10:32:46.251][INFO][58467][f951w1of] 4B audio sh, codec(10, profile=LC, 2channels, 0kbps, 44100HZ), flv(16bits, 2channels, 44100HZ)
[2024-09-27 10:32:46.253][INFO][58467][f951w1of] RTMP2RTC: Init audio codec to 10(AAC)
[2024-09-27 10:32:48.385][INFO][58467][f951w1of] cleanup when unpublish
```

For server logs, it’s essential to include IDs, such as session-level IDs. This enables quick identification of specific logs when serving multiple clients or when aggregating logs. SRS refers to this mechanism as “traceable logs,” allowing all logs related to a specific ID to be easily found. Due to SRS’s coroutine-based design, a session like an RTMP push connection may contain one or multiple coroutines. Hence, when logging, passing the ID isn’t necessary as it’s automatically obtained:

```cpp
srs_error_t SrsRtmpConn::do_cycle() {
    srs_trace("RTMP client ip=%s:%d, fd=%d", ip.c_str(), port, srs_netfd_fileno(stfd));
}

void SrsProtocol::print_debug_info() {
    srs_trace("protocol in.buffer=%d, in.ack=%d, out.ack=%d, in.chunk=%d, out.chunk=%d", in_buffer_length,
        in_ack_size.window, out_ack_size.window, in_chunk_size, out_chunk_size);
}

// This log macro definition automatically fetches the coroutine ID from get_id().
#define srs_trace(msg, ...) srs_logger_impl(SrsLogLevelTrace, NULL, _srs_context->get_id(), msg, ##__VA_ARGS__)
```

SRS errors contain stack information and error details, resembling Go’s error and wrap mechanism. With this approach, the error object alone provides the complete error context:

```text
[2024-09-27 10:40:30.836][INFO][62805][l9067692] RTMP client ip=127.0.0.1:57942, fd=15
[2024-09-27 10:40:30.837][INFO][62805][l9067692] client identified, type=fmle-publish, vhost=localhost, app=live, stream=livestream, param=, duration=0ms
[2024-09-27 10:40:30.838][ERROR][62805][l9067692][35] serve error code=1028(StreamBusy)(Stream already exists or busy) : service cycle : rtmp: stream service : rtmp: stream /live/livestream is busy
thread [62805][l9067692]: do_cycle() [./src/app/srs_app_rtmp_conn.cpp:263][errno=35]
thread [62805][l9067692]: service_cycle() [./src/app/srs_app_rtmp_conn.cpp:457][errno=35]
thread [62805][l9067692]: acquire_publish() [./src/app/srs_app_rtmp_conn.cpp:1078][errno=35](Resource temporarily unavailable)
```

In SRS, errors are objects that provide complete context and can be sent through a tracing system, like OpenTelemetry APM, for full-chain error display. This necessitates that SRS errors are wrapped rather than directly returned as integers:

```cpp
srs_error_t SrsRtmpConn::do_cycle() {
    if ((err = service_cycle()) != srs_success) {
        err = srs_error_wrap(err, "service cycle");
    }
}

srs_error_t SrsRtmpConn::service_cycle() {
        if (!srs_is_system_control_error(err)) {
            return srs_error_wrap(err, "rtmp: stream service");
        }
}

srs_error_t SrsRtmpConn::acquire_publish(SrsSharedPtr<SrsLiveSource> source) {
    if (!source->can_publish(info->edge)) {
        return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "rtmp: stream %s is busy", req->get_stream_url().c_str());
    }
}
```

It’s common practice not to return error objects but to directly log errors, which can be a less sophisticated approach. For instance, low-level function errors that can be ignored might lead to log flooding if directly printed, prompting the function to return error codes instead. This can result in losing stack information. By returning error objects, the application layer can decide whether to discard or ignore errors, log warnings, log errors, or send alerts to monitoring systems. Directly logging errors fundamentally limits error handling possibilities.

Challenges with SRS’s error mechanism include:

* All functions must return srs_error_t pointer objects, not int error codes, making code writing somewhat rigid and relying solely on Code Review to maintain this rule.
* When calling third-party libraries like FFmpeg, complete stack traces cannot be obtained, and only error code information is available. These libraries typically provide log hooks to route internal logs to SRS’s logs, but C libraries generally do not offer error object mechanisms.
* High-frequency ignorable errors, like certain UDP packet errors, can lead to performance issues. Since UDP packet errors are numerous, generating an error object for each error can create performance bottlenecks. However, erroneous UDP packets are uncommon and can be directly discarded through early judgment.

Initially, SRS used a globally incremented integer for logging, but later switched to generating random string IDs to reduce the likelihood of ID conflicts when logs are centrally collected. Besides IDs, timestamps are included, and logs are typically searched within a limited timeframe, such as one or three hours. The probability of random string ID conflicts is almost negligible.

SRS 6.0 now supports OpenTelemetry APM for application performance monitoring, full-chain logging, and errors. Since APM requires integration with platforms, SRS integrates with Tencent Cloud’s APM. The APM protocol stack involves HTTP/2 and Protobuf, with SRS implementing the Protobuf protocol but supporting only the HTTP/1 protocol. While most APM platforms support integration using HTTP/1 + Protobuf, it’s not the default method. The use of newer protocols and inconsistent authentication mechanisms across cloud providers limits APM applications.

In practice, system OpenAPIs are more commonly used than errors and logs, as detailed below.

## OpenAPI
API refers to the system’s external interface, where in a narrow sense, API generally refers to HTTP API. In reality, logs and errors can be considered as a broader form of API, including system configurations, HTTP APIs, Prometheus Exporter, and more.

SRS has supported HTTP API and Callback since its early days, making it easy to integrate with business systems. Apart from supporting queries for stream and client information, it also enables actions like kicking off streams. Callback includes callbacks for events such as streaming, playback, recording, HLS, etc. Here are some areas that still need improvement:

* Only supports HTTP Basic Authentication, lacking support for Bearer Token and other authentication methods. It is generally recommended to implement an HTTP proxy in Go for authentication and then forward requests to SRS. For instance, Oryx has implemented such a proxy.
* Documentation is not comprehensive, only outlining request and response formats, including field information without detailed explanations of field meanings. Initially, this was due to the API’s instability, with the intention to explain field meanings in detail once stabilized, but this was not followed through.
* Limited pagination and query capabilities; the clients API may contain a significant amount of data, requiring support for paginated queries. For example, it lacks the ability to search based on stream names fuzzily; only specific information can be queried based on IDs. It also lacks sorting based on time. It is generally recommended to maintain a database of streams and clients based on callbacks to support more comprehensive queries.

It is evident that providing APIs and Callbacks is not sufficient; processing of this data is necessary, supporting comprehensive queries, and even graphical representation. For operations and maintenance, monitoring alerts and historical query support are needed. These can be achieved through Prometheus and Grafana using SRS’s Exporter:

```text
+-----+               +-----------+     +---------+
| SRS +--Exporter-->--| Prometheus +-->--+ Grafana +
+-----+   (HTTP)      +-----------+     +---------+
```

Prometheus is a comprehensive monitoring and operations system that fetches data from SRS through a standardized Exporter API, stores it in its own time-series database, and uses its query language PromQL for querying. Grafana enables graphical representation. This approach eliminates the need for each user to convert HTTP APIs and Callbacks for their own monitoring and operations systems. By utilizing SRS’s Prometheus Exporter functionality, you can set up a graphical monitoring and operations dashboard within 10 minutes. For more details, refer to SRS Exporter. Here are some areas where the Exporter can be improved:

* Supports limited metrics, currently only supporting machine-level monitoring data and lacking support for stream or client-level data. For instance, you can know the connection count, bandwidth, stream count, and client count for a specific SRS or entire cluster, but you cannot access statistics for a specific stream.
* The provided dashboard is relatively simple, offering only a common Grafana dashboard. Different scenarios may require different Grafana dashboards.
* Data on error rates and success rates are not precise enough. Typically, such data needs to be provided by clients; the server can only provide success rates for certain scenarios. Additionally, SRS’s current success rate is not accurate enough.

Apart from data metrics, configuration can also be considered part of the interface, determining how the system behaves. SRS uses the configuration file method similar to nginx and supports reloading. Due to users managing configuration files, SRS ensures strong compatibility. Configuration files from SRS 1.0 can still be used in subsequent versions; whenever there are changes like renaming, SRS ensures compatibility with both old and new names after parsing the configuration. Here are some areas where configuration can be improved:

* Configuration files are not very friendly in cloud-native systems; in reality, using environment variables is a more convenient configuration method. Referring to Grafana’s configuration, besides using file configuration, each configuration item can be controlled using environment variables. While SRS has implemented some configuration using environment variables, it does not yet support environment variable configuration at the vhost level.
* At one point, SRS supported an HTTP RAW API that allowed configuration modification and serialization into configuration files, enabling configuration distribution. However, this method led to many issues, easily causing data conflicts and competition, ultimately leading to crashes. Consequently, this feature was removed in official versions. In cloud-native environments, configurations are managed through environment variables in YAML, requiring only YAML changes for configuration adjustments.
* SRS supports configuration reloading, especially useful for system performance optimization. By changing the configuration and reloading, you can quickly verify if optimizations are effective without needing to restart the entire load test. However, this feature is overly complex; certain functions like changing listening ports require very intricate implementations, significantly impacting maintainability and stability. In the future, this feature may be gradually weakened or removed.

Comparing configuration files and environment variable configurations, below is an example configuration file example.conf:

```nginx
listen 1935;
http_api {
  enabled on;
}
rtc_server {
  enabled on;
  candidate 192.168.3.82;
}
vhost __defaultVhost__ {
  rtc {
    enabled on;
  }
}
```

```bash
docker run --rm -it -p 1985:1985 -p 8000:8000 \
  -v $(pwd)/example.conf:/usr/local/srs/conf/docker.conf \
  ossrs/srs:5
```

Describing the configuration file content in the documentation requires users to copy content, create and edit files, and then specify the file during startup, which is cumbersome and prone to errors. In contrast, using environment variables, everything can be done with a simple copy-paste, reducing the likelihood of user errors:

```bash
docker run --rm -it -p 1985:1985 -p 8000:8000 \
  -e SRS_LISTEN=1935 \
  -e SRS_HTTP_API_ENABLED=on \
  -e SRS_RTC_SERVER_ENABLED=on \
  -e CANDIDATE="192.168.3.82" \
  -e SRS_VHOST_RTC_ENABLED=on \
  ossrs/srs:5
```

Maintaining a comprehensive API requires significant effort and time, especially when adding new features to ensure that both the API and documentation are promptly updated. SRS’s API is far from perfect and requires further effort.

## Protocols
Streaming protocols are crucial capabilities of streaming servers, and the protocol capabilities required on servers differ from general media processing. For example, the discontinuation of ffserver was due to FFmpeg’s I/O design primarily focusing on clients, making it unsuitable for handling high concurrency as a server. For instance, SRS does not use a WebRTC codebase to implement the WebRTC protocol because SFU servers require lightweight protocol stacks without the need for media processing, device management, signaling, and algorithms.

SRS natively supports protocols like RTMP, HTTP-FLV, HLS, and WebRTC, ensuring high stability and maintainability. SRT is integrated with the libsrt library for stability, but it lacks perfection in protocol conversion and performance. Protocols like DASH and GB28181 have inherent flaws leading to various issues. HDS and RTSP are outdated streaming protocols, but RTSP still finds application in certain scenarios in the AI era.

Initially, SRS only supported common live streaming protocols: RTMP, HTTP-FLV, and HLS, along with live clusters such as Edge and Origin clusters. With the evolution of live streaming, protocols like WebRTC and SRT are increasingly used, significantly impacting the entire system architecture, especially in protocol conversion.

Initially, protocol conversion only required support for RTMP to HLS. Although OBS can stream using HLS, it is not commonly used. In reality, SRS also supports streaming via POST HTTP-FLV, similar to POST HLS, but FLV streaming is not widely supported by CDNs and common clients, making this method less popular. However, SRS uses the Stream Caster structure to support this unique stream input and then converts it to RTMP; for instance, PUSH MPEGTS over UDP follows this method.

Initially, both SRT and GB28181 used the Stream Caster structure; after SRS receives the stream, it initiates an RTMP Client to stream to localhost, somewhat resembling an external method of receiving these two protocols’ streams. These protocols encounter different issues; starting with SRT:

* SRS did not fully implement the SRT protocol stack but used the libsrt library, which has its threads. This caused many problems in early SRS support for SRT and posed high maintenance challenges. Later, in SRS 5.0, this logic was rewritten, still using the libsrt library but modified based on StateThreads, improving overall stability and maintainability.
* SRT uses UDP for data transmission, and Linux kernel’s UDP transmission performance is inherently low. Additionally, SRT uses TS over UDP, resulting in additional application-level overhead. Consequently, SRT is primarily suited for scenarios with low concurrency rather than high concurrency.
* SRS supports both pushing and pulling streams using SRT, supports SRT to RTMP and WebRTC protocols but does not support RTMP to SRT conversion. Essentially, SRS treats SRT as an ingress protocol rather than a distribution protocol.

The situation with GB28181 is different; in SRS 5.0, GB28181 protocol stack was completely re-implemented. Typically, GB28181 streams to SRS for viewing via WebRTC without plugins, addressing issues such as:

* GB28181 is designed for intranet use and does not consider scenarios like packet loss and jitter, making it usable only with TCP on the internet. Hence, after rewriting the GB28181 protocol in SRS, only GB28181 2016 (TCP) protocol stack is supported, with UDP not implemented.
* GB28181 includes various business capabilities like playback, storage, PTZ, with control messages implemented using SIP. SRS has a basic SIP protocol stack based on HTTP transformation, suitable for simple scenarios. Subsequently, a separate srs-sip project was implemented to allow SRS to use the GB28181 protocol without the built-in SIP stack, although this solution is not yet comprehensive, covering only basic functionalities.
* GB28181 is primarily used in the domestic security field, emphasizing security, resulting in many private information and protocols. In reality, GB28181 has minimal internet application and is predominantly used in the security field. Projects focusing on private networks and security are challenging to maintain in open-source projects, making it difficult for open-source projects to excel in this area.

The RTSP protocol was removed from SRS, initially supported through the Stream Caster method for RTSP stream ingestion. Main issues include:

* Lack of RTSP streaming scenarios; security cameras typically do not use RTSP to stream to SRS but rather play streams using the RTSP protocol from security cameras. This is why SRS supports GB28181 streaming, although GB28181 also has limited internet application.
* Unlike WebRTC, RTSP lacks robust congestion control capabilities, making it prone to stuttering and screen freezing issues during internet transmission. Consequently, RTSP is not an ideal protocol for streaming media over the internet.
* Given that AI applications often involve object recognition in security cameras, many AI systems prefer using the RTSP protocol for integration. However, the future trend is towards RTMP and WebRTC, not RTSP. For example, Google’s first-generation cameras only support RTSP, while the second generation supports WebRTC.

> Note: SRS is willing to support the RTSP protocol in the future, but the long-term assessment indicates that the primary streaming protocols will remain RTMP, WebRTC, and SRT, rather than RTSP. The community’s willingness and resources to support and maintain a declining RTSP protocol remain uncertain.

SRS’s RTMP implementation is undoubtedly the most comprehensive, addressing several issues but also having areas for improvement:

* Enhanced RTMP protocol now supports HEVC, AV1, Opus, and other new encoding standards, allowing avoidance of audio transcoding when converting to the WebRTC protocol. Currently, SRS only supports HEVC in RTMP and has yet to support AV1 and Opus.

SRS supports WebRTC as a core protocol because it is the only option for streaming protocols supported by browsers. However, there are several areas where WebRTC implementation in SRS can be improved:

* Inadequate support for congestion control algorithms in WebRTC; currently, only NACK is supported, lacking FEC and GCC. This is partly due to the lower performance of SRS’s WebRTC protocol stack and the complexity and limited applicability of these algorithms.
* Lack of support for WebRTC clusters; while SRS 7.0 supports WebRTC Origin clusters (Proxy solution) for expanding source station support, Edge clusters are not yet supported for high viewer counts per stream. Expected support for Edge clusters is targeted for SRS 8.0.
* Performance and compatibility challenges; the single-process architecture makes it difficult to enhance performance as WebRTC performance bottlenecks exist at both the application and kernel levels. Continuous adaptation and issue resolution are necessary to improve protocol conversion compatibility. Rust is being evaluated as a potential solution for these challenges; refer to the subsequent section on Rust for detailed information.

SRS’s segmented protocols like HLS, DASH, and HDS, with HLS being the most widely used, HDS nearly obsolete, and DASH having some users but facing certain issues:

* SRS’s HLS does not support multiple bitrates, as video transcoding is required for multi-bitrate HLS, which can only be achieved using FFmpeg. SRS’s HLS also lacks support for MP4 and LLHLS. SRS already supports DASH, including MP4 encapsulation, and supporting LLHLS is relatively straightforward.
* DASH is not as friendly for live streaming as HLS in terms of protocol design simplicity and reliability; refer to DASH’s issues for a detailed description.

MP4 can also be considered a protocol commonly used in recording files; LLHLS also utilizes MP4, especially for HLS HEVC, as Safari only supports MP4 segmented files, not TS segmented files. MP4 compatibility issues are more common than with RTMP, occasionally leading to various unexpected problems. It is recommended to use FFmpeg for recording and transcoding, while relying on SRS for stream reception and distribution only.

## Testing
SRS’s quality assurance mechanisms include unit tests based on gtest, black-box testing based on srs-bench, and Code Review. These mechanisms are interconnected through GitHub Actions, automatically triggered for every Pull Request, Commit, and Release.

These tests are highly effective, often leading to Pull Requests failing due to logic issues caused by code modifications in other areas. It seems that the test cases are not as effective, essentially confirming common-sense judgments like 1+1=2. In reality, errors are seldom introduced in the modified areas but rather surface in other parts due to the intricate relationships between different sections of code. As code grows, these interdependencies often lead to such issues.

Programmers typically have great confidence in their own code but lack confidence in others’ code. Few developers voluntarily write test cases, and when asked to add unit tests during Code Review, most acknowledge the necessity of testing, especially after realizing how tests can uncover logic issues. Personally, even when recognizing the effectiveness of testing, voluntarily writing tests remains a challenging task, making it difficult to adhere to the practice of writing tests before functional code.

I believe that one of the most crucial roles of Code Review is to require the submission of test code. Of course, the primary function of Code Review is to integrate a contributor’s code into one’s own codebase, necessitating an understanding of the code’s purpose and intent, and exploring better implementation options. Common issues include developers unknowingly duplicating existing code functions or failing to leverage existing code for improvements due to unfamiliarity with the current codebase. Additionally, contributors often prioritize habitual implementations over the most maintainable solutions.

In Code Review, open-source communities typically adhere to a single rule: one Pull Request should contain only one feature or bug fix. Due to limited time in open-source communities, including multiple features in a single change makes explaining the code to different individuals challenging, requiring detailed explanations of each function’s purpose, making the review process difficult. Conversely, in a corporate setting, almost every change includes some “convenient” modifications, leading to ineffective Code Reviews and significant code quality issues.

Moreover, although companies have ample time for coding, the urgency from clients often results in a rush to deploy numerous features and changes, leading to Pull Requests containing multiple features. Companies spend less time on code than open-source communities, failing to allocate sufficient time to weigh the most maintainable solutions. This is why open-source communities are more likely to produce high-quality code, whereas companies are more prone to lower-quality code, despite having top-tier programmers.

This is not to say that open-source communities always produce high-quality code; Code Review standards can easily slip, especially when introducing significant code changes or large features. The cases of SRS’s GB28181 and RTSP are typical examples where, due to substantial changes and time constraints, insufficient Review time led to a compromise in code quality. This resulted in numerous bug reports, necessitating the rewriting of existing code and the diligent addition of tests and tools.

It is essential to emphasize that the issue is not with the low quality of code submitted by other developers but rather the lack of sufficient time spent during Code Review, resulting in lower code quality post-merge. For instance, when reviewing GB28181 code, I did not invest time in thoroughly understanding the GB28181 protocol, reviewing each line of code meticulously, demanding comprehensive testing, or conducting stress and black-box testing. This was simply negligence on my part, failing to enforce strict Review standards, for which I take full responsibility.

There are significant issues with SRS’s testing, primarily inadequate coverage. Despite a 53% coverage rate, some core functionalities remain uncovered, such as the Edge cluster. At times, there is a tendency towards laziness; for example, even with the development of the Proxy cluster, there was no test coverage. Although the importance of test coverage is well understood, at times, it is challenging to implement, with the thought that the feature is still unstable and testing can be added once it stabilizes.

Code quality is independent of programming language; without effective code quality standards and strict adherence to these standards, any language can produce low-quality code.

## RUST
It’s evident that Go is not the future of streaming servers due to several reasons:

* Go incurs significant performance losses, with multithreading and garbage collection leading to a 30% increase in overall operational costs, which can be unsustainable for an online system. While Go’s technology stack can be used for learning about streaming or quickly prototyping, the performance issues of Go render it inadequate for long-term development in the streaming server domain.
* The disparity between Go’s ecosystem and C poses challenges when incorporating C code into Go, as using Cgo can result in numerous memory issues. This essentially means abandoning Go’s goroutines in favor of dealing with more memory issues from C, making it challenging to continuously rewrite the entire ecosystem from C to Go, as seen in projects like pion.
* Go is not a replacement for C and C++, implying that for future development, the community and projects relying on C cannot depend on Go for improvements and enhancements. Fundamentally, Go is not the solution for streaming servers, regardless of the efforts invested in this direction.

Life goes on, projects need to progress, new capabilities must be developed, and we cannot wait; action must be taken. Is RUST a suitable path forward? It might be worth exploring if RUST addresses the key challenges faced by streaming servers and if the issues introduced by RUST are manageable.

Firstly, let’s revisit some key themes mentioned earlier:

* Cluster: The cluster capabilities, whether implemented in C or RUST, essentially involve similar logic for cluster implementation, including Proxy proxies, Origin stream processing and conversion, Edge aggregation, and more.
* Multi-processing: RUST may have a slight advantage over C in terms of multi-threading capabilities. With the need for asynchronous and coroutine technologies, C faces some challenges as described earlier. RUST, being a newer language, provides encapsulation for threads at the language level, improving portability between systems with better support for async operations and basic communication components like channels.
* Smart Pointer: RUST, similar to C++11, offers smart pointers in its standard library, providing a potential advantage over C, where developers need to implement smart pointers themselves.

Apart from these issues, there are additional considerations that haven’t been discussed yet, which I will summarize below.

* Dangling Pointers: RUST has a clear advantage in handling dangling pointers, as RUST does not encounter issues with dangling pointers, preventing crashes caused by such pointers. This problem fundamentally arises from C’s flexibility without restrictions, potentially leading to issues like accessing variables across threads, resulting in dangling pointers.

In C++, the following code snippet does not have dangling pointer issues:
```cpp
int x = 100;
std::thread t([&]() {
  std::cout << "x is " << x << std::endl;
});
t.join();
```

However, if the thread does not release as expected and references a variable after its local scope, it can lead to dangling pointer issues. This is a real problem that I have encountered. On the other hand, in RUST:
```rust
let x = 100;
thread::spawn(move || {
    println!("x is {}", x);
}).join().unwrap();
```

This problem does not exist in RUST, as the compiler ensures that variables must be moved into the thread. RUST restricts sharing regular variables between threads due to potential risks. Although sharing in this case is not problematic, as the variable will be valid until the thread ends, it is not always the case, especially when continually maintaining a project and adding logic incrementally. Dangling pointer issues are quite common, and I have often seen them during crash troubleshooting in real projects.

RUST, like any other language, has its strengths and weaknesses. It solves some problems while introducing others:

* Steep Learning Curve: RUST introduces ownership mechanisms to address memory issues without a garbage collector, which can be challenging to grasp when combined with ownership, multithreading, and async concepts. Understanding these concepts becomes increasingly difficult. While GPT-4 may struggle to explain these complex ideas and compiler errors, the good news is that O1 works well.
* Optional Async Mechanism: RUST’s async behavior is similar to Python, where not all functions are async. If an IO library does not implement async, you need to do it yourself. In contrast, Go follows an all-async strategy, with all libraries supporting async operations. RUST’s std library has limited async and await support, with runtime being a third-party library like tokio, meaning tokio needs to implement its async std, and other libraries also need to support async in their own way. If a third-party library includes threads, it may not work seamlessly with tokio, which contains a thread pool.
* Limited std and Inconsistent Third-party Libraries: While RUST’s std documentation is comprehensive and of high quality, it lacks the extensive libraries found in Go’s std. Many third-party RUST libraries have varying levels of documentation and quality, posing challenges for the open-source community.

For server programs, supporting asynchronous IO (async) is essential. RUST’s async and ST are quite similar in this regard. Most async IO operations are based on polling, such as Linux epoll. Nginx, for instance, directly uses epoll. When reading and writing, partial writes may occur, indicating that the buffer is full, requiring readiness polling before writing again. Dealing with these issues for every read and write can lead to cumbersome application logic, resulting in callback hell. Go and ST create application-level coroutines, while RUST also uses spawn to execute a future. Comparing the code snippets:

```go
//////////////////////////////////////////////////////////////////////////////
// Go
listener, _ := net.Listen("tcp", "0.0.0.0:8080")
for {
    conn, _ := listener.Accept()
    go handleTCP(conn)
}

// Goroutine handling the TCP connection
func handleTCP(conn net.Conn) {
    defer conn.Close()
    buf := make([]byte, 1024)
    n, _ := conn.Read(buf)
}
```

```c
//////////////////////////////////////////////////////////////////////////////
// ST(State Threads)
int fd = socket(AF_INET, SOCK_STREAM, 0);
::bind(fd, (const sockaddr*)&addr, sizeof(addr)); // addr is the listening address
::listen(fd, 10);
st_netfd_t stfd = st_netfd_open_socket(fd);
for {
    st_netfd_t client = st_accept(stfd, NULL, NULL, ST_UTIME_NO_TIMEOUT);
    st_thread_create(serverTCP, client, 0, 0);
}

// ST coroutine serving the TCP connection
void* serverTCP(void* arg) {
    st_netfd_t client = (st_netfd_t)arg;
    char buf[1024];
    int n = st_read(client, buf, sizeof(buf), ST_UTIME_NO_TIMEOUT);
}
```

```rust
//////////////////////////////////////////////////////////////////////////////
// RUST async
let listener = tokio::net::TcpListener::bind("0.0.0.0:8080").await.unwrap();
loop {
    let (socket, _) = listener.accept().await.unwrap();
    tokio::spawn(processTCP(socket));
}

// Handling the TCP connection using async
async fn processTCP(mut socket: tokio::net::TcpStream) {
    let mut buf = vec![0; 1024];
    let n = socket.read(&mut buf).await;
}
```

RUST Runtime Support: RUST’s runtime supports both single-threaded and multi-threaded environments, allowing tasks to potentially move between different threads when using spawn and await. In contrast, ST only supports thread-local operations, requiring the application layer to handle how tasks are passed, which is not yet supported. Go’s M and N model provides a sophisticated multi-threaded scheduler implemented at the language level, offering the highest level of support.

Performance Analysis: ST relies entirely on assembly for coroutine switching without a multithreading mechanism, with a codebase of around 5,000 lines. On the other hand, RUST’s tokio runtime delivers high performance without the impact of garbage collection. In the realm of high-performance servers, RUST tokio emerges as a suitable solution, especially given its third-party async library that allows for customization and modification. In terms of maintainability, RUST tokio outperforms ST.

Compatibility: Both RUST and Go excel in terms of compatibility. ST in SRS requires manual assembly for coroutine switching, necessitating adaptation to hardware registers and function call mechanisms when encountering new hardware and CPU chips. Particularly, ST currently lacks compatibility with Windows SEH exception mechanisms. RUST and Go support a wide range of systems and CPUs without compatibility issues.

## Next
Streaming media falls within the realm of media technology, which can vary across different countries. Some countries may regulate media, limiting the availability of services in the media sector. However, the global user and developer base of SRS confirms its practical value. Our efforts can continue to have a positive impact on this planet, making it worthwhile to persist in our endeavors. It’s unfortunate that nginx-rtmp ceased maintenance, despite having a larger user base compared to SRS.

The architecture of SRS primarily focuses on maintainability. Rather than adding more features, it is essential to listen to diverse voices from different countries and regions and explore various technological solutions and languages. I believe RUST could be an interesting pursuit. Even if it may not ultimately materialize, the process can yield many intriguing ideas, similar to how SRS incorporates many concepts from Go. Additionally, without in-person interactions, building an effective open-source community is challenging. It’s important to participate in open-source conferences and exchanges in different countries to foster such a community.

Maintaining SRS involves encountering numerous fascinating technical challenges, engaging discussions, and diverse viewpoints. While these joys may not translate into monetary gains or serve as a means of livelihood, solely focusing on survival can be stifling. Conversely, even if these joys do not directly contribute to survival, they can still warm the heart and bring fulfillment.
