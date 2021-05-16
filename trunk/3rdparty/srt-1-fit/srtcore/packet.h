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
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 01/02/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef __UDT_PACKET_H__
#define __UDT_PACKET_H__

#include "udt.h"
#include "common.h"
#include "utilities.h"
#include "packetfilter_api.h"

//////////////////////////////////////////////////////////////////////////////
// The purpose of the IOVector class is to proide a platform-independet interface
// to the WSABUF on Windows and iovec on Linux, that can be easilly converted
// to the native structure for use in WSARecvFrom() and recvmsg(...) functions 
class IOVector
#ifdef _WIN32
	: public WSABUF
#else
	: public iovec
#endif
{
public:

	inline void set(void  *buffer, size_t length)
	{
#ifdef _WIN32
		len = (ULONG)length;
		buf = (CHAR*)buffer;
#else
		iov_base = (void*)buffer;
		iov_len = length;
#endif
	}

	inline char*& dataRef()
	{
#ifdef _WIN32
		return buf;
#else
		return (char*&) iov_base;
#endif
	}

	inline char* data()
	{
#ifdef _WIN32
		return buf;
#else
		return (char*)iov_base;
#endif
	}

	inline size_t size() const
	{
#ifdef _WIN32
		return (size_t) len;
#else
		return iov_len;
#endif
	}

	inline void setLength(size_t length)
	{
#ifdef _WIN32
		len = length;
#else
		iov_len = length;
#endif
	}
};


/// To define packets in order in the buffer. This is public due to being used in buffer.
enum PacketBoundary
{
    PB_SUBSEQUENT = 0, // 00
///      01: last packet of a message
    PB_LAST = 1, // 01
///      10: first packet of a message
    PB_FIRST = 2, // 10
///      11: solo message packet
    PB_SOLO = 3, // 11
};

// Breakdown of the PM_SEQNO field in the header:
//  C| X X ... X, where:
typedef Bits<31> SEQNO_CONTROL;
//  1|T T T T T T T T T T T T T T T|E E...E
typedef Bits<30, 16> SEQNO_MSGTYPE;
typedef Bits<15, 0> SEQNO_EXTTYPE;
//  0|S S ... S
typedef Bits<30, 0> SEQNO_VALUE;

// This bit cannot be used by SEQNO anyway, so it's additionally used
// in LOSSREPORT data specification to define that this value is the
// BEGIN value for a SEQNO range (to distinguish it from a SOLO loss SEQNO value).
const int32_t LOSSDATA_SEQNO_RANGE_FIRST = SEQNO_CONTROL::mask;

// Just cosmetics for readability.
const int32_t LOSSDATA_SEQNO_RANGE_LAST = 0, LOSSDATA_SEQNO_SOLO = 0;

inline int32_t CreateControlSeqNo(UDTMessageType type)
{
    return SEQNO_CONTROL::mask | SEQNO_MSGTYPE::wrap(size_t(type));
}

inline int32_t CreateControlExtSeqNo(int exttype)
{
    return SEQNO_CONTROL::mask | SEQNO_MSGTYPE::wrap(size_t(UMSG_EXT)) | SEQNO_EXTTYPE::wrap(exttype);
}

// MSGNO breakdown: B B|O|K K|R|M M M M M M M M M M...M
typedef Bits<31, 30> MSGNO_PACKET_BOUNDARY;
typedef Bits<29> MSGNO_PACKET_INORDER;
typedef Bits<28, 27> MSGNO_ENCKEYSPEC;
#if 1 // can block rexmit flag
// New bit breakdown - rexmit flag supported.
typedef Bits<26> MSGNO_REXMIT;
typedef Bits<25, 0> MSGNO_SEQ;
// Old bit breakdown - no rexmit flag
typedef Bits<26, 0> MSGNO_SEQ_OLD;
// This symbol is for older SRT version, where the peer does not support the MSGNO_REXMIT flag.
// The message should be extracted as PMASK_MSGNO_SEQ, if REXMIT is supported, and PMASK_MSGNO_SEQ_OLD otherwise.

const uint32_t PACKET_SND_NORMAL = 0, PACKET_SND_REXMIT = MSGNO_REXMIT::mask;

#else
// Old bit breakdown - no rexmit flag
typedef Bits<26, 0> MSGNO_SEQ;
#endif


// constexpr in C++11 !
inline int32_t PacketBoundaryBits(PacketBoundary o) { return MSGNO_PACKET_BOUNDARY::wrap(int32_t(o)); }


enum EncryptionKeySpec
{
    EK_NOENC = 0,
    EK_EVEN = 1,
    EK_ODD = 2
};

enum EncryptionStatus
{
    ENCS_CLEAR = 0,
    ENCS_FAILED = -1,
    ENCS_NOTSUP = -2
};

const int32_t PMASK_MSGNO_ENCKEYSPEC = MSGNO_ENCKEYSPEC::mask;
inline int32_t EncryptionKeyBits(EncryptionKeySpec f)
{
    return MSGNO_ENCKEYSPEC::wrap(int32_t(f));
}
inline EncryptionKeySpec GetEncryptionKeySpec(int32_t msgno)
{
    return EncryptionKeySpec(MSGNO_ENCKEYSPEC::unwrap(msgno));
}

const int32_t PUMASK_SEQNO_PROBE = 0xF;

std::string PacketMessageFlagStr(uint32_t msgno_field);

class CChannel;

class CPacket
{
friend class CChannel;
friend class CSndQueue;
friend class CRcvQueue;

public:
   CPacket();
   ~CPacket();

   void allocate(size_t size);
   void deallocate();

      /// Get the payload or the control information field length.
      /// @return the payload or the control information field length.

   size_t getLength() const;

      /// Set the payload or the control information field length.
      /// @param len [in] the payload or the control information field length.

   void setLength(size_t len);

      /// Pack a Control packet.
      /// @param pkttype [in] packet type filed.
      /// @param lparam [in] pointer to the first data structure, explained by the packet type.
      /// @param rparam [in] pointer to the second data structure, explained by the packet type.
      /// @param size [in] size of rparam, in number of bytes;

   void pack(UDTMessageType pkttype, const void* lparam = NULL, void* rparam = NULL, int size = 0);

      /// Read the packet vector.
      /// @return Pointer to the packet vector.

   IOVector* getPacketVector();

   uint32_t* getHeader() { return m_nHeader; }

      /// Read the packet flag.
      /// @return packet flag (0 or 1).

   // XXX DEPRECATED. Use isControl() instead
   ATR_DEPRECATED
   int getFlag() const
   {
       return isControl() ? 1 : 0;
   }

      /// Read the packet type.
      /// @return packet type filed (000 ~ 111).

   UDTMessageType getType() const;

   bool isControl(UDTMessageType type) const
   {
       return isControl() && type == getType();
   }

   bool isControl() const
   {
       // read bit 0
       return 0!=  SEQNO_CONTROL::unwrap(m_nHeader[SRT_PH_SEQNO]);
   }

   void setControl(UDTMessageType type)
   {
       m_nHeader[SRT_PH_SEQNO] = SEQNO_CONTROL::mask | SEQNO_MSGTYPE::wrap(type);
   }

      /// Read the extended packet type.
      /// @return extended packet type filed (0x000 ~ 0xFFF).

   int getExtendedType() const;

      /// Read the ACK-2 seq. no.
      /// @return packet header field (bit 16~31).

   int32_t getAckSeqNo() const;
   uint16_t getControlFlags() const;

   // Note: this will return a "singular" value, if the packet
   // contains the control message
   int32_t getSeqNo() const
   {
       return m_nHeader[SRT_PH_SEQNO];
   }

      /// Read the message boundary flag bit.
      /// @return packet header field [1] (bit 0~1).

   PacketBoundary getMsgBoundary() const;

      /// Read the message inorder delivery flag bit.
      /// @return packet header field [1] (bit 2).

   bool getMsgOrderFlag() const;

   /// Read the rexmit flag (true if the packet was sent due to retransmission).
   /// If the peer does not support retransmission flag, the current agent cannot use it as well
   /// (because the peer will understand this bit as a part of MSGNO field).

   bool getRexmitFlag() const;

      /// Read the message sequence number.
      /// @return packet header field [1]

   int32_t getMsgSeq(bool has_rexmit = true) const;

      /// Read the message crypto key bits.
      /// @return packet header field [1] (bit 3~4).

   EncryptionKeySpec getMsgCryptoFlags() const;
   void setMsgCryptoFlags(EncryptionKeySpec spec);

      /// Read the message time stamp.
      /// @return packet header field [2] (bit 0~31, bit 0-26 if SRT_DEBUG_TSBPD_WRAP).

   uint32_t getMsgTimeStamp() const;

#ifdef SRT_DEBUG_TSBPD_WRAP //Receiver
   static const uint32_t MAX_TIMESTAMP = 0x07FFFFFF; //27 bit fast wraparound for tests (~2m15s)
#else
   static const uint32_t MAX_TIMESTAMP = 0xFFFFFFFF; //Full 32 bit (01h11m35s)
#endif

protected:
   static const uint32_t TIMESTAMP_MASK = MAX_TIMESTAMP; // this value to be also used as a mask
public:

      /// Clone this packet.
      /// @return Pointer to the new packet.

   CPacket* clone() const;

   enum PacketVectorFields
   {
       PV_HEADER = 0,
       PV_DATA = 1,

       PV_SIZE = 2
   };

protected:
   // Length in bytes

   // DynamicStruct is the same as array of given type and size, just it
   // enforces that you index it using a symbol from symbolic enum type, not by a bare integer.

   typedef DynamicStruct<uint32_t, SRT_PH__SIZE, SrtPktHeaderFields> HEADER_TYPE;
   HEADER_TYPE m_nHeader;  //< The 128-bit header field

   // XXX NOTE: iovec here is not portable. On Windows there's a different
   // (although similar) structure defined, which means that this way the
   // Windows function that is an equivalent of `recvmsg` cannot be used.
   // For example, something like that:
   // class IoVector: public iovec { public: size_t size() { return iov_len; } char* data() { return iov_base; } };
   // class IoVector: public WSAMSG { public: size_t size() { return len; } char* data() { return buf; } };
   IOVector m_PacketVector[PV_SIZE];             //< The 2-demension vector of UDT packet [header, data]

   int32_t __pad;
   bool m_data_owned;

protected:
   CPacket& operator=(const CPacket&);
   CPacket (const CPacket&);

public:

   int32_t& m_iSeqNo;                   // alias: sequence number
   int32_t& m_iMsgNo;                   // alias: message number
   int32_t& m_iTimeStamp;               // alias: timestamp
   int32_t& m_iID;                      // alias: socket ID
   char*& m_pcData;                     // alias: data/control information

   //static const int m_iPktHdrSize;	// packet header size
   static const size_t HDR_SIZE = sizeof(HEADER_TYPE); // packet header size = SRT_PH__SIZE * sizeof(uint32_t)

   // Used in many computations
   // Actually this can be also calculated as: sizeof(struct ether_header) + sizeof(struct ip) + sizeof(struct udphdr).
   static const size_t UDP_HDR_SIZE = 28; // 20 bytes IPv4 + 8 bytes of UDP { u16 sport, dport, len, csum }.

   static const size_t SRT_DATA_HDR_SIZE = UDP_HDR_SIZE + HDR_SIZE;

   // Some well known data
   static const size_t ETH_MAX_MTU_SIZE = 1500;

   // And derived
   static const size_t SRT_MAX_PAYLOAD_SIZE = ETH_MAX_MTU_SIZE - SRT_DATA_HDR_SIZE;

   // Packet interface
   char* data() { return m_pcData; }
   const char* data() const { return m_pcData; }
   size_t size() const { return getLength(); }
   uint32_t header(SrtPktHeaderFields field) const { return m_nHeader[field]; }

   std::string MessageFlagStr()
#if ENABLE_LOGGING
   { return PacketMessageFlagStr(m_nHeader[SRT_PH_MSGNO]); }
#else
   { return ""; }
#endif
};


#endif
