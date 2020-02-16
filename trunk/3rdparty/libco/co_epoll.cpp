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

#include "co_epoll.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#if !defined( __APPLE__ ) && !defined( __FreeBSD__ )

int	co_epoll_wait( int epfd,struct co_epoll_res *events,int maxevents,int timeout )
{
	return epoll_wait( epfd,events->events,maxevents,timeout );
}
int	co_epoll_ctl( int epfd,int op,int fd,struct epoll_event * ev )
{
	return epoll_ctl( epfd,op,fd,ev );
}
int	co_epoll_create( int size )
{
	return epoll_create( size );
}

struct co_epoll_res *co_epoll_res_alloc( int n )
{
	struct co_epoll_res * ptr = 
		(struct co_epoll_res *)malloc( sizeof( struct co_epoll_res ) );

	ptr->size = n;
	ptr->events = (struct epoll_event*)calloc( 1,n * sizeof( struct epoll_event ) );

	return ptr;

}
void co_epoll_res_free( struct co_epoll_res * ptr )
{
	if( !ptr ) return;
	if( ptr->events ) free( ptr->events );
	free( ptr );
}

#else
class clsFdMap // million of fd , 1024 * 1024 
{
private:
	static const int row_size = 1024;
	static const int col_size = 1024;

	void **m_pp[ 1024 ];
public:
	clsFdMap()
	{
		memset( m_pp,0,sizeof(m_pp) );
	}
	~clsFdMap()
	{
		for(int i=0;i<sizeof(m_pp)/sizeof(m_pp[0]);i++)
		{
			if( m_pp[i] ) 
			{
				free( m_pp[i] );
				m_pp[i] = NULL;
			}
		}
	}
	inline int clear( int fd )
	{
		set( fd,NULL );
		return 0;
	}
	inline int set( int fd,const void * ptr )
	{
		int idx = fd / row_size;
		if( idx < 0 || idx >= sizeof(m_pp)/sizeof(m_pp[0]) )
		{
			assert( __LINE__ == 0 );
			return -__LINE__;
		}
		if( !m_pp[ idx ] ) 
		{
			m_pp[ idx ] = (void**)calloc( 1,sizeof(void*) * col_size );
		}
		m_pp[ idx ][ fd % col_size ] = (void*)ptr;
		return 0;
	}
	inline void *get( int fd )
	{
		int idx = fd / row_size;
		if( idx < 0 || idx >= sizeof(m_pp)/sizeof(m_pp[0]) )
		{
			return NULL;
		}
		void **lp = m_pp[ idx ];
		if( !lp ) return NULL;

		return lp[ fd % col_size ];
	}
};

__thread clsFdMap *s_fd_map = NULL;

static inline clsFdMap *get_fd_map()
{
	if( !s_fd_map )
	{
		s_fd_map = new clsFdMap();
	}
	return s_fd_map;
}

struct kevent_pair_t
{
	int fire_idx;
	int events;
	uint64_t u64;
};
int co_epoll_create( int size )
{
	return kqueue();
}
int co_epoll_wait( int epfd,struct co_epoll_res *events,int maxevents,int timeout )
{
	struct timespec t = { 0 };
	if( timeout > 0 )
	{
		t.tv_sec = timeout;
	}
	int ret = kevent( epfd, 
					NULL, 0, //register null
					events->eventlist, maxevents,//just retrival
					( -1 == timeout ) ? NULL : &t );
	int j = 0;
	for(int i=0;i<ret;i++)
	{
		struct kevent &kev = events->eventlist[i];
		struct kevent_pair_t *ptr = (struct kevent_pair_t*)kev.udata;
		struct epoll_event *ev = events->events + i;
		if( 0 == ptr->fire_idx )
		{
			ptr->fire_idx = i + 1;
			memset( ev,0,sizeof(*ev) );
			++j;
		}
		else
		{
			ev = events->events + ptr->fire_idx - 1;
		}
		if( EVFILT_READ == kev.filter )
		{
			ev->events |= EPOLLIN;
		}
		else if( EVFILT_WRITE == kev.filter )
		{
			ev->events |= EPOLLOUT;
		}
		ev->data.u64 = ptr->u64;
	}
	for(int i=0;i<ret;i++)
	{
		(( struct kevent_pair_t* )(events->eventlist[i].udata) )->fire_idx = 0;
	}
	return j;
}
int co_epoll_del( int epfd,int fd )
{

	struct timespec t = { 0 };
	struct kevent_pair_t *ptr = ( struct kevent_pair_t* )get_fd_map()->get( fd );
	if( !ptr ) return 0;
	if( EPOLLIN & ptr->events )
	{
		struct kevent kev = { 0 };
		kev.ident = fd;
		kev.filter = EVFILT_READ;
		kev.flags = EV_DELETE;
		kevent( epfd,&kev,1, NULL,0,&t );
	}
	if( EPOLLOUT & ptr->events )
	{
		struct kevent kev = { 0 };
		kev.ident = fd;
		kev.filter = EVFILT_WRITE;
		kev.flags = EV_DELETE;
		kevent( epfd,&kev,1, NULL,0,&t );
	}
	get_fd_map()->clear( fd );
	free( ptr );
	return 0;
}
int co_epoll_ctl( int epfd,int op,int fd,struct epoll_event * ev )
{
	if( EPOLL_CTL_DEL == op )
	{
		return co_epoll_del( epfd,fd );
	}

	const int flags = ( EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP );
	if( ev->events & ~flags ) 
	{
		return -1;
	}

	if( EPOLL_CTL_ADD == op && get_fd_map()->get( fd ) )
	{
		errno = EEXIST;
		return -1;
	}
	else if( EPOLL_CTL_MOD == op && !get_fd_map()->get( fd ) )
	{
		errno = ENOENT;
		return -1;
	}

	struct kevent_pair_t *ptr = (struct kevent_pair_t*)get_fd_map()->get( fd );
	if( !ptr )
	{
		ptr = (kevent_pair_t*)calloc(1,sizeof(kevent_pair_t));
		get_fd_map()->set( fd,ptr );
	}

	int ret = 0;
	struct timespec t = { 0 };

	// printf("ptr->events 0x%X\n",ptr->events);

	if( EPOLL_CTL_MOD == op )
	{
		//1.delete if exists
		if( ptr->events & EPOLLIN ) 
		{
			struct kevent kev = { 0 };
			EV_SET( &kev,fd,EVFILT_READ,EV_DELETE,0,0,NULL );
			kevent( epfd, &kev,1, NULL,0, &t );
		}	
		//1.delete if exists
		if( ptr->events & EPOLLOUT ) 
		{
			struct kevent kev = { 0 };
			EV_SET( &kev,fd,EVFILT_WRITE,EV_DELETE,0,0,NULL );
			ret = kevent( epfd, &kev,1, NULL,0, &t );
			// printf("delete write ret %d\n",ret );
		}
	}

	do
	{
		if( ev->events & EPOLLIN )
		{
			
			//2.add
			struct kevent kev = { 0 };
			EV_SET( &kev,fd,EVFILT_READ,EV_ADD,0,0,ptr );
			ret = kevent( epfd, &kev,1, NULL,0, &t );
			if( ret ) break;
		}
		if( ev->events & EPOLLOUT )
		{
				//2.add
			struct kevent kev = { 0 };
			EV_SET( &kev,fd,EVFILT_WRITE,EV_ADD,0,0,ptr );
			ret = kevent( epfd, &kev,1, NULL,0, &t );
			if( ret ) break;
		}
	} while( 0 );
	
	if( ret )
	{
		get_fd_map()->clear( fd );
		free( ptr );
		return ret;
	}

	ptr->events = ev->events;
	ptr->u64 = ev->data.u64;
	 

	return ret;
}

struct co_epoll_res *co_epoll_res_alloc( int n )
{
	struct co_epoll_res * ptr = 
		(struct co_epoll_res *)malloc( sizeof( struct co_epoll_res ) );

	ptr->size = n;
	ptr->events = (struct epoll_event*)calloc( 1,n * sizeof( struct epoll_event ) );
	ptr->eventlist = (struct kevent*)calloc( 1,n * sizeof( struct kevent) );

	return ptr;
}

void co_epoll_res_free( struct co_epoll_res * ptr )
{
	if( !ptr ) return;
	if( ptr->events ) free( ptr->events );
	if( ptr->eventlist ) free( ptr->eventlist );
	free( ptr );
}

#endif


