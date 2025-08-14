---
slug: smart-pointer
title: SRS - Fix Memory Leaks with Smart Pointers
authors: []
tags: [srs, memory, smart-pointer]
custom_edit_url: null
---

# Fix Memory Leaks with Smart Pointers

After 8 years, we resolved the memory leak issue in SRS using our own implementation of basic Smart Pointers, 
maintaining the project's manageability.

## Introduction

Each stream on the SRS server has a Source object that manages the stream's lifecycle. To keep the logic and code 
simple, SRS did not release the Source object; with a large number of streams, such as constantly changing streaming 
addresses, this led to continuous memory growth and leaks.

<!--truncate-->

Previously, the workaround was to restart the service late at night. SRS supports [Gracefully Quit](https://github.com/ossrs/srs/issues/1579#issuecomment-587414898), 
which restarts the service when there are no connections, but this did not completely solve the problem.

To address this issue, we introduced Smart Pointers to manage the lifecycle of Source objects. We did not use C++ 11 
because SRS needs to compile in various environments. Instead, we implemented a simplified version of Smart Pointers, 
supporting only some features.

## Design

Before any work addressing this issue, we need refine the shared ptr of SRS, which is used in RTMP shared message, 
GB sessions, and in future source objects.

Refer to [Using C++11’s Smart Pointers](https://public.websites.umich.edu/~eecs381/handouts/C++11_smart_ptrs.pdf), 
there are three varieties of smart pointers:

* Shared ptr: Akin to `SrsSharedPtrMessage` used for RTMP message, and `SrsLazyObjectWrapper` for GB28181.
* Weak ptr: No such ptr in SRS before 5.0 version. We can avoid this by removing any circle reference of pointer.
* Unique ptr: The corresponding ptr is `SrsAutoFree` that free the ptr in the local scope.

If we carefully design and limit the usage of smart ptr, such as no cycle link that means we do not need weak ptr, 
we can archive a simple and easy to maintain smart ptr for SRS. We also consider bellow features:

* Inheritance: Do not use inheritance with smart pointer, although we can write correct code, but it's complex to discuss.
* Comparing: Do not support comparing smart pointers, because we only need to manage the memory automatically.
* make_shared: Do not support this helper function, because we do not consider performance issue by new operator.
* shared_from_this: Do not support this helper template, because we do not need this feature.
* make_unique: Do not support this, because we allow naked new operator, and carefully code review is required.

Again, we don't want to switch to C++11 because I think the modern C++ 11/14/17/20 is really a horrible thing that 
does not fix anything, but introduces a lot of bad grammar sugar. We do not want any grammar sugar, which is very 
harmful for communication. We  have experienced a lot of disaster of smart pointers and sugars, so we want to keep 
the usage very simple:

1. No grammar sugar: No weak ptr, no make shared, no make unique, no inheritance, no comparing, no circle reference, no C++11/14/17/20/22 etc.
2. Follow the C++ standard API if any API is really required, do not invent new wheels, no new functions and names.
3. Carefully code review, programmer should take responsibility for code and memory issues.

In the other hand, we should not reinvent wheels especially for smart ptr, so we must use almost the same API of 
C++ standard library. That is why we should learn from C++11 and refactor the similar smart ptrs in SRS.

Smart pointers might seem simple when they’re just about memory management, but when you delve into inheritance, 
copying, comparison, performance, circular references, pointer conversion, creation, and the purism of "no naked new", 
their capabilities become significantly richer. In reality, the main issues with C++ programs stem from programmers 
messing around, and this purism along with various syntactic sugar makes it hard to notice when they are.

Most problems can be addressed with something as straightforward as an AK47, yet there's an obsession with acquiring 
more advanced weaponry, which leads to a situation where, on the battlefield, you can’t even fire a shot due to jams 
and blowbacks. This is because it’s very hard to spot when programmers are messing about in companies, with large 
amounts of code being submitted, the usage of various new features, and tight deadlines.

The true gates of hell in C++ are not the limited old features of C, but its plethora of new features.

## Shared Ptr

We implemented a simple Shared Ptr that supports reference counting and automatic release, based on 
[#6834ec2](https://github.com/ossrs/srs/commit/6834ec208d67fa47c21536d1f1041bb6d60c1834).

```cpp
template<class T>
class SrsSharedPtr
{
private:
    // Pointer to the object.
    T* ptr_;
    // Reference count of the object.
    uint32_t* ref_count_;
```

Usage is similar to std::shared_ptr, but it supports fewer interfaces:

```cpp
SrsSharedPtr<MyClass> ptr(new MyClass());
ptr->do_something();

SrsSharedPtr<MyClass> cp = ptr;
cp->do_something();
```

It also involves copy constructor, assignment, and move constructor, as well as some operator overloading. 
For details, refer to the code and the unit test examples. Overall, this part is relatively simple to implement.

## Shared Resource

In addition to the Shared Ptr smart pointer, we also support Shared Resource, which mainly implements the 
ISrsResource interface and can be released by the ResourceManager. Refer to [#6834ec2](https://github.com/ossrs/srs/commit/6834ec208d67fa47c21536d1f1041bb6d60c1834).

```cpp
template<typename T>
class SrsSharedResource : public ISrsResource
{
private:
    SrsSharedPtr<T> ptr_;
```

In fact, Shared Resource and Shared Ptr have exactly the same interface, but we did not use inheritance; instead, 
we used composition. Using it is the same as using Shared Ptr, but it can be managed by a Manager:

```cpp
SrsSharedResource<MyClass>* ptr = new SrsSharedResource<MyClass>(new MyClass());
(*ptr)->do_something();

ISrsResourceManager* manager = ...;
manager->remove(ptr);
```

In practical application scenarios, resources are often managed by a coroutine. For example, a GB SIP connection 
will start a separate coroutine to serve this connection. Similarly, there are GB Media connections, GB Session 
sessions, RTC TCP connections, etc. We created an Executor to implement this common logic:

```cpp
SrsExecutorCoroutine::SrsExecutorCoroutine(ISrsResourceManager* m, ISrsResource* r)
{
    resource_ = r;
    manager_ = m;
    trd_ = new SrsSTCoroutine("ar", this, resource_->get_id());
}

SrsExecutorCoroutine::~SrsExecutorCoroutine()
{
    manager_->remove(resource_);
    srs_freep(trd_);
}

srs_error_t SrsExecutorCoroutine::cycle()
{
    srs_error_t err = handler_->cycle();
    manager_->remove(this);
    return err;
}
```

Therefore, we can easily use the Executor to manage this type of Resource:

```cpp
SrsGbSipTcpConn* raw_conn = new SrsGbSipTcpConn();
SrsSharedResource<SrsGbSipTcpConn>* conn = new SrsSharedResource<SrsGbSipTcpConn>(raw_conn);
SrsExecutorCoroutine* executor = new SrsExecutorCoroutine(_srs_gb_manager, conn, raw_conn, raw_conn);
if ((err = executor->start()) != srs_success) {
    srs_freep(executor);
    return srs_error_wrap(err, "gb sip");
}
```

It is worth emphasizing that the Executor and Smart Ptr are not inherently related; they are designed as 
a mechanism to solve a common scenario. Otherwise, multiple places would need to implement repetitive logic, 
making it more complex. Additionally, since the Executor references ISrsResource, we use the Resource Ptr 
pointer object instead of directly using the Shared Ptr pointer.

## GB28181

GB28181's SIP and Media are two separate protocols and transmission channels. This involves independent lifecycles 
and management of objects. Refer to [#4080](https://github.com/ossrs/srs/pull/4080) for details, as illustrated in 
the diagram below:

![](/img/blog-2024-06-15-01.png)

SIPTcpConn is the SIP object for GB, usually listening on TCP port 5060, responsible for device registration and 
information collection. MediaTcpConn is the media transmission object for GB, transmitting PS streams. The GB 
Session is created and managed by SIPTcpConn or MediaTcpConn objects.

In practice, Session, SIPTcpConn, and MediaTcpConn are managed as Shared Resource objects. The Session uses Shared 
Resource to reference SIPTcpConn and MediaTcpConn. However, SIPTcpConn and MediaTcpConn reference the Session using 
raw pointers, not Shared Resource, because we have not implemented Weak Ptr and do not support circular references.

## WebRTC over TCP

WebRTC over UDP is the default transport method. Since UDP is connectionless, SRS only needs an RTC Connection 
object to manage the RTC session. For WebRTC over TCP, an additional TCP connection is added, managed by `SrsRtcTcpConn`. 
Refer to [#4083](https://github.com/ossrs/srs/pull/4083).

`SrsRtcConnection` uses a Shared Resource to reference the `SrsRtcTcpConn` object, while `SrsRtcTcpConn` directly 
references the raw pointer of `SrsRtcConnection` because the lifecycle of `SrsRtcConnection` is longer.

```cpp
class SrsRtcTcpConn
{
private:
    // Since the session references this object, we should directly use the session pointer.
    SrsRtcConnection* session_;
};

class SrsRtcTcpNetwork: public ISrsRtcNetwork
{
private:
    SrsSharedResource<SrsRtcTcpConn> owner_;
};
```

Since we have not implemented `shared_from_this`, we need to pass the Shared Resource to `SrsRtcTcpConn`, 
which is ultimately managed by the Executor and `SrsRtcConnection`. As `SrsRtcTcpConn` starts a coroutine to 
serve this object, we use the Executor to manage it:

```cpp
resource = new SrsRtcTcpConn(new SrsTcpConnection(stfd2), ip, port);

SrsRtcTcpConn* raw_conn = dynamic_cast<SrsRtcTcpConn*>(resource);
SrsSharedResource<SrsRtcTcpConn>* conn = new SrsSharedResource<SrsRtcTcpConn>(raw_conn);
SrsExecutorCoroutine* executor = new SrsExecutorCoroutine(_srs_rtc_manager, conn, raw_conn, raw_conn);
if ((err = executor->start()) != srs_success) {
    srs_freep(executor);
    return srs_error_wrap(err, "start executor");
}
```

Before using Smart Ptr, we needed to release the references between `SrsRtcConnection` and `SrsRtcTcpConn` when 
either object was destroyed. With Smart Ptr, this logic is no longer needed; simply releasing `SrsRtcTcpConn` will 
automatically handle the cleanup.

## SRT Source

The SRT Source also involves cleanup, but it is relatively simple; just switch to using Shared Ptr. For details, 
refer to [#4084](https://github.com/ossrs/srs/pull/4084).

```cpp
class SrsSrtSourceManager
{
public:
    virtual srs_error_t fetch_or_create(SrsRequest* r, SrsSharedPtr<SrsSrtSource>& pps);
    virtual void eliminate(SrsRequest* r);
};

SrsSharedPtr<SrsSrtSource> srt;
if ((err = _srs_srt_sources->fetch_or_create(r, srt)) != srs_success) {
    return srs_error_wrap(err, "create source");
}
```

It is important to note that the Source actually manages the Consumer, so in the Consumer, we use the raw pointer
of the Source:

```cpp
class SrsSrtConsumer
{
private:
    // Because source references to this object, so we should directly use the source ptr.
    SrsSrtSource* source_;
};
```

Release the Source object when there is no streaming or playback:

```cpp
void SrsSrtSource::on_consumer_destroy(SrsSrtConsumer* consumer)
{
    // Destroy and cleanup source when no publishers and consumers.
    if (can_publish_ && consumers.empty()) {
        _srs_srt_sources->eliminate(req);
    }
}

void SrsSrtSource::on_unpublish()
{
    // Destroy and cleanup source when no publishers and consumers.
    if (can_publish_ && consumers.empty()) {
        _srs_srt_sources->eliminate(req);
    }
}
```

The logic for releasing the Live Source is slightly different (it will be the same in the future), but the use of 
smart pointers is consistent.

## WebRTC Source

Similar to SRT, the WebRTC Source is also relatively simple and only needs to be modified to use a shared pointer. 
For more details, refer to [#4085](https://github.com/ossrs/srs/pull/4085).

```cpp
class SrsRtcSourceManager
{
public:
    virtual srs_error_t fetch_or_create(SrsRequest* r, SrsSharedPtr<SrsRtcSource>& pps);
    virtual SrsSharedPtr<SrsRtcSource> fetch(SrsRequest* r);
    virtual void eliminate(SrsRequest* r);
};
```

It is important to note that the Source actually manages the Consumer, so in the Consumer, we use the raw pointer 
of the Source:

```cpp
class SrsRtcConsumer
{
private:
    // Because source references to this object, so we should directly use the source ptr.
    SrsRtcSource* source_;
};
```

The WebRTC Source Manager supports a `fetch` API, which might return a null pointer, for example:

```cpp
SrsSharedPtr<SrsRtcSource> source = _srs_rtc_sources->fetch(ruc->req_);
is_rtc_stream_active = (source.get() && !source->can_publish());
```

When there is no publishing or playing, release the Source object:

```cpp
void SrsRtcSource::on_consumer_destroy(SrsRtcConsumer* consumer)
{
    // Destroy and cleanup source when no publishers and consumers.
    if (!is_created_ && consumers.empty()) {
        _srs_rtc_sources->eliminate(req);
    }
}

void SrsRtcSource::on_unpublish()
{
    // Destroy and cleanup source when no publishers and consumers.
    if (!is_created_ && consumers.empty()) {
        _srs_rtc_sources->eliminate(req);
    }
}
```

The logic for releasing the Live Source is slightly different (it will be made the same in the future), but the 
use of smart pointers is consistent.

## Live Source

Live Source is the most complex improvement, primarily because HTTP Streaming also uses Source. For more details, 
refer to [#4089](https://github.com/ossrs/srs/pull/4089).

![](/img/blog-2024-06-15-02.png)

Therefore, we need to remove some unnecessary references to Source and instead get it from the manager:

```cpp
srs_error_t SrsLiveStream::do_serve_http(ISrsHttpResponseWriter* w, ISrsHttpMessage* r)
{
    SrsSharedPtr<SrsLiveSource> live_source = _srs_sources->fetch(req);
    if (!live_source.get()) {
        return srs_error_new(ERROR_NO_SOURCE, "no source for %s", req->get_stream_url().c_str());
    }
};
```

The manager is defined as follows. Note that it uses a Timer to check and release Source, which is different from the 
logic in SRT/RTC. This will be unified in the future:

```cpp
class SrsLiveSourceManager : public ISrsHourGlass
{
public:
    virtual srs_error_t fetch_or_create(SrsRequest* r, ISrsLiveSourceHandler* h, SrsSharedPtr<SrsLiveSource>& pps);
    virtual SrsSharedPtr<SrsLiveSource> fetch(SrsRequest* r);
};
```

It is important to note that Source actually manages objects like Consumer/OriginHub/Edge, so we use raw pointers to 
Source in these objects:

```cpp
class SrsEdgeIngester : public ISrsCoroutineHandler
{
private:
    // Because source references this object, we should directly use the source ptr.
    SrsLiveSource* source_;
};

class SrsEdgeForwarder : public ISrsCoroutineHandler
{
private:
    // Because source references this object, we should directly use the source ptr.
    SrsLiveSource* source_;
};

class SrsLiveConsumer : public ISrsWakable
{
private:
    // Because source references this object, we should directly use the source ptr.
    SrsLiveSource* source_;
};

class SrsOriginHub : public ISrsReloadHandler
{
private:
    // Because source references this object, we should directly use the source ptr.
    SrsLiveSource* source_;
};
```

The manager uses a timer to check and release Source:

```cpp
srs_error_t SrsLiveSourceManager::notify(int event, srs_utime_t interval, srs_utime_t tick)
{
    srs_error_t err = srs_success;

    std::map< std::string, SrsSharedPtr<SrsLiveSource> >::iterator it;
    for (it = pool.begin(); it != pool.end();) {
        SrsSharedPtr<SrsLiveSource>& source = it->second;

        if (source->stream_is_dead()) {
            const SrsContextId& cid = source->source_id();
            srs_trace("cleanup dead source, id=[%s], total=%d", cid.c_str(), (int)pool.size());
            pool.erase(it++);
        } else {
            ++it;
        }
    }

    return err;
}
```

This release logic is different from SRT/RTC, but it will be unified in the future. However, the use of smart 
pointers is consistent.

## Conclusion

To simplify the process, SRS initially did not release the Source object. By introducing Smart Pointers, this issue 
was finally resolved. By applying business constraints and combining raw pointers with Smart Pointers, we avoided the 
complexity of multiple Smart Pointer functionalities. Overall, the complexity introduced by this change is manageable, 
and future maintainability is also good.

## Contact

Welcome for more discussion at [discord](https://discord.gg/bQUPDRqy79).

![](https://ossrs.io/gif/v1/sls.gif?site=ossrs.io&path=/lts/blog-en/24-06-15-SRS-Smart-Pointer)

