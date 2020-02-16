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

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <queue>
#include "co_routine.h"

const char* CGI_ENV_HOOK_LIST [] = 
{
	"CGINAME",
};
struct stRoutineArgs_t
{
	int iRoutineID;
};
void SetAndGetEnv(int iRoutineID)
{
	printf("routineid %d begin\n", iRoutineID);

	//use poll as sleep
	poll(NULL, 0, 500);

	char sBuf[128];
	sprintf(sBuf, "cgi_routine_%d", iRoutineID);
	int ret = setenv("CGINAME", sBuf, 1);
	if (ret)
	{
		printf("%s:%d set env err ret %d errno %d %s\n", __func__, __LINE__,
				ret, errno, strerror(errno));
		return;
	}
	printf("routineid %d set env CGINAME %s\n", iRoutineID, sBuf);

	poll(NULL, 0, 500);

	char* env = getenv("CGINAME");
	if (!env)
	{
		printf("%s:%d get env err errno %d %s\n", __func__, __LINE__,
				errno, strerror(errno));
		return;
	}
	printf("routineid %d get env CGINAME %s\n", iRoutineID, env);
}

void* RoutineFunc(void* args)
{
	co_enable_hook_sys();

	stRoutineArgs_t* g = (stRoutineArgs_t*)args;

	SetAndGetEnv(g->iRoutineID);
	return NULL;
}

int main(int argc, char* argv[])
{
	co_set_env_list(CGI_ENV_HOOK_LIST, sizeof(CGI_ENV_HOOK_LIST) / sizeof(char*));
	stRoutineArgs_t  args[3];
	for (int i = 0; i < 3; i++)
	{
		stCoRoutine_t* co = NULL;
		args[i].iRoutineID = i;
		co_create(&co, NULL, RoutineFunc, &args[i]);
		co_resume(co);
	}
	co_eventloop(co_get_epoll_ct(), NULL, NULL);
	return 0;
}

