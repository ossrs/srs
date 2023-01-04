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

#include "list.h"
#include "packet.h"
#include "logging.h"

// Use "inline namespace" in C++11
namespace srt_logging
{
extern Logger qrlog;
extern Logger qslog;
}

using srt_logging::qrlog;
using srt_logging::qslog;

using namespace srt::sync;

srt::CSndLossList::CSndLossList(int size)
    : m_caSeq()
    , m_iHead(-1)
    , m_iLength(0)
    , m_iSize(size)
    , m_iLastInsertPos(-1)
    , m_ListLock()
{
    m_caSeq = new Seq[size];

    // -1 means there is no data in the node
    for (int i = 0; i < size; ++i)
    {
        m_caSeq[i].seqstart = SRT_SEQNO_NONE;
        m_caSeq[i].seqend   = SRT_SEQNO_NONE;
    }

    // sender list needs mutex protection
    setupMutex(m_ListLock, "LossList");
}

srt::CSndLossList::~CSndLossList()
{
    delete[] m_caSeq;
    releaseMutex(m_ListLock);
}

void srt::CSndLossList::traceState() const
{
    int pos = m_iHead;
    while (pos != SRT_SEQNO_NONE)
    {
        std::cout << pos << ":[" << m_caSeq[pos].seqstart;
        if (m_caSeq[pos].seqend != SRT_SEQNO_NONE)
            std::cout << ", " << m_caSeq[pos].seqend;
        std::cout << "], ";
        pos = m_caSeq[pos].inext;
    }
    std::cout << "\n";
}

int srt::CSndLossList::insert(int32_t seqno1, int32_t seqno2)
{
    if (seqno1 < 0 || seqno2 < 0 ) {
        LOGC(qslog.Error, log << "IPE: Tried to insert negative seqno " << seqno1 << ":" << seqno2
            << " into sender's loss list. Ignoring.");
        return 0;
    }

    const int inserted_range = CSeqNo::seqlen(seqno1, seqno2);
    if (inserted_range <= 0 || inserted_range >= m_iSize) {
        LOGC(qslog.Error, log << "IPE: Tried to insert too big range of seqno: " << inserted_range <<  ". Ignoring. "
                << "seqno " << seqno1 << ":" << seqno2);
        return 0;
    }

    ScopedLock listguard(m_ListLock);

    if (m_iLength == 0)
    {
        insertHead(0, seqno1, seqno2);
        return m_iLength;
    }

    // Find the insert position in the non-empty list
    const int origlen = m_iLength;
    const int offset  = CSeqNo::seqoff(m_caSeq[m_iHead].seqstart, seqno1);

    if (offset >= m_iSize)
    {
        LOGC(qslog.Error, log << "IPE: New loss record is too far from the first record. Ignoring. "
                << "First loss seqno " << m_caSeq[m_iHead].seqstart
                << ", insert seqno " << seqno1 << ":" << seqno2);
        return 0;
    }

    int loc = (m_iHead + offset + m_iSize) % m_iSize;

    if (loc < 0)
    {
        const int offset_seqno2 = CSeqNo::seqoff(m_caSeq[m_iHead].seqstart, seqno2);
        const int loc_seqno2    = (m_iHead + offset_seqno2 + m_iSize) % m_iSize;

        if (loc_seqno2 < 0)
        {
            // The size of the CSndLossList should be at least the size of the flow window.
            // It means that all the packets sender has sent should fit within m_iSize.
            // If the new loss does not fit, there is some error.
            LOGC(qslog.Error, log << "IPE: New loss record is too old. Ignoring. "
                << "First loss seqno " << m_caSeq[m_iHead].seqstart
                << ", insert seqno " << seqno1 << ":" << seqno2);
            return 0;
        }

        loc = loc_seqno2;
    }

    if (offset < 0)
    {
        insertHead(loc, seqno1, seqno2);
    }
    else if (offset > 0)
    {
        if (seqno1 == m_caSeq[loc].seqstart)
        {
            const bool updated = updateElement(loc, seqno1, seqno2);
            if (!updated)
                return 0;
        }
        else
        {
            // Find the prior node.
            // It should be the highest sequence number less than seqno1.
            // 1. Start the search either from m_iHead, or from m_iLastInsertPos
            int i = m_iHead;
            if ((m_iLastInsertPos != -1) && (CSeqNo::seqcmp(m_caSeq[m_iLastInsertPos].seqstart, seqno1) < 0))
                i = m_iLastInsertPos;

            // 2. Find the highest sequence number less than seqno1.
            while (m_caSeq[i].inext != -1 && CSeqNo::seqcmp(m_caSeq[m_caSeq[i].inext].seqstart, seqno1) < 0)
                i = m_caSeq[i].inext;

            // 3. Check if seqno1 overlaps with (seqbegin, seqend)
            const int seqend = m_caSeq[i].seqend == SRT_SEQNO_NONE ? m_caSeq[i].seqstart : m_caSeq[i].seqend;

            if (CSeqNo::seqcmp(seqend, seqno1) < 0 && CSeqNo::incseq(seqend) != seqno1)
            {
                // No overlap
                // TODO: Here we should actually insert right after i, not at loc.
                insertAfter(loc, i, seqno1, seqno2);
            }
            else
            {
                // TODO: Replace with updateElement(i, seqno1, seqno2).
                // Some changes to updateElement(..) are required.
                m_iLastInsertPos = i;
                if (CSeqNo::seqcmp(seqend, seqno2) >= 0)
                    return 0;

                // overlap, coalesce with prior node, insert(3, 7) to [2, 5], ... becomes [2, 7]
                m_iLength += CSeqNo::seqlen(seqend, seqno2) - 1;
                m_caSeq[i].seqend = seqno2;

                loc = i;
            }
        }
    }
    else // offset == 0, loc == m_iHead
    {
        const bool updated = updateElement(m_iHead, seqno1, seqno2);
        if (!updated)
            return 0;
    }

    coalesce(loc);
    return m_iLength - origlen;
}

void srt::CSndLossList::removeUpTo(int32_t seqno)
{
    ScopedLock listguard(m_ListLock);

    if (0 == m_iLength)
        return;

    // Remove all from the head pointer to a node with a larger seq. no. or the list is empty
    int offset = CSeqNo::seqoff(m_caSeq[m_iHead].seqstart, seqno);
    int loc    = (m_iHead + offset + m_iSize) % m_iSize;

    if (0 == offset)
    {
        // It is the head. Remove the head and point to the next node
        loc = (loc + 1) % m_iSize;

        if (SRT_SEQNO_NONE == m_caSeq[m_iHead].seqend)
            loc = m_caSeq[m_iHead].inext;
        else
        {
            m_caSeq[loc].seqstart = CSeqNo::incseq(seqno);
            if (CSeqNo::seqcmp(m_caSeq[m_iHead].seqend, CSeqNo::incseq(seqno)) > 0)
                m_caSeq[loc].seqend = m_caSeq[m_iHead].seqend;

            m_caSeq[m_iHead].seqend = SRT_SEQNO_NONE;

            m_caSeq[loc].inext = m_caSeq[m_iHead].inext;
        }

        m_caSeq[m_iHead].seqstart = SRT_SEQNO_NONE;

        if (m_iLastInsertPos == m_iHead)
            m_iLastInsertPos = -1;

        m_iHead = loc;

        m_iLength--;
    }
    else if (offset > 0)
    {
        int h = m_iHead;

        if (seqno == m_caSeq[loc].seqstart)
        {
            // target node is not empty, remove part/all of the seqno in the node.
            int temp = loc;
            loc      = (loc + 1) % m_iSize;

            if (SRT_SEQNO_NONE == m_caSeq[temp].seqend)
                m_iHead = m_caSeq[temp].inext;
            else
            {
                // remove part, e.g., [3, 7] becomes [], [4, 7] after remove(3)
                m_caSeq[loc].seqstart = CSeqNo::incseq(seqno);
                if (CSeqNo::seqcmp(m_caSeq[temp].seqend, m_caSeq[loc].seqstart) > 0)
                    m_caSeq[loc].seqend = m_caSeq[temp].seqend;
                m_iHead              = loc;
                m_caSeq[loc].inext   = m_caSeq[temp].inext;
                m_caSeq[temp].inext  = loc;
                m_caSeq[temp].seqend = SRT_SEQNO_NONE;
            }
        }
        else
        {
            // target node is empty, check prior node
            int i = m_iHead;
            while ((-1 != m_caSeq[i].inext) && (CSeqNo::seqcmp(m_caSeq[m_caSeq[i].inext].seqstart, seqno) < 0))
                i = m_caSeq[i].inext;

            loc = (loc + 1) % m_iSize;

            if (SRT_SEQNO_NONE == m_caSeq[i].seqend)
                m_iHead = m_caSeq[i].inext;
            else if (CSeqNo::seqcmp(m_caSeq[i].seqend, seqno) > 0)
            {
                // remove part/all seqno in the prior node
                m_caSeq[loc].seqstart = CSeqNo::incseq(seqno);
                if (CSeqNo::seqcmp(m_caSeq[i].seqend, m_caSeq[loc].seqstart) > 0)
                    m_caSeq[loc].seqend = m_caSeq[i].seqend;

                m_caSeq[i].seqend = seqno;

                m_caSeq[loc].inext = m_caSeq[i].inext;
                m_caSeq[i].inext   = loc;

                m_iHead = loc;
            }
            else
                m_iHead = m_caSeq[i].inext;
        }

        // Remove all nodes prior to the new head
        while (h != m_iHead)
        {
            if (m_caSeq[h].seqend != SRT_SEQNO_NONE)
            {
                m_iLength -= CSeqNo::seqlen(m_caSeq[h].seqstart, m_caSeq[h].seqend);
                m_caSeq[h].seqend = SRT_SEQNO_NONE;
            }
            else
                m_iLength--;

            m_caSeq[h].seqstart = SRT_SEQNO_NONE;

            if (m_iLastInsertPos == h)
                m_iLastInsertPos = -1;

            h = m_caSeq[h].inext;
        }
    }
}

int srt::CSndLossList::getLossLength() const
{
    ScopedLock listguard(m_ListLock);

    return m_iLength;
}

int32_t srt::CSndLossList::popLostSeq()
{
    ScopedLock listguard(m_ListLock);

    if (0 == m_iLength)
    {
        SRT_ASSERT(m_iHead == -1);
        return SRT_SEQNO_NONE;
    }

    if (m_iLastInsertPos == m_iHead)
        m_iLastInsertPos = -1;

    // return the first loss seq. no.
    const int32_t seqno = m_caSeq[m_iHead].seqstart;

    // head moves to the next node
    if (SRT_SEQNO_NONE == m_caSeq[m_iHead].seqend)
    {
        //[3, SRT_SEQNO_NONE] becomes [], and head moves to next node in the list
        m_caSeq[m_iHead].seqstart = SRT_SEQNO_NONE;
        m_iHead                   = m_caSeq[m_iHead].inext;
    }
    else
    {
        // shift to next node, e.g., [3, 7] becomes [], [4, 7]
        int loc = (m_iHead + 1) % m_iSize;

        m_caSeq[loc].seqstart = CSeqNo::incseq(seqno);
        if (CSeqNo::seqcmp(m_caSeq[m_iHead].seqend, m_caSeq[loc].seqstart) > 0)
            m_caSeq[loc].seqend = m_caSeq[m_iHead].seqend;

        m_caSeq[m_iHead].seqstart = SRT_SEQNO_NONE;
        m_caSeq[m_iHead].seqend   = SRT_SEQNO_NONE;

        m_caSeq[loc].inext = m_caSeq[m_iHead].inext;
        m_iHead            = loc;
    }

    m_iLength--;

    return seqno;
}

void srt::CSndLossList::insertHead(int pos, int32_t seqno1, int32_t seqno2)
{
    SRT_ASSERT(pos >= 0);
    m_caSeq[pos].seqstart = seqno1;
    SRT_ASSERT(m_caSeq[pos].seqend == SRT_SEQNO_NONE);
    if (seqno2 != seqno1)
        m_caSeq[pos].seqend = seqno2;

    // new node becomes head
    m_caSeq[pos].inext = m_iHead;
    m_iHead            = pos;
    m_iLastInsertPos   = pos;

    m_iLength += CSeqNo::seqlen(seqno1, seqno2);
}

void srt::CSndLossList::insertAfter(int pos, int pos_after, int32_t seqno1, int32_t seqno2)
{
    m_caSeq[pos].seqstart = seqno1;
    SRT_ASSERT(m_caSeq[pos].seqend == SRT_SEQNO_NONE);
    if (seqno2 != seqno1)
        m_caSeq[pos].seqend = seqno2;

    m_caSeq[pos].inext       = m_caSeq[pos_after].inext;
    m_caSeq[pos_after].inext = pos;
    m_iLastInsertPos         = pos;

    m_iLength += CSeqNo::seqlen(seqno1, seqno2);
}

void srt::CSndLossList::coalesce(int loc)
{
    // coalesce with next node. E.g., [3, 7], ..., [6, 9] becomes [3, 9]
    while ((m_caSeq[loc].inext != -1) && (m_caSeq[loc].seqend != SRT_SEQNO_NONE))
    {
        const int i = m_caSeq[loc].inext;
        if (CSeqNo::seqcmp(m_caSeq[i].seqstart, CSeqNo::incseq(m_caSeq[loc].seqend)) > 0)
            break;

        // coalesce if there is overlap
        if (m_caSeq[i].seqend != SRT_SEQNO_NONE)
        {
            if (CSeqNo::seqcmp(m_caSeq[i].seqend, m_caSeq[loc].seqend) > 0)
            {
                if (CSeqNo::seqcmp(m_caSeq[loc].seqend, m_caSeq[i].seqstart) >= 0)
                    m_iLength -= CSeqNo::seqlen(m_caSeq[i].seqstart, m_caSeq[loc].seqend);

                m_caSeq[loc].seqend = m_caSeq[i].seqend;
            }
            else
                m_iLength -= CSeqNo::seqlen(m_caSeq[i].seqstart, m_caSeq[i].seqend);
        }
        else
        {
            if (m_caSeq[i].seqstart == CSeqNo::incseq(m_caSeq[loc].seqend))
                m_caSeq[loc].seqend = m_caSeq[i].seqstart;
            else
                m_iLength--;
        }

        m_caSeq[i].seqstart = SRT_SEQNO_NONE;
        m_caSeq[i].seqend   = SRT_SEQNO_NONE;
        m_caSeq[loc].inext  = m_caSeq[i].inext;
    }
}

bool srt::CSndLossList::updateElement(int pos, int32_t seqno1, int32_t seqno2)
{
    m_iLastInsertPos = pos;

    if (seqno2 == SRT_SEQNO_NONE || seqno2 == seqno1)
        return false;

    if (m_caSeq[pos].seqend == SRT_SEQNO_NONE)
    {
        m_iLength += CSeqNo::seqlen(seqno1, seqno2) - 1;
        m_caSeq[pos].seqend = seqno2;
        return true;
    }

    // seqno2 <= m_caSeq[pos].seqend
    if (CSeqNo::seqcmp(seqno2, m_caSeq[pos].seqend) <= 0)
        return false;

    // seqno2 > m_caSeq[pos].seqend
    m_iLength += CSeqNo::seqlen(m_caSeq[pos].seqend, seqno2) - 1;
    m_caSeq[pos].seqend = seqno2;
    return true;
}

////////////////////////////////////////////////////////////////////////////////

srt::CRcvLossList::CRcvLossList(int size)
    : m_caSeq()
    , m_iHead(-1)
    , m_iTail(-1)
    , m_iLength(0)
    , m_iSize(size)
    , m_iLargestSeq(SRT_SEQNO_NONE)
{
    m_caSeq = new Seq[m_iSize];

    // -1 means there is no data in the node
    for (int i = 0; i < size; ++i)
    {
        m_caSeq[i].seqstart = SRT_SEQNO_NONE;
        m_caSeq[i].seqend   = SRT_SEQNO_NONE;
    }
}

srt::CRcvLossList::~CRcvLossList()
{
    delete[] m_caSeq;
}

void srt::CRcvLossList::insert(int32_t seqno1, int32_t seqno2)
{
    // Data to be inserted must be larger than all those in the list
    if (m_iLargestSeq != SRT_SEQNO_NONE && CSeqNo::seqcmp(seqno1, m_iLargestSeq) <= 0)
    {
        if (CSeqNo::seqcmp(seqno2, m_iLargestSeq) > 0)
        {
            LOGC(qrlog.Warn,
                 log << "RCV-LOSS/insert: seqno1=" << seqno1 << " too small, adjust to "
                     << CSeqNo::incseq(m_iLargestSeq));
            seqno1 = CSeqNo::incseq(m_iLargestSeq);
        }
        else
        {
            LOGC(qrlog.Warn,
                 log << "RCV-LOSS/insert: (" << seqno1 << "," << seqno2
                     << ") to be inserted is too small: m_iLargestSeq=" << m_iLargestSeq << ", m_iLength=" << m_iLength
                     << ", m_iHead=" << m_iHead << ", m_iTail=" << m_iTail << " -- REJECTING");
            return;
        }
    }
    m_iLargestSeq = seqno2;

    if (0 == m_iLength)
    {
        // insert data into an empty list
        m_iHead                   = 0;
        m_iTail                   = 0;
        m_caSeq[m_iHead].seqstart = seqno1;
        if (seqno2 != seqno1)
            m_caSeq[m_iHead].seqend = seqno2;

        m_caSeq[m_iHead].inext  = -1;
        m_caSeq[m_iHead].iprior = -1;
        m_iLength += CSeqNo::seqlen(seqno1, seqno2);

        return;
    }

    // otherwise searching for the position where the node should be
    int offset = CSeqNo::seqoff(m_caSeq[m_iHead].seqstart, seqno1);
    if (offset < 0)
    {
        LOGC(qrlog.Error,
             log << "RCV-LOSS/insert: IPE: new LOSS %(" << seqno1 << "-" << seqno2 << ") PREDATES HEAD %"
                 << m_caSeq[m_iHead].seqstart << " -- REJECTING");
        return;
    }

    int loc = (m_iHead + offset) % m_iSize;

    if ((SRT_SEQNO_NONE != m_caSeq[m_iTail].seqend) && (CSeqNo::incseq(m_caSeq[m_iTail].seqend) == seqno1))
    {
        // coalesce with prior node, e.g., [2, 5], [6, 7] becomes [2, 7]
        loc                 = m_iTail;
        m_caSeq[loc].seqend = seqno2;
    }
    else
    {
        // create new node
        m_caSeq[loc].seqstart = seqno1;

        if (seqno2 != seqno1)
            m_caSeq[loc].seqend = seqno2;

        m_caSeq[m_iTail].inext = loc;
        m_caSeq[loc].iprior    = m_iTail;
        m_caSeq[loc].inext     = -1;
        m_iTail                = loc;
    }

    m_iLength += CSeqNo::seqlen(seqno1, seqno2);
}

bool srt::CRcvLossList::remove(int32_t seqno)
{
    if (m_iLargestSeq == SRT_SEQNO_NONE || CSeqNo::seqcmp(seqno, m_iLargestSeq) > 0)
        m_iLargestSeq = seqno;

    if (0 == m_iLength)
        return false;

    // locate the position of "seqno" in the list
    int offset = CSeqNo::seqoff(m_caSeq[m_iHead].seqstart, seqno);
    if (offset < 0)
        return false;

    int loc = (m_iHead + offset) % m_iSize;

    if (seqno == m_caSeq[loc].seqstart)
    {
        // This is a seq. no. that starts the loss sequence

        if (SRT_SEQNO_NONE == m_caSeq[loc].seqend)
        {
            // there is only 1 loss in the sequence, delete it from the node
            if (m_iHead == loc)
            {
                m_iHead = m_caSeq[m_iHead].inext;
                if (-1 != m_iHead)
                    m_caSeq[m_iHead].iprior = -1;
                else
                    m_iTail = -1;
            }
            else
            {
                m_caSeq[m_caSeq[loc].iprior].inext = m_caSeq[loc].inext;
                if (-1 != m_caSeq[loc].inext)
                    m_caSeq[m_caSeq[loc].inext].iprior = m_caSeq[loc].iprior;
                else
                    m_iTail = m_caSeq[loc].iprior;
            }

            m_caSeq[loc].seqstart = SRT_SEQNO_NONE;
        }
        else
        {
            // there are more than 1 loss in the sequence
            // move the node to the next and update the starter as the next loss inSeqNo(seqno)

            // find next node
            int i = (loc + 1) % m_iSize;

            // remove the "seqno" and change the starter as next seq. no.
            m_caSeq[i].seqstart = CSeqNo::incseq(m_caSeq[loc].seqstart);

            // process the sequence end
            if (CSeqNo::seqcmp(m_caSeq[loc].seqend, CSeqNo::incseq(m_caSeq[loc].seqstart)) > 0)
                m_caSeq[i].seqend = m_caSeq[loc].seqend;

            // remove the current node
            m_caSeq[loc].seqstart = SRT_SEQNO_NONE;
            m_caSeq[loc].seqend   = SRT_SEQNO_NONE;

            // update list pointer
            m_caSeq[i].inext  = m_caSeq[loc].inext;
            m_caSeq[i].iprior = m_caSeq[loc].iprior;

            if (m_iHead == loc)
                m_iHead = i;
            else
                m_caSeq[m_caSeq[i].iprior].inext = i;

            if (m_iTail == loc)
                m_iTail = i;
            else
                m_caSeq[m_caSeq[i].inext].iprior = i;
        }

        m_iLength--;

        return true;
    }

    // There is no loss sequence in the current position
    // the "seqno" may be contained in a previous node

    // searching previous node
    int i = (loc - 1 + m_iSize) % m_iSize;
    while (SRT_SEQNO_NONE == m_caSeq[i].seqstart)
        i = (i - 1 + m_iSize) % m_iSize;

    // not contained in this node, return
    if ((SRT_SEQNO_NONE == m_caSeq[i].seqend) || (CSeqNo::seqcmp(seqno, m_caSeq[i].seqend) > 0))
        return false;

    if (seqno == m_caSeq[i].seqend)
    {
        // it is the sequence end

        if (seqno == CSeqNo::incseq(m_caSeq[i].seqstart))
            m_caSeq[i].seqend = SRT_SEQNO_NONE;
        else
            m_caSeq[i].seqend = CSeqNo::decseq(seqno);
    }
    else
    {
        // split the sequence

        // construct the second sequence from CSeqNo::incseq(seqno) to the original sequence end
        // located at "loc + 1"
        loc = (loc + 1) % m_iSize;

        m_caSeq[loc].seqstart = CSeqNo::incseq(seqno);
        if (CSeqNo::seqcmp(m_caSeq[i].seqend, m_caSeq[loc].seqstart) > 0)
            m_caSeq[loc].seqend = m_caSeq[i].seqend;

        // the first (original) sequence is between the original sequence start to CSeqNo::decseq(seqno)
        if (seqno == CSeqNo::incseq(m_caSeq[i].seqstart))
            m_caSeq[i].seqend = SRT_SEQNO_NONE;
        else
            m_caSeq[i].seqend = CSeqNo::decseq(seqno);

        // update the list pointer
        m_caSeq[loc].inext  = m_caSeq[i].inext;
        m_caSeq[i].inext    = loc;
        m_caSeq[loc].iprior = i;

        if (m_iTail == i)
            m_iTail = loc;
        else
            m_caSeq[m_caSeq[loc].inext].iprior = loc;
    }

    m_iLength--;

    return true;
}

bool srt::CRcvLossList::remove(int32_t seqno1, int32_t seqno2)
{
    if (seqno1 <= seqno2)
    {
        for (int32_t i = seqno1; i <= seqno2; ++i)
            remove(i);
    }
    else
    {
        for (int32_t j = seqno1; j < CSeqNo::m_iMaxSeqNo; ++j)
            remove(j);
        for (int32_t k = 0; k <= seqno2; ++k)
            remove(k);
    }

    return true;
}

bool srt::CRcvLossList::find(int32_t seqno1, int32_t seqno2) const
{
    if (0 == m_iLength)
        return false;

    int p = m_iHead;

    while (-1 != p)
    {
        if ((CSeqNo::seqcmp(m_caSeq[p].seqstart, seqno1) == 0) ||
            ((CSeqNo::seqcmp(m_caSeq[p].seqstart, seqno1) > 0) && (CSeqNo::seqcmp(m_caSeq[p].seqstart, seqno2) <= 0)) ||
            ((CSeqNo::seqcmp(m_caSeq[p].seqstart, seqno1) < 0) && (m_caSeq[p].seqend != SRT_SEQNO_NONE) &&
             CSeqNo::seqcmp(m_caSeq[p].seqend, seqno1) >= 0))
            return true;

        p = m_caSeq[p].inext;
    }

    return false;
}

int srt::CRcvLossList::getLossLength() const
{
    return m_iLength;
}

int32_t srt::CRcvLossList::getFirstLostSeq() const
{
    if (0 == m_iLength)
        return SRT_SEQNO_NONE;

    return m_caSeq[m_iHead].seqstart;
}

void srt::CRcvLossList::getLossArray(int32_t* array, int& len, int limit)
{
    len = 0;

    int i = m_iHead;

    while ((len < limit - 1) && (-1 != i))
    {
        array[len] = m_caSeq[i].seqstart;
        if (SRT_SEQNO_NONE != m_caSeq[i].seqend)
        {
            // there are more than 1 loss in the sequence
            array[len] |= LOSSDATA_SEQNO_RANGE_FIRST;
            ++len;
            array[len] = m_caSeq[i].seqend;
        }

        ++len;

        i = m_caSeq[i].inext;
    }
}

srt::CRcvFreshLoss::CRcvFreshLoss(int32_t seqlo, int32_t seqhi, int initial_age)
    : ttl(initial_age)
    , timestamp(steady_clock::now())
{
    seq[0] = seqlo;
    seq[1] = seqhi;
}

srt::CRcvFreshLoss::Emod srt::CRcvFreshLoss::revoke(int32_t sequence)
{
    int32_t diffbegin = CSeqNo::seqcmp(sequence, seq[0]);
    int32_t diffend   = CSeqNo::seqcmp(sequence, seq[1]);

    if (diffbegin < 0 || diffend > 0)
    {
        return NONE; // not within the range at all.
    }

    if (diffbegin == 0)
    {
        if (diffend == 0) // exactly at begin and end
        {
            return DELETE;
        }

        // only exactly at begin. Shrink the range
        seq[0] = CSeqNo::incseq(seq[0]);
        return STRIPPED;
    }

    if (diffend == 0) // exactly at end
    {
        seq[1] = CSeqNo::decseq(seq[1]);
        return STRIPPED;
    }

    return SPLIT;
}

srt::CRcvFreshLoss::Emod srt::CRcvFreshLoss::revoke(int32_t lo, int32_t hi)
{
    // This should only if the range lo-hi is anyhow covered by seq[0]-seq[1].

    // Note: if the checked item contains sequences that are OLDER
    // than the oldest sequence in this range, they should be deleted,
    // even though this wasn't explicitly requested.

    // LOHI:               <lo, hi>
    // ITEM:  <lo, hi>                      <--- delete
    // If the sequence range is older than the range to be revoked,
    // delete it anyway.
    if (CSeqNo::seqcmp(lo, seq[1]) > 0)
        return DELETE;

    // LOHI:  <lo, hi>
    // ITEM:             <lo, hi>  <-- NOTFOUND
    // This element is newer than the given sequence, so match failed.
    if (CSeqNo::seqcmp(hi, seq[0]) < 0)
        return NONE;

    // LOHI:     <lo,     hi>
    // ITEM:       <lo,    !     hi>
    // RESULT:            <lo,   hi>
    // 2. If the 'hi' is in the middle (less than seq[1]), delete partially.
    // That is, take care of this range for itself and return STRIPPED.
    if (CSeqNo::seqcmp(hi, seq[1]) < 0)
    {
        seq[0] = CSeqNo::incseq(hi);
        return STRIPPED;
    }

    // LOHI:            <lo,         hi>
    // ITEM:       <lo,    !     hi>
    // RESULT: DELETE.
    // 3. Otherwise delete the record, even if this was covering only part of this range.
    // This is not possible that the sequences OLDER THAN THIS are not required to be
    // revoken together with this one.

    return DELETE;
}
