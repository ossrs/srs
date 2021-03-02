/*
 * test_srtp.c
 *
 * Unit tests for internal srtp functions
 *
 * Cisco Systems, Inc.
 *
 */

/*
 *
 * Copyright (c) 2017, Cisco Systems, Inc.
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

/*
 * libSRTP specific.
 */
#include "../srtp/srtp.c" // Get access to static functions

/*
 * Test specific.
 */
#include "cutest.h"

/*
 * Standard library.
 */

/*
 * Forward declarations for all tests.
 */

void srtp_calc_aead_iv_srtcp_all_zero_input_yield_zero_output(void);
void srtp_calc_aead_iv_srtcp_seq_num_over_0x7FFFFFFF_bad_param(void);
void srtp_calc_aead_iv_srtcp_distinct_iv_per_sequence_number(void);

/*
 * NULL terminated array of tests.
 * The first item in the array is a char[] which give some information about
 * what is being tested and is displayed to the user during runtime, the second
 * item is the test function.
 */

TEST_LIST = { { "srtp_calc_aead_iv_srtcp_all_zero_input_yield_zero_output()",
                srtp_calc_aead_iv_srtcp_all_zero_input_yield_zero_output },
              { "srtp_calc_aead_iv_srtcp_seq_num_over_0x7FFFFFFF_bad_param()",
                srtp_calc_aead_iv_srtcp_seq_num_over_0x7FFFFFFF_bad_param },
              { "srtp_calc_aead_iv_srtcp_distinct_iv_per_sequence_number()",
                srtp_calc_aead_iv_srtcp_distinct_iv_per_sequence_number },
              { NULL } /* End of tests */ };

/*
 * Implementation.
 */

void srtp_calc_aead_iv_srtcp_all_zero_input_yield_zero_output()
{
    // Preconditions
    srtp_session_keys_t session_keys;
    v128_t init_vector;
    srtcp_hdr_t header;
    uint32_t sequence_num;

    // Postconditions
    srtp_err_status_t status;
    const v128_t zero_vector;
    memset((v128_t *)&zero_vector, 0, sizeof(v128_t));

    // Given
    memset(&session_keys, 0, sizeof(srtp_session_keys_t));
    memset(&init_vector, 0, sizeof(v128_t));
    memset(&header, 0, sizeof(srtcp_hdr_t));
    sequence_num = 0x0UL;

    // When
    status = srtp_calc_aead_iv_srtcp(&session_keys, &init_vector, sequence_num,
                                     &header);

    // Then
    TEST_CHECK(status == srtp_err_status_ok);
    TEST_CHECK(memcmp(&zero_vector, &init_vector, sizeof(v128_t)) == 0);
}

void srtp_calc_aead_iv_srtcp_seq_num_over_0x7FFFFFFF_bad_param()
{
    // Preconditions
    srtp_session_keys_t session_keys;
    v128_t init_vector;
    srtcp_hdr_t header;
    uint32_t sequence_num;

    // Postconditions
    srtp_err_status_t status;

    // Given
    memset(&session_keys, 0, sizeof(srtp_session_keys_t));
    memset(&init_vector, 0, sizeof(v128_t));
    memset(&header, 0, sizeof(srtcp_hdr_t));
    sequence_num = 0x7FFFFFFFUL + 0x1UL;

    // When
    status = srtp_calc_aead_iv_srtcp(&session_keys, &init_vector, sequence_num,
                                     &header);

    // Then
    TEST_CHECK(status == srtp_err_status_bad_param);
}

/*
 * Regression test for issue #256:
 * Srtcp IV calculation incorrectly masks high bit of sequence number for
 * little-endian platforms.
 * Ensure that for each valid sequence number where the most significant bit is
 * high that we get an expected and unique IV.
 */
void srtp_calc_aead_iv_srtcp_distinct_iv_per_sequence_number()
{
#define SAMPLE_COUNT (3)
    // Preconditions
    // Test each significant bit high in each full byte.
    srtp_session_keys_t session_keys;
    srtcp_hdr_t header;
    v128_t output_iv[SAMPLE_COUNT];
    uint32_t sequence_num[SAMPLE_COUNT];
    v128_t final_iv[SAMPLE_COUNT];
    size_t i = 0;
    memset(&output_iv, 0, SAMPLE_COUNT * sizeof(v128_t));
    sequence_num[0] = 0xFF;
    sequence_num[1] = 0xFF00;
    sequence_num[2] = 0xFF0000;

    // Postconditions
    memset(&final_iv, 0, SAMPLE_COUNT * sizeof(v128_t));
    final_iv[0].v8[11] = 0xFF;
    final_iv[1].v8[10] = 0xFF;
    final_iv[2].v8[9] = 0xFF;

    // Given
    memset(&session_keys, 0, sizeof(srtp_session_keys_t));
    memset(&header, 0, sizeof(srtcp_hdr_t));

    // When
    for (i = 0; i < SAMPLE_COUNT; i++) {
        TEST_CHECK(srtp_calc_aead_iv_srtcp(&session_keys, &output_iv[i],
                                           sequence_num[i],
                                           &header) == srtp_err_status_ok);
    }

    // Then all IVs are as expected
    for (i = 0; i < SAMPLE_COUNT; i++) {
        TEST_CHECK(memcmp(&final_iv[i], &output_iv[i], sizeof(v128_t)) == 0);
    }
#undef SAMPLE_COUNT
}
