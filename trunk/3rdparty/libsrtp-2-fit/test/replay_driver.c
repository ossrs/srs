/*
 * replay_driver.c
 *
 * A driver for the replay_database implementation
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

#include "rdb.h"
#include "ut_sim.h"

#include "cipher_priv.h"

/*
 * num_trials defines the number of trials that are used in the
 * validation functions below
 */

unsigned num_trials = 1 << 16;

srtp_err_status_t test_rdb_db(void);

double rdb_check_adds_per_second(void);

int main(void)
{
    srtp_err_status_t err;

    printf("testing anti-replay database (srtp_rdb_t)...\n");
    err = test_rdb_db();
    if (err) {
        printf("failed\n");
        exit(1);
    }
    printf("done\n");

    printf("rdb_check/rdb_adds per second: %e\n", rdb_check_adds_per_second());

    return 0;
}

void print_rdb(srtp_rdb_t *rdb)
{
    printf("rdb: {%u, %s}\n", rdb->window_start,
           v128_bit_string(&rdb->bitmask));
}

srtp_err_status_t rdb_check_add(srtp_rdb_t *rdb, uint32_t idx)
{
    if (srtp_rdb_check(rdb, idx) != srtp_err_status_ok) {
        printf("rdb_check failed at index %u\n", idx);
        return srtp_err_status_fail;
    }
    if (srtp_rdb_add_index(rdb, idx) != srtp_err_status_ok) {
        printf("rdb_add_index failed at index %u\n", idx);
        return srtp_err_status_fail;
    }

    return srtp_err_status_ok;
}

srtp_err_status_t rdb_check_expect_failure(srtp_rdb_t *rdb, uint32_t idx)
{
    srtp_err_status_t err;

    err = srtp_rdb_check(rdb, idx);
    if ((err != srtp_err_status_replay_old) &&
        (err != srtp_err_status_replay_fail)) {
        printf("rdb_check failed at index %u (false positive)\n", idx);
        return srtp_err_status_fail;
    }

    return srtp_err_status_ok;
}

srtp_err_status_t rdb_check_add_unordered(srtp_rdb_t *rdb, uint32_t idx)
{
    srtp_err_status_t rstat;

    /* printf("index: %u\n", idx); */
    rstat = srtp_rdb_check(rdb, idx);
    if ((rstat != srtp_err_status_ok) &&
        (rstat != srtp_err_status_replay_old)) {
        printf("rdb_check_add_unordered failed at index %u\n", idx);
        return rstat;
    }
    if (rstat == srtp_err_status_replay_old) {
        return srtp_err_status_ok;
    }
    if (srtp_rdb_add_index(rdb, idx) != srtp_err_status_ok) {
        printf("rdb_add_index failed at index %u\n", idx);
        return srtp_err_status_fail;
    }

    return srtp_err_status_ok;
}

srtp_err_status_t test_rdb_db()
{
    srtp_rdb_t rdb;
    uint32_t idx, ircvd;
    ut_connection utc;
    srtp_err_status_t err;

    if (srtp_rdb_init(&rdb) != srtp_err_status_ok) {
        printf("rdb_init failed\n");
        return srtp_err_status_init_fail;
    }

    /* test sequential insertion */
    for (idx = 0; idx < num_trials; idx++) {
        err = rdb_check_add(&rdb, idx);
        if (err)
            return err;
    }

    /* test for false positives */
    for (idx = 0; idx < num_trials; idx++) {
        err = rdb_check_expect_failure(&rdb, idx);
        if (err)
            return err;
    }

    /* re-initialize */
    if (srtp_rdb_init(&rdb) != srtp_err_status_ok) {
        printf("rdb_init failed\n");
        return srtp_err_status_fail;
    }

    /* test non-sequential insertion */
    ut_init(&utc);

    for (idx = 0; idx < num_trials; idx++) {
        ircvd = ut_next_index(&utc);
        err = rdb_check_add_unordered(&rdb, ircvd);
        if (err)
            return err;
        err = rdb_check_expect_failure(&rdb, ircvd);
        if (err)
            return err;
    }

    /* re-initialize */
    if (srtp_rdb_init(&rdb) != srtp_err_status_ok) {
        printf("rdb_init failed\n");
        return srtp_err_status_fail;
    }

    /* test insertion with large gaps */
    for (idx = 0, ircvd = 0; idx < num_trials;
         idx++, ircvd += (1 << (srtp_cipher_rand_u32_for_tests() % 10))) {
        err = rdb_check_add(&rdb, ircvd);
        if (err)
            return err;
        err = rdb_check_expect_failure(&rdb, ircvd);
        if (err)
            return err;
    }

    /* re-initialize */
    if (srtp_rdb_init(&rdb) != srtp_err_status_ok) {
        printf("rdb_init failed\n");
        return srtp_err_status_fail;
    }

    /* test loss of first 513 packets */
    for (idx = 0; idx < num_trials; idx++) {
        err = rdb_check_add(&rdb, idx + 513);
        if (err)
            return err;
    }

    /* test for false positives */
    for (idx = 0; idx < num_trials + 513; idx++) {
        err = rdb_check_expect_failure(&rdb, idx);
        if (err)
            return err;
    }

    /* test for key expired */
    if (srtp_rdb_init(&rdb) != srtp_err_status_ok) {
        printf("rdb_init failed\n");
        return srtp_err_status_fail;
    }
    rdb.window_start = 0x7ffffffe;
    if (srtp_rdb_increment(&rdb) != srtp_err_status_ok) {
        printf("srtp_rdb_increment of 0x7ffffffe failed\n");
        return srtp_err_status_fail;
    }
    if (srtp_rdb_get_value(&rdb) != 0x7fffffff) {
        printf("rdb valiue was not 0x7fffffff\n");
        return srtp_err_status_fail;
    }
    if (srtp_rdb_increment(&rdb) != srtp_err_status_key_expired) {
        printf("srtp_rdb_increment of 0x7fffffff did not return "
               "srtp_err_status_key_expired\n");
        return srtp_err_status_fail;
    }
    if (srtp_rdb_get_value(&rdb) != 0x7fffffff) {
        printf("rdb valiue was not 0x7fffffff\n");
        return srtp_err_status_fail;
    }

    return srtp_err_status_ok;
}

#include <time.h>   /* for clock()  */
#include <stdlib.h> /* for random() */

#define REPLAY_NUM_TRIALS 10000000

double rdb_check_adds_per_second(void)
{
    uint32_t i;
    srtp_rdb_t rdb;
    clock_t timer;
    int failures = 0; /* count number of failures        */

    if (srtp_rdb_init(&rdb) != srtp_err_status_ok) {
        printf("rdb_init failed\n");
        exit(1);
    }

    timer = clock();
    for (i = 0; i < REPLAY_NUM_TRIALS; i += 3) {
        if (srtp_rdb_check(&rdb, i + 2) != srtp_err_status_ok)
            ++failures;
        if (srtp_rdb_add_index(&rdb, i + 2) != srtp_err_status_ok)
            ++failures;
        if (srtp_rdb_check(&rdb, i + 1) != srtp_err_status_ok)
            ++failures;
        if (srtp_rdb_add_index(&rdb, i + 1) != srtp_err_status_ok)
            ++failures;
        if (srtp_rdb_check(&rdb, i) != srtp_err_status_ok)
            ++failures;
        if (srtp_rdb_add_index(&rdb, i) != srtp_err_status_ok)
            ++failures;
    }
    timer = clock() - timer;

    return (double)CLOCKS_PER_SEC * REPLAY_NUM_TRIALS / timer;
}
