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
Copyright (c) 2001 - 2009, The Board of Trustees of the University of Illinois.
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
   Yunhong Gu, last updated 08/01/2009
modified by
   Haivision Systems Inc.
*****************************************************************************/

#ifndef INC_SRT_COMMON_H
#define INC_SRT_COMMON_H

#include <memory>
#include <cstdlib>
#include <cstdio>
#ifndef _WIN32
   #include <sys/time.h>
   #include <sys/uio.h>
#else
   // #include <winsock2.h>
   //#include <windows.h>
#endif

#include "srt.h"
#include "utilities.h"
#include "sync.h"
#include "netinet_any.h"
#include "packetfilter_api.h"

// System-independent errno
#ifndef _WIN32
   #define NET_ERROR errno
#else
   #define NET_ERROR WSAGetLastError()
#endif

#ifdef _DEBUG
#include <assert.h>
#define SRT_ASSERT(cond) assert(cond)
#else
#define SRT_ASSERT(cond)
#endif

#if HAVE_FULL_CXX11
#define SRT_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define SRT_STATIC_ASSERT(cond, msg)
#endif

#include <exception>

namespace srt_logging
{
    std::string SockStatusStr(SRT_SOCKSTATUS s);
#if ENABLE_BONDING
    std::string MemberStatusStr(SRT_MEMBERSTATUS s);
#endif
}

namespace srt
{

// Class CUDTException exposed for C++ API.
// This is actually useless, unless you'd use a DIRECT C++ API,
// however there's no such API so far. The current C++ API for UDT/SRT
// is predicted to NEVER LET ANY EXCEPTION out of implementation,
// so it's useless to catch this exception anyway.

class CUDTException: public std::exception
{
public:

    CUDTException(CodeMajor major = MJ_SUCCESS, CodeMinor minor = MN_NONE, int err = -1);
    virtual ~CUDTException() ATR_NOTHROW {}

    /// Get the description of the exception.
    /// @return Text message for the exception description.
    const char* getErrorMessage() const ATR_NOTHROW;

    virtual const char* what() const ATR_NOTHROW ATR_OVERRIDE
    {
        return getErrorMessage();
    }

    std::string getErrorString() const;

    /// Get the system errno for the exception.
    /// @return errno.
    int getErrorCode() const;

    /// Get the system network errno for the exception.
    /// @return errno.
    int getErrno() const;

    /// Clear the error code.
    void clear();

private:
    CodeMajor m_iMajor;        // major exception categories
    CodeMinor m_iMinor;		// for specific error reasons
    int m_iErrno;		// errno returned by the system if there is any
    mutable std::string m_strMsg; // text error message (cache)

    std::string m_strAPI;	// the name of UDT function that returns the error
    std::string m_strDebug;	// debug information, set to the original place that causes the error

public: // Legacy Error Code

    static const int EUNKNOWN = SRT_EUNKNOWN;
    static const int SUCCESS = SRT_SUCCESS;
    static const int ECONNSETUP = SRT_ECONNSETUP;
    static const int ENOSERVER = SRT_ENOSERVER;
    static const int ECONNREJ = SRT_ECONNREJ;
    static const int ESOCKFAIL = SRT_ESOCKFAIL;
    static const int ESECFAIL = SRT_ESECFAIL;
    static const int ECONNFAIL = SRT_ECONNFAIL;
    static const int ECONNLOST = SRT_ECONNLOST;
    static const int ENOCONN = SRT_ENOCONN;
    static const int ERESOURCE = SRT_ERESOURCE;
    static const int ETHREAD = SRT_ETHREAD;
    static const int ENOBUF = SRT_ENOBUF;
    static const int EFILE = SRT_EFILE;
    static const int EINVRDOFF = SRT_EINVRDOFF;
    static const int ERDPERM = SRT_ERDPERM;
    static const int EINVWROFF = SRT_EINVWROFF;
    static const int EWRPERM = SRT_EWRPERM;
    static const int EINVOP = SRT_EINVOP;
    static const int EBOUNDSOCK = SRT_EBOUNDSOCK;
    static const int ECONNSOCK = SRT_ECONNSOCK;
    static const int EINVPARAM = SRT_EINVPARAM;
    static const int EINVSOCK = SRT_EINVSOCK;
    static const int EUNBOUNDSOCK = SRT_EUNBOUNDSOCK;
    static const int ESTREAMILL = SRT_EINVALMSGAPI;
    static const int EDGRAMILL = SRT_EINVALBUFFERAPI;
    static const int ENOLISTEN = SRT_ENOLISTEN;
    static const int ERDVNOSERV = SRT_ERDVNOSERV;
    static const int ERDVUNBOUND = SRT_ERDVUNBOUND;
    static const int EINVALMSGAPI = SRT_EINVALMSGAPI;
    static const int EINVALBUFFERAPI = SRT_EINVALBUFFERAPI;
    static const int EDUPLISTEN = SRT_EDUPLISTEN;
    static const int ELARGEMSG = SRT_ELARGEMSG;
    static const int EINVPOLLID = SRT_EINVPOLLID;
    static const int EASYNCFAIL = SRT_EASYNCFAIL;
    static const int EASYNCSND = SRT_EASYNCSND;
    static const int EASYNCRCV = SRT_EASYNCRCV;
    static const int ETIMEOUT = SRT_ETIMEOUT;
    static const int ECONGEST = SRT_ECONGEST;
    static const int EPEERERR = SRT_EPEERERR;
};



enum UDTSockType
{
    UDT_UNDEFINED = 0, // initial trap representation
    UDT_STREAM = 1,
    UDT_DGRAM
};


/// The message types used by UDT protocol. This is a part of UDT
/// protocol and should never be changed.
enum UDTMessageType
{
    UMSG_HANDSHAKE = 0, //< Connection Handshake. Control: see @a CHandShake.
    UMSG_KEEPALIVE = 1, //< Keep-alive.
    UMSG_ACK = 2, //< Acknowledgement. Control: past-the-end sequence number up to which packets have been received.
    UMSG_LOSSREPORT = 3, //< Negative Acknowledgement (NAK). Control: Loss list.
    UMSG_CGWARNING = 4, //< Congestion warning.
    UMSG_SHUTDOWN = 5, //< Shutdown.
    UMSG_ACKACK = 6, //< Acknowledgement of Acknowledgement. Add info: The ACK sequence number
    UMSG_DROPREQ = 7, //< Message Drop Request. Add info: Message ID. Control Info: (first, last) number of the message.
    UMSG_PEERERROR = 8, //< Signal from the Peer side. Add info: Error code.
    // ... add extra code types here
    UMSG_END_OF_TYPES,
    UMSG_EXT = 0x7FFF //< For the use of user-defined control packets.
};

// This side's role is: INITIATOR prepares the environment first, and sends
// appropriate information to the peer. The peer must be RESPONDER and be ready
// to receive it. It's important for the encryption: the INITIATOR side generates
// the KM, and sends it to RESPONDER. RESPONDER awaits KM received from the
// INITIATOR. Note that in bidirectional mode - that is always with HSv5 - the
// INITIATOR creates both sending and receiving contexts, then sends the key to
// RESPONDER, which creates both sending and receiving contexts, using the same
// key received from INITIATOR.
//
// The method of selection:
//
// In HSv4, it's always data sender (the party that sets SRTO_SENDER flag on the
// socket) INITIATOR, and receiver - RESPONDER. The HSREQ and KMREQ are done
// AFTER the UDT connection is done using UMSG_EXT extension messages. As this
// is unidirectional, the INITIATOR prepares the sending context only, the
// RESPONDER - receiving context only.
//
// In HSv5, for caller-listener configuration, it's simple: caller is INITIATOR,
// listener is RESPONDER. In case of rendezvous the parties are equivalent,
// so the role is resolved by "cookie contest". Rendezvous sockets both know
// each other's cookie generated during the URQ_WAVEAHAND handshake phase.
// The cookies are simply compared as integer numbers; the party which's cookie
// is a greater number becomes an INITIATOR, and the other party becomes a
// RESPONDER. 
//
// The case of a draw - that both occasionally have baked identical cookies -
// is treated as an extremely rare and virtually impossible case, so this
// results in connection rejected.
enum HandshakeSide
{
    HSD_DRAW,
    HSD_INITIATOR,    //< Side that initiates HSREQ/KMREQ. HSv4: data sender, HSv5: connecting socket or winner rendezvous socket
    HSD_RESPONDER  //< Side that expects HSREQ/KMREQ from the peer. HSv4: data receiver, HSv5: accepted socket or loser rendezvous socket
};

// For debug
std::string MessageTypeStr(UDTMessageType mt, uint32_t extt = 0);

////////////////////////////////////////////////////////////////////////////////

// Commonly used by various reading facilities
enum EReadStatus
{
    RST_OK = 0,      //< A new portion of data has been received
    RST_AGAIN,       //< Nothing has been received, try again
    RST_ERROR = -1   //< Irrecoverable error, please close descriptor and stop reading.
};

enum EConnectStatus
{
    CONN_ACCEPT = 0,     //< Received final handshake that confirms connection established
    CONN_REJECT = -1,    //< Error during processing handshake.
    CONN_CONTINUE = 1,   //< induction->conclusion phase
    CONN_RENDEZVOUS = 2, //< pass to a separate rendezvous processing (HSv5 only)
    CONN_CONFUSED = 3,   //< listener thinks it's connected, but caller missed conclusion
    CONN_RUNNING = 10,   //< no connection in progress, already connected
    CONN_AGAIN = -2      //< No data was read, don't change any state.
};

enum EConnectMethod
{
    COM_ASYNCHRO,
    COM_SYNCHRO
};

std::string ConnectStatusStr(EConnectStatus est);


const int64_t BW_INFINITE =  1000000000/8;         //Infinite=> 1 Gbps


enum ETransmissionEvent
{
    TEV_INIT,       // --> After creation, and after any parameters were updated.
    TEV_ACK,        // --> When handling UMSG_ACK - older CCC:onAck()
    TEV_ACKACK,     // --> UDT does only RTT sync, can be read from CUDT::SRTT().
    TEV_LOSSREPORT, // --> When handling UMSG_LOSSREPORT - older CCC::onLoss()
    TEV_CHECKTIMER, // --> See TEV_CHT_REXMIT
    TEV_SEND,       // --> When the packet is scheduled for sending - older CCC::onPktSent
    TEV_RECEIVE,    // --> When a data packet was received - older CCC::onPktReceived
    TEV_CUSTOM,     // --> probably dead call - older CCC::processCustomMsg
    TEV_SYNC,       // --> Backup group. When rate estimation is derived from an active member, and update is needed.

    TEV_E_SIZE
};

std::string TransmissionEventStr(ETransmissionEvent ev);

// Special parameter for TEV_CHECKTIMER
enum ECheckTimerStage
{
    TEV_CHT_INIT,       // --> UDT: just update parameters, don't call any CCC::*
    TEV_CHT_FASTREXMIT, // --> not available on UDT
    TEV_CHT_REXMIT      // --> CCC::onTimeout() in UDT
};

enum EInitEvent
{
    TEV_INIT_RESET = 0,
    TEV_INIT_INPUTBW,
    TEV_INIT_OHEADBW
};

class CPacket;

// XXX Use some more standard less hand-crafted solution, if possible
// XXX Consider creating a mapping between TEV_* values and associated types,
// so that the type is compiler-enforced when calling updateCC() and when
// connecting signals to slots.
struct EventVariant
{
    enum Type {UNDEFINED, PACKET, ARRAY, ACK, STAGE, INIT} type;
    union U
    {
        const srt::CPacket* packet;
        int32_t ack;
        struct
        {
            const int32_t* ptr;
            size_t len;
        } array;
        ECheckTimerStage stage;
        EInitEvent init;
    } u;


    template<Type t>
    struct VariantFor;


    // Note: UNDEFINED and ARRAY don't have assignment operator.
    // For ARRAY you'll use 'set' function. For UNDEFINED there's nothing.

    explicit EventVariant(const srt::CPacket* arg)
    {
        type = PACKET;
        u.packet = arg;
    }

    explicit EventVariant(int32_t arg)
    {
        type = ACK;
        u.ack = arg;
    }

    explicit EventVariant(ECheckTimerStage arg)
    {
        type = STAGE;
        u.stage = arg;
    }

    explicit EventVariant(EInitEvent arg)
    {
        type = INIT;
        u.init = arg;
    }

    const int32_t* get_ptr() const
    {
        return u.array.ptr;
    }

    size_t get_len() const
    {
        return u.array.len;
    }

    void set(const int32_t* ptr, size_t len)
    {
        type = ARRAY;
        u.array.ptr = ptr;
        u.array.len = len;
    }

    EventVariant(const int32_t* ptr, size_t len)
    {
        set(ptr, len);
    }

    template<Type T>
    typename VariantFor<T>::type get() const
    {
        return u.*(VariantFor<T>::field());
    }
};

/*
    Maybe later.
    This had to be a solution for automatic extraction of the
    type hidden in particular EventArg for particular event so
    that it's not runtime-mistaken.

    In order that this make sense there would be required an array
    indexed by event id (just like a slot array m_Slots in CUDT),
    where the "type distiller" function would be extracted and then
    combined with the user-connected slot function this would call
    it already with correct type. Note that also the ConnectSignal
    function would have to get the signal id by template parameter,
    not function parameter. For example:

    m_parent->ConnectSignal<TEV_ACK>(SSLOT(updateOnSent));

    in which updateOnSent would have to receive an appropriate type.
    This has a disadvantage that you can't connect multiple signals
    with different argument types to the same slot, you'd have to
    make slot wrappers to translate arguments.

    It seems that a better idea would be to create binders that would
    translate the argument from EventArg to the correct type according
    to the rules imposed by particular event id. But I'd not make it
    until there's a green light on C++11 for SRT, so maybe in a far future.

template <ETransmissionEvent type>
class EventArgType;
#define MAP_EVENT_TYPE(tev, tp) template<> class EventArgType<tev> { typedef tp type; }
*/


// The 'type' field wouldn't be even necessary if we
// use a full-templated version. TBD.
template<> struct EventVariant::VariantFor<EventVariant::PACKET>
{
    typedef const srt::CPacket* type;
    static type U::*field() {return &U::packet;}
};

template<> struct EventVariant::VariantFor<EventVariant::ACK>
{
    typedef int32_t type;
    static type U::*field() { return &U::ack; }
};

template<> struct EventVariant::VariantFor<EventVariant::STAGE>
{
    typedef ECheckTimerStage type;
    static type U::*field() { return &U::stage; }
};

template<> struct EventVariant::VariantFor<EventVariant::INIT>
{
    typedef EInitEvent type;
    static type U::*field() { return &U::init; }
};

// Using a hand-crafted solution because there's a non-backward-compatible
// change between C++03 and others on the way up to C++17 (and we want this
// code to be compliant with all C++ standards):
//
// - there's std::mem_fun in C++03 - deprecated in C++11, removed in C++17
// - std::function in C++11 would be perfect, but not in C++03

// This can be changed in future to use C++11 way, but only after C++03
// compatibility is finally abaondoned. Until then, this stays with a custom
// class.

class EventSlotBase
{
public:
    virtual void emit(ETransmissionEvent tev, EventVariant var) = 0;
    typedef void dispatcher_t(void* opaque, ETransmissionEvent tev, EventVariant var);

    virtual ~EventSlotBase() {}
};

class SimpleEventSlot: public EventSlotBase
{
public:
    void* opaque;
    dispatcher_t* dispatcher;

    SimpleEventSlot(void* op, dispatcher_t* disp): opaque(op), dispatcher(disp) {}

    void emit(ETransmissionEvent tev, EventVariant var) ATR_OVERRIDE
    {
        (*dispatcher)(opaque, tev, var);
    }
};

template <class Class>
class ObjectEventSlot: public EventSlotBase
{
public:
    typedef void (Class::*method_ptr_t)(ETransmissionEvent tev, EventVariant var);

    method_ptr_t pm;
    Class* po;

    ObjectEventSlot(Class* o, method_ptr_t m): pm(m), po(o) {}

    void emit(ETransmissionEvent tev, EventVariant var) ATR_OVERRIDE
    {
        (po->*pm)(tev, var);
    }
};


struct EventSlot
{
    mutable EventSlotBase* slot;
    // Create empty slot. Calls are ignored.
    EventSlot(): slot(0) {}

    // "Stealing" copy constructor, following the auto_ptr method.
    // This isn't very nice, but no other way to do it in C++03
    // without rvalue-reference and move.
    void moveFrom(const EventSlot& victim)
    {
        slot = victim.slot; // Should MOVE.
        victim.slot = 0;
    }

    EventSlot(const EventSlot& victim) { moveFrom(victim); }
    EventSlot& operator=(const EventSlot& victim) { moveFrom(victim); return *this; }

    EventSlot(void* op, EventSlotBase::dispatcher_t* disp)
    {
        slot = new SimpleEventSlot(op, disp);
    }

    template <class ObjectClass>
    EventSlot(ObjectClass* obj, typename ObjectEventSlot<ObjectClass>::method_ptr_t method)
    {
        slot = new ObjectEventSlot<ObjectClass>(obj, method);
    }

    void emit(ETransmissionEvent tev, EventVariant var)
    {
        if (!slot)
            return;
        slot->emit(tev, var);
    }

    ~EventSlot()
    {
        delete slot;
    }
};


// UDT Sequence Number 0 - (2^31 - 1)

// seqcmp: compare two seq#, considering the wraping
// seqlen: length from the 1st to the 2nd seq#, including both
// seqoff: offset from the 2nd to the 1st seq#
// incseq: increase the seq# by 1
// decseq: decrease the seq# by 1
// incseq: increase the seq# by a given offset

class CSeqNo
{
    int32_t value;

public:

   explicit CSeqNo(int32_t v): value(v) {}

   // Comparison
   bool operator == (const CSeqNo& other) const { return other.value == value; }
   bool operator < (const CSeqNo& other) const
   {
       return seqcmp(value, other.value) < 0;
   }

   // The std::rel_ops namespace cannot be "imported"
   // as a whole into the class - it can only be used
   // in the application code. 
   bool operator != (const CSeqNo& other) const { return other.value != value; }
   bool operator > (const CSeqNo& other) const { return other < *this; }
   bool operator >= (const CSeqNo& other) const
   {
       return seqcmp(value, other.value) >= 0;
   }
   bool operator <=(const CSeqNo& other) const
   {
       return seqcmp(value, other.value) <= 0;
   }

   // circular arithmetics
   friend int operator-(const CSeqNo& c1, const CSeqNo& c2)
   {
       return seqoff(c2.value, c1.value);
   }

   friend CSeqNo operator-(const CSeqNo& c1, int off)
   {
       return CSeqNo(decseq(c1.value, off));
   }

   friend CSeqNo operator+(const CSeqNo& c1, int off)
   {
       return CSeqNo(incseq(c1.value, off));
   }

   friend CSeqNo operator+(int off, const CSeqNo& c1)
   {
       return CSeqNo(incseq(c1.value, off));
   }

   CSeqNo& operator++()
   {
       value = incseq(value);
       return *this;
   }

   /// This behaves like seq1 - seq2, in comparison to numbers,
   /// and with the statement that only the sign of the result matters.
   /// Returns a negative value if seq1 < seq2,
   /// positive if seq1 > seq2, and zero if they are equal.
   /// The only correct application of this function is when you
   /// compare two values and it works faster than seqoff. However
   /// the result's meaning is only in its sign. DO NOT USE THE
   /// VALUE for any other purpose. It is not meant to be the
   /// distance between two sequence numbers.
   ///
   /// Example: to check if (seq1 %> seq2): seqcmp(seq1, seq2) > 0.
   /// Note: %> stands for "later than".
   inline static int seqcmp(int32_t seq1, int32_t seq2)
   {return (abs(seq1 - seq2) < m_iSeqNoTH) ? (seq1 - seq2) : (seq2 - seq1);}

   /// This function measures a length of the range from seq1 to seq2,
   /// including endpoints (seqlen(a, a) = 1; seqlen(a, a + 1) = 2),
   /// WITH A PRECONDITION that certainly @a seq1 is earlier than @a seq2.
   /// This can also include an enormously large distance between them,
   /// that is, exceeding the m_iSeqNoTH value (can be also used to test
   /// if this distance is larger).
   /// Prior to calling this function the caller must be certain that
   /// @a seq2 is a sequence coming from a later time than @a seq1,
   /// and that the distance does not exceed m_iMaxSeqNo.
   inline static int seqlen(int32_t seq1, int32_t seq2)
   {
       SRT_ASSERT(seq1 >= 0 && seq1 <= m_iMaxSeqNo);
       SRT_ASSERT(seq2 >= 0 && seq2 <= m_iMaxSeqNo);
       return (seq1 <= seq2) ? (seq2 - seq1 + 1) : (seq2 - seq1 + m_iMaxSeqNo + 2);
   }

   /// This behaves like seq2 - seq1, with the precondition that the true
   /// distance between two sequence numbers never exceeds m_iSeqNoTH.
   /// That is, if the difference in numeric values of these two arguments
   /// exceeds m_iSeqNoTH, it is treated as if the later of these two
   /// sequence numbers has overflown and actually a segment of the
   /// MAX+1 value should be added to it to get the proper result.
   ///
   /// Note: this function does more calculations than seqcmp, so it should
   /// be used if you need the exact distance between two sequences. If 
   /// you are only interested with their relationship, use seqcmp.
   inline static int seqoff(int32_t seq1, int32_t seq2)
   {
      if (abs(seq1 - seq2) < m_iSeqNoTH)
         return seq2 - seq1;

      if (seq1 < seq2)
         return seq2 - seq1 - m_iMaxSeqNo - 1;

      return seq2 - seq1 + m_iMaxSeqNo + 1;
   }

   inline static int32_t incseq(int32_t seq)
   {return (seq == m_iMaxSeqNo) ? 0 : seq + 1;}

   inline static int32_t decseq(int32_t seq)
   {return (seq == 0) ? m_iMaxSeqNo : seq - 1;}

   inline static int32_t incseq(int32_t seq, int32_t inc)
   {return (m_iMaxSeqNo - seq >= inc) ? seq + inc : seq - m_iMaxSeqNo + inc - 1;}
   // m_iMaxSeqNo >= inc + sec  --- inc + sec <= m_iMaxSeqNo
   // if inc + sec > m_iMaxSeqNo then return seq + inc - (m_iMaxSeqNo+1)

   inline static int32_t decseq(int32_t seq, int32_t dec)
   {
       // Check if seq - dec < 0, but before it would have happened
       if ( seq < dec )
       {
           int32_t left = dec - seq; // This is so many that is left after dragging dec to 0
           // So now decrement the (m_iMaxSeqNo+1) by "left"
           return m_iMaxSeqNo - left + 1;
       }
       return seq - dec;
   }

   static int32_t maxseq(int32_t seq1, int32_t seq2)
   {
       if (seqcmp(seq1, seq2) < 0)
           return seq2;
       return seq1;
   }

public:
   static const int32_t m_iSeqNoTH = 0x3FFFFFFF;             // threshold for comparing seq. no.
   static const int32_t m_iMaxSeqNo = 0x7FFFFFFF;            // maximum sequence number used in UDT
};

////////////////////////////////////////////////////////////////////////////////

// UDT ACK Sub-sequence Number: 0 - (2^31 - 1)

class CAckNo
{
public:
   inline static int32_t incack(int32_t ackno)
   {return (ackno == m_iMaxAckSeqNo) ? 0 : ackno + 1;}

public:
   static const int32_t m_iMaxAckSeqNo = 0x7FFFFFFF;         // maximum ACK sub-sequence number used in UDT
};

template <size_t BITS, uint32_t MIN = 0>
class RollNumber
{
    typedef RollNumber<BITS, MIN> this_t;
    typedef Bits<BITS, 0> number_t;
    uint32_t number;

public:

    static const size_t OVER = number_t::mask+1;
    static const size_t HALF = (OVER-MIN)/2;

private:
    static int Diff(uint32_t left, uint32_t right)
    {
        // UNExpected order, diff is negative
        if ( left < right )
        {
            int32_t diff = right - left;
            if ( diff >= int32_t(HALF) ) // over barrier
            {
                // It means that left is less than right because it was overflown
                // For example: left = 0x0005, right = 0xFFF0; diff = 0xFFEB > HALF
                left += OVER - MIN;  // left was really 0x00010005, just narrowed.
                // Now the difference is 0x0015, not 0xFFFF0015
            }
        }
        else
        {
            int32_t diff = left - right;
            if ( diff >= int32_t(HALF) )
            {
                right += OVER - MIN;
            }
        }

        return left - right;
    }

public:
    explicit RollNumber(uint32_t val): number(val)
    {
    }

    bool operator<(const this_t& right) const
    {
        int32_t ndiff = number - right.number;
        if (ndiff < -int32_t(HALF))
        {
            // it' like ndiff > 0
            return false;
        }

        if (ndiff > int32_t(HALF))
        {
            // it's like ndiff < 0
            return true;
        }

        return ndiff < 0;
    }

    bool operator>(const this_t& right) const
    {
        return right < *this;
    }

    bool operator==(const this_t& right) const
    {
        return number == right.number;
    }

    bool operator<=(const this_t& right) const
    {
        return !(*this > right);
    }

    bool operator>=(const this_t& right) const
    {
        return !(*this < right);
    }

    void operator++(int)
    {
        ++number;
        if (number > number_t::mask)
            number = MIN;
    }

    this_t& operator++() { (*this)++; return *this; }

    void operator--(int)
    {
        if (number == MIN)
            number = number_t::mask;
        else
            --number;
    }
    this_t& operator--() { (*this)--; return *this; }

    int32_t operator-(this_t right)
    {
        return Diff(this->number, right.number);
    }

    void operator+=(int32_t delta)
    {
        // NOTE: this condition in practice tests if delta is negative.
        // That's because `number` is always positive, so negated delta
        // can't be ever greater than this, unless it's negative.
        if (-delta > int64_t(number))
        {
            number = OVER - MIN + number + delta; // NOTE: delta is negative
        }
        else
        {
            number += delta;
            if (number >= OVER)
                number -= OVER - MIN;
        }
    }

    operator uint32_t() const { return number; }
};

////////////////////////////////////////////////////////////////////////////////

struct CIPAddress
{
   static bool ipcmp(const struct sockaddr* addr1, const struct sockaddr* addr2, int ver = AF_INET);
   static void ntop(const struct sockaddr_any& addr, uint32_t ip[4]);
   static void pton(sockaddr_any& addr, const uint32_t ip[4], const sockaddr_any& peer);
   static std::string show(const struct sockaddr* adr);
};

////////////////////////////////////////////////////////////////////////////////

struct CMD5
{
   static void compute(const char* input, unsigned char result[16]);
};

// Debug stats
template <size_t SIZE>
class StatsLossRecords
{
    int32_t initseq;
    std::bitset<SIZE> array;

public:
    StatsLossRecords(): initseq(SRT_SEQNO_NONE) {}

    // To check if this structure still keeps record of that sequence.
    // This is to check if the information about this not being found
    // is still reliable.
    bool exists(int32_t seq)
    {
        return initseq != SRT_SEQNO_NONE && CSeqNo::seqcmp(seq, initseq) >= 0;
    }

    int32_t base() { return initseq; }

    void clear()
    {
        initseq = SRT_SEQNO_NONE;
        array.reset();
    }

    void add(int32_t lo, int32_t hi)
    {
        int32_t end = CSeqNo::incseq(hi);
        for (int32_t i = lo; i != end; i = CSeqNo::incseq(i))
            add(i);
    }

    void add(int32_t seq)
    {
        if ( array.none() )
        {
            // May happen it wasn't initialized. Set it as initial loss sequence.
            initseq = seq;
            array[0] = true;
            return;
        }

        // Calculate the distance between this seq and the oldest one.
        int seqdiff = CSeqNo::seqoff(initseq, seq);
        if ( seqdiff > int(SIZE) )
        {
            // Size exceeded. Drop the oldest sequences.
            // First calculate how many must be removed.
            size_t toremove = seqdiff - SIZE;
            // Now, since that position, find the nearest 1
            while ( !array[toremove] && toremove <= SIZE )
                ++toremove;

            // All have to be dropped, so simply reset the array
            if ( toremove == SIZE )
            {
                initseq = seq;
                array[0] = true;
                return;
            }

            // Now do the shift of the first found 1 to position 0
            // and its index add to initseq
            initseq += toremove;
            seqdiff -= toremove;
            array >>= toremove;
        }

        // Now set appropriate bit that represents this seq
        array[seqdiff] = true;
    }

    StatsLossRecords& operator << (int32_t seq)
    {
        add(seq);
        return *this;
    }

    void remove(int32_t seq)
    {
        // Check if is in range. If not, ignore.
        int seqdiff = CSeqNo::seqoff(initseq, seq);
        if ( seqdiff < 0 )
            return; // already out of array
        if ( seqdiff > SIZE )
            return; // never was added!

        array[seqdiff] = true;
    }

    bool find(int32_t seq) const
    {
        int seqdiff = CSeqNo::seqoff(initseq, seq);
        if ( seqdiff < 0 )
            return false; // already out of array
        if ( size_t(seqdiff) > SIZE )
            return false; // never was added!

        return array[seqdiff];
    }

#if HAVE_CXX11

    std::string to_string() const
    {
        std::string out;
        for (size_t i = 0; i < SIZE; ++i)
        {
            if ( array[i] )
                out += std::to_string(initseq+i) + " ";
        }

        return out;
    }
#endif
};


// There are some better or worse things you can find outside,
// there's also boost::circular_buffer, but it's too overspoken
// to be included here. We also can't rely on boost. Maybe in future
// when it's added to the standard and SRT can heighten C++ standard
// requirements; until then it needs this replacement.
template <class Value>
class CircularBuffer
{
#ifdef SRT_TEST_CIRCULAR_BUFFER
public:
#endif
    int m_iSize;
    Value* m_aStorage;
    int m_xBegin;
    int m_xEnd;

    static void destr(Value& v)
    {
        v.~Value();
    }

    static void constr(Value& v)
    {
        new ((void*)&v) Value();
    }

    template <class V>
    static void constr(Value& v, const V& source)
    {
        new ((void*)&v) Value(source);
    }

    // Wipe the copy constructor
    CircularBuffer(const CircularBuffer&);

public:
    typedef Value value_type;

    CircularBuffer(int size)
        :m_iSize(size+1),
         m_xBegin(0),
         m_xEnd(0)
    {
        // We reserve one spare element just for a case.
        if (size == 0)
            m_aStorage = 0;
        else
            m_aStorage = (Value*)::operator new (sizeof(Value) * m_iSize);
    }

    void set_capacity(int size)
    {
        reset();

        // This isn't called resize (the size is 0 after the operation)
        // nor reserve (the existing elements are removed).
        if (size != m_iSize)
        {
            if (m_aStorage)
                ::operator delete (m_aStorage);
            m_iSize = size+1;
            m_aStorage = (Value*)::operator new (sizeof(Value) * m_iSize);
        }
    }

    void reset()
    {
        if (m_xEnd < m_xBegin)
        {
            for (int i = m_xBegin; i < m_iSize; ++i)
                destr(m_aStorage[i]);
            for (int i = 0; i < m_xEnd; ++i)
                destr(m_aStorage[i]);
        }
        else
        {
            for (int i = m_xBegin; i < m_xEnd; ++i)
                destr(m_aStorage[i]);
        }

        m_xBegin = 0;
        m_xEnd = 0;
    }

    ~CircularBuffer()
    {
        reset();
        ::operator delete (m_aStorage);
    }

    // In the beginning, m_xBegin == m_xEnd, which
    // means that the container is empty. Adding can
    // be done exactly at the place pointed to by m_xEnd,
    // and m_xEnd must be then shifted to the next unused one.
    // When (m_xEnd + 1) % m_zSize == m_xBegin, the container
    // is considered full and the element adding is rejected.
    //
    // This container is not designed to be STL-compatible
    // because it doesn't make much sense. It's not a typical
    // container, even treated as random-access container.

    int shift(int basepos, int shift) const
    {
        return (basepos + shift) % m_iSize;
    }

    // Simplified versions with ++ and --; avoid using division instruction
    int shift_forward(int basepos) const
    {
        if (++basepos == m_iSize)
            return 0;
        return basepos;
    }

    int shift_backward(int basepos) const
    {
        if (basepos == 0)
            return m_iSize-1;
        return --basepos;
    }

    int size() const
    {
        // Count the distance between begin and end
        if (m_xEnd < m_xBegin)
        {
            // Use "merge two slices" method.
            // (BEGIN - END) is the distance of the unused
            // space in the middle. Used space is left to END
            // and right to BEGIN, the sum of the left and right
            // slice and the free space is the size.

            // This includes also a case when begin and end
            // are equal, which means that it's empty, so
            // spaceleft() should simply return m_iSize.
            return m_iSize - (m_xBegin - m_xEnd);
        }

        return m_xEnd - m_xBegin;
    }

    bool empty() const { return m_xEnd == m_xBegin; }

    size_t capacity() const { return m_iSize-1; }

    int spaceleft() const
    {
        // It's kinda tautology, but this will be more efficient.
        if (m_xEnd < m_xBegin)
        {
            return m_xBegin - m_xEnd;
        }

        return m_iSize - (m_xEnd - m_xBegin);
    }

    // This is rather written for testing and rather won't
    // be used in the real code.
    template <class V>
    int push(const V& v)
    {
        // Check if you can add
        int nend = shift_forward(m_xEnd);
        if ( nend == m_xBegin)
            return -1;

        constr(m_aStorage[m_xEnd], v);
        m_xEnd = nend;
        return size() - 1;
    }

    Value* push()
    {
        int nend = shift_forward(m_xEnd);
        if ( nend == m_xBegin)
            return NULL;

        Value* pos = &m_aStorage[m_xEnd];
        constr(*pos);
        m_xEnd = nend;
        return pos;
    }

    bool access(int position, Value*& w_v)
    {
        // This version doesn't require the boolean value to report
        // whether the element is newly added because it never adds
        // a new element.
        int ipos, vend;

        if (!INT_checkAccess(position, ipos, vend))
            return false;
        if (ipos >= vend) // exceeds
            return false;

        INT_access(ipos, false, (w_v)); // never exceeds
        return true;
    }

    // Ok, now it's the real deal.
    bool access(int position, Value*& w_v, bool& w_isnew)
    {
        int ipos, vend;

        if (!INT_checkAccess(position, ipos, vend))
            return false;
        bool exceeds = (ipos >= vend);
        w_isnew = exceeds;

        INT_access(ipos, exceeds, (w_v));
        return true;
    }

private:
    bool INT_checkAccess(int position, int& ipos, int& vend)
    {
        // Reject if no space left.
        // Also INVAL if negative position.
        if (position >= (m_iSize-1) || position < 0)
            return false; // That's way to far, we can't even calculate

        ipos = m_xBegin + position;

        vend = m_xEnd;
        if (m_xEnd < m_xBegin)
            vend += m_iSize;

        return true;
    }

    void INT_access(int ipos, bool exceeds, Value*& w_v)
    {
        if (ipos >= m_iSize)
            ipos -= m_iSize; // wrap around

        // Update the end position.
        if (exceeds)
        {
            int nend = ipos+1;
            if (m_xEnd > nend)
            {
                // Here we know that the current index exceeds the size.
                // So, if this happens, it's m_xEnd wrapped around.
                // Clear out elements in two slices:
                // - from m_xEnd to m_iSize-1
                // - from 0 to nend
                for (int i = m_xEnd; i < m_iSize; ++i)
                    constr(m_aStorage[i]);
                for (int i = 0; i < nend; ++i)
                    constr(m_aStorage[i]);
            }
            else
            {
                for (int i = m_xEnd; i < nend; ++i)
                    constr(m_aStorage[i]);
            }

            if (nend == m_iSize)
                nend = 0;

            m_xEnd = nend;
        }

        w_v = &m_aStorage[ipos];
    }

public:
    bool set(int position, const Value& newval, bool overwrite = true)
    {
        Value* pval = 0;
        bool isnew = false;
        if (!access(position, (pval), (isnew)))
            return false;

        if (isnew || overwrite)
            *pval = newval;
        return true;
    }

    template<class Updater>
    bool update(int position, Updater updater)
    {
        Value* pval = 0;
        bool isnew = false;
        if (!access(position, (pval), (isnew)))
            return false;

        updater(*pval, isnew);
        return true;
    }

    int getIndexFor(int position) const
    {
        int ipos = m_xBegin + position;

        int vend = m_xEnd;
        if (vend < m_xBegin)
            vend += m_iSize;

        if (ipos >= vend)
            return -1;

        if (ipos >= m_iSize)
            ipos -= m_iSize;

        return ipos;
    }

    bool get(int position, Value& w_out) const
    {
        // Check if that position is occupied
        if (position > m_iSize || position < 0)
            return false;

        int ipos = getIndexFor(position);
        if (ipos == -1)
            return false;

        w_out = m_aStorage[ipos];
        return true;
    }

    bool drop(int position)
    {
        // This function "deletes" items by shifting the
        // given position to position 0. That is,
        // elements from the beginning are being deleted
        // up to (including) the given position.
        if (position > m_iSize || position < 1)
            return false;

        int ipos = m_xBegin + position;
        int vend = m_xEnd;
        if (vend < m_xBegin)
            vend += m_iSize;

        // Destroy the elements in the removed range

        if (ipos >= vend)
        {
            // There was a request to drop; the position
            // is higher than the number of items. Allow this
            // and simply make the container empty.
            reset();
            return true;
        }

        // Otherwise we have a new beginning.
        int nbegin = ipos;

        // Destroy the old elements
        if (nbegin >= m_iSize)
        {
            nbegin -= m_iSize;

            for (int i = m_xBegin; i < m_iSize; ++i)
                destr(m_aStorage[i]);
            for (int i = 0; i < nbegin; ++i)
                destr(m_aStorage[i]);
        }
        else
        {
            for (int i = m_xBegin; i < nbegin; ++i)
                destr(m_aStorage[i]);
        }

        m_xBegin = nbegin;

        return true;
    }

    // This function searches for an element that satisfies
    // the given predicate. If none found, returns -1.
    template <class Predicate>
    int find_if(Predicate pred)
    {
        if (m_xEnd < m_xBegin)
        {
            // Loop in two slices
            for (int i = m_xBegin; i < m_iSize; ++i)
                if (pred(m_aStorage[i]))
                    return i - m_xBegin;

            for (int i = 0; i < m_xEnd; ++i)
                if (pred(m_aStorage[i]))
                    return i + m_iSize - m_xBegin;
        }
        else
        {
            for (int i = m_xBegin; i < m_xEnd; ++i)
                if (pred(m_aStorage[i]))
                    return i - m_xBegin;
        }

        return -1;
    }
};

// Version parsing
inline ATR_CONSTEXPR uint32_t SrtVersion(int major, int minor, int patch)
{
    return patch + minor*0x100 + major*0x10000;
}

inline int32_t SrtParseVersion(const char* v)
{
    int major, minor, patch;
#if defined(_MSC_VER)
    int result = sscanf_s(v, "%d.%d.%d", &major, &minor, &patch);
#else
    int result = sscanf(v, "%d.%d.%d", &major, &minor, &patch);
#endif
    if (result != 3)
    {
        return 0;
    }

    return SrtVersion(major, minor, patch);
}

inline std::string SrtVersionString(int version)
{
    int patch = version % 0x100;
    int minor = (version/0x100)%0x100;
    int major = version/0x10000;

    char buf[22];
#if defined(_MSC_VER) && _MSC_VER < 1900
    _snprintf(buf, sizeof(buf) - 1, "%d.%d.%d", major, minor, patch);
#else
    snprintf(buf, sizeof(buf), "%d.%d.%d", major, minor, patch);
#endif
    return buf;
}

bool SrtParseConfig(const std::string& s, SrtConfig& w_config);

} // namespace srt

#endif
