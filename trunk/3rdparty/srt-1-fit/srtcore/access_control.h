/*
 * SRT - Secure, Reliable, Transport
 * Copyright (c) 2020 Haivision Systems Inc.
 * 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * 
 */

/*****************************************************************************
written by
   Haivision Systems Inc.
 *****************************************************************************/

#ifndef INC_F_ACCESS_CONTROL_H
#define INC_F_ACCESS_CONTROL_H

// A list of rejection codes that are SRT specific.

#define SRT_REJX_FALLBACK 1000 // A code used in case when the application wants to report some problem, but can't precisely specify it.
#define SRT_REJX_KEY_NOTSUP 1001  // The key used in the StreamID keyed string is not supported by the service.
#define SRT_REJX_FILEPATH 1002  // The resource type designates a file and the path is either wrong syntax or not found
#define SRT_REJX_HOSTNOTFOUND 1003 // The `h` host specification was not recognized by the service

// The list of http codes adopted for SRT.
// An example C++ header for HTTP codes can be found at:
// https://github.com/j-ulrich/http-status-codes-cpp

// Some of the unused code can be revived in the future, if there
// happens to be a good reason for it.

#define SRT_REJX_BAD_REQUEST 1400  // General syntax error in the SocketID specification (also a fallback code for undefined cases)
#define SRT_REJX_UNAUTHORIZED 1401  // Authentication failed, provided that the user was correctly identified and access to the required resource would be granted
#define SRT_REJX_OVERLOAD 1402  // The server is too heavily loaded, or you have exceeded credits for accessing the service and the resource.
#define SRT_REJX_FORBIDDEN 1403  // Access denied to the resource by any kind of reason.
#define SRT_REJX_NOTFOUND 1404  // Resource not found at this time.
#define SRT_REJX_BAD_MODE 1405  // The mode specified in `m` key in StreamID is not supported for this request.
#define SRT_REJX_UNACCEPTABLE 1406  // The requested parameters specified in SocketID cannot be satisfied for the requested resource. Also when m=publish and the data format is not acceptable.
// CODE NOT IN USE 407: unused: proxy functionality not predicted
// CODE NOT IN USE 408: unused: no timeout predicted for listener callback
#define SRT_REJX_CONFLICT 1409  // The resource being accessed is already locked for modification. This is in case of m=publish and the specified resource is currently read-only.
// CODE NOT IN USE 410: unused: treated as a specific case of 404
// CODE NOT IN USE 411: unused: no reason to include length in the protocol
// CODE NOT IN USE 412: unused: preconditions not predicted in AC
// CODE NOT IN USE 413: unused: AC size is already defined as 512
// CODE NOT IN USE 414: unused: AC size is already defined as 512
#define SRT_REJX_NOTSUP_MEDIA 1415  // The media type is not supported by the application. This is the `t` key that specifies the media type as stream, file and auth, possibly extended by the application.
// CODE NOT IN USE 416: unused: no detailed specification defined
// CODE NOT IN USE 417: unused: expectations not supported
// CODE NOT IN USE 418: unused: sharks do not drink tea
// CODE NOT IN USE 419: not defined in HTTP
// CODE NOT IN USE 420: not defined in HTTP
// CODE NOT IN USE 421: unused: misdirection not supported
// CODE NOT IN USE 422: unused: aligned to general 400
#define SRT_REJX_LOCKED 1423  // The resource being accessed is locked for any access.
#define SRT_REJX_FAILED_DEPEND 1424  // The request failed because it specified a dependent session ID that has been disconnected.
// CODE NOT IN USE 425: unused: replaying not supported
// CODE NOT IN USE 426: unused: tempting, but it requires resend in connected
// CODE NOT IN USE 427: not defined in HTTP
// CODE NOT IN USE 428: unused: renders to 409
// CODE NOT IN USE 429: unused: renders to 402
// CODE NOT IN USE 451: unused: renders to 403
#define SRT_REJX_ISE 1500  // Unexpected internal server error
#define SRT_REJX_UNIMPLEMENTED 1501  // The request was recognized, but the current version doesn't support it.
#define SRT_REJX_GW 1502  // The server acts as a gateway and the target endpoint rejected the connection.
#define SRT_REJX_DOWN 1503  // The service has been temporarily taken over by a stub reporting this error. The real service can be down for maintenance or crashed.
// CODE NOT IN USE 504: unused: timeout not supported
#define SRT_REJX_VERSION 1505  // SRT version not supported. This might be either unsupported backward compatibility, or an upper value of a version.
// CODE NOT IN USE 506: unused: negotiation and references not supported
#define SRT_REJX_NOROOM 1507  // The data stream cannot be archived due to lacking storage space. This is in case when the request type was to send a file or the live stream to be archived.
// CODE NOT IN USE 508: unused: no redirection supported
// CODE NOT IN USE 509: not defined in HTTP
// CODE NOT IN USE 510: unused: extensions not supported
// CODE NOT IN USE 511: unused: intercepting proxies not supported



#endif
