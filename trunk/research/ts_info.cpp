/**
g++ -o ts_info ts_info.cpp -g -O0 -ansi
*/
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

/**
ISO/IEC 13818-1:2000(E)
Introduction
SECTION 1 ¨C GENERAL
SECTION 2 ¨C TECHNICAL ELEMENTS
    2.4 Transport Stream bitstream requirements
    2.5 Program Stream bitstream requirements
    2.6 Program and program element descriptors
    2.7 Restrictions on the multiplexed stream semantics
Annex A ¨C CRC Decoder Model
*/

#define trace(msg, ...) printf(msg"\n", ##__VA_ARGS__);

int main(int /*argc*/, char** /*argv*/)
{
    const char* file = "livestream-1347.ts";
    int fd = open(file, O_RDONLY);
    
    trace("demuxer+read packet count offset P+0  P+1  P+2  P+x P+L2 P+L1 P+L0");
    for (int i = 0, offset = 0; ; i++) {
        unsigned char PES[188];
        memset(PES, 0, sizeof(PES));
        
        int ret = read(fd, PES, sizeof(PES));
        if (ret == 0) {
            trace("demuxer+read EOF, read completed, offset: %07d.", offset);
            break;
        }
        trace("demuxer+read packet %04d %07d 0x%02x 0x%02x 0x%02x ... 0x%02x 0x%02x 0x%02x", 
            i, offset, PES[0], PES[1], PES[2], PES[185], PES[186], PES[187]);
        
        
        offset += ret;
    }
    
    close(fd);
    return 0;
}

