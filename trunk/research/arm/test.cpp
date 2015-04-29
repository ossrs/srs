/*
# see: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SrsLinuxArm
 arm-linux-gnueabi-g++ -o test test.cpp -static
 arm-linux-gnueabi-strip test
*/
#include <stdio.h>

int main(int argc, char** argv) {
	printf("hello, arm!\n");
	return 0;
}
