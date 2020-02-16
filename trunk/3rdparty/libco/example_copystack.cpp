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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>
#include "coctx.h"
#include "co_routine.h"
#include "co_routine_inner.h"

void* RoutineFunc(void* args)
{
	co_enable_hook_sys();
	int* routineid = (int*)args;
	while (true)
	{
		char sBuff[128];
		sprintf(sBuff, "from routineid %d stack addr %p\n", *routineid, sBuff);

		printf("%s", sBuff);
		poll(NULL, 0, 1000); //sleep 1s
	}
	return NULL;
}

int main()
{
	stShareStack_t* share_stack= co_alloc_sharestack(1, 1024 * 128);
	stCoRoutineAttr_t attr;
	attr.stack_size = 0;
	attr.share_stack = share_stack;

	stCoRoutine_t* co[2];
	int routineid[2];
	for (int i = 0; i < 2; i++)
	{
		routineid[i] = i;
		co_create(&co[i], &attr, RoutineFunc, routineid + i);
		co_resume(co[i]);
	}
	co_eventloop(co_get_epoll_ct(), NULL, NULL);
	return 0;
}
