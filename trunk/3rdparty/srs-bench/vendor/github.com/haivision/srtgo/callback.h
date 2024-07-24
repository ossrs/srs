#include <srt/srt.h>

int srtListenCBWrapper(void* opaque, SRTSOCKET ns, int hs_version, struct sockaddr* peeraddr, char* streamid);
void srtConnectCBWrapper(void* opaque, SRTSOCKET ns, int errorcode, struct sockaddr* peeraddr, int token);

int srtListenCB(void* opaque, SRTSOCKET ns, int hs_version, const struct sockaddr* peeraddr, const char* streamid);
void srtConnectCB(void* opaque, SRTSOCKET ns, int errorcode, const struct sockaddr* peeraddr, int token);