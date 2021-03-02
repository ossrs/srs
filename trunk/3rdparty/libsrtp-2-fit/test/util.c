/*
 * util.c
 *
 * Utilities used by the test apps
 *
 * John A. Foley
 * Cisco Systems, Inc.
 */
/*
 *
 * Copyright (c) 2014-2017, Cisco Systems, Inc.
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

#include "config.h"
#include "util.h"

#include <string.h>
#include <stdint.h>

/* include space for null terminator */
char bit_string[MAX_PRINT_STRING_LEN + 1];

static inline int hex_char_to_nibble(uint8_t c)
{
    switch (c) {
    case ('0'):
        return 0x0;
    case ('1'):
        return 0x1;
    case ('2'):
        return 0x2;
    case ('3'):
        return 0x3;
    case ('4'):
        return 0x4;
    case ('5'):
        return 0x5;
    case ('6'):
        return 0x6;
    case ('7'):
        return 0x7;
    case ('8'):
        return 0x8;
    case ('9'):
        return 0x9;
    case ('a'):
        return 0xa;
    case ('A'):
        return 0xa;
    case ('b'):
        return 0xb;
    case ('B'):
        return 0xb;
    case ('c'):
        return 0xc;
    case ('C'):
        return 0xc;
    case ('d'):
        return 0xd;
    case ('D'):
        return 0xd;
    case ('e'):
        return 0xe;
    case ('E'):
        return 0xe;
    case ('f'):
        return 0xf;
    case ('F'):
        return 0xf;
    default:
        return -1; /* this flags an error */
    }
    /* NOTREACHED */
    return -1; /* this keeps compilers from complaining */
}

uint8_t nibble_to_hex_char(uint8_t nibble)
{
    char buf[16] = { '0', '1', '2', '3', '4', '5', '6', '7',
                     '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };

    return buf[nibble & 0xF];
}

/*
 * hex_string_to_octet_string converts a hexadecimal string
 * of length 2 * len to a raw octet string of length len
 */
int hex_string_to_octet_string(char *raw, char *hex, int len)
{
    uint8_t x;
    int tmp;
    int hex_len;

    hex_len = 0;
    while (hex_len < len) {
        tmp = hex_char_to_nibble(hex[0]);
        if (tmp == -1) {
            return hex_len;
        }
        x = (tmp << 4);
        hex_len++;
        tmp = hex_char_to_nibble(hex[1]);
        if (tmp == -1) {
            return hex_len;
        }
        x |= (tmp & 0xff);
        hex_len++;
        *raw++ = x;
        hex += 2;
    }
    return hex_len;
}

char *octet_string_hex_string(const void *s, int length)
{
    const uint8_t *str = (const uint8_t *)s;
    int i;

    /* double length, since one octet takes two hex characters */
    length *= 2;

    /* truncate string if it would be too long */
    if (length > MAX_PRINT_STRING_LEN) {
        length = MAX_PRINT_STRING_LEN;
    }

    for (i = 0; i < length; i += 2) {
        bit_string[i] = nibble_to_hex_char(*str >> 4);
        bit_string[i + 1] = nibble_to_hex_char(*str++ & 0xF);
    }
    bit_string[i] = 0; /* null terminate string */
    return bit_string;
}

static const char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                               "abcdefghijklmnopqrstuvwxyz0123456789+/";

static int base64_block_to_octet_triple(char *out, char *in)
{
    unsigned char sextets[4] = { 0 };
    int j = 0;
    int i;

    for (i = 0; i < 4; i++) {
        char *p = strchr(b64chars, in[i]);
        if (p != NULL) {
            sextets[i] = p - b64chars;
        } else {
            j++;
        }
    }

    out[0] = (sextets[0] << 2) | (sextets[1] >> 4);
    if (j < 2) {
        out[1] = (sextets[1] << 4) | (sextets[2] >> 2);
    }
    if (j < 1) {
        out[2] = (sextets[2] << 6) | sextets[3];
    }
    return j;
}

int base64_string_to_octet_string(char *out, int *pad, char *in, int len)
{
    int k = 0;
    int i = 0;
    int j = 0;

    if (len % 4 != 0) {
        return 0;
    }

    while (i < len && j == 0) {
        j = base64_block_to_octet_triple(out + k, in + i);
        k += 3;
        i += 4;
    }
    *pad = j;
    return i;
}
