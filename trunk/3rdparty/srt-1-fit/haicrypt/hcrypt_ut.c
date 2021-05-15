/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */


/*****************************************************************************
written by
   Haivision Systems Inc.

   2011-07-11 (jdube)
        HaiCrypt initial implementation.
*****************************************************************************/

#include <string.h>				/* memcpy */
#include <stdio.h>
#include <haicrypt.h>
#include "hcrypt.h"

#ifndef _WIN32

/* RFC6070 PBKDF2 Tests Vectors */

static struct TestVector {
		size_t pwd_len;
		const char *pwd;
		size_t salt_len;
		const unsigned char *salt;
		int cnt;
		size_t dk_len;
		unsigned char dk[32];
} tv[] = {
	{	/* 1 */
		.pwd_len = 8,	.pwd = "password",
		.salt_len = 4,	.salt = (unsigned char *)"salt",
		.cnt  = 1,
		.dk_len = 20,
		.dk   = {
			0x0c, 0x60, 0xc8, 0x0f, 0x96, 0x1f, 0x0e, 0x71,
			0xf3, 0xa9, 0xb5, 0x24, 0xaf, 0x60, 0x12, 0x06,
			0x2f, 0xe0, 0x37, 0xa6
		}
	},
	{	/* 2 */
		.pwd_len = 8,	.pwd = "password",
		.salt_len = 4,	.salt = (unsigned char *)"salt",
		.cnt  = 2,
		.dk_len = 20,
		.dk   = {
			0xea, 0x6c, 0x01, 0x4d, 0xc7, 0x2d, 0x6f, 0x8c,
			0xcd, 0x1e, 0xd9, 0x2a, 0xce, 0x1d, 0x41, 0xf0,
			0xd8, 0xde, 0x89, 0x57
		}
	},
	{	/* 3 */
		.pwd_len = 8,	.pwd = "password",
		.salt_len = 4,	.salt = (unsigned char *)"salt",
		.cnt  = 4096,
		.dk_len = 20,
		.dk   = {
			0x4b, 0x00, 0x79, 0x01, 0xb7, 0x65, 0x48, 0x9a,
			0xbe, 0xad, 0x49, 0xd9, 0x26, 0xf7, 0x21, 0xd0,
			0x65, 0xa4, 0x29, 0xc1
		}
	},
	{	/* 4 */
		.pwd_len = 8,	.pwd = "password",
		.salt_len = 4,	.salt = (unsigned char *)"salt",
		.cnt  = 16777216,
		.dk_len = 20,
		.dk   = {
			0xee, 0xfe, 0x3d, 0x61, 0xcd, 0x4d, 0xa4, 0xe4,
			0xe9, 0x94, 0x5b, 0x3d, 0x6b, 0xa2, 0x15, 0x8c,
			0x26, 0x34, 0xe9, 0x84
		}
	},
	{	/* 5 */
		.pwd_len = 24,	.pwd = "passwordPASSWORDpassword",
		.salt_len = 36,	.salt = (unsigned char *)"saltSALTsaltSALTsaltSALTsaltSALTsalt",
		.cnt  = 4096,
		.dk_len = 25,
		.dk   = {
			0x3d, 0x2e, 0xec, 0x4f, 0xe4, 0x1c, 0x84, 0x9b,
			0x80, 0xc8, 0xd8, 0x36, 0x62, 0xc0, 0xe4, 0x4a,
			0x8b, 0x29, 0x1a, 0x96, 0x4c, 0xf2, 0xf0, 0x70,
			0x38
		}
	},
	{	/* 6 */
		.pwd_len = 9,	.pwd = "pass\0word",
		.salt_len = 5,	.salt = (unsigned char *)"sa\0lt",
		.cnt  = 4096,
		.dk_len = 16,
		.dk   = {
			0x56, 0xfa, 0x6a, 0xa7, 0x55, 0x48, 0x09, 0x9d,
			0xcc, 0x37, 0xd7, 0xf0, 0x34, 0x25, 0xe0, 0xc3
		}
	},
};

#include <sys/time.h>

static int hc_ut_pbkdf2(unsigned verbose)
{
	int i;
	int nbt = sizeof(tv)/sizeof(tv[0]);	
	int nbe = 0;
	unsigned char dk[32];
	struct timeval tstart, tstop, tdiff;

	for (i=0; i<nbt; i++) {
		if (verbose) {
			printf("PBKDF2 test vector %d", i+1);
			fflush(stdout);
			gettimeofday(&tstart, NULL);
		}	

		hcrypt_pbkdf2_hmac_sha1(tv[i].pwd, tv[i].pwd_len, 
			tv[i].salt, tv[i].salt_len, 
			tv[i].cnt, tv[i].dk_len, dk);

		if (verbose) {
			gettimeofday(&tstop, NULL);
			timersub(&tstop, &tstart, &tdiff);
		}

		if(memcmp(dk, tv[i].dk, tv[i].dk_len)) {
			if (verbose) {
				printf(": failed in %lu.%06lu sec\n", tdiff.tv_sec, (unsigned long)tdiff.tv_usec);
			} else {
				printf("PBKDF2 test vector %d: failed\n", i+1);
			}
			nbe++;
		} else if (verbose) {
			printf(": passed in %lu.%06lu sec\n", tdiff.tv_sec, (unsigned long)tdiff.tv_usec);
		}
	}
	return(nbe);
}

int hc_ut_encrypt_ctr_speed(void)
{
	static HaiCrypt_Secret secret = {
		.typ    = HAICRYPT_SECTYP_PASSPHRASE,
		.len    = 12,
		.str    = "000000000000"
	};
	HaiCrypt_Cfg crypto_cfg;
	HaiCrypt_Handle hcrypto;
	struct timeval tstart, tstop, tdiff;
	unsigned char pkt[1500];
	int nbe = 0;
	int i;
#ifdef HAICRYPT_USE_OPENSSL_EVP_CBC
	HaiCrypt_Cipher HaiCryptCipher_OpenSSL_EVP_CBC(void);   /* OpenSSL EVP interface CBC mode*/
#endif

	memset(&crypto_cfg, 0, sizeof(crypto_cfg));

	crypto_cfg.flags = HAICRYPT_CFG_F_CRYPTO | HAICRYPT_CFG_F_TX;
	crypto_cfg.xport = HAICRYPT_XPT_SRT;
#ifdef HAICRYPT_USE_OPENSSL_EVP_CBC
	crypto_cfg.cipher = HaiCryptCipher_OpenSSL_EVP_CBC();
#else
	crypto_cfg.cipher = HaiCryptCipher_Get_Instance();
#endif
	crypto_cfg.key_len = (size_t)128/8;
	crypto_cfg.data_max_len = HAICRYPT_DEF_DATA_MAX_LENGTH;    //MTU
	crypto_cfg.km_tx_period_ms = 0;//No HaiCrypt KM inject period, handled in SRT;
	crypto_cfg.km_refresh_rate_pkt = HAICRYPT_DEF_KM_REFRESH_RATE;
	crypto_cfg.km_pre_announce_pkt = 0x10000; //HAICRYPT_DEF_KM_PRE_ANNOUNCE;

	memcpy(&crypto_cfg.secret, &secret, sizeof(crypto_cfg.secret));

	if (HaiCrypt_Create(&crypto_cfg, &hcrypto)) {
		fprintf(stderr, "haicrypt: HaiCrypt_Create failed\n");
		return(1);
	}

	for (i=0; i<1500; i++) {
		pkt[i] = i & 0xff;
	}

#define UT_NBPKTS	100000L
#define UT_PKTSZ	(7*188)

	gettimeofday(&tstart, NULL);
	for (i=0; i<UT_NBPKTS; i++) {
		if (0 > HaiCrypt_Tx_Data(hcrypto, &pkt[0], &pkt[16], UT_PKTSZ)) nbe++;
		if (0 == (i % 1000)) {
			printf("\b\b\b\b\b\b%6d", i);
			fflush(stdout);
		}
	}
	gettimeofday(&tstop, NULL);
	timersub(&tstop, &tstart, &tdiff);
	printf("\nhaicrypt: encrypted %ld packets in %lu.%06lu sec (%ld.%03ld kbps)\n", 
		UT_NBPKTS, tdiff.tv_sec, (unsigned long)tdiff.tv_usec, 
		(((UT_NBPKTS * UT_PKTSZ*10)/((tdiff.tv_sec*10) + (tdiff.tv_usec/100))) / 1000),
		(((UT_NBPKTS * UT_PKTSZ*10)/((tdiff.tv_sec*10) + (tdiff.tv_usec/100))) % 1000));

	HaiCrypt_Close(hcrypto);
	return(nbe);
}

int main(int argc, char *argv[])
{
    
	int nbe = 0;
    (void)argc;
    (void)argv;
	nbe += hc_ut_encrypt_ctr_speed();
	nbe += hc_ut_pbkdf2(1);

	printf("haicrypt unit test %s: %d errors found\n", nbe ? "failed" : "passed", nbe);
	return(nbe);
}

#endif // _WIN32
