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

#ifndef __UDT_LIST_H__
#define __UDT_LIST_H__


#include "udt.h"
#include "common.h"


class CSndLossList
{
public:
   CSndLossList(int size = 1024);
   ~CSndLossList();

      /// Insert a seq. no. into the sender loss list.
      /// @param [in] seqno1 sequence number starts.
      /// @param [in] seqno2 sequence number ends.
      /// @return number of packets that are not in the list previously.

   int insert(int32_t seqno1, int32_t seqno2);

      /// Remove ALL the seq. no. that are not greater than the parameter.
      /// @param [in] seqno sequence number.

   void remove(int32_t seqno);

      /// Read the loss length.
      /// @return The length of the list.

   int getLossLength() const;

      /// Read the first (smallest) loss seq. no. in the list and remove it.
      /// @return The seq. no. or -1 if the list is empty.

   int32_t popLostSeq();

private:
   struct Seq
   {
       int32_t data1;                  // sequence number starts
       int32_t data2;                  // seqnence number ends
       int next;                       // next node in the list
   }* m_caSeq;

   int m_iHead;                         // first node
   int m_iLength;                       // loss length
   int m_iSize;                         // size of the static array
   int m_iLastInsertPos;                // position of last insert node

   mutable pthread_mutex_t m_ListLock; // used to synchronize list operation

private:
   CSndLossList(const CSndLossList&);
   CSndLossList& operator=(const CSndLossList&);
};

////////////////////////////////////////////////////////////////////////////////

class CRcvLossList
{
public:
   CRcvLossList(int size = 1024);
   ~CRcvLossList();

      /// Insert a series of loss seq. no. between "seqno1" and "seqno2" into the receiver's loss list.
      /// @param [in] seqno1 sequence number starts.
      /// @param [in] seqno2 seqeunce number ends.

   void insert(int32_t seqno1, int32_t seqno2);

      /// Remove a loss seq. no. from the receiver's loss list.
      /// @param [in] seqno sequence number.
      /// @return if the packet is removed (true) or no such lost packet is found (false).

   bool remove(int32_t seqno);

      /// Remove all packets between seqno1 and seqno2.
      /// @param [in] seqno1 start sequence number.
      /// @param [in] seqno2 end sequence number.
      /// @return if the packet is removed (true) or no such lost packet is found (false).

   bool remove(int32_t seqno1, int32_t seqno2);

      /// Find if there is any lost packets whose sequence number falling seqno1 and seqno2.
      /// @param [in] seqno1 start sequence number.
      /// @param [in] seqno2 end sequence number.
      /// @return True if found; otherwise false.

   bool find(int32_t seqno1, int32_t seqno2) const;

      /// Read the loss length.
      /// @return the length of the list.

   int getLossLength() const;

      /// Read the first (smallest) seq. no. in the list.
      /// @return the sequence number or -1 if the list is empty.

   int getFirstLostSeq() const;

      /// Get a encoded loss array for NAK report.
      /// @param [out] array the result list of seq. no. to be included in NAK.
      /// @param [out] len physical length of the result array.
      /// @param [in] limit maximum length of the array.

   void getLossArray(int32_t* array, int& len, int limit);

private:
   struct Seq
   {
        int32_t data1;                  // sequence number starts
        int32_t data2;                  // sequence number ends
        int next;                       // next node in the list
        int prior;                      // prior node in the list;
   }* m_caSeq;

   int m_iHead;                         // first node in the list
   int m_iTail;                         // last node in the list;
   int m_iLength;                       // loss length
   int m_iSize;                         // size of the static array

private:
   CRcvLossList(const CRcvLossList&);
   CRcvLossList& operator=(const CRcvLossList&);
public:

   struct iterator
   {
       int32_t head;
       Seq* seq;

       iterator(Seq* str, int32_t v): head(v), seq(str) {}

       iterator next() const
       {
           if ( head == -1 )
               return *this; // should report error, but we can only throw exception, so simply ignore it.

           return iterator(seq, seq[head].next);
       }

       iterator& operator++()
       {
           *this = next();
           return *this;
       }

       iterator operator++(int)
       {
           iterator old (seq, head);
           *this = next();
           return old;
       }

       bool operator==(const iterator& second) const
       {
           // Ignore seq - should be the same and this is only a sanity check.
           return head == second.head;
       }

       bool operator!=(const iterator& second) const { return !(*this == second); }

       std::pair<int32_t, int32_t> operator*()
       {
           return std::make_pair(seq[head].data1, seq[head].data2);
       }
   };

   iterator begin() { return iterator(m_caSeq, m_iHead); }
   iterator end() { return iterator(m_caSeq, -1); }

};

struct CRcvFreshLoss
{
    int32_t seq[2];
    int ttl;
    uint64_t timestamp;

    CRcvFreshLoss(int32_t seqlo, int32_t seqhi, int initial_ttl);

// Don't WTF when looking at this. The Windows system headers define
// a publicly visible preprocessor macro with that name. REALLY!
#ifdef DELETE
#undef DELETE
#endif
    enum Emod {
        NONE, //< the given sequence was not found in this range
        STRIPPED, //< it was equal to first or last, already taken care of
        SPLIT, //< found in the middle, you have to split this range into two
        DELETE //< This was a range of one element exactly equal to sequence. Simply delete it.
    };

    Emod revoke(int32_t sequence);
    Emod revoke(int32_t lo, int32_t hi);
};

#endif
