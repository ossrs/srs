//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_manual_st2.hpp>

#include <srs_app_st.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_st.hpp>

#include <sys/time.h>
#include <time.h>
#include <unistd.h>

using namespace std;

VOID TEST(StTest, AnonymouseSingleCoroutine)
{
    SRS_COROUTINE_GO({
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    });

    // Wait for coroutine to terminate. Otherwise, it will be stopped
    // and terminated, which cause some of the code not executed.
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
}

VOID TEST(StTest, AnonymouseMultipleCoroutines)
{
    SRS_COROUTINE_GO2(coroutine1, {
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    });

    // If multiple coroutines in the same scope, we should use different id.
    SRS_COROUTINE_GO2(coroutine2, {
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    });

    // Wait for coroutine to terminate. Otherwise, it will be stopped
    // and terminated, which cause some of the code not executed.
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
}

VOID TEST(StTest, AnonymouseCoroutinePull)
{
    int counter = 0;

    if (true) {
        SrsCoroutineChan ctx;
        ctx.push(&counter);

        SRS_COROUTINE_GO_CTX(&ctx, {
            int *counter = (int *)ctx.pop();
            srs_assert(ctx.trd_);
            while (ctx.trd_->pull() == srs_success) {
                srs_usleep(1 * SRS_UTIME_MILLISECONDS);
            }
            (*counter)++;
        });

        // Coroutine not terminated, so the counter is not increased.
        EXPECT_TRUE(counter == 0);

        // Wait for coroutine to run and terminated, or it will crash
        // because the ctx.pop is called after coroutine terminated.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }

    EXPECT_TRUE(counter == 1);
}

VOID TEST(StTest, AnonymouseCoroutineWithContext)
{
    int counter = 0;

    SrsCoroutineChan ctx;
    ctx.push(&counter);

    SRS_COROUTINE_GO_CTX(&ctx, {
        int *counter = (int *)ctx.pop();
        (*counter)++;
    });

    // Coroutine not terminated, so the counter is not increased.
    EXPECT_TRUE(counter == 0);

    // Wait for coroutine to run and terminated, or it will crash
    // because the ctx.pop is called after coroutine terminated.
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    EXPECT_TRUE(counter == 1);

    // Wait for coroutine to terminate. Otherwise, it will be stopped
    // and terminated, which cause some of the code not executed.
    srs_usleep(1 * SRS_UTIME_MILLISECONDS);
}

VOID TEST(StTest, AnonymouseCoroutineWithSync)
{
    SrsUniquePtr<SrsCond> cond(new SrsCond());
    int counter = 0;

    SrsCoroutineChan ctx;
    ctx.push(cond.get());
    ctx.push(&counter);

    SRS_COROUTINE_GO_CTX(&ctx, {
        SrsCond *cond = (SrsCond *)ctx.pop();
        int *counter = (int *)ctx.pop();

        (*counter)++;

        // Notify main thread the work is done.
        cond->signal();
    });

    // The coroutine not terminated, so the counter is not increased.
    EXPECT_TRUE(counter == 0);

    // Wait for the coroutine to terminate. The counter is increased.
    cond->wait();
    EXPECT_TRUE(counter == 1);
}

VOID TEST(StTest, AnonymouseCoroutineWithWaitgroup)
{
    SrsWaitGroup wg;
    int counter = 0;

    SrsCoroutineChan ctx;
    ctx.push(&wg);
    ctx.push(&counter);

    wg.add(1);
    SRS_COROUTINE_GO_CTX(&ctx, {
        SrsWaitGroup *wg = (SrsWaitGroup *)ctx.pop();
        int *counter = (int *)ctx.pop();

        (*counter)++;

        // Notify main thread the work is done.
        wg->done();
    });

    // The coroutine not terminated, so the counter is not increased.
    EXPECT_TRUE(counter == 0);

    // Wait for the coroutine to terminate. The counter is increased.
    wg.wait();
    EXPECT_TRUE(counter == 1);
}

VOID TEST(StTest, AnonymouseCoroutineWithWaitgroups)
{
    SrsWaitGroup wg;
    int counter = 0;

    SrsCoroutineChan ctx;
    ctx.push(&wg);
    ctx.push(&counter);

    wg.add(1);
    SRS_COROUTINE_GO_CTX2(&ctx, coroutine1, {
        // The ctx is copied, so we can pop it again in different coroutines.
        SrsWaitGroup *wg = (SrsWaitGroup *)ctx.pop();
        int *counter = (int *)ctx.pop();

        (*counter)++;

        // Notify main thread the work is done.
        wg->done();
    });

    wg.add(1);
    SRS_COROUTINE_GO_CTX2(&ctx, coroutine2, {
        // The ctx is copied, so we can pop it again in different coroutines.
        SrsWaitGroup *wg = (SrsWaitGroup *)ctx.pop();
        int *counter = (int *)ctx.pop();

        (*counter)++;

        // Notify main thread the work is done.
        wg->done();
    });

    // The coroutine not terminated, so the counter is not increased.
    EXPECT_TRUE(counter == 0);

    // Wait for the coroutine to terminate. The counter is increased.
    wg.wait();
    EXPECT_TRUE(counter == 2);
}

VOID TEST(StTest, VerifyUsingRawCoroutine)
{
    srs_error_t err;

    class NormalThread : public ISrsCoroutineHandler
    {
    public:
        virtual srs_error_t cycle()
        {
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
            return srs_success;
        }
    };

    NormalThread trd;
    SrsSTCoroutine st("test", &trd);
    HELPER_ASSERT_SUCCESS(st.start());
}

VOID TEST(StTest, VerifyMultipleAnonymousClasses)
{
    do {
        class AnonymousCoroutineHandler
        {
        };
    } while (0);

    do {
        class AnonymousCoroutineHandler
        {
        };
    } while (0);

    SrsUniquePtr<SrsCond> cond(new SrsCond());
    cond->signal();

    SrsUniquePtr<SrsMutex> mutex(new SrsMutex());
    SrsLocker(mutex->get());
}

// CAUTION: Badcase, you should not follow this style.
VOID TEST(StTest, AnonymouseBadcase)
{
    // Generally we SHOULD NOT do this, unless you intend to.
    if (true) {
        SRS_COROUTINE_GO({
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        });
    }

    // CAUTION: If multiple coroutines in the different scope, it's ok without id,
    // but it's not recommended, becuase it will be stopped and your code
    // maybe not executed.
    // Generally we SHOULD NOT do this, unless you intend to.
    if (true) {
        SRS_COROUTINE_GO({
            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
        });
    }
}

// CAUTION: Badcase, you should not follow this style.
VOID TEST(StTest, AnonymouseBadcase2)
{
    int counter = 0;

    SrsCoroutineChan ctx;
    ctx.push(&counter);

    // Generally we SHOULD NOT do this, unless you intend to.
    if (true) {
        SRS_COROUTINE_GO_CTX(&ctx, {
            int *counter = (int *)ctx.pop();
            (*counter)++;
        });

        // Wait for coroutine to terminate. Otherwise, it will crash, for the
        // coroutine is terminated while ctx.pop(), the lock is invalid.
        srs_usleep(1 * SRS_UTIME_MILLISECONDS);
    }

    // Coroutine terminated, so the counter is increased.
    EXPECT_TRUE(counter == 1);
}
