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
   Yunhong Gu, last updated 01/22/2011
modified by
   Haivision Systems Inc.
*****************************************************************************/

#include "platform_sys.h"

#include <cmath>
#include <cstring>
#include "common.h"
#include "window.h"
#include <algorithm>

using namespace std;
using namespace srt::sync;

namespace srt
{
namespace ACKWindowTools
{

void store(Seq* r_aSeq, const size_t size, int& r_iHead, int& r_iTail, int32_t seq, int32_t ack)
{
   r_aSeq[r_iHead].iACKSeqNo = seq;
   r_aSeq[r_iHead].iACK = ack;
   r_aSeq[r_iHead].tsTimeStamp = steady_clock::now();

   r_iHead = (r_iHead + 1) % size;

   // overwrite the oldest ACK since it is not likely to be acknowledged
   if (r_iHead == r_iTail)
      r_iTail = (r_iTail + 1) % size;
}

int acknowledge(Seq* r_aSeq, const size_t size, int& r_iHead, int& r_iTail, int32_t seq, int32_t& r_ack, const steady_clock::time_point& currtime)
{
   // Head has not exceeded the physical boundary of the window
   if (r_iHead >= r_iTail)
   {
      for (int i = r_iTail, n = r_iHead; i < n; ++ i)
      {
         // Looking for an identical ACK Seq. No.
         if (seq == r_aSeq[i].iACKSeqNo)
         {
            // Return the Data ACK it carried
            r_ack = r_aSeq[i].iACK;

            // Calculate RTT estimate
            const int rtt = (int)count_microseconds(currtime - r_aSeq[i].tsTimeStamp);

            if (i + 1 == r_iHead)
            {
               r_iTail = r_iHead = 0;
               r_aSeq[0].iACKSeqNo = SRT_SEQNO_NONE;
            }
            else
               r_iTail = (i + 1) % size;

            return rtt;
         }
      }

      // The record about ACK is not found in the buffer, RTT can not be calculated
      return -1;
   }

   // Head has exceeded the physical window boundary, so it is behind tail
   for (int j = r_iTail, n = r_iHead + (int)size; j < n; ++ j)
   {
      // Looking for an identical ACK Seq. No.
      if (seq == r_aSeq[j % size].iACKSeqNo)
      {
         // Return the Data ACK it carried
         j %= size;
         r_ack = r_aSeq[j].iACK;

         // Calculate RTT estimate
         const int rtt = (int)count_microseconds(currtime - r_aSeq[j].tsTimeStamp);

         if (j == r_iHead)
         {
            r_iTail = r_iHead = 0;
            r_aSeq[0].iACKSeqNo = -1;
         }
         else
            r_iTail = (j + 1) % size;

         return rtt;
      }
   }

   // The record about ACK is not found in the buffer, RTT can not be calculated
   return -1;
}

} // namespace AckTools
} // namespace srt

////////////////////////////////////////////////////////////////////////////////

void srt::CPktTimeWindowTools::initializeWindowArrays(int* r_pktWindow, int* r_probeWindow, int* r_bytesWindow, size_t asize, size_t psize, size_t max_payload_size)
{
   for (size_t i = 0; i < asize; ++ i)
      r_pktWindow[i] = 1000000;   //1 sec -> 1 pkt/sec

   for (size_t k = 0; k < psize; ++ k)
      r_probeWindow[k] = 1000;    //1 msec -> 1000 pkts/sec

   for (size_t i = 0; i < asize; ++ i)
      r_bytesWindow[i] = max_payload_size; //based on 1 pkt/sec set in r_pktWindow[i]
}

int srt::CPktTimeWindowTools::ceilPerMega(double value, double count)
{
    static const double MEGA = 1000.0 * 1000.0;
    return ::ceil(MEGA / (value / count));
}

int srt::CPktTimeWindowTools::getPktRcvSpeed_in(const int* window, int* replica, const int* abytes, size_t asize, size_t hdr_size, int& w_bytesps)
{
    PassFilter<int> filter = GetPeakRange(window, replica, asize);

    unsigned count = 0;
    int sum = 0;

    w_bytesps = 0;
    unsigned long bytes = 0;
    // // (explicit specialization due to problems on MSVC 2013 and 2015)
    AccumulatePassFilterParallel<unsigned, unsigned long>(window, asize, filter, abytes,
            (sum), (count), (bytes));

    // claculate speed, or return 0 if not enough valid value
    if (count <= (asize/2))
    {
        w_bytesps = 0;
        return 0;
    }

    bytes += (hdr_size * count); //Add protocol headers to bytes received
    w_bytesps = ceilPerMega(sum, bytes);
    return ceilPerMega(sum, count);
}

int srt::CPktTimeWindowTools::getBandwidth_in(const int* window, int* replica, size_t psize)
{
    PassFilter<int> filter = GetPeakRange(window, replica, psize);

    int sum, count;
    Tie2(sum, count) = AccumulatePassFilter(window, psize, filter);
    sum   += filter.median;
    count += 1;

    return ceilPerMega(sum, count);
}


