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

#include "co_closure.h"
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <pthread.h>
#include <unistd.h>
using namespace std;

static void *thread_func( void * arg )
{
	stCoClosure_t *p = (stCoClosure_t*) arg;
	p->exec();
	return 0;
}
static void batch_exec( vector<stCoClosure_t*> &v )
{
	vector<pthread_t> ths;
	for( size_t i=0;i<v.size();i++ )
	{
		pthread_t tid;
		pthread_create( &tid,0,thread_func,v[i] );
		ths.push_back( tid );
	}
	for( size_t i=0;i<v.size();i++ )
	{
		pthread_join( ths[i],0 );
	}
}
int main( int argc,char *argv[] )
{
	vector< stCoClosure_t* > v;

	pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

	int total = 100;
	vector<int> v2;
	co_ref( ref,total,v2,m);
	for(int i=0;i<10;i++)
	{
		co_func( f,ref,i )
		{
			printf("ref.total %d i %d\n",ref.total,i );
			//lock
			pthread_mutex_lock(&ref.m);
			ref.v2.push_back( i );
			pthread_mutex_unlock(&ref.m);
			//unlock
		}
		co_func_end;
		v.push_back( new f( ref,i ) );
	}
	for(int i=0;i<2;i++)
	{
		co_func( f2,i )
		{
			printf("i: %d\n",i);
			for(int j=0;j<2;j++)
			{
				usleep( 1000 );
				printf("i %d j %d\n",i,j);
			}
		}
		co_func_end;
		v.push_back( new f2( i ) );
	}

	batch_exec( v );
	printf("done\n");

	return 0;
}


