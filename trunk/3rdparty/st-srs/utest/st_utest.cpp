/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2013-2022 Winlin */

#include <st_utest.hpp>

#include <st.h>
#include <assert.h>

std::ostream& operator<<(std::ostream& out, const ErrorObject* err) {
    if (!err) return out;
    if (err->r0_) out << "r0=" << err->r0_;
    if (err->errno_) out << ", errno=" << err->errno_;
    if (!err->message_.empty()) out << ", msg=" << err->message_;
    return out;
}

// We could do something in the main of utest.
// Copy from gtest-1.6.0/src/gtest_main.cc
GTEST_API_ int main(int argc, char **argv) {
    // Select the best event system available on the OS. In Linux this is
    // epoll(). On BSD it will be kqueue. On Cygwin it will be select.
#if __CYGWIN__
    assert(st_set_eventsys(ST_EVENTSYS_SELECT) != -1);
#else
    assert(st_set_eventsys(ST_EVENTSYS_ALT) != -1);
#endif

    // Initialize state-threads, create idle coroutine.
    assert(st_init() == 0);

    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

// basic test and samples.
VOID TEST(SampleTest, ExampleIntSizeTest)
{
    EXPECT_EQ(1, (int)sizeof(int8_t));
    EXPECT_EQ(2, (int)sizeof(int16_t));
    EXPECT_EQ(4, (int)sizeof(int32_t));
    EXPECT_EQ(8, (int)sizeof(int64_t));
}

