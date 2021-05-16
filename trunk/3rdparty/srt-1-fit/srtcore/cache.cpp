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
   Yunhong Gu, last updated 05/05/2009
*****************************************************************************/

#ifdef _WIN32
   #include <winsock2.h>
   #include <ws2tcpip.h>
#endif

#include <cstring>
#include "cache.h"
#include "core.h"

using namespace std;

CInfoBlock& CInfoBlock::operator=(const CInfoBlock& obj)
{
   std::copy(obj.m_piIP, obj.m_piIP + 4, m_piIP);
   m_iIPversion = obj.m_iIPversion;
   m_ullTimeStamp = obj.m_ullTimeStamp;
   m_iRTT = obj.m_iRTT;
   m_iBandwidth = obj.m_iBandwidth;
   m_iLossRate = obj.m_iLossRate;
   m_iReorderDistance = obj.m_iReorderDistance;
   m_dInterval = obj.m_dInterval;
   m_dCWnd = obj.m_dCWnd;

   return *this;
}

bool CInfoBlock::operator==(const CInfoBlock& obj)
{
   if (m_iIPversion != obj.m_iIPversion)
      return false;

   else if (m_iIPversion == AF_INET)
      return (m_piIP[0] == obj.m_piIP[0]);

   for (int i = 0; i < 4; ++ i)
   {
      if (m_piIP[i] != obj.m_piIP[i])
         return false;
   }

   return true;
}

CInfoBlock* CInfoBlock::clone()
{
   CInfoBlock* obj = new CInfoBlock;

   std::copy(m_piIP, m_piIP + 4, obj->m_piIP);
   obj->m_iIPversion = m_iIPversion;
   obj->m_ullTimeStamp = m_ullTimeStamp;
   obj->m_iRTT = m_iRTT;
   obj->m_iBandwidth = m_iBandwidth;
   obj->m_iLossRate = m_iLossRate;
   obj->m_iReorderDistance = m_iReorderDistance;
   obj->m_dInterval = m_dInterval;
   obj->m_dCWnd = m_dCWnd;

   return obj;
}

int CInfoBlock::getKey()
{
   if (m_iIPversion == AF_INET)
      return m_piIP[0];

   return m_piIP[0] + m_piIP[1] + m_piIP[2] + m_piIP[3];
}

void CInfoBlock::convert(const sockaddr* addr, int ver, uint32_t ip[])
{
   if (ver == AF_INET)
   {
      ip[0] = ((sockaddr_in*)addr)->sin_addr.s_addr;
      ip[1] = ip[2] = ip[3] = 0;
   }
   else
   {
      memcpy((char*)ip, (char*)((sockaddr_in6*)addr)->sin6_addr.s6_addr, 16);
   }
}
