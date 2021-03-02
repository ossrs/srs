/*
 * sha1_driver.c
 *
 * a test driver for SHA-1
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
#include <string.h>
#include "sha1.h"
#include "util.h"

#define SHA_PASS 0
#define SHA_FAIL 1

#define MAX_HASH_DATA_LEN 1024
#define MAX_HASH_OUT_LEN 20

typedef struct hash_test_case_t {
    unsigned data_len;               /* number of octets in data        */
    unsigned hash_len;               /* number of octets output by hash */
    uint8_t data[MAX_HASH_DATA_LEN]; /* message data                    */
    uint8_t hash[MAX_HASH_OUT_LEN];  /* expected hash output            */
    struct hash_test_case_t *next_test_case;
} hash_test_case_t;

hash_test_case_t *sha1_test_case_list;

srtp_err_status_t hash_test_case_add(hash_test_case_t **list_ptr,
                                     char *hex_data,
                                     unsigned data_len,
                                     char *hex_hash,
                                     unsigned hash_len)
{
    hash_test_case_t *list_head = *list_ptr;
    hash_test_case_t *test_case;
    unsigned tmp_len;

    test_case = malloc(sizeof(hash_test_case_t));
    if (test_case == NULL)
        return srtp_err_status_alloc_fail;

    tmp_len = hex_string_to_octet_string((char *)test_case->data, hex_data,
                                         data_len * 2);
    if (tmp_len != data_len * 2) {
        free(test_case);
        return srtp_err_status_parse_err;
    }

    tmp_len = hex_string_to_octet_string((char *)test_case->hash, hex_hash,
                                         hash_len * 2);
    if (tmp_len != hash_len * 2) {
        free(test_case);
        return srtp_err_status_parse_err;
    }

    test_case->data_len = data_len;
    test_case->hash_len = hash_len;

    /* add the new test case to the head of the list */
    test_case->next_test_case = list_head;
    *list_ptr = test_case;

    return srtp_err_status_ok;
}

srtp_err_status_t sha1_test_case_validate(const hash_test_case_t *test_case)
{
    srtp_sha1_ctx_t ctx;
    uint32_t hash_value[5];

    if (test_case == NULL)
        return srtp_err_status_bad_param;

    if (test_case->hash_len != 20)
        return srtp_err_status_bad_param;
    if (test_case->data_len > MAX_HASH_DATA_LEN)
        return srtp_err_status_bad_param;

    srtp_sha1_init(&ctx);
    srtp_sha1_update(&ctx, test_case->data, test_case->data_len);
    srtp_sha1_final(&ctx, hash_value);
    if (0 == memcmp(test_case->hash, hash_value, 20)) {
#if VERBOSE
        printf("PASSED: reference value: %s\n",
               octet_string_hex_string((const uint8_t *)test_case->hash, 20));
        printf("PASSED: computed value:  %s\n",
               octet_string_hex_string((const uint8_t *)hash_value, 20));
#endif
        return srtp_err_status_ok;
    }

    printf("reference value: %s\n",
           octet_string_hex_string((const uint8_t *)test_case->hash, 20));
    printf("computed value:  %s\n",
           octet_string_hex_string((const uint8_t *)hash_value, 20));

    return srtp_err_status_algo_fail;
}

struct hex_sha1_test_case_t {
    unsigned bit_len;
    char hex_data[MAX_HASH_DATA_LEN * 2];
    char hex_hash[40];
};

srtp_err_status_t sha1_add_test_cases(void)
{
    int i;
    srtp_err_status_t err;

    /*
     * these test cases are taken from the "SHA-1 Sample Vectors"
     * provided by NIST at http://csrc.nist.gov/cryptval/shs.html
     */

    struct hex_sha1_test_case_t tc[] = {
        { 0, "", "da39a3ee5e6b4b0d3255bfef95601890afd80709" },
        { 8, "a8", "99f2aa95e36f95c2acb0eaf23998f030638f3f15" },
        { 16, "3000", "f944dcd635f9801f7ac90a407fbc479964dec024" },
        { 24, "42749e", "a444319e9b6cc1e8464c511ec0969c37d6bb2619" },
        { 32, "9fc3fe08", "16a0ff84fcc156fd5d3ca3a744f20a232d172253" },
        { 40, "b5c1c6f1af", "fec9deebfcdedaf66dda525e1be43597a73a1f93" },
        { 48, "e47571e5022e", "8ce051181f0ed5e9d0c498f6bc4caf448d20deb5" },
        { 56, "3e1b28839fb758", "67da53837d89e03bf652ef09c369a3415937cfd3" },
        { 64, "a81350cbb224cb90", "305e4ff9888ad855a78573cddf4c5640cce7e946" },
        { 72, "c243d167923dec3ce1",
          "5902b77b3265f023f9bbc396ba1a93fa3509bde7" },
        { 80, "50ac18c59d6a37a29bf4",
          "fcade5f5d156bf6f9af97bdfa9c19bccfb4ff6ab" },
        { 88, "98e2b611ad3b1cccf634f6",
          "1d20fbe00533c10e3cbd6b27088a5de0c632c4b5" },
        { 96, "73fe9afb68e1e8712e5d4eec",
          "7e1b7e0f7a8f3455a9c03e9580fd63ae205a2d93" },
        { 104, "9e701ed7d412a9226a2a130e66",
          "706f0677146307b20bb0e8d6311e329966884d13" },
        { 112, "6d3ee90413b0a7cbf69e5e6144ca",
          "a7241a703aaf0d53fe142f86bf2e849251fa8dff" },
        { 120, "fae24d56514efcb530fd4802f5e71f",
          "400f53546916d33ad01a5e6df66822dfbdc4e9e6" },
        { 128, "c5a22dd6eda3fe2bdc4ddb3ce6b35fd1",
          "fac8ab93c1ae6c16f0311872b984f729dc928ccd" },
        { 136, "d98cded2adabf08fda356445c781802d95",
          "fba6d750c18da58f6e2aab10112b9a5ef3301b3b" },
        { 144, "bcc6d7087a84f00103ccb32e5f5487a751a2",
          "29d27c2d44c205c8107f0351b05753ac708226b6" },
        { 152, "36ecacb1055434190dbbc556c48bafcb0feb0d",
          "b971bfc1ebd6f359e8d74cb7ecfe7f898d0ba845" },
        { 160, "5ff9edb69e8f6bbd498eb4537580b7fba7ad31d0",
          "96d08c430094b9fcc164ad2fb6f72d0a24268f68" },
        { 168, "c95b441d8270822a46a798fae5defcf7b26abace36",
          "a287ea752a593d5209e287881a09c49fa3f0beb1" },
        { 176, "83104c1d8a55b28f906f1b72cb53f68cbb097b44f860",
          "a06c713779cbd88519ed4a585ac0cb8a5e9d612b" },
        { 184, "755175528d55c39c56493d697b790f099a5ce741f7754b",
          "bff7d52c13a3688132a1d407b1ab40f5b5ace298" },
        { 192, "088fc38128bbdb9fd7d65228b3184b3faac6c8715f07272f",
          "c7566b91d7b6f56bdfcaa9781a7b6841aacb17e9" },
        { 200, "a4a586eb9245a6c87e3adf1009ac8a49f46c07e14185016895",
          "ffa30c0b5c550ea4b1e34f8a60ec9295a1e06ac1" },
        { 208, "8e7c555270c006092c2a3189e2a526b873e2e269f0fb28245256",
          "29e66ed23e914351e872aa761df6e4f1a07f4b81" },
        { 216, "a5f3bfa6bb0ba3b59f6b9cbdef8a558ec565e8aa3121f405e7f2f0",
          "b28cf5e5b806a01491d41f69bd9248765c5dc292" },
        { 224, "589054f0d2bd3c2c85b466bfd8ce18e6ec3e0b87d944cd093ba36469",
          "60224fb72c46069652cd78bcd08029ef64da62f3" },
        { 232, "a0abb12083b5bbc78128601bf1cbdbc0fdf4b862b24d899953d8da0ff3",
          "b72c4a86f72608f24c05f3b9088ef92fba431df7" },
        { 240, "82143f4cea6fadbf998e128a8811dc75301cf1db4f079501ea568da68eeb",
          "73779ad5d6b71b9b8328ef7220ff12eb167076ac" },
        { 248, "9f1231dd6df1ff7bc0b0d4f989d048672683ce35d956d2f57913046267e6f3",
          "a09671d4452d7cf50015c914a1e31973d20cc1a0" },
        { 256,
          "041c512b5eed791f80d3282f3a28df263bb1df95e1239a7650e5670fc2187919",
          "e88cdcd233d99184a6fd260b8fca1b7f7687aee0" },
        { 264,
          "17e81f6ae8c2e5579d69dafa6e070e7111461552d314b691e7a3e7a4feb3fae418",
          "010def22850deb1168d525e8c84c28116cb8a269" },
        { 272, "d15976b23a1d712ad28fad04d805f572026b54dd64961fda94d5355a0cc9862"
               "0cf77",
          "aeaa40ba1717ed5439b1e6ea901b294ba500f9ad" },
        { 280, "09fce4d434f6bd32a44e04b848ff50ec9f642a8a85b37a264dc73f130f22838"
               "443328f",
          "c6433791238795e34f080a5f1f1723f065463ca0" },
        { 288, "f17af27d776ec82a257d8d46d2b46b639462c56984cc1be9c1222eadb8b2659"
               "4a25c709d",
          "e21e22b89c1bb944a32932e6b2a2f20d491982c3" },
        { 296, "b13ce635d6f8758143ffb114f2f601cb20b6276951416a2f94fbf4ad081779d"
               "79f4f195b22",
          "575323a9661f5d28387964d2ba6ab92c17d05a8a" },
        { 304, "5498793f60916ff1c918dde572cdea76da8629ba4ead6d065de3dfb48de94d2"
               "34cc1c5002910",
          "feb44494af72f245bfe68e86c4d7986d57c11db7" },
        { 312, "498a1e0b39fa49582ae688cd715c86fbaf8a81b8b11b4d1594c49c902d197c8"
               "ba8a621fd6e3be5",
          "cff2290b3648ba2831b98dde436a72f9ebf51eee" },
        { 320, "3a36ae71521f9af628b3e34dcb0d4513f84c78ee49f10416a98857150b8b15c"
               "b5c83afb4b570376e",
          "9b4efe9d27b965905b0c3dab67b8d7c9ebacd56c" },
        { 328, "dcc76b40ae0ea3ba253e92ac50fcde791662c5b6c948538cffc2d95e9de99ca"
               "c34dfca38910db2678f",
          "afedb0ff156205bcd831cbdbda43db8b0588c113" },
        { 336, "5b5ec6ec4fd3ad9c4906f65c747fd4233c11a1736b6b228b92e90cddabb0c7c"
               "2fcf9716d3fad261dff33",
          "8deb1e858f88293a5e5e4d521a34b2a4efa70fc4" },
        { 344, "df48a37b29b1d6de4e94717d60cdb4293fcf170bba388bddf7a9035a15d433f"
               "20fd697c3e4c8b8c5f590ab",
          "95cbdac0f74afa69cebd0e5c7defbc6faf0cbeaf" },
        { 352, "1f179b3b82250a65e1b0aee949e218e2f45c7a8dbfd6ba08de05c55acfc226b"
               "48c68d7f7057e5675cd96fcfc",
          "f0307bcb92842e5ae0cd4f4f14f3df7f877fbef2" },
        { 360, "ee3d72da3a44d971578972a8e6780ce64941267e0f7d0179b214fa97855e179"
               "0e888e09fbe3a70412176cb3b54",
          "7b13bb0dbf14964bd63b133ac85e22100542ef55" },
        { 368, "d4d4c7843d312b30f610b3682254c8be96d5f6684503f8fbfbcd15774fc1b08"
               "4d3741afb8d24aaa8ab9c104f7258",
          "c314d2b6cf439be678d2a74e890d96cfac1c02ed" },
        { 376, "32c094944f5936a190a0877fb9178a7bf60ceae36fd530671c5b38c5dbd5e6a"
               "6c0d615c2ac8ad04b213cc589541cf6",
          "4d0be361e410b47a9d67d8ce0bb6a8e01c53c078" },
        { 384, "e5d3180c14bf27a5409fa12b104a8fd7e9639609bfde6ee82bbf9648be2546d"
               "29688a65e2e3f3da47a45ac14343c9c02",
          "e5353431ffae097f675cbf498869f6fbb6e1c9f2" },
        { 392, "e7b6e4b69f724327e41e1188a37f4fe38b1dba19cbf5a7311d6e32f1038e97a"
               "b506ee05aebebc1eed09fc0e357109818b9",
          "b8720a7068a085c018ab18961de2765aa6cd9ac4" },
        { 400, "bc880cb83b8ac68ef2fedc2da95e7677ce2aa18b0e2d8b322701f67af7d5e7a"
               "0d96e9e33326ccb7747cfff0852b961bfd475",
          "b0732181568543ba85f2b6da602b4b065d9931aa" },
        { 408, "235ea9c2ba7af25400f2e98a47a291b0bccdaad63faa2475721fda5510cc7da"
               "d814bce8dabb611790a6abe56030b798b75c944",
          "9c22674cf3222c3ba921672694aafee4ce67b96b" },
        { 416, "07e3e29fed63104b8410f323b975fd9fba53f636af8c4e68a53fb202ca35dd9"
               "ee07cb169ec5186292e44c27e5696a967f5e67709",
          "d128335f4cecca9066cdae08958ce656ff0b4cfc" },
        { 424, "65d2a1dd60a517eb27bfbf530cf6a5458f9d5f4730058bd9814379547f34241"
               "822bf67e6335a6d8b5ed06abf8841884c636a25733f",
          "0b67c57ac578de88a2ae055caeaec8bb9b0085a0" },
        { 432, "dcc86b3bd461615bab739d8daafac231c0f462e819ad29f9f14058f3ab5b759"
               "41d4241ea2f17ebb8a458831b37a9b16dead4a76a9b0e",
          "c766f912a89d4ccda88e0cce6a713ef5f178b596" },
        { 440, "4627d54f0568dc126b62a8c35fb46a9ac5024400f2995e51635636e1afc4373"
               "dbb848eb32df23914230560b82477e9c3572647a7f2bb92",
          "9aa3925a9dcb177b15ccff9b78e70cf344858779" },
        { 448, "ba531affd4381168ef24d8b275a84d9254c7f5cc55fded53aa8024b2c5c5c8a"
               "a7146fe1d1b83d62b70467e9a2e2cb67b3361830adbab28d7",
          "4811fa30042fc076acf37c8e2274d025307e5943" },
        { 456, "8764dcbcf89dcf4282eb644e3d568bdccb4b13508bfa7bfe0ffc05efd1390be"
               "22109969262992d377691eb4f77f3d59ea8466a74abf57b2ef4",
          "6743018450c9730761ee2b130df9b91c1e118150" },
        { 464, "497d9df9ddb554f3d17870b1a31986c1be277bc44feff713544217a9f579623"
               "d18b5ffae306c25a45521d2759a72c0459b58957255ab592f3be4",
          "71ad4a19d37d92a5e6ef3694ddbeb5aa61ada645" },
        { 472, "72c3c2e065aefa8d9f7a65229e818176eef05da83f835107ba90ec2e95472e7"
               "3e538f783b416c04654ba8909f26a12db6e5c4e376b7615e4a25819",
          "a7d9dc68dacefb7d6116186048cb355cc548e11d" },
        { 480, "7cc9894454d0055ab5069a33984e2f712bef7e3124960d33559f5f3b81906bb"
               "66fe64da13c153ca7f5cabc89667314c32c01036d12ecaf5f9a78de98",
          "142e429f0522ba5abf5131fa81df82d355b96909" },
        { 488, "74e8404d5a453c5f4d306f2cfa338ca65501c840ddab3fb82117933483afd69"
               "13c56aaf8a0a0a6b2a342fc3d9dc7599f4a850dfa15d06c61966d74ea59",
          "ef72db70dcbcab991e9637976c6faf00d22caae9" },
        { 496, "46fe5ed326c8fe376fcc92dc9e2714e2240d3253b105adfbb256ff7a19bc409"
               "75c604ad7c0071c4fd78a7cb64786e1bece548fa4833c04065fe593f6fb10",
          "f220a7457f4588d639dc21407c942e9843f8e26b" },
        { 504, "836dfa2524d621cf07c3d2908835de859e549d35030433c796b81272fd8bc03"
               "48e8ddbc7705a5ad1fdf2155b6bc48884ac0cd376925f069a37849c089c864"
               "5",
          "ddd2117b6e309c233ede85f962a0c2fc215e5c69" },
        { 512, "7e3a4c325cb9c52b88387f93d01ae86d42098f5efa7f9457388b5e74b6d28b2"
               "438d42d8b64703324d4aa25ab6aad153ae30cd2b2af4d5e5c00a8a2d0220c61"
               "16",
          "a3054427cdb13f164a610b348702724c808a0dcc" }
    };

    for (i = 0; i < 65; i++) {
        err = hash_test_case_add(&sha1_test_case_list, tc[i].hex_data,
                                 tc[i].bit_len / 8, tc[i].hex_hash, 20);
        if (err) {
            printf("error adding hash test case (code %d)\n", err);
            return err;
        }
    }

    return srtp_err_status_ok;
}

srtp_err_status_t sha1_dealloc_test_cases(void)
{
    hash_test_case_t *t, *next;

    for (t = sha1_test_case_list; t != NULL; t = next) {
        next = t->next_test_case;
        free(t);
    }

    sha1_test_case_list = NULL;

    return srtp_err_status_ok;
}

srtp_err_status_t sha1_validate(void)
{
    hash_test_case_t *test_case;
    srtp_err_status_t err;

    err = sha1_add_test_cases();
    if (err) {
        printf("error adding SHA1 test cases (error code %d)\n", err);
        return err;
    }

    if (sha1_test_case_list == NULL)
        return srtp_err_status_cant_check;

    test_case = sha1_test_case_list;
    while (test_case != NULL) {
        err = sha1_test_case_validate(test_case);
        if (err) {
            printf("error validating hash test case (error code %d)\n", err);
            return err;
        }
        test_case = test_case->next_test_case;
    }

    sha1_dealloc_test_cases();

    return srtp_err_status_ok;
}

int main(void)
{
    srtp_err_status_t err;

    printf("sha1 test driver\n");

    err = sha1_validate();
    if (err) {
        printf("SHA1 did not pass validation testing\n");
        return 1;
    }
    printf("SHA1 passed validation tests\n");

    return 0;
}
