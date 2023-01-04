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
            const int rtt = count_microseconds(currtime - r_aSeq[i].tsTimeStamp);

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
   for (int j = r_iTail, n = r_iHead + size; j < n; ++ j)
   {
      // Looking for an identical ACK Seq. No.
      if (seq == r_aSeq[j % size].iACKSeqNo)
      {
         // Return the Data ACK it carried
         j %= size;
         r_ack = r_aSeq[j].iACK;

         // Calculate RTT estimate
         const int rtt = count_microseconds(currtime - r_aSeq[j].tsTimeStamp);

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

void srt::CPktTimeWindowTools::initializeWindowArrays(int* r_pktWindow, int* r_probeWindow, int* r_bytesWindow, size_t asize, size_t psize)
{
   for (size_t i = 0; i < asize; ++ i)
      r_pktWindow[i] = 1000000;   //1 sec -> 1 pkt/sec

   for (size_t k = 0; k < psize; ++ k)
      r_probeWindow[k] = 1000;    //1 msec -> 1000 pkts/sec

   for (size_t i = 0; i < asize; ++ i)
      r_bytesWindow[i] = srt::CPacket::SRT_MAX_PAYLOAD_SIZE; //based on 1 pkt/sec set in r_pktWindow[i]
}


int srt::CPktTimeWindowTools::getPktRcvSpeed_in(const int* window, int* replica, const int* abytes, size_t asize, int& bytesps)
{
   // get median value, but cannot change the original value order in the window
   std::copy(window, window + asize, replica);
   std::nth_element(replica, replica + (asize / 2), replica + asize);
   //std::sort(replica, replica + asize);
   int median = replica[asize / 2];

   unsigned count = 0;
   int sum = 0;
   int upper = median << 3;
   int lower = median >> 3;

   bytesps = 0;
   unsigned long bytes = 0;
   const int* bp = abytes;
   // median filtering
   const int* p = window;
   for (int i = 0, n = asize; i < n; ++ i)
   {
      if ((*p < upper) && (*p > lower))
      {
         ++ count;  //packet counter
         sum += *p; //usec counter
         bytes += (unsigned long)*bp;   //byte counter
      }
      ++ p;     //advance packet pointer
      ++ bp;    //advance bytes pointer
   }

   // claculate speed, or return 0 if not enough valid value
   if (count > (asize >> 1))
   {
      bytes += (srt::CPacket::SRT_DATA_HDR_SIZE * count); //Add protocol headers to bytes received
      bytesps = (unsigned long)ceil(1000000.0 / (double(sum) / double(bytes)));
      return (int)ceil(1000000.0 / (sum / count));
   }
   else
   {
      bytesps = 0;
      return 0;
   }
}

int srt::CPktTimeWindowTools::getBandwidth_in(const int* window, int* replica, size_t psize)
{
    // This calculation does more-less the following:
    //
    // 1. Having example window:
    //  - 50, 51, 100, 55, 80, 1000, 600, 1500, 1200, 10, 90
    // 2. This window is now sorted, but we only know the value in the middle:
    //  - 10, 50, 51, 55, 80, [[90]], 100, 600, 1000, 1200, 1500
    // 3. Now calculate:
    //   - lower: 90/8 = 11.25
    //   - upper: 90*8 = 720
    // 4. Now calculate the arithmetic median from all these values,
    //    but drop those from outside the <lower, upper> range:
    //  - 10, (11<) [ 50, 51, 55, 80, 90, 100, 600, ] (>720) 1000, 1200, 1500
    // 5. Calculate the median from the extracted range,
    //    NOTE: the median is actually repeated once, so size is +1.
    //
    //    values = { 50, 51, 55, 80, 90, 100, 600 };
    //    sum = 90 + accumulate(values); ==> 1026
    //    median = sum/(1 + values.size()); ==> 147
    //
    // For comparison: the overall arithmetic median from this window == 430
    //
    // 6. Returned value = 1M/median

   // get median value, but cannot change the original value order in the window
   std::copy(window, window + psize - 1, replica);
   std::nth_element(replica, replica + (psize / 2), replica + psize - 1);
   //std::sort(replica, replica + psize); <--- was used for debug, just leave it as a mark
   int median = replica[psize / 2];

   int count = 1;
   int sum = median;
   int upper = median << 3; // median*8
   int lower = median >> 3; // median/8

   // median filtering
   const int* p = window;
   for (int i = 0, n = psize; i < n; ++ i)
   {
      if ((*p < upper) && (*p > lower))
      {
         ++ count;
         sum += *p;
      }
      ++ p;
   }

   return (int)ceil(1000000.0 / (double(sum) / double(count)));
}


