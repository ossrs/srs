#include <srtp2/srtp.h>

#include <stdio.h>
#include <string.h>

int main()
{
    if (srtp_init() != srtp_err_status_ok) {
        printf("srtp_init failed\n");
        return 1;
    }

    unsigned char key[SRTP_MASTER_KEY_LEN];
    memset(key, 0x5a, sizeof(key));

    srtp_policy_t policy;
    memset(&policy, 0, sizeof(policy));
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtp);
    srtp_crypto_policy_set_aes_cm_128_hmac_sha1_80(&policy.rtcp);
    policy.ssrc.type = ssrc_any_outbound;
    policy.key = key;
    policy.window_size = 8192;

    srtp_t session = NULL;
    if (srtp_create(&session, &policy) != srtp_err_status_ok) {
        printf("srtp_create failed\n");
        return 1;
    }

    // An RTP header (version 2, PT 111, seq 1, SSRC 1) and a payload, with room for the auth tag.
    unsigned char packet[64] = {0x80, 0x6f, 0x00, 0x01, 0, 0, 0, 0, 0, 0, 0, 1};
    int size = 12 + 20;
    if (srtp_protect(session, packet, &size) != srtp_err_status_ok || size != 12 + 20 + 10) {
        printf("srtp_protect failed\n");
        return 1;
    }

    srtp_dealloc(session);
    srtp_shutdown();
    printf("%s session protected one RTP packet\n", srtp_get_version_string());
    return 0;
}
