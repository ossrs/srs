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
 *****************************************************************************/

#ifndef SRT4UDT_H
#define SRT4UDT_H

#ifndef INC__SRTC_H
#error "This is protected header, used by udt.h. This shouldn't be included directly"
#endif

//undef SRT_ENABLE_ECN 1                /* Early Congestion Notification (for source bitrate control) */

//undef SRT_DEBUG_TSBPD_OUTJITTER 1     /* Packet Delivery histogram */
//undef SRT_DEBUG_TSBPD_DRIFT 1         /* Debug Encoder-Decoder Drift) */
//undef SRT_DEBUG_TSBPD_WRAP 1          /* Debug packet timestamp wraparound */
//undef SRT_DEBUG_TLPKTDROP_DROPSEQ 1
//undef SRT_DEBUG_SNDQ_HIGHRATE 1


/*
* SRT_ENABLE_CONNTIMEO
* Option UDT_CONNTIMEO added to the API to set/get the connection timeout.
* The UDT hard coded default of 3000 msec is too small for some large RTT (satellite) use cases.
* The SRT handshake (2 exchanges) needs 2 times the RTT to complete with no packet loss.
*/
#define SRT_ENABLE_CONNTIMEO 1

/*
* SRT_ENABLE_NOCWND
* Set the congestion window at its max (then disabling it) to prevent stopping transmission
* when too many packets are not acknowledged.
* The congestion windows is the maximum distance in pkts since the last acknowledged packets.
*/
#define SRT_ENABLE_NOCWND 1

/*
* SRT_ENABLE_NAKREPORT
* Send periodic NAK report for more efficient retransmission instead of relying on ACK timeout
* to retransmit all non-ACKed packets, very inefficient with real-time and no congestion window.
*/
#define SRT_ENABLE_NAKREPORT 1

#define SRT_ENABLE_RCVBUFSZ_MAVG 1      /* Recv buffer size moving average */
#define SRT_ENABLE_SNDBUFSZ_MAVG 1      /* Send buffer size moving average */
#define SRT_MAVG_SAMPLING_RATE 40       /* Max sampling rate */

#define SRT_ENABLE_LOSTBYTESCOUNT 1


/*
* SRT_ENABLE_IPOPTS
* Enable IP TTL and ToS setting
*/
#define SRT_ENABLE_IPOPTS 1


#define SRT_ENABLE_CLOSE_SYNCH 1

#endif /* SRT4UDT_H */
