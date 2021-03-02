/*
 * rdbx_driver.c
 *
 * driver for the rdbx implementation (replay database with extended range)
 *
 * David A. McGrew
 * Cisco Systems, Inc.
 */
/*
 *
 * Copyright (c) 2001-2017, Cisco Systems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 *   Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following
 *   disclaimer in the documentation and/or other materials provided
 *   with the distribution.
 *
 *   Neither the name of the Cisco Systems, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>    /* for printf()          */
#include "getopt_s.h" /* for local getopt()    */

#include "rdbx.h"
#include "cipher_priv.h"

#ifdef ROC_TEST
#error "srtp_rdbx_t won't work with ROC_TEST - bitmask same size as seq_median"
#endif

#include "ut_sim.h"

srtp_err_status_t test_replay_dbx(int num_trials, unsigned long ws);

double rdbx_check_adds_per_second(int num_trials, unsigned long ws);

void usage(char *prog_name)
{
    printf("usage: %s [ -t | -v ]\n", prog_name);
    exit(255);
}

int main(int argc, char *argv[])
{
    double rate;
    srtp_err_status_t status;
    int q;
    unsigned do_timing_test = 0;
    unsigned do_validation = 0;

    /* process input arguments */
    while (1) {
        q = getopt_s(argc, argv, "tv");
        if (q == -1)
            break;
        switch (q) {
        case 't':
            do_timing_test = 1;
            break;
        case 'v':
            do_validation = 1;
            break;
        default:
            usage(argv[0]);
        }
    }

    printf("rdbx (replay database w/ extended range) test driver\n"
           "David A. McGrew\n"
           "Cisco Systems, Inc.\n");

    if (!do_validation && !do_timing_test)
        usage(argv[0]);

    if (do_validation) {
        printf("testing srtp_rdbx_t (ws=128)...\n");

        status = test_replay_dbx(1 << 12, 128);
        if (status) {
            printf("failed\n");
            exit(1);
        }
        printf("passed\n");

        printf("testing srtp_rdbx_t (ws=1024)...\n");

        status = test_replay_dbx(1 << 12, 1024);
        if (status) {
            printf("failed\n");
            exit(1);
        }
        printf("passed\n");
    }

    if (do_timing_test) {
        rate = rdbx_check_adds_per_second(1 << 18, 128);
        printf("rdbx_check/replay_adds per second (ws=128): %e\n", rate);
        rate = rdbx_check_adds_per_second(1 << 18, 1024);
        printf("rdbx_check/replay_adds per second (ws=1024): %e\n", rate);
    }

    return 0;
}

void print_rdbx(srtp_rdbx_t *rdbx)
{
    char buf[2048];
    printf("rdbx: {%llu, %s}\n", (unsigned long long)(rdbx->index),
           bitvector_bit_string(&rdbx->bitmask, buf, sizeof(buf)));
}

/*
 * rdbx_check_add(rdbx, idx) checks a known-to-be-good idx against
 * rdbx, then adds it.  if a failure is detected (i.e., the check
 * indicates that the value is already in rdbx) then
 * srtp_err_status_algo_fail is returned.
 *
 */

srtp_err_status_t rdbx_check_add(srtp_rdbx_t *rdbx, uint32_t idx)
{
    int delta;
    srtp_xtd_seq_num_t est;

    delta = srtp_index_guess(&rdbx->index, &est, idx);

    if (srtp_rdbx_check(rdbx, delta) != srtp_err_status_ok) {
        printf("replay_check failed at index %u\n", idx);
        return srtp_err_status_algo_fail;
    }

    /*
     * in practice, we'd authenticate the packet containing idx, using
     * the estimated value est, at this point
     */

    if (srtp_rdbx_add_index(rdbx, delta) != srtp_err_status_ok) {
        printf("rdbx_add_index failed at index %u\n", idx);
        return srtp_err_status_algo_fail;
    }

    return srtp_err_status_ok;
}

/*
 * rdbx_check_expect_failure(srtp_rdbx_t *rdbx, uint32_t idx)
 *
 * checks that a sequence number idx is in the replay database
 * and thus will be rejected
 */

srtp_err_status_t rdbx_check_expect_failure(srtp_rdbx_t *rdbx, uint32_t idx)
{
    int delta;
    srtp_xtd_seq_num_t est;
    srtp_err_status_t status;

    delta = srtp_index_guess(&rdbx->index, &est, idx);

    status = srtp_rdbx_check(rdbx, delta);
    if (status == srtp_err_status_ok) {
        printf("delta: %d ", delta);
        printf("replay_check failed at index %u (false positive)\n", idx);
        return srtp_err_status_algo_fail;
    }

    return srtp_err_status_ok;
}

srtp_err_status_t rdbx_check_add_unordered(srtp_rdbx_t *rdbx, uint32_t idx)
{
    int delta;
    srtp_xtd_seq_num_t est;
    srtp_err_status_t rstat;

    delta = srtp_index_guess(&rdbx->index, &est, idx);

    rstat = srtp_rdbx_check(rdbx, delta);
    if ((rstat != srtp_err_status_ok) &&
        (rstat != srtp_err_status_replay_old)) {
        printf("replay_check_add_unordered failed at index %u\n", idx);
        return srtp_err_status_algo_fail;
    }
    if (rstat == srtp_err_status_replay_old) {
        return srtp_err_status_ok;
    }
    if (srtp_rdbx_add_index(rdbx, delta) != srtp_err_status_ok) {
        printf("rdbx_add_index failed at index %u\n", idx);
        return srtp_err_status_algo_fail;
    }

    return srtp_err_status_ok;
}

srtp_err_status_t test_replay_dbx(int num_trials, unsigned long ws)
{
    srtp_rdbx_t rdbx;
    uint32_t idx, ircvd;
    ut_connection utc;
    srtp_err_status_t status;
    int num_fp_trials;

    status = srtp_rdbx_init(&rdbx, ws);
    if (status) {
        printf("replay_init failed with error code %d\n", status);
        exit(1);
    }

    /*
     *  test sequential insertion
     */
    printf("\ttesting sequential insertion...");
    for (idx = 0; (int)idx < num_trials; idx++) {
        status = rdbx_check_add(&rdbx, idx);
        if (status)
            return status;
    }
    printf("passed\n");

    /*
     *  test for false positives by checking all of the index
     *  values which we've just added
     *
     * note that we limit the number of trials here, since allowing the
     * rollover counter to roll over would defeat this test
     */
    num_fp_trials = num_trials % 0x10000;
    if (num_fp_trials == 0) {
        printf("warning: no false positive tests performed\n");
    }
    printf("\ttesting for false positives...");
    for (idx = 0; (int)idx < num_fp_trials; idx++) {
        status = rdbx_check_expect_failure(&rdbx, idx);
        if (status)
            return status;
    }
    printf("passed\n");

    /* re-initialize */
    srtp_rdbx_dealloc(&rdbx);

    if (srtp_rdbx_init(&rdbx, ws) != srtp_err_status_ok) {
        printf("replay_init failed\n");
        return srtp_err_status_init_fail;
    }

    /*
     * test non-sequential insertion
     *
     * this test covers only fase negatives, since the values returned
     * by ut_next_index(...) are distinct
     */
    ut_init(&utc);

    printf("\ttesting non-sequential insertion...");
    for (idx = 0; (int)idx < num_trials; idx++) {
        ircvd = ut_next_index(&utc);
        status = rdbx_check_add_unordered(&rdbx, ircvd);
        if (status)
            return status;
        status = rdbx_check_expect_failure(&rdbx, ircvd);
        if (status)
            return status;
    }
    printf("passed\n");

    /* re-initialize */
    srtp_rdbx_dealloc(&rdbx);

    if (srtp_rdbx_init(&rdbx, ws) != srtp_err_status_ok) {
        printf("replay_init failed\n");
        return srtp_err_status_init_fail;
    }

    /*
     * test insertion with large gaps.
     * check for false positives for each insertion.
     */
    printf("\ttesting insertion with large gaps...");
    for (idx = 0, ircvd = 0; (int)idx < num_trials;
         idx++, ircvd += (1 << (srtp_cipher_rand_u32_for_tests() % 12))) {
        status = rdbx_check_add(&rdbx, ircvd);
        if (status)
            return status;
        status = rdbx_check_expect_failure(&rdbx, ircvd);
        if (status)
            return status;
    }
    printf("passed\n");

    srtp_rdbx_dealloc(&rdbx);

    return srtp_err_status_ok;
}

#include <time.h> /* for clock()  */

double rdbx_check_adds_per_second(int num_trials, unsigned long ws)
{
    uint32_t i;
    int delta;
    srtp_rdbx_t rdbx;
    srtp_xtd_seq_num_t est;
    clock_t timer;
    int failures; /* count number of failures */

    if (srtp_rdbx_init(&rdbx, ws) != srtp_err_status_ok) {
        printf("replay_init failed\n");
        exit(1);
    }

    failures = 0;
    timer = clock();
    for (i = 0; (int)i < num_trials; i++) {
        delta = srtp_index_guess(&rdbx.index, &est, i);

        if (srtp_rdbx_check(&rdbx, delta) != srtp_err_status_ok)
            ++failures;
        else if (srtp_rdbx_add_index(&rdbx, delta) != srtp_err_status_ok)
            ++failures;
    }
    timer = clock() - timer;
    if (timer < 1) {
        timer = 1;
    }

    printf("number of failures: %d \n", failures);

    srtp_rdbx_dealloc(&rdbx);

    return (double)CLOCKS_PER_SEC * num_trials / timer;
}
