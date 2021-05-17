/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2018 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */
#ifndef INC__PLATFORM_SYS_H
#define INC__PLATFORM_SYS_H

#ifdef _WIN32
   #include <winsock2.h>
   #include <ws2tcpip.h>
   #include <ws2ipdef.h>
   #include <windows.h>
   #include <stdint.h>
   #include <inttypes.h>
   #if defined(_MSC_VER)
      #pragma warning(disable:4251)
   #endif
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#endif

#endif
