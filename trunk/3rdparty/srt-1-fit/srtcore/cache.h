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
   Yunhong Gu, last updated 01/27/2011
*****************************************************************************/

#ifndef INC_SRT_CACHE_H
#define INC_SRT_CACHE_H

#include <list>
#include <vector>

#include "sync.h"
#include "netinet_any.h"
#include "udt.h"

namespace srt
{

class CCacheItem
{
public:
   virtual ~CCacheItem() {}

public:
   virtual CCacheItem& operator=(const CCacheItem&) = 0;

   // The "==" operator SHOULD only compare key values.
   virtual bool operator==(const CCacheItem&) = 0;

      /// get a deep copy clone of the current item
      /// @return Pointer to the new item, or NULL if failed.

   virtual CCacheItem* clone() = 0;

      /// get a random key value between 0 and MAX_INT to be used for the hash in cache
      /// @return A random hash key.

   virtual int getKey() = 0;

   // If there is any shared resources between the cache item and its clone,
   // the shared resource should be released by this function.
   virtual void release() {}
};

template<typename T> class CCache
{
public:
   CCache(int size = 1024):
   m_iMaxSize(size),
   m_iHashSize(size * 3),
   m_iCurrSize(0)
   {
      m_vHashPtr.resize(m_iHashSize);
      // Exception: -> CUDTUnited ctor
      srt::sync::setupMutex(m_Lock, "Cache");
   }

   ~CCache()
   {
      clear();
   }

public:
      /// find the matching item in the cache.
      /// @param [in,out] data storage for the retrieved item; initially it must carry the key information
      /// @return 0 if found a match, otherwise -1.

   int lookup(T* data)
   {
      srt::sync::ScopedLock cacheguard(m_Lock);

      int key = data->getKey();
      if (key < 0)
         return -1;
      if (key >= m_iMaxSize)
         key %= m_iHashSize;

      const ItemPtrList& item_list = m_vHashPtr[key];
      for (typename ItemPtrList::const_iterator i = item_list.begin(); i != item_list.end(); ++ i)
      {
         if (*data == ***i)
         {
            // copy the cached info
            *data = ***i;
            return 0;
         }
      }

      return -1;
   }

      /// update an item in the cache, or insert one if it doesn't exist; oldest item may be removed
      /// @param [in] data the new item to updated/inserted to the cache
      /// @return 0 if success, otherwise -1.

   int update(T* data)
   {
      srt::sync::ScopedLock cacheguard(m_Lock);

      int key = data->getKey();
      if (key < 0)
         return -1;
      if (key >= m_iMaxSize)
         key %= m_iHashSize;

      T* curr = NULL;

      ItemPtrList& item_list = m_vHashPtr[key];
      for (typename ItemPtrList::iterator i = item_list.begin(); i != item_list.end(); ++ i)
      {
         if (*data == ***i)
         {
            // update the existing entry with the new value
            ***i = *data;
            curr = **i;

            // remove the current entry
            m_StorageList.erase(*i);
            item_list.erase(i);

            // re-insert to the front
            m_StorageList.push_front(curr);
            item_list.push_front(m_StorageList.begin());

            return 0;
         }
      }

      // create new entry and insert to front
      curr = data->clone();
      m_StorageList.push_front(curr);
      item_list.push_front(m_StorageList.begin());

      ++ m_iCurrSize;
      if (m_iCurrSize >= m_iMaxSize)
      {
         // Cache overflow, remove oldest entry.
         T* last_data = m_StorageList.back();
         int last_key = last_data->getKey() % m_iHashSize;

         ItemPtrList& last_item_list = m_vHashPtr[last_key];
         for (typename ItemPtrList::iterator i = last_item_list.begin(); i != last_item_list.end(); ++ i)
         {
            if (*last_data == ***i)
            {
               last_item_list.erase(i);
               break;
            }
         }

         last_data->release();
         delete last_data;
         m_StorageList.pop_back();
         -- m_iCurrSize;
      }

      return 0;
   }

      /// Specify the cache size (i.e., max number of items).
      /// @param [in] size max cache size.

   void setSizeLimit(int size)
   {
      m_iMaxSize = size;
      m_iHashSize = size * 3;
      m_vHashPtr.resize(m_iHashSize);
   }

      /// Clear all entries in the cache, restore to initialization state.

   void clear()
   {
      for (typename std::list<T*>::iterator i = m_StorageList.begin(); i != m_StorageList.end(); ++ i)
      {
         (*i)->release();
         delete *i;
      }
      m_StorageList.clear();
      for (typename std::vector<ItemPtrList>::iterator i = m_vHashPtr.begin(); i != m_vHashPtr.end(); ++ i)
         i->clear();
      m_iCurrSize = 0;
   }

private:
   std::list<T*> m_StorageList;
   typedef typename std::list<T*>::iterator ItemPtr;
   typedef std::list<ItemPtr> ItemPtrList;
   std::vector<ItemPtrList> m_vHashPtr;

   int m_iMaxSize;
   int m_iHashSize;
   int m_iCurrSize;

   srt::sync::Mutex m_Lock;

private:
   CCache(const CCache&);
   CCache& operator=(const CCache&);
};


class CInfoBlock
{
public:
   uint32_t m_piIP[4];        // IP address, machine read only, not human readable format.
   int m_iIPversion;          // Address family: AF_INET or AF_INET6.
   uint64_t m_ullTimeStamp;   // Last update time.
   int m_iSRTT;               // Smoothed RTT.
   int m_iBandwidth;          // Estimated link bandwidth.
   int m_iLossRate;           // Average loss rate.
   int m_iReorderDistance;    // Packet reordering distance.
   double m_dInterval;        // Inter-packet time (Congestion Control).
   double m_dCWnd;            // Congestion window size (Congestion Control).

public:
   CInfoBlock() {} // NOTE: leaves uninitialized
   CInfoBlock& copyFrom(const CInfoBlock& obj);
   CInfoBlock(const CInfoBlock& src) { copyFrom(src); }
   CInfoBlock& operator=(const CInfoBlock& src) { return copyFrom(src); }
   bool operator==(const CInfoBlock& obj) const;
   CInfoBlock* clone();
   int getKey();
   void release() {}

public:

      /// convert sockaddr structure to an integer array
      /// @param [in] addr network address
      /// @param [in] ver IP version
      /// @param [out] ip the result machine readable IP address in integer array

   static void convert(const sockaddr_any& addr, uint32_t ip[4]);
};

} // namespace srt

#endif
