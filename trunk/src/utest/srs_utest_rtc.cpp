//
// Copyright (c) 2013-2023 The SRS Authors
//
// SPDX-License-Identifier: MIT or MulanPSL-2.0
//
#include <srs_utest_rtc.hpp>

#include <srs_kernel_error.hpp>
#include <srs_core_autofree.hpp>
#include <srs_app_rtc_queue.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_kernel_codec.hpp>
#include <srs_app_conn.hpp>

#include <srs_utest_service.hpp>

#include <vector>
using namespace std;

VOID TEST(KernelRTCTest, RtpSTAPPayloadException)
{
    srs_error_t err = srs_success;

    unsigned char rtp_pkt[328] = {
        0x90, 0xe0, 0x65, 0x8d, 0x37, 0xbc, 0x20, 0xb7, 0xd8, 0xf7, 0xae, 0x77, 0xbe, 0xde, 0x00, 0x03,
        0x51, 0x06, 0x4f, 0xd5, 0x2f, 0x0e, 0xe1, 0x90, 0x75, 0xc3, 0x00, 0x00, 0xd8, 0x01, 0x00, 0x03,
        0xef, 0x93, 0xc7, 0x6a, 0x23, 0x45, 0xdc, 0xb0, 0xce, 0x2b, 0x51, 0x1a, 0x8a, 0xd1, 0x35, 0xab,
        0x11, 0xa7, 0x15, 0xc4, 0xd6, 0xe4, 0x5d, 0x12, 0x6c, 0x04, 0x86, 0x25, 0xd3, 0x88, 0x76, 0xa2,
        0xb8, 0x58, 0x47, 0x0d, 0x0a, 0xd6, 0x2b, 0x85, 0x04, 0x6a, 0x09, 0x2a, 0x4a, 0xce, 0x22, 0xa2,
        0x05, 0x78, 0x8e, 0x71, 0x5c, 0x22, 0x23, 0x58, 0x9e, 0x16, 0x15, 0xe1, 0x5f, 0xff, 0xfd, 0x32,
        0x0a, 0xe2, 0xb8, 0xea, 0xd6, 0xba, 0xd5, 0x7e, 0x5a, 0xd6, 0x61, 0x1c, 0x82, 0x38, 0xce, 0x4a,
        0xd7, 0xe2, 0xea, 0xaa, 0xab, 0xa8, 0x83, 0xf6, 0x7f, 0x10, 0xf1, 0x7c, 0x55, 0x4d, 0xeb, 0xaa,
        0xf8, 0xfd, 0x35, 0xaa, 0xeb, 0x59, 0x8e, 0xf8, 0x8f, 0x12, 0xb9, 0xdd, 0x39, 0xfa, 0x3f, 0x62,
        0x9e, 0x23, 0x96, 0xab, 0x5e, 0xc4, 0xce, 0x97, 0x55, 0x43, 0x65, 0x29, 0xde, 0x8f, 0xe2, 0xb9,
        0x0f, 0xb8, 0xd0, 0xee, 0x00, 0x31, 0x35, 0xdb, 0x5a, 0xff, 0xff, 0xf8, 0x10, 0xa9, 0x3c, 0xf7,
        0x90, 0x8c, 0xf7, 0x3f, 0x5f, 0xd7, 0x15, 0xac, 0xee, 0xa8, 0xfe, 0x23, 0x84, 0x8b, 0xe6, 0x97,
        0x2a, 0x61, 0x38, 0xba, 0xd3, 0xee, 0x7b, 0x49, 0xfa, 0x81, 0xcb, 0x3f, 0x72, 0xd5, 0x56, 0x8f,
        0xe7, 0x7b, 0x1d, 0xda, 0x85, 0x71, 0xbc, 0x45, 0x75, 0x5d, 0x55, 0x47, 0xc5, 0xf5, 0x36, 0xe4,
        0xa9, 0x17, 0x4a, 0x84, 0xf9, 0xdd, 0xd0, 0xa5, 0xb1, 0xcf, 0x69, 0xcf, 0xcd, 0x1d, 0xac, 0xe4,
        0xc6, 0x3d, 0xd0, 0x95, 0xa3, 0xbd, 0x0a, 0xd4, 0xa2, 0xb9, 0x05, 0x78, 0xae, 0x5a, 0x92, 0xb5,
        0x90, 0x4b, 0xa6, 0x85, 0x3c, 0x27, 0xb3, 0x4d, 0xd2, 0x5c, 0xfa, 0x61, 0x01, 0x4a, 0xa6, 0xd9,
        0x26, 0xf3, 0x78, 0x44, 0x57, 0x2e, 0x79, 0xc5, 0x71, 0x42, 0xb5, 0x34, 0x87, 0x94, 0x57, 0x8a,
        0xe1, 0x09, 0xb3, 0x8a, 0xe7, 0x0b, 0x7f, 0xfc, 0xff, 0xec, 0x28, 0xe3, 0x4c, 0xff, 0xff, 0xa6,
        0x6a, 0xca, 0x2b, 0x84, 0xab, 0x0a, 0xd7, 0xf1, 0xf5, 0x9a, 0x47, 0x08, 0x54, 0xd5, 0xac, 0x9a,
        0xf5, 0x09, 0x5a, 0x29, 0x35, 0x52, 0x79, 0xe0,
    };

    int nb_buf = sizeof(rtp_pkt);
    SrsBuffer buf((char*)rtp_pkt, nb_buf);

    SrsRtpHeader header;
    EXPECT_TRUE((err = header.decode(&buf)) == srs_success);

    // We must skip the padding bytes before parsing payload.
    uint8_t padding = header.get_padding();
    EXPECT_TRUE(buf.require(padding));
    buf.set_size(buf.size() - padding);

    SrsAvcNaluType nalu_type = SrsAvcNaluTypeReserved;
    // Try to parse the NALU type for video decoder.
    if (!buf.empty()) {
        nalu_type = SrsAvcNaluType((uint8_t)(buf.head()[0] & kNalTypeMask));
    }

    EXPECT_TRUE(nalu_type == kStapA);
    ISrsRtpPayloader* payload = new SrsRtpSTAPPayload();

    HELPER_ASSERT_FAILED(payload->decode(&buf));
    srs_freep(payload);
}

class MockResource : public ISrsDisposingHandler, public ISrsResource
{
public:
    SrsResourceManager* manager_;
    MockResource(SrsResourceManager* manager) {
        manager_ = manager;
        if (manager_) {
            manager_->subscribe(this);
        }
    }
    virtual ~MockResource() {
        if (manager_) {
            manager_->unsubscribe(this);
        }
    }
    virtual const SrsContextId& get_id() {
        return _srs_context->get_id();
    }
    virtual std::string desc() {
        return "";
    }
};

class MockResourceHookOwner : public MockResource
{
public:
    ISrsResource* owner_;
    MockResourceHookOwner(SrsResourceManager* manager) : MockResource(manager) {
        owner_ = NULL;
    }
    virtual ~MockResourceHookOwner() {
    }
    virtual void on_before_dispose(ISrsResource* c) {
        if (c == owner_) { // Remove self if its owner is disposing.
            manager_->remove(this);
        }
    }
    virtual void on_disposing(ISrsResource* c) {
    }
};

class MockResourceSelf : public MockResource
{
public:
    bool remove_in_before_dispose;
    bool remove_in_disposing;
    MockResourceSelf(SrsResourceManager* manager) : MockResource(manager) {
        remove_in_before_dispose = remove_in_disposing = false;
    }
    virtual ~MockResourceSelf() {
    }
    virtual void on_before_dispose(ISrsResource* c) {
        if (remove_in_before_dispose) {
            manager_->remove(this);
        }
    }
    virtual void on_disposing(ISrsResource* c) {
        if (remove_in_disposing) {
            manager_->remove(this);
        }
    }
};

class MockResourceUnsubscribe : public MockResource
{
public:
    int nn_before_dispose;
    int nn_disposing;
    bool unsubscribe_in_before_dispose;
    bool unsubscribe_in_disposing;
    MockResourceUnsubscribe* result;
    MockResourceUnsubscribe(SrsResourceManager* manager) : MockResource(manager) {
        unsubscribe_in_before_dispose = unsubscribe_in_disposing = false;
        nn_before_dispose = nn_disposing = 0;
        result = NULL;
    }
    virtual ~MockResourceUnsubscribe() {
        if (result) { // Copy result before disposing it.
            *result = *this;
        }
    }
    virtual void on_before_dispose(ISrsResource* c) {
        nn_before_dispose++;
        if (unsubscribe_in_before_dispose) {
            manager_->unsubscribe(this);
        }
    }
    virtual void on_disposing(ISrsResource* c) {
        nn_disposing++;
        if (unsubscribe_in_disposing) {
            manager_->unsubscribe(this);
        }
    }
};

VOID TEST(KernelRTCTest, ConnectionManagerTest)
{
    srs_error_t err = srs_success;

    // When notifying, the handlers changed, disposing event may lost.
    if (true) {
        SrsResourceManager manager("mgr");
        HELPER_EXPECT_SUCCESS(manager.start());
        EXPECT_EQ(0, (int)manager.size()); EXPECT_TRUE(manager.empty());

        MockResourceUnsubscribe* conn0 = new MockResourceUnsubscribe(&manager);
        conn0->unsubscribe_in_disposing = true;
        manager.add(conn0);

        MockResourceUnsubscribe* conn1 = new MockResourceUnsubscribe(&manager);
        manager.add(conn1);

        MockResourceUnsubscribe* conn2 = new MockResourceUnsubscribe(&manager);
        manager.add(conn2);

        // When removing conn0, it will unsubscribe and change the handlers,
        // which should not cause the conn1 lost event.
        manager.remove(conn0);
        srs_usleep(0);
        ASSERT_EQ(2, (int)manager.size());

        EXPECT_EQ(1, conn1->nn_before_dispose);
        EXPECT_EQ(1, conn1->nn_disposing); // Should get event.

        EXPECT_EQ(1, conn2->nn_before_dispose);
        EXPECT_EQ(1, conn2->nn_disposing);
    }

    // When notifying, the handlers changed, before-dispose event may lost.
    if (true) {
        SrsResourceManager manager("mgr");
        HELPER_EXPECT_SUCCESS(manager.start());
        EXPECT_EQ(0, (int)manager.size()); EXPECT_TRUE(manager.empty());

        MockResourceUnsubscribe* conn0 = new MockResourceUnsubscribe(&manager);
        conn0->unsubscribe_in_before_dispose = true;
        manager.add(conn0);

        MockResourceUnsubscribe* conn1 = new MockResourceUnsubscribe(&manager);
        manager.add(conn1);

        MockResourceUnsubscribe* conn2 = new MockResourceUnsubscribe(&manager);
        manager.add(conn2);

        // When removing conn0, it will unsubscribe and change the handlers,
        // which should not cause the conn1 lost event.
        manager.remove(conn0);
        srs_usleep(0);
        ASSERT_EQ(2, (int)manager.size());

        EXPECT_EQ(1, conn1->nn_before_dispose); // Should get event.
        EXPECT_EQ(1, conn1->nn_disposing);

        EXPECT_EQ(1, conn2->nn_before_dispose);
        EXPECT_EQ(1, conn2->nn_disposing);
    }

    // Subscribe or unsubscribe for multiple times.
    if (true) {
        SrsResourceManager manager("mgr");
        HELPER_EXPECT_SUCCESS(manager.start());
        EXPECT_EQ(0, (int)manager.size()); EXPECT_TRUE(manager.empty());

        MockResourceUnsubscribe* resource = new MockResourceUnsubscribe(&manager);
        resource->unsubscribe_in_before_dispose = true;
        manager.add(resource);

        MockResourceUnsubscribe result(NULL); // No manager for result.
        resource->result = &result;

        manager.remove(resource);
        srs_usleep(0);
        ASSERT_EQ(0, (int)manager.size());

        EXPECT_EQ(1, result.nn_before_dispose);
        EXPECT_EQ(0, result.nn_disposing); // No disposing event, because we unsubscribe in before-dispose.
    }

    // Count the event for disposing.
    if (true) {
        SrsResourceManager manager("mgr");
        HELPER_EXPECT_SUCCESS(manager.start());
        EXPECT_EQ(0, (int)manager.size()); EXPECT_TRUE(manager.empty());

        MockResourceUnsubscribe* resource = new MockResourceUnsubscribe(&manager);
        manager.add(resource);

        MockResourceUnsubscribe result(NULL); // No manager for result.
        resource->result = &result;

        manager.remove(resource);
        srs_usleep(0);
        ASSERT_EQ(0, (int)manager.size());

        EXPECT_EQ(1, result.nn_before_dispose);
        EXPECT_EQ(1, result.nn_disposing);
    }

    // When hooks disposing, remove itself again.
    if (true) {
        SrsResourceManager manager("mgr");
        HELPER_EXPECT_SUCCESS(manager.start());
        EXPECT_EQ(0, (int)manager.size()); EXPECT_TRUE(manager.empty());

        MockResourceSelf* resource = new MockResourceSelf(&manager);
        resource->remove_in_disposing = true;
        manager.add(resource);
        EXPECT_EQ(1, (int)manager.size());

        manager.remove(resource);
        srs_usleep(0);
        ASSERT_EQ(0, (int)manager.size());
    }

    // When hooks before-dispose, remove itself again.
    if (true) {
        SrsResourceManager manager("mgr");
        HELPER_EXPECT_SUCCESS(manager.start());
        EXPECT_EQ(0, (int)manager.size()); EXPECT_TRUE(manager.empty());

        MockResourceSelf* resource = new MockResourceSelf(&manager);
        resource->remove_in_before_dispose = true;
        manager.add(resource);
        EXPECT_EQ(1, (int)manager.size());

        manager.remove(resource);
        srs_usleep(0);
        ASSERT_EQ(0, (int)manager.size());
    }

    // Cover all normal scenarios.
    if (true) {
        SrsResourceManager manager("mgr", true);
        HELPER_EXPECT_SUCCESS(manager.start());
        EXPECT_EQ(0, (int)manager.size()); EXPECT_TRUE(manager.empty());

        // Resource without id or name.
        manager.add_with_id("100", new MockSrsConnection());
        manager.add_with_id("101", new MockSrsConnection());
        manager.add_with_name("srs", new MockSrsConnection());
        manager.add_with_name("av", new MockSrsConnection());
        ASSERT_EQ(4, (int)manager.size());

        manager.remove(manager.at(3));
        manager.remove(manager.at(2));
        manager.remove(manager.at(1));
        manager.remove(manager.at(0));
        srs_usleep(0);
        ASSERT_EQ(0, (int)manager.size());
    }

    // Callback: Remove worker when its master is disposing.
    if (true) {
        SrsResourceManager manager("mgr");
        HELPER_EXPECT_SUCCESS(manager.start());
        EXPECT_EQ(0, (int)manager.size()); EXPECT_TRUE(manager.empty());

        MockResourceHookOwner* master = new MockResourceHookOwner(&manager);
        manager.add(master);
        EXPECT_EQ(1, (int)manager.size());

        MockResourceHookOwner* worker = new MockResourceHookOwner(&manager);
        worker->owner_ = master; // When disposing master, worker will hook the event and remove itself.
        manager.add(worker);
        EXPECT_EQ(2, (int)manager.size());

        manager.remove(master);
        srs_usleep(0); // Trigger the disposing.

        // Both master and worker should be disposed.
        EXPECT_EQ(0, (int)manager.size()); EXPECT_TRUE(manager.empty());
    }

    // Normal scenario, free object by manager.
    if (true) {
        SrsResourceManager manager("mgr");
        HELPER_EXPECT_SUCCESS(manager.start());
        EXPECT_EQ(0, (int)manager.size()); EXPECT_TRUE(manager.empty());

        MockSrsConnection* conn = new MockSrsConnection();
        manager.add(conn);
        EXPECT_EQ(1, (int)manager.size()); EXPECT_FALSE(manager.empty());

        manager.remove(conn);
        srs_usleep(0); // Switch context for manager to dispose connections.
        EXPECT_EQ(0, (int)manager.size()); EXPECT_TRUE(manager.empty());
    }

    // Resource with id or name.
    if (true) {
        SrsResourceManager manager("mgr");
        HELPER_EXPECT_SUCCESS(manager.start());
        EXPECT_EQ(0, (int)manager.size()); EXPECT_TRUE(manager.empty());

        // Resource without id or name.
        MockSrsConnection* conn = new MockSrsConnection();
        manager.add(conn);
        ASSERT_EQ(1, (int)manager.size());
        EXPECT_TRUE(manager.at(0));
        EXPECT_TRUE(!manager.at(1));
        EXPECT_TRUE(!manager.find_by_id("100"));
        EXPECT_TRUE(!manager.find_by_name("srs"));

        manager.remove(conn);
        srs_usleep(0);
        ASSERT_EQ(0, (int)manager.size());

        // Resource with id.
        if (true) {
            MockSrsConnection* id = new MockSrsConnection();
            manager.add_with_id("100", id);
            EXPECT_EQ(1, (int)manager.size());
            EXPECT_TRUE(manager.find_by_id("100"));
            EXPECT_TRUE(!manager.find_by_id("101"));
            EXPECT_TRUE(!manager.find_by_name("100"));

            manager.remove(id);
            srs_usleep(0);
            ASSERT_EQ(0, (int)manager.size());
        }

        // Resource with name.
        if (true) {
            MockSrsConnection* name = new MockSrsConnection();
            manager.add_with_name("srs", name);
            EXPECT_EQ(1, (int)manager.size());
            EXPECT_TRUE(manager.find_by_name("srs"));
            EXPECT_TRUE(!manager.find_by_name("srs0"));
            EXPECT_TRUE(!manager.find_by_id("srs"));

            manager.remove(name);
            srs_usleep(0);
            ASSERT_EQ(0, (int)manager.size());
        }

        // Resource with id and name.
        if (true) {
            MockSrsConnection* id_name = new MockSrsConnection();
            manager.add_with_id("100", id_name);
            manager.add_with_id("200", id_name);
            manager.add_with_name("srs", id_name);
            manager.add_with_name("av", id_name);
            EXPECT_EQ(1, (int)manager.size());
            EXPECT_TRUE(manager.find_by_name("srs"));
            EXPECT_TRUE(manager.find_by_name("av"));
            EXPECT_TRUE(manager.find_by_id("100"));
            EXPECT_TRUE(manager.find_by_id("200"));
            EXPECT_TRUE(!manager.find_by_name("srs0"));
            EXPECT_TRUE(!manager.find_by_id("101"));

            manager.remove(id_name);
            srs_usleep(0);
            ASSERT_EQ(0, (int)manager.size());
        }

        // Resource with same id or name.
        if (true) {
            MockSrsConnection* conn0 = new MockSrsConnection();
            MockSrsConnection* conn1 = new MockSrsConnection();
            manager.add_with_id("100", conn0);
            manager.add_with_id("100", conn1);

            EXPECT_TRUE(conn0 != manager.find_by_id("100"));
            EXPECT_TRUE(conn1 == manager.find_by_id("100"));

            manager.remove(conn0);
            srs_usleep(0);
            ASSERT_EQ(1, (int)manager.size());

            manager.remove(conn1);
            srs_usleep(0);
            ASSERT_EQ(0, (int)manager.size());
        }
    }

    // Coroutine switch context, signal is lost.
    if (true) {
        SrsResourceManager manager("mgr");
        HELPER_EXPECT_SUCCESS(manager.start());
        EXPECT_EQ(0, (int)manager.size()); EXPECT_TRUE(manager.empty());

        if (true) { // First connection, which will switch context when deleting.
            MockSrsConnection* conn = new MockSrsConnection();
            conn->do_switch = true;
            manager.add(conn);
            EXPECT_EQ(1, (int)manager.size()); EXPECT_EQ(0, (int)manager.zombies_.size());

            manager.remove(conn); // Remove conn to zombies.
            EXPECT_EQ(1, (int)manager.size()); EXPECT_EQ(1, (int)manager.zombies_.size());

            srs_usleep(0); // Switch to manager coroutine to try to free zombies.
            EXPECT_EQ(0, (int)manager.size()); EXPECT_EQ(0, (int)manager.zombies_.size());
        }

        if (true) { // Now the previous conn switch back to here, and lost the signal.
            MockSrsConnection* conn = new MockSrsConnection();
            manager.add(conn);
            EXPECT_EQ(1, (int)manager.size()); EXPECT_EQ(0, (int)manager.zombies_.size());

            manager.remove(conn); // Remove conn to zombies, signal is lost.
            EXPECT_EQ(1, (int)manager.size()); EXPECT_EQ(1, (int)manager.zombies_.size());

            srs_usleep(0); // Switch to manager, but no signal is triggered before, so conn will be freed by loop.
            EXPECT_EQ(0, (int)manager.size()); EXPECT_EQ(0, (int)manager.zombies_.size());
        }
    }
}

VOID TEST(KernelRTCTest, StringDumpHexTest)
{
    // Typical normal case.
    if (false) {
        char data[8];
        data[0] = (char)0x3c; data[sizeof(data) - 2] = (char)0x67; data[sizeof(data) - 1] = (char)0xc3;
        string r = srs_string_dumps_hex(data, sizeof(data), INT_MAX, 0, 0, 0);
        EXPECT_EQ(16, (int)r.length());
        EXPECT_EQ('3', r.at(0)); EXPECT_EQ('c', r.at(1));
        EXPECT_EQ('c', r.at(r.length() - 2)); EXPECT_EQ('3', r.at(r.length() - 1));
        EXPECT_EQ('6', r.at(r.length() - 4)); EXPECT_EQ('7', r.at(r.length() - 3));
    }

    // Fill all buffer.
    if (false) {
        char data[8 * 1024];
        data[0] = (char)0x3c; data[sizeof(data) - 2] = (char)0x67; data[sizeof(data) - 1] = (char)0xc3;
        string r = srs_string_dumps_hex(data, sizeof(data), INT_MAX, 0, 0, 0);
        EXPECT_EQ(16 * 1024, (int)r.length());
        EXPECT_EQ('3', r.at(0)); EXPECT_EQ('c', r.at(1));
        EXPECT_EQ('c', r.at(r.length() - 2)); EXPECT_EQ('3', r.at(r.length() - 1));
        EXPECT_EQ('6', r.at(r.length() - 4)); EXPECT_EQ('7', r.at(r.length() - 3));
    }

    // Overflow 1 byte.
    if (true) {
        char data[8 * 1024 + 1];
        data[0] = (char)0x3c; data[sizeof(data) - 2] = (char)0x67; data[sizeof(data) - 1] = (char)0xc3;
        string r = srs_string_dumps_hex(data, sizeof(data), INT_MAX, 0, 0, 0);
        EXPECT_EQ(16 * 1024, (int)r.length());
        EXPECT_EQ('3', r.at(0)); EXPECT_EQ('c', r.at(1));
        EXPECT_EQ('6', r.at(r.length() - 2)); EXPECT_EQ('7', r.at(r.length() - 1));
    }

    // Fill all buffer, with seperator.
    if (true) {
        char data[5461];
        data[0] = (char)0x3c; data[sizeof(data) - 2] = (char)0x67; data[sizeof(data) - 1] = (char)0xc3;
        string r = srs_string_dumps_hex(data, sizeof(data), INT_MAX, ',', 0, 0);
        EXPECT_EQ(16383 - 1, (int)r.length());
        EXPECT_EQ('3', r.at(0)); EXPECT_EQ('c', r.at(1));
        EXPECT_EQ('c', r.at(r.length() - 2)); EXPECT_EQ('3', r.at(r.length() - 1));
        EXPECT_EQ('6', r.at(r.length() - 5)); EXPECT_EQ('7', r.at(r.length() - 4));
    }

    // Overflow 1 byte, with seperator.
    if (true) {
        char data[5461 + 1];
        data[0] = (char)0x3c; data[sizeof(data) - 2] = (char)0x67; data[sizeof(data) - 1] = (char)0xc3;
        string r = srs_string_dumps_hex(data, sizeof(data), INT_MAX, ',', 0, 0);
        EXPECT_EQ(16383 - 1, (int)r.length());
        EXPECT_EQ('3', r.at(0)); EXPECT_EQ('c', r.at(1));
        EXPECT_EQ('6', r.at(r.length() - 2)); EXPECT_EQ('7', r.at(r.length() - 1));
    }

    // Overflow 1 byte, with seperator and newline.
    if (true) {
        char data[5461 + 1];
        data[0] = (char)0x3c; data[sizeof(data) - 2] = (char)0x67; data[sizeof(data) - 1] = (char)0xc3;
        string r = srs_string_dumps_hex(data, sizeof(data), INT_MAX, ',', 5461, '\n');
        EXPECT_EQ(16383 - 1, (int)r.length());
        EXPECT_EQ('3', r.at(0)); EXPECT_EQ('c', r.at(1));
        EXPECT_EQ('6', r.at(r.length() - 2)); EXPECT_EQ('7', r.at(r.length() - 1));
    }
}

VOID TEST(KernelRTCTest, SequenceCompare)
{
    if (true) {
        EXPECT_EQ(0, srs_rtp_seq_distance(0, 0));
        EXPECT_EQ(0, srs_rtp_seq_distance(1, 1));
        EXPECT_EQ(0, srs_rtp_seq_distance(3, 3));

        EXPECT_EQ(1, srs_rtp_seq_distance(0, 1));
        EXPECT_EQ(-1, srs_rtp_seq_distance(1, 0));
        EXPECT_EQ(1, srs_rtp_seq_distance(65535, 0));
    }

    if (true) {
        EXPECT_FALSE(srs_rtp_seq_distance(1, 1) > 0);
        EXPECT_TRUE(srs_rtp_seq_distance(65534, 65535) > 0);
        EXPECT_TRUE(srs_rtp_seq_distance(0, 1) > 0);
        EXPECT_TRUE(srs_rtp_seq_distance(255, 256) > 0);

        EXPECT_TRUE(srs_rtp_seq_distance(65535, 0) > 0);
        EXPECT_TRUE(srs_rtp_seq_distance(65280, 0) > 0);
        EXPECT_TRUE(srs_rtp_seq_distance(65535, 255) > 0);
        EXPECT_TRUE(srs_rtp_seq_distance(65280, 255) > 0);

        EXPECT_FALSE(srs_rtp_seq_distance(0, 65535) > 0);
        EXPECT_FALSE(srs_rtp_seq_distance(0, 65280) > 0);
        EXPECT_FALSE(srs_rtp_seq_distance(255, 65535) > 0);
        EXPECT_FALSE(srs_rtp_seq_distance(255, 65280) > 0);

        // Note that srs_rtp_seq_distance(0, 32768)>0 is TRUE by https://mp.weixin.qq.com/s/JZTInmlB9FUWXBQw_7NYqg
        //      but for WebRTC jitter buffer it's FALSE and we follow it.
        EXPECT_FALSE(srs_rtp_seq_distance(0, 32768) > 0);
        // It's FALSE definitely.
        EXPECT_FALSE(srs_rtp_seq_distance(32768, 0) > 0);
    }

    if (true) {
        EXPECT_FALSE(srs_seq_is_newer(1, 1));
        EXPECT_TRUE(srs_seq_is_newer(65535, 65534));
        EXPECT_TRUE(srs_seq_is_newer(1, 0));
        EXPECT_TRUE(srs_seq_is_newer(256, 255));

        EXPECT_TRUE(srs_seq_is_newer(0, 65535));
        EXPECT_TRUE(srs_seq_is_newer(0, 65280));
        EXPECT_TRUE(srs_seq_is_newer(255, 65535));
        EXPECT_TRUE(srs_seq_is_newer(255, 65280));

        EXPECT_FALSE(srs_seq_is_newer(65535, 0));
        EXPECT_FALSE(srs_seq_is_newer(65280, 0));
        EXPECT_FALSE(srs_seq_is_newer(65535, 255));
        EXPECT_FALSE(srs_seq_is_newer(65280, 255));

        EXPECT_FALSE(srs_seq_is_newer(32768, 0));
        EXPECT_FALSE(srs_seq_is_newer(0, 32768));
    }

    if (true) {
        EXPECT_FALSE(srs_seq_distance(1, 1) > 0);
        EXPECT_TRUE(srs_seq_distance(65535, 65534) > 0);
        EXPECT_TRUE(srs_seq_distance(1, 0) > 0);
        EXPECT_TRUE(srs_seq_distance(256, 255) > 0);

        EXPECT_TRUE(srs_seq_distance(0, 65535) > 0);
        EXPECT_TRUE(srs_seq_distance(0, 65280) > 0);
        EXPECT_TRUE(srs_seq_distance(255, 65535) > 0);
        EXPECT_TRUE(srs_seq_distance(255, 65280) > 0);

        EXPECT_FALSE(srs_seq_distance(65535, 0) > 0);
        EXPECT_FALSE(srs_seq_distance(65280, 0) > 0);
        EXPECT_FALSE(srs_seq_distance(65535, 255) > 0);
        EXPECT_FALSE(srs_seq_distance(65280, 255) > 0);

        EXPECT_FALSE(srs_seq_distance(32768, 0) > 0);
        EXPECT_FALSE(srs_seq_distance(0, 32768) > 0);
    }

    if (true) {
        EXPECT_FALSE(srs_seq_is_rollback(1, 1));
        EXPECT_FALSE(srs_seq_is_rollback(65535, 65534));
        EXPECT_FALSE(srs_seq_is_rollback(1, 0));
        EXPECT_FALSE(srs_seq_is_rollback(256, 255));

        EXPECT_TRUE(srs_seq_is_rollback(0, 65535));
        EXPECT_TRUE(srs_seq_is_rollback(0, 65280));
        EXPECT_TRUE(srs_seq_is_rollback(255, 65535));
        EXPECT_TRUE(srs_seq_is_rollback(255, 65280));

        EXPECT_FALSE(srs_seq_is_rollback(65535, 0));
        EXPECT_FALSE(srs_seq_is_rollback(65280, 0));
        EXPECT_FALSE(srs_seq_is_rollback(65535, 255));
        EXPECT_FALSE(srs_seq_is_rollback(65280, 255));

        EXPECT_FALSE(srs_seq_is_rollback(32768, 0));
        EXPECT_FALSE(srs_seq_is_rollback(0, 32768));
    }
}

VOID TEST(KernelRTCTest, DecodeHeaderWithPadding)
{
    srs_error_t err = srs_success;

    // RTP packet cipher with padding.
    uint8_t data[] = {
        0xb0, 0x66, 0x0a, 0x97, 0x7e, 0x32, 0x10, 0xee, 0x7d, 0xe6, 0xd0, 0xe6, 0xbe, 0xde, 0x00, 0x01, 0x31, 0x00, 0x16, 0x00, 0x25, 0xcd, 0xef, 0xce, 0xd7, 0x24, 0x57, 0xd9, 0x3c, 0xfd, 0x0f, 0x77, 0xea, 0x89, 0x61, 0xcb, 0x67, 0xa1, 0x65, 0x4a, 0x7d, 0x1f, 0x10, 0x4e, 0xed, 0x5e, 0x74, 0xe8, 0x7e, 0xce, 0x4d, 0xcf, 0xd5, 0x58, 0xd1, 0x2c, 0x30, 0xf1, 0x26, 0x62, 0xd3, 0x0c, 0x6a, 0x48,
        0x29, 0x83, 0xd2, 0x3d, 0x30, 0xa1, 0x7c, 0x6f, 0xa1, 0x5c, 0x9f, 0x08, 0x43, 0x50, 0x34, 0x2b, 0x3c, 0xa1, 0xf0, 0xb0, 0xe2, 0x0e, 0xc8, 0xf9, 0x79, 0x06, 0x51, 0xfe, 0xbb, 0x13, 0x54, 0x3e, 0xb4, 0x37, 0x91, 0x96, 0x94, 0xb7, 0x61, 0x2e, 0x97, 0x09, 0xb8, 0x27, 0x10, 0x6a, 0x2e, 0xe0, 0x62, 0xe4, 0x37, 0x41, 0xab, 0x4f, 0xbf, 0x06, 0x0a, 0x89, 0x80, 0x18, 0x0d, 0x6e, 0x0a, 0xd1,
        0x9f, 0xf1, 0xdd, 0x12, 0xbd, 0x1a, 0x70, 0x72, 0x33, 0xcc, 0xaa, 0x82, 0xdf, 0x92, 0x90, 0x45, 0xda, 0x3e, 0x88, 0x1c, 0x63, 0x83, 0xbc, 0xc8, 0xff, 0xfd, 0x64, 0xe3, 0xd4, 0x68, 0xe6, 0xc8, 0xdc, 0x81, 0x72, 0x5f, 0x38, 0x5b, 0xab, 0x63, 0x7b, 0x96, 0x03, 0x03, 0x54, 0xc5, 0xe6, 0x35, 0xf6, 0x86, 0xcc, 0xac, 0x74, 0xb0, 0xf4, 0x07, 0x9e, 0x19, 0x30, 0x4f, 0x90, 0xd6, 0xdb, 0x8b,
        0x0d, 0xcb, 0x76, 0x71, 0x55, 0xc7, 0x4a, 0x6e, 0x1b, 0xb4, 0x42, 0xf4, 0xae, 0x81, 0x17, 0x08, 0xb7, 0x50, 0x61, 0x5a, 0x42, 0xde, 0x1f, 0xf3, 0xfd, 0xe2, 0x30, 0xff, 0xb7, 0x07, 0xdd, 0x4b, 0xb1, 0x00, 0xd9, 0x6c, 0x43, 0xa0, 0x9a, 0xfa, 0xbb, 0xec, 0xdf, 0x51, 0xce, 0x33, 0x79, 0x4b, 0xa7, 0x02, 0xf3, 0x96, 0x62, 0x42, 0x25, 0x28, 0x85, 0xa7, 0xe7, 0xd1, 0xd3, 0xf3,
    };

    // If not plaintext, the padding in body is invalid,
    // so it will fail if decoding the header with padding.
    if (true) {
        SrsBuffer b((char*)data, sizeof(data)); SrsRtpHeader h;
        HELPER_EXPECT_FAILED(h.decode(&b));
    }

    // Should ok if ignore padding.
    if (true) {
        SrsBuffer b((char*)data, sizeof(data)); SrsRtpHeader h;
        h.ignore_padding(true);
        HELPER_EXPECT_SUCCESS(h.decode(&b));
    }
}

VOID TEST(KernelRTCTest, DumpsHexToString)
{
    if (true) {
        EXPECT_STREQ("", srs_string_dumps_hex(NULL, 0).c_str());
    }

    if (true) {
        uint8_t data[] = {0, 0, 0, 0};
        EXPECT_STREQ("00 00 00 00", srs_string_dumps_hex((const char*)data, sizeof(data)).c_str());
    }

    if (true) {
        uint8_t data[] = {0, 1, 2, 3};
        EXPECT_STREQ("00 01 02 03", srs_string_dumps_hex((const char*)data, sizeof(data)).c_str());
    }

    if (true) {
        uint8_t data[] = {0xa, 3, 0xf, 3};
        EXPECT_STREQ("0a 03 0f 03", srs_string_dumps_hex((const char*)data, sizeof(data)).c_str());
    }

    if (true) {
        uint8_t data[] = {0xa, 3, 0xf, 3};
        EXPECT_STREQ("0a,03,0f,03", srs_string_dumps_hex((const char*)data, sizeof(data), INT_MAX, ',', 0, 0).c_str());
        EXPECT_STREQ("0a030f03", srs_string_dumps_hex((const char*)data, sizeof(data), INT_MAX, '\0', 0, 0).c_str());
        EXPECT_STREQ("0a,03,\n0f,03", srs_string_dumps_hex((const char*)data, sizeof(data), INT_MAX, ',', 2, '\n').c_str());
        EXPECT_STREQ("0a,03,0f,03", srs_string_dumps_hex((const char*)data, sizeof(data), INT_MAX, ',', 2,'\0').c_str());
    }

    if (true) {
        uint8_t data[] = {0xa, 3, 0xf};
        EXPECT_STREQ("0a 03", srs_string_dumps_hex((const char*)data, sizeof(data), 2).c_str());
    }
}

VOID TEST(KernelRTCTest, NACKFetchRTPPacket)
{
    SrsRtcConnection s(NULL, SrsContextId());
    SrsRtcPlayStream play(&s, SrsContextId());

    SrsRtcTrackDescription ds;
    SrsRtcVideoSendTrack* track = new SrsRtcVideoSendTrack(&s, &ds);
    SrsAutoFree(SrsRtcVideoSendTrack, track);

    // The RTP queue will free the packet.
    if (true) {
        SrsRtpPacket* pkt = new SrsRtpPacket();
        pkt->header.set_sequence(100);
        track->rtp_queue_->set(pkt->header.get_sequence(), pkt);
    }

    // If sequence not match, packet not found.
    if (true) {
        SrsRtpPacket* pkt = track->fetch_rtp_packet(10);
        EXPECT_TRUE(pkt == NULL);
    }

    // The sequence matched, we got the packet.
    if (true) {
        SrsRtpPacket* pkt = track->fetch_rtp_packet(100);
        EXPECT_TRUE(pkt != NULL);
    }

    // NACK special case.
    if (true) {
        // The sequence is the "same", 1100%1000 is 100,
        // so we can also get it from the RTP queue.
        SrsRtpPacket* pkt = track->rtp_queue_->at(1100);
        EXPECT_TRUE(pkt != NULL);

        // But the track requires exactly match, so it returns NULL.
        pkt = track->fetch_rtp_packet(1100);
        EXPECT_TRUE(pkt == NULL);
    }
}

VOID TEST(KernelRTCTest, NACKEncode)
{
    uint32_t ssrc = 123;
    char buf_before[kRtcpPacketSize];
    SrsBuffer stream_before(buf_before, sizeof(buf_before));
    
    SrsRtcpNack rtcp_nack_encode(ssrc);
    for(uint16_t i = 16; i < 50; ++i) {
        rtcp_nack_encode.add_lost_sn(i);
    }
    srs_error_t err_before = rtcp_nack_encode.encode(&stream_before);
    EXPECT_TRUE(err_before == 0);
    char buf_after[kRtcpPacketSize];
    memcpy(buf_after, buf_before, kRtcpPacketSize);
    SrsBuffer stream_after(buf_after, sizeof(buf_after));
    SrsRtcpNack rtcp_nack_decode(ssrc);
    srs_error_t err_after = rtcp_nack_decode.decode(&stream_after);
    EXPECT_TRUE(err_after == 0);
    vector<uint16_t> before = rtcp_nack_encode.get_lost_sns();
    vector<uint16_t> after = rtcp_nack_decode.get_lost_sns();
    EXPECT_TRUE(before.size() == after.size());
    for(int i = 0; i < (int)before.size() && i < (int)after.size(); ++i) {
        EXPECT_TRUE(before.at(i) == after.at(i));
    }
}

extern bool srs_is_stun(const uint8_t* data, size_t size);
extern bool srs_is_dtls(const uint8_t* data, size_t len);
extern bool srs_is_rtp_or_rtcp(const uint8_t* data, size_t len);
extern bool srs_is_rtcp(const uint8_t* data, size_t len);

#define mock_arr_push(arr, elem) arr.push_back(vector<uint8_t>(elem, elem + sizeof(elem)))

VOID TEST(KernelRTCTest, TestPacketType)
{
    // DTLS packet.
    vector< vector<uint8_t> > dtlss;
    if (true) { uint8_t data[13] = {20}; mock_arr_push(dtlss, data); } // change_cipher_spec(20)
    if (true) { uint8_t data[13] = {21}; mock_arr_push(dtlss, data); } // alert(21)
    if (true) { uint8_t data[13] = {22}; mock_arr_push(dtlss, data); } // handshake(22)
    if (true) { uint8_t data[13] = {23}; mock_arr_push(dtlss, data); } // application_data(23)
    for (int i = 0; i < (int)dtlss.size(); i++) {
        vector<uint8_t> elem = dtlss.at(i);
        EXPECT_TRUE(srs_is_dtls(&elem[0], (size_t)elem.size()));
    }

    for (int i = 0; i < (int)dtlss.size(); i++) {
        vector<uint8_t> elem = dtlss.at(i);
        EXPECT_FALSE(srs_is_dtls(&elem[0], 1));

        // All DTLS should not be other packets.
        EXPECT_FALSE(srs_is_stun(&elem[0], (size_t)elem.size()));
        EXPECT_TRUE(srs_is_dtls(&elem[0], (size_t)elem.size()));
        EXPECT_FALSE(srs_is_rtp_or_rtcp(&elem[0], (size_t)elem.size()));
        EXPECT_FALSE(srs_is_rtcp(&elem[0], (size_t)elem.size()));
    }

    // STUN packet.
    vector< vector<uint8_t> > stuns;
    if (true) { uint8_t data[1] = {0}; mock_arr_push(stuns, data); } // binding request.
    if (true) { uint8_t data[1] = {1}; mock_arr_push(stuns, data); } // binding success response.
    for (int i = 0; i < (int)stuns.size(); i++) {
        vector<uint8_t> elem = stuns.at(i);
        EXPECT_TRUE(srs_is_stun(&elem[0], (size_t)elem.size()));
    }

    for (int i = 0; i < (int)stuns.size(); i++) {
        vector<uint8_t> elem = stuns.at(i);
        EXPECT_FALSE(srs_is_stun(&elem[0], 0));

        // All STUN should not be other packets.
        EXPECT_TRUE(srs_is_stun(&elem[0], (size_t)elem.size()));
        EXPECT_FALSE(srs_is_dtls(&elem[0], (size_t)elem.size()));
        EXPECT_FALSE(srs_is_rtp_or_rtcp(&elem[0], (size_t)elem.size()));
        EXPECT_FALSE(srs_is_rtcp(&elem[0], (size_t)elem.size()));
    }

    // RTCP packet.
    vector< vector<uint8_t> > rtcps;
    if (true) { uint8_t data[12] = {0x80, 192}; mock_arr_push(rtcps, data); }
    if (true) { uint8_t data[12] = {0x80, 200}; mock_arr_push(rtcps, data); } // SR
    if (true) { uint8_t data[12] = {0x80, 201}; mock_arr_push(rtcps, data); } // RR
    if (true) { uint8_t data[12] = {0x80, 202}; mock_arr_push(rtcps, data); } // SDES
    if (true) { uint8_t data[12] = {0x80, 203}; mock_arr_push(rtcps, data); } // BYE
    if (true) { uint8_t data[12] = {0x80, 204}; mock_arr_push(rtcps, data); } // APP
    if (true) { uint8_t data[12] = {0x80, 223}; mock_arr_push(rtcps, data); }
    for (int i = 0; i < (int)rtcps.size(); i++) {
        vector<uint8_t> elem = rtcps.at(i);
        EXPECT_TRUE(srs_is_rtcp(&elem[0], (size_t)elem.size()));
    }

    for (int i = 0; i < (int)rtcps.size(); i++) {
        vector<uint8_t> elem = rtcps.at(i);
        EXPECT_FALSE(srs_is_rtcp(&elem[0], 2));

        // All RTCP should not be other packets.
        EXPECT_FALSE(srs_is_stun(&elem[0], (size_t)elem.size()));
        EXPECT_FALSE(srs_is_dtls(&elem[0], (size_t)elem.size()));
        EXPECT_TRUE(srs_is_rtp_or_rtcp(&elem[0], (size_t)elem.size()));
        EXPECT_TRUE(srs_is_rtcp(&elem[0], (size_t)elem.size()));
    }

    // RTP packet.
    vector< vector<uint8_t> > rtps;
    if (true) { uint8_t data[12] = {0x80, 96}; mock_arr_push(rtps, data); }
    if (true) { uint8_t data[12] = {0x80, 127}; mock_arr_push(rtps, data); }
    if (true) { uint8_t data[12] = {0x80, 224}; mock_arr_push(rtps, data); }
    if (true) { uint8_t data[12] = {0x80, 255}; mock_arr_push(rtps, data); }
    for (int i = 0; i < (int)rtps.size(); i++) {
        vector<uint8_t> elem = rtps.at(i);
        EXPECT_TRUE(srs_is_rtp_or_rtcp(&elem[0], (size_t)elem.size()));
        EXPECT_FALSE(srs_is_rtcp(&elem[0], (size_t)elem.size()));
    }

    for (int i = 0; i < (int)rtps.size(); i++) {
        vector<uint8_t> elem = rtps.at(i);
        EXPECT_FALSE(srs_is_rtp_or_rtcp(&elem[0], 2));

        // All RTP should not be other packets.
        EXPECT_FALSE(srs_is_stun(&elem[0], (size_t)elem.size()));
        EXPECT_FALSE(srs_is_dtls(&elem[0], (size_t)elem.size()));
        EXPECT_TRUE(srs_is_rtp_or_rtcp(&elem[0], (size_t)elem.size()));
        EXPECT_FALSE(srs_is_rtcp(&elem[0], (size_t)elem.size()));
    }
}

VOID TEST(KernelRTCTest, DefaultTrackStatus)
{
    // By default, track is disabled.
    if (true) {
        SrsRtcTrackDescription td;

        // The track must default to disable, that is, the active is false.
        EXPECT_FALSE(td.is_active_);
    }

    // Enable it by player.
    if (true) {
        SrsRtcConnection s(NULL, SrsContextId()); SrsRtcPlayStream play(&s, SrsContextId());
        SrsRtcAudioSendTrack* audio; SrsRtcVideoSendTrack *video;

        if (true) {
            SrsRtcTrackDescription ds; ds.type_ = "audio"; ds.id_ = "NSNWOn19NDn12o8nNeji2"; ds.ssrc_ = 100;
            play.audio_tracks_[ds.ssrc_] = audio = new SrsRtcAudioSendTrack(&s, &ds);
        }
        if (true) {
            SrsRtcTrackDescription ds; ds.type_ = "video"; ds.id_ = "VMo22nfLDn122nfnDNL2"; ds.ssrc_ = 200;
            play.video_tracks_[ds.ssrc_] = video = new SrsRtcVideoSendTrack(&s, &ds);
        }
        EXPECT_FALSE(audio->get_track_status());
        EXPECT_FALSE(video->get_track_status());

        play.set_all_tracks_status(true);
        EXPECT_TRUE(audio->get_track_status());
        EXPECT_TRUE(video->get_track_status());
    }

    // Enable it by publisher.
    if (true) {
        SrsRtcConnection s(NULL, SrsContextId()); SrsRtcPublishStream publish(&s, SrsContextId());
        SrsRtcAudioRecvTrack* audio; SrsRtcVideoRecvTrack *video;

        if (true) {
            SrsRtcTrackDescription ds; ds.type_ = "audio"; ds.id_ = "NSNWOn19NDn12o8nNeji2"; ds.ssrc_ = 100;
            audio = new SrsRtcAudioRecvTrack(&s, &ds); publish.audio_tracks_.push_back(audio);
        }
        if (true) {
            SrsRtcTrackDescription ds; ds.type_ = "video"; ds.id_ = "VMo22nfLDn122nfnDNL2"; ds.ssrc_ = 200;
            video = new SrsRtcVideoRecvTrack(&s, &ds); publish.video_tracks_.push_back(video);
        }
        EXPECT_FALSE(audio->get_track_status());
        EXPECT_FALSE(video->get_track_status());

        publish.set_all_tracks_status(true);
        EXPECT_TRUE(audio->get_track_status());
        EXPECT_TRUE(video->get_track_status());
    }
}

VOID TEST(KernelRTCTest, Ntp)
{
    if (true) {
        // Test small systime, from 0-10000ms.
        for (int i = 0; i < 10000; ++i) {
            srs_utime_t now_ms = i;
            // Cover systime to ntp
            SrsNtp ntp = SrsNtp::from_time_ms(now_ms);

            ASSERT_EQ((srs_utime_t)ntp.system_ms_, now_ms);

            // Cover ntp to systime
            SrsNtp ntp1 = SrsNtp::to_time_ms(ntp.ntp_);
            ASSERT_EQ((srs_utime_t)ntp1.system_ms_, now_ms);
        }
    }

    if (true) {
        // Test current systime to ntp.
        srs_utime_t now_ms = srs_get_system_time() / 1000;
        SrsNtp ntp = SrsNtp::from_time_ms(now_ms);

        ASSERT_EQ((srs_utime_t)ntp.system_ms_, now_ms);

        SrsNtp ntp1 = SrsNtp::to_time_ms(ntp.ntp_);
        ASSERT_EQ((srs_utime_t)ntp1.system_ms_, now_ms);
    }
}

VOID TEST(KernelRTCTest, SyncTimestampBySenderReportNormal)
{
    SrsRtcConnection s(NULL, SrsContextId()); 
    SrsRtcPublishStream publish(&s, SrsContextId());

    SrsRtcTrackDescription video_ds; 
    video_ds.type_ = "video"; 
    video_ds.id_ = "VMo22nfLDn122nfnDNL2"; 
    video_ds.ssrc_ = 200;

    SrsRtcVideoRecvTrack* video = new SrsRtcVideoRecvTrack(&s, &video_ds);
    publish.video_tracks_.push_back(video);

    publish.set_all_tracks_status(true);

    SrsRtcSource* rtc_source = new SrsRtcSource();
    SrsAutoFree(SrsRtcSource, rtc_source);
    
    srand(time(NULL));

    if (true)
    {
        SrsRtpPacket* video_rtp_pkt = new SrsRtpPacket();
        SrsAutoFree(SrsRtpPacket, video_rtp_pkt);

        uint32_t video_absolute_ts = srs_get_system_time();
        uint32_t video_rtp_ts = random();

        video_rtp_pkt->header.set_timestamp(video_rtp_ts);
        video->on_rtp(rtc_source, video_rtp_pkt);
        // No received any sender report, can not calculate absolute time, expect equal to -1.
        EXPECT_EQ(video_rtp_pkt->get_avsync_time(), -1);

        SrsNtp ntp = SrsNtp::from_time_ms(video_absolute_ts);

        SrsRtcpSR* video_sr = new SrsRtcpSR();
        SrsAutoFree(SrsRtcpSR, video_sr);
        video_sr->set_ssrc(200);

        video_sr->set_ntp(ntp.ntp_);
        video_sr->set_rtp_ts(video_rtp_ts);
        publish.on_rtcp_sr(video_sr);

        // Video timebase 90000, fps=25
        video_rtp_ts += 3600;
        video_absolute_ts += 40;
        video_rtp_pkt->header.set_timestamp(video_rtp_ts);
        video->on_rtp(rtc_source, video_rtp_pkt);

        // Received one sender report, can not calculate absolute time, expect equal to -1.
        EXPECT_EQ(video_rtp_pkt->get_avsync_time(), -1);

        ntp = SrsNtp::from_time_ms(video_absolute_ts);
        video_sr->set_ntp(ntp.ntp_);
        video_sr->set_rtp_ts(video_rtp_ts);
        publish.on_rtcp_sr(video_sr);

        for (int i = 0; i <= 1000; ++i) {
            // Video timebase 90000, fps=25
            video_rtp_ts += 3600;
            video_absolute_ts += 40;
            video_rtp_pkt->header.set_timestamp(video_rtp_ts);
            video->on_rtp(rtc_source, video_rtp_pkt);
            EXPECT_NEAR(video_rtp_pkt->get_avsync_time(), video_absolute_ts, 1);
        }
    }
}

VOID TEST(KernelRTCTest, SyncTimestampBySenderReportOutOfOrder)
{
    SrsRtcConnection s(NULL, SrsContextId()); 
    SrsRtcPublishStream publish(&s, SrsContextId());

    SrsRtcTrackDescription video_ds; 
    video_ds.type_ = "video"; 
    video_ds.id_ = "VMo22nfLDn122nfnDNL2"; 
    video_ds.ssrc_ = 200;

    SrsRtcVideoRecvTrack* video = new SrsRtcVideoRecvTrack(&s, &video_ds);
    publish.video_tracks_.push_back(video);

    publish.set_all_tracks_status(true);

    SrsRtcSource* rtc_source = new SrsRtcSource();
    SrsAutoFree(SrsRtcSource, rtc_source);
    
    srand(time(NULL));

    if (true)
    {
        SrsRtpPacket* video_rtp_pkt = new SrsRtpPacket();
        SrsAutoFree(SrsRtpPacket, video_rtp_pkt);

        uint32_t video_absolute_ts = srs_get_system_time();
        uint32_t video_rtp_ts = random();

        video_rtp_pkt->header.set_timestamp(video_rtp_ts);
        video->on_rtp(rtc_source, video_rtp_pkt);
        // No received any sender report, can not calculate absolute time, expect equal to -1.
        EXPECT_EQ(video_rtp_pkt->get_avsync_time(), -1);

        SrsNtp ntp = SrsNtp::from_time_ms(video_absolute_ts);

        SrsRtcpSR* video_sr1 = new SrsRtcpSR();
        SrsAutoFree(SrsRtcpSR, video_sr1);
        video_sr1->set_ssrc(200);

        video_sr1->set_ntp(ntp.ntp_);
        video_sr1->set_rtp_ts(video_rtp_ts);

        // Video timebase 90000, fps=25
        video_rtp_ts += 3600;
        video_absolute_ts += 40;
        video_rtp_pkt->header.set_timestamp(video_rtp_ts);
        video->on_rtp(rtc_source, video_rtp_pkt);

        // No received any sender report, can not calculate absolute time, expect equal to -1.
        EXPECT_EQ(video_rtp_pkt->get_avsync_time(), -1);

        ntp = SrsNtp::from_time_ms(video_absolute_ts);
        SrsRtcpSR* video_sr2 = new SrsRtcpSR();
        SrsAutoFree(SrsRtcpSR, video_sr2);
        video_sr2->set_ssrc(200);
        video_sr2->set_ntp(ntp.ntp_);
        video_sr2->set_rtp_ts(video_rtp_ts);

        // Sender report out of order, sr2 arrived befreo sr1.
        publish.on_rtcp_sr(video_sr2);
        publish.on_rtcp_sr(video_sr1);

        for (int i = 0; i <= 1000; ++i) {
            // Video timebase 90000, fps=25
            video_rtp_ts += 3600;
            video_absolute_ts += 40;
            video_rtp_pkt->header.set_timestamp(video_rtp_ts);
            video->on_rtp(rtc_source, video_rtp_pkt);
            EXPECT_NEAR(video_rtp_pkt->get_avsync_time(), video_absolute_ts, 1);
        }
    }
}

VOID TEST(KernelRTCTest, SyncTimestampBySenderReportConsecutive)
{
    SrsRtcConnection s(NULL, SrsContextId()); 
    SrsRtcPublishStream publish(&s, SrsContextId());

    SrsRtcTrackDescription video_ds; 
    video_ds.type_ = "video"; 
    video_ds.id_ = "VMo22nfLDn122nfnDNL2"; 
    video_ds.ssrc_ = 200;

    SrsRtcVideoRecvTrack* video = new SrsRtcVideoRecvTrack(&s, &video_ds);
    publish.video_tracks_.push_back(video);

    publish.set_all_tracks_status(true);

    SrsRtcSource* rtc_source = new SrsRtcSource();
    SrsAutoFree(SrsRtcSource, rtc_source);
    
    srand(time(NULL));

    if (true)
    {
        SrsRtpPacket* video_rtp_pkt = new SrsRtpPacket();
        SrsAutoFree(SrsRtpPacket, video_rtp_pkt);

        uint32_t video_absolute_ts = srs_get_system_time();
        uint32_t video_rtp_ts = random();

        video_rtp_pkt->header.set_timestamp(video_rtp_ts);
        video->on_rtp(rtc_source, video_rtp_pkt);
        // No received any sender report, can not calculate absolute time, expect equal to -1.
        EXPECT_EQ(video_rtp_pkt->get_avsync_time(), -1);

        SrsNtp ntp = SrsNtp::from_time_ms(video_absolute_ts);

        SrsRtcpSR* video_sr = new SrsRtcpSR();
        SrsAutoFree(SrsRtcpSR, video_sr);
        video_sr->set_ssrc(200);

        video_sr->set_ntp(ntp.ntp_);
        video_sr->set_rtp_ts(video_rtp_ts);
        publish.on_rtcp_sr(video_sr);

        // Video timebase 90000, fps=25
        video_rtp_ts += 3600;
        video_absolute_ts += 40;
        video_rtp_pkt->header.set_timestamp(video_rtp_ts);
        video->on_rtp(rtc_source, video_rtp_pkt);

        // Received one sender report, can not calculate absolute time, expect equal to -1.
        EXPECT_EQ(video_rtp_pkt->get_avsync_time(), -1);

        ntp = SrsNtp::from_time_ms(video_absolute_ts);
        video_sr->set_ntp(ntp.ntp_);
        video_sr->set_rtp_ts(video_rtp_ts);
        publish.on_rtcp_sr(video_sr);

        for (int i = 0; i <= 1000; ++i) {
            // Video timebase 90000, fps=25
            video_rtp_ts += 3600;
            video_absolute_ts += 40;
            video_rtp_pkt->header.set_timestamp(video_rtp_ts);
            video->on_rtp(rtc_source, video_rtp_pkt);
            EXPECT_NEAR(video_rtp_pkt->get_avsync_time(), video_absolute_ts, 1);

            // Send sender report every 4 seconds.
            if (i % 100 == 99) {
                ntp = SrsNtp::from_time_ms(video_absolute_ts);
                video_sr->set_ntp(ntp.ntp_);
                video_sr->set_rtp_ts(video_rtp_ts);
                publish.on_rtcp_sr(video_sr);
            }
        }
    }
}

VOID TEST(KernelRTCTest, SrsRtcpNack)
{
    uint32_t sender_ssrc = 0x0A;
    uint32_t media_ssrc = 0x0B;

    SrsRtcpNack nack_encoder(sender_ssrc);
    nack_encoder.set_media_ssrc(media_ssrc);

    for (uint16_t seq = 15; seq < 45; seq++) {
        nack_encoder.add_lost_sn(seq);
    }
    EXPECT_FALSE(nack_encoder.empty());

    char buf[kRtcpPacketSize];
    SrsBuffer stream(buf, sizeof(buf));

    srs_error_t err = srs_success;
    err = nack_encoder.encode(&stream);
    EXPECT_EQ(srs_error_code(err), srs_success);

    SrsRtcpNack nack_decoder;
    stream.skip(-stream.pos());
    err = nack_decoder.decode(&stream);
    EXPECT_EQ(srs_error_code(err), srs_success);

    vector<uint16_t> actual_lost_sn = nack_encoder.get_lost_sns();
    vector<uint16_t> req_lost_sns = nack_decoder.get_lost_sns();
    EXPECT_EQ(actual_lost_sn.size(), req_lost_sns.size());
}

VOID TEST(KernelRTCTest, SyncTimestampBySenderReportDuplicated)
{
    SrsRtcConnection s(NULL, SrsContextId()); 
    SrsRtcPublishStream publish(&s, SrsContextId());

    SrsRtcTrackDescription video_ds; 
    video_ds.type_ = "video"; 
    video_ds.id_ = "VMo22nfLDn122nfnDNL2"; 
    video_ds.ssrc_ = 200;

    SrsRtcVideoRecvTrack* video = new SrsRtcVideoRecvTrack(&s, &video_ds);
    publish.video_tracks_.push_back(video);

    publish.set_all_tracks_status(true);

    SrsRtcSource* rtc_source = new SrsRtcSource();
    SrsAutoFree(SrsRtcSource, rtc_source);
    
    srand(time(NULL));

    if (true)
    {
        SrsRtpPacket* video_rtp_pkt = new SrsRtpPacket();
        SrsAutoFree(SrsRtpPacket, video_rtp_pkt);

        uint32_t video_absolute_ts = srs_get_system_time();
        uint32_t video_rtp_ts = random();

        video_rtp_pkt->header.set_timestamp(video_rtp_ts);
        video->on_rtp(rtc_source, video_rtp_pkt);
        // No received any sender report, can not calculate absolute time, expect equal to -1.
        EXPECT_EQ(video_rtp_pkt->get_avsync_time(), -1);

        SrsNtp ntp = SrsNtp::from_time_ms(video_absolute_ts);

        SrsRtcpSR* video_sr = new SrsRtcpSR();
        SrsAutoFree(SrsRtcpSR, video_sr);
        video_sr->set_ssrc(200);

        video_sr->set_ntp(ntp.ntp_);
        video_sr->set_rtp_ts(video_rtp_ts);
        publish.on_rtcp_sr(video_sr);

        // Video timebase 90000, fps=25
        video_rtp_ts += 3600;
        video_absolute_ts += 40;
        video_rtp_pkt->header.set_timestamp(video_rtp_ts);
        video->on_rtp(rtc_source, video_rtp_pkt);

        // Received one sender report, can not calculate absolute time, expect equal to -1.
        EXPECT_EQ(video_rtp_pkt->get_avsync_time(), -1);

        ntp = SrsNtp::from_time_ms(video_absolute_ts);
        video_sr->set_ntp(ntp.ntp_);
        video_sr->set_rtp_ts(video_rtp_ts);
        publish.on_rtcp_sr(video_sr);

        for (int i = 0; i <= 1000; ++i) {
            // Video timebase 90000, fps=25
            video_rtp_ts += 3600;
            video_absolute_ts += 40;
            video_rtp_pkt->header.set_timestamp(video_rtp_ts);
            video->on_rtp(rtc_source, video_rtp_pkt);
            EXPECT_NEAR(video_rtp_pkt->get_avsync_time(), video_absolute_ts, 1);
            // Duplicate 3 sender report packets.
            if (i % 3 == 0) {
                ntp = SrsNtp::from_time_ms(video_absolute_ts);
                video_sr->set_ntp(ntp.ntp_);
                video_sr->set_rtp_ts(video_rtp_ts);
            }
            publish.on_rtcp_sr(video_sr);
        }
    }
}

VOID TEST(KernelRTCTest, JitterTimestamp)
{
    SrsRtcTsJitter jitter(1000);

    // Starts from the base.
    EXPECT_EQ((uint32_t)1000, jitter.correct(0));

    // Start from here.
    EXPECT_EQ((uint32_t)1010, jitter.correct(10));
    EXPECT_EQ((uint32_t)1010, jitter.correct(10));
    EXPECT_EQ((uint32_t)1020, jitter.correct(20));

    // Reset the base for jitter detected.
    EXPECT_EQ((uint32_t)1020, jitter.correct(20 + 90*3*1000 + 1));
    EXPECT_EQ((uint32_t)1019, jitter.correct(20 + 90*3*1000));
    EXPECT_EQ((uint32_t)1021, jitter.correct(20 + 90*3*1000 + 2));
    EXPECT_EQ((uint32_t)1019, jitter.correct(20 + 90*3*1000));
    EXPECT_EQ((uint32_t)1020, jitter.correct(20 + 90*3*1000 + 1));

    // Rollback the timestamp.
    EXPECT_EQ((uint32_t)1020, jitter.correct(20));
    EXPECT_EQ((uint32_t)1021, jitter.correct(20 + 1));
    EXPECT_EQ((uint32_t)1021, jitter.correct(21));

    // Reset for jitter again.
    EXPECT_EQ((uint32_t)1021, jitter.correct(21 + 90*3*1000 + 1));
    EXPECT_EQ((uint32_t)1021, jitter.correct(21));

    // No jitter at edge.
    EXPECT_EQ((uint32_t)(1021 + 90*3*1000), jitter.correct(21 + 90*3*1000));
    EXPECT_EQ((uint32_t)(1021 + 90*3*1000 + 1), jitter.correct(21 + 90*3*1000 + 1));
    EXPECT_EQ((uint32_t)(1021 + 1), jitter.correct(21 + 1));

    // Also safety to decrease the value.
    EXPECT_EQ((uint32_t)1021, jitter.correct(21));
    EXPECT_EQ((uint32_t)1010, jitter.correct(10));

    // Try to reset to 0 base.
    EXPECT_EQ((uint32_t)1010, jitter.correct(10 + 90*3*1000 + 1010));
    EXPECT_EQ((uint32_t)0, jitter.correct(10 + 90*3*1000));
    EXPECT_EQ((uint32_t)0, jitter.correct(0));

    // Also safety to start from zero.
    EXPECT_EQ((uint32_t)10, jitter.correct(10));
    EXPECT_EQ((uint32_t)11, jitter.correct(11));
}

VOID TEST(KernelRTCTest, JitterSequence)
{
    SrsRtcSeqJitter jitter(100);

    // Starts from the base.
    EXPECT_EQ((uint32_t)100, jitter.correct(0));

    // Normal without jitter.
    EXPECT_EQ((uint32_t)101, jitter.correct(1));
    EXPECT_EQ((uint32_t)102, jitter.correct(2));
    EXPECT_EQ((uint32_t)101, jitter.correct(1));
    EXPECT_EQ((uint32_t)103, jitter.correct(3));
    EXPECT_EQ((uint32_t)110, jitter.correct(10));

    // Reset the base for jitter detected.
    EXPECT_EQ((uint32_t)110, jitter.correct(10 + 128 + 1));
    EXPECT_EQ((uint32_t)109, jitter.correct(10 + 128));
    EXPECT_EQ((uint32_t)110, jitter.correct(10 + 128 + 1));

    // Rollback the timestamp.
    EXPECT_EQ((uint32_t)110, jitter.correct(10));
    EXPECT_EQ((uint32_t)111, jitter.correct(10 + 1));
    EXPECT_EQ((uint32_t)111, jitter.correct(11));

    // Reset for jitter again.
    EXPECT_EQ((uint32_t)111, jitter.correct(11 + 128 + 1));
    EXPECT_EQ((uint32_t)111, jitter.correct(11));

    // No jitter at edge.
    EXPECT_EQ((uint32_t)(111 + 128), jitter.correct(11 + 128));
    EXPECT_EQ((uint32_t)(111 + 128 + 1), jitter.correct(11 + 128 + 1));
    EXPECT_EQ((uint32_t)(111 + 1), jitter.correct(11 + 1));

    // Also safety to decrease the value.
    EXPECT_EQ((uint32_t)111, jitter.correct(11));
    EXPECT_EQ((uint32_t)110, jitter.correct(10));

    // Try to reset to 0 base.
    EXPECT_EQ((uint32_t)110, jitter.correct(10 + 128 + 110));
    EXPECT_EQ((uint32_t)0, jitter.correct(10 + 128));
    EXPECT_EQ((uint32_t)0, jitter.correct(0));

    // Also safety to start from zero.
    EXPECT_EQ((uint32_t)10, jitter.correct(10));
    EXPECT_EQ((uint32_t)11, jitter.correct(11));
}

