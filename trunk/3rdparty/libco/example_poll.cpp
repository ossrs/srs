/*
* Tencent is pleased to support the open source community by making Libco available.

* Copyright (C) 2014 THL A29 Limited, a Tencent company. All rights reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License"); 
* you may not use this file except in compliance with the License. 
* You may obtain a copy of the License at
*
*	http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, 
* software distributed under the License is distributed on an "AS IS" BASIS, 
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
* See the License for the specific language governing permissions and 
* limitations under the License.
*/

#include "co_routine.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>
#include <stack>

#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <errno.h>
#include <vector>
#include <set>
#include <unistd.h>

#ifdef __FreeBSD__
#include <cstring>
#endif

using namespace std;

struct task_t
{
	stCoRoutine_t *co;
	int fd;
	struct sockaddr_in addr;
};

static int SetNonBlock(int iSock)
{
    int iFlags;

    iFlags = fcntl(iSock, F_GETFL, 0);
    iFlags |= O_NONBLOCK;
    iFlags |= O_NDELAY;
    int ret = fcntl(iSock, F_SETFL, iFlags);
    return ret;
}



static void SetAddr(const char *pszIP,const unsigned short shPort,struct sockaddr_in &addr)
{
	bzero(&addr,sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(shPort);
	int nIP = 0;
	if( !pszIP || '\0' == *pszIP   
	    || 0 == strcmp(pszIP,"0") || 0 == strcmp(pszIP,"0.0.0.0") 
		|| 0 == strcmp(pszIP,"*") 
	  )
	{
		nIP = htonl(INADDR_ANY);
	}
	else
	{
		nIP = inet_addr(pszIP);
	}
	addr.sin_addr.s_addr = nIP;

}

static int CreateTcpSocket(const unsigned short shPort  = 0 ,const char *pszIP  = "*" ,bool bReuse  = false )
{
	int fd = socket(AF_INET,SOCK_STREAM, IPPROTO_TCP);
	if( fd >= 0 )
	{
		if(shPort != 0)
		{
			if(bReuse)
			{
				int nReuseAddr = 1;
				setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&nReuseAddr,sizeof(nReuseAddr));
			}
			struct sockaddr_in addr ;
			SetAddr(pszIP,shPort,addr);
			int ret = bind(fd,(struct sockaddr*)&addr,sizeof(addr));
			if( ret != 0)
			{
				close(fd);
				return -1;
			}
		}
	}
	return fd;
}

static void *poll_routine( void *arg )
{
	co_enable_hook_sys();

	vector<task_t> &v = *(vector<task_t>*)arg;
	for(size_t i=0;i<v.size();i++)
	{
		int fd = CreateTcpSocket();
		SetNonBlock( fd );
		v[i].fd = fd;

		int ret = connect(fd,(struct sockaddr*)&v[i].addr,sizeof( v[i].addr )); 
		printf("co %p connect i %ld ret %d errno %d (%s)\n",
			co_self(),i,ret,errno,strerror(errno));
	}
	struct pollfd *pf = (struct pollfd*)calloc( 1,sizeof(struct pollfd) * v.size() );

	for(size_t i=0;i<v.size();i++)
	{
		pf[i].fd = v[i].fd;
		pf[i].events = ( POLLOUT | POLLERR | POLLHUP );
	}
	set<int> setRaiseFds;
	size_t iWaitCnt = v.size();
	for(;;)
	{
		int ret = poll( pf,iWaitCnt,1000 );
		printf("co %p poll wait %ld ret %d\n",
				co_self(),iWaitCnt,ret);
		for(int i=0;i<(int)iWaitCnt;i++)
		{
			printf("co %p fire fd %d revents 0x%X POLLOUT 0x%X POLLERR 0x%X POLLHUP 0x%X\n",
					co_self(),
					pf[i].fd,
					pf[i].revents,
					POLLOUT,
					POLLERR,
					POLLHUP
					);
			setRaiseFds.insert( pf[i].fd );
		}
		if( setRaiseFds.size() == v.size())
		{
			break;
		}
		if( ret <= 0 )
		{
			break;
		}

		iWaitCnt = 0;
		for(size_t i=0;i<v.size();i++)
		{
			if( setRaiseFds.find( v[i].fd ) == setRaiseFds.end() )
			{
				pf[ iWaitCnt ].fd = v[i].fd;
				pf[ iWaitCnt ].events = ( POLLOUT | POLLERR | POLLHUP );
				++iWaitCnt;
			}
		}
	}
	for(size_t i=0;i<v.size();i++)
	{
		close( v[i].fd );
		v[i].fd = -1;
	}

	printf("co %p task cnt %ld fire %ld\n",
			co_self(),v.size(),setRaiseFds.size() );
	return 0;
}
int main(int argc,char *argv[])
{
	vector<task_t> v;
	for(int i=1;i<argc;i+=2)
	{
		task_t task = { 0 };
		SetAddr( argv[i],atoi(argv[i+1]),task.addr );
		v.push_back( task );
	}

//------------------------------------------------------------------------------------
	printf("--------------------- main -------------------\n");
	vector<task_t> v2 = v;
	poll_routine( &v2 );
	printf("--------------------- routine -------------------\n");

	for(int i=0;i<10;i++)
	{
		stCoRoutine_t *co = 0;
		vector<task_t> *v2 = new vector<task_t>();
		*v2 = v;
		co_create( &co,NULL,poll_routine,v2 );
		printf("routine i %d\n",i);
		co_resume( co );
	}

	co_eventloop( co_get_epoll_ct(),0,0 );

	return 0;
}
//./example_poll 127.0.0.1 12365 127.0.0.1 12222 192.168.1.1 1000 192.168.1.2 1111

