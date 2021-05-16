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
   Yunhong Gu, last updated 02/28/2012
*****************************************************************************/


#ifndef __UDT_CCC_H__
#define __UDT_CCC_H__


#include "udt.h"
#include "packet.h"


class UDT_API CCC
{
friend class CUDT;

public:
   CCC();
   virtual ~CCC();

private:
   CCC(const CCC&);
   CCC& operator=(const CCC&) {return *this;}

public:

      /// Callback function to be called (only) at the start of a UDT connection.
      /// note that this is different from CCC(), which is always called.

   virtual void init() {}

      /// Callback function to be called when a UDT connection is closed.

   virtual void close() {}

      /// Callback function to be called when an ACK packet is received.
      /// @param [in] ackno the data sequence number acknowledged by this ACK.

   virtual void onACK(int32_t) {}

      /// Callback function to be called when a loss report is received.
      /// @param [in] losslist list of sequence number of packets, in the format describled in packet.cpp.
      /// @param [in] size length of the loss list.

   virtual void onLoss(const int32_t*, int) {}

      /// Callback function to be called when a timeout event occurs.

   virtual void onTimeout() {}

      /// Callback function to be called when a data is sent.
      /// @param [in] seqno the data sequence number.
      /// @param [in] size the payload size.

   virtual void onPktSent(const CPacket*) {}

      /// Callback function to be called when a data is received.
      /// @param [in] seqno the data sequence number.
      /// @param [in] size the payload size.

   virtual void onPktReceived(const CPacket*) {}

      /// Callback function to Process a user defined packet.
      /// @param [in] pkt the user defined packet.

   virtual void processCustomMsg(const CPacket*) {}

protected:

      /// Set periodical acknowldging and the ACK period.
      /// @param [in] msINT the period to send an ACK.

   void setACKTimer(int msINT);

      /// Set packet-based acknowldging and the number of packets to send an ACK.
      /// @param [in] pktINT the number of packets to send an ACK.

   void setACKInterval(int pktINT);

      /// Set RTO value.
      /// @param [in] msRTO RTO in macroseconds.

   void setRTO(int usRTO);

      /// Send a user defined control packet.
      /// @param [in] pkt user defined packet.

   void sendCustomMsg(CPacket& pkt) const;

      /// retrieve performance information.
      /// @return Pointer to a performance info structure.

   const CPerfMon* getPerfInfo();

      /// Set user defined parameters.
      /// @param [in] param the paramters in one buffer.
      /// @param [in] size the size of the buffer.

   void setUserParam(const char* param, int size);

private:
   void setMSS(int mss);
   void setMaxCWndSize(int cwnd);
   void setBandwidth(int bw);
   void setSndCurrSeqNo(int32_t seqno);
   void setRcvRate(int rcvrate);
   void setRTT(int rtt);

protected:
   const int32_t& m_iSYNInterval;	// UDT constant parameter, SYN

   double m_dPktSndPeriod;              // Packet sending period, in microseconds
   double m_dCWndSize;                  // Congestion window size, in packets

   int m_iBandwidth;			// estimated bandwidth, packets per second
   double m_dMaxCWndSize;               // maximum cwnd size, in packets

   int m_iMSS;				// Maximum Packet Size, including all packet headers
   int32_t m_iSndCurrSeqNo;		// current maximum seq no sent out
   int m_iRcvRate;			// packet arrive rate at receiver side, packets per second
   int m_iRTT;				// current estimated RTT, microsecond

   char* m_pcParam;			// user defined parameter
   int m_iPSize;			// size of m_pcParam

private:
   UDTSOCKET m_UDT;                     // The UDT entity that this congestion control algorithm is bound to

   int m_iACKPeriod;                    // Periodical timer to send an ACK, in milliseconds
   int m_iACKInterval;                  // How many packets to send one ACK, in packets

   bool m_bUserDefinedRTO;              // if the RTO value is defined by users
   int m_iRTO;                          // RTO value, microseconds

   CPerfMon m_PerfInfo;                 // protocol statistics information
};

class CCCVirtualFactory
{
public:
   virtual ~CCCVirtualFactory() {}

   virtual CCC* create() = 0;
   virtual CCCVirtualFactory* clone() = 0;
};

template <class T>
class CCCFactory: public CCCVirtualFactory
{
public:
   virtual ~CCCFactory() {}

   virtual CCC* create() {return new T;}
   virtual CCCVirtualFactory* clone() {return new CCCFactory<T>;}
};

class CUDTCC: public CCC
{
public:
   CUDTCC();

public:
   virtual void init();
   virtual void onACK(int32_t);
   virtual void onLoss(const int32_t*, int);
   virtual void onTimeout();

private:
   int m_iRCInterval;			// UDT Rate control interval
   uint64_t m_LastRCTime;		// last rate increase time
   bool m_bSlowStart;			// if in slow start phase
   int32_t m_iLastAck;			// last ACKed seq no
   bool m_bLoss;			// if loss happened since last rate increase
   int32_t m_iLastDecSeq;		// max pkt seq no sent out when last decrease happened
   double m_dLastDecPeriod;		// value of pktsndperiod when last decrease happened
   int m_iNAKCount;                     // NAK counter
   int m_iDecRandom;                    // random threshold on decrease by number of loss events
   int m_iAvgNAKNum;                    // average number of NAKs per congestion
   int m_iDecCount;			// number of decreases in a congestion epoch
};

#endif
