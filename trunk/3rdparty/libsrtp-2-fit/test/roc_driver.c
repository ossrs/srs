/*
 * roc_driver.c
 *
 * test driver for rollover counter replay implementation
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

#include <stdio.h>

/*
 * defining ROC_TEST causes small datatypes to be used in
 * srtp_xtd_seq_num_t - this allows the functions to be exhaustively tested.
 */
#if ROC_NEEDS_TO_BE_TESTED
#define ROC_TEST
#endif

#include "rdbx.h"
#include "ut_sim.h"

srtp_err_status_t roc_test(int num_trials);

int main(void)
{
    srtp_err_status_t status;

    printf("rollover counter test driver\n"
           "David A. McGrew\n"
           "Cisco Systems, Inc.\n");

    printf("testing index functions...");
    status = roc_test(1 << 18);
    if (status) {
        printf("failed\n");
        exit(status);
    }
    printf("passed\n");
    return 0;
}

#define ROC_VERBOSE 0

srtp_err_status_t roc_test(int num_trials)
{
    srtp_xtd_seq_num_t local, est, ref;
    ut_connection utc;
    int i, num_bad_est = 0;
    int delta;
    uint32_t ircvd;
    double failure_rate;

    srtp_index_init(&local);
    srtp_index_init(&ref);
    srtp_index_init(&est);

    printf("\n\ttesting sequential insertion...");
    for (i = 0; i < 2048; i++) {
        srtp_index_guess(&local, &est, (uint16_t)ref);
#if ROC_VERBOSE
        printf("%lld, %lld, %d\n", ref, est, i);
#endif
        if (ref != est) {
#if ROC_VERBOSE
            printf(" *bad estimate*\n");
#endif
            ++num_bad_est;
        }
        srtp_index_advance(&ref, 1);
    }
    failure_rate = (double)num_bad_est / num_trials;
    if (failure_rate > 0.01) {
        printf("error: failure rate too high (%d bad estimates in %d trials)\n",
               num_bad_est, num_trials);
        return srtp_err_status_algo_fail;
    }
    printf("done\n");

    printf("\ttesting non-sequential insertion...");
    srtp_index_init(&local);
    srtp_index_init(&ref);
    srtp_index_init(&est);
    ut_init(&utc);

    for (i = 0; i < num_trials; i++) {
        /* get next seq num from unreliable transport simulator */
        ircvd = ut_next_index(&utc);

        /* set ref to value of ircvd */
        ref = ircvd;

        /* estimate index based on low bits of ircvd */
        delta = srtp_index_guess(&local, &est, (uint16_t)ref);
#if ROC_VERBOSE
        printf("ref: %lld, local: %lld, est: %lld, ircvd: %d, delta: %d\n", ref,
               local, est, ircvd, delta);
#endif

        if (local + delta != est) {
            printf(" *bad delta*: local %llu + delta %d != est %llu\n",
                   (unsigned long long)local, delta, (unsigned long long)est);
            return srtp_err_status_algo_fail;
        }

        /* now update local srtp_xtd_seq_num_t as necessary */
        if (delta > 0)
            srtp_index_advance(&local, delta);

        if (ref != est) {
#if ROC_VERBOSE
            printf(" *bad estimate*\n");
#endif
            /* record failure event */
            ++num_bad_est;

            /* reset local value to correct value */
            local = ref;
        }
    }
    failure_rate = (double)num_bad_est / num_trials;
    if (failure_rate > 0.01) {
        printf("error: failure rate too high (%d bad estimates in %d trials)\n",
               num_bad_est, num_trials);
        return srtp_err_status_algo_fail;
    }
    printf("done\n");

    return srtp_err_status_ok;
}
