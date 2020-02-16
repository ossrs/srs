#
# Tencent is pleased to support the open source community by making Libco available.
# 
# Copyright (C) 2014 THL A29 Limited, a Tencent company. All rights reserved.
# 
# Licensed under the Apache License, Version 2.0 (the "License"); 
# you may not use this file except in compliance with the License. 
# You may obtain a copy of the License at
# 
#   http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, 
# software distributed under the License is distributed on an "AS IS" BASIS, 
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
# See the License for the specific language governing permissions and 
# limitations under the License.
#


COMM_MAKE = 1
COMM_ECHO = 1
version=0.5
v=debug
include co.mk

########## options ##########
CFLAGS += -g -fno-strict-aliasing -O2 -Wall -export-dynamic \
	-Wall -pipe  -D_GNU_SOURCE -D_REENTRANT -fPIC -Wno-deprecated -m64

UNAME := $(shell uname -s)

ifeq ($(UNAME), FreeBSD)
LINKS += -g -L./lib -lcolib -lpthread
else
LINKS += -g -L./lib -lcolib -lpthread -ldl
endif

COLIB_OBJS=co_epoll.o co_routine.o co_hook_sys_call.o coctx_swap.o coctx.o
#co_swapcontext.o

PROGS = colib example_poll example_echosvr example_echocli example_thread  example_cond example_specific example_copystack example_closure

all:$(PROGS)

colib:libcolib.a libcolib.so

libcolib.a: $(COLIB_OBJS)
	$(ARSTATICLIB) 
libcolib.so: $(COLIB_OBJS)
	$(BUILDSHARELIB) 

example_echosvr:example_echosvr.o
	$(BUILDEXE) 
example_echocli:example_echocli.o
	$(BUILDEXE) 
example_thread:example_thread.o
	$(BUILDEXE) 
example_poll:example_poll.o
	$(BUILDEXE) 
example_exit:example_exit.o
	$(BUILDEXE) 
example_cond:example_cond.o
	$(BUILDEXE)
example_specific:example_specific.o
	$(BUILDEXE)
example_copystack:example_copystack.o
	$(BUILDEXE)
example_setenv:example_setenv.o
	$(BUILDEXE)
example_closure:example_closure.o
	$(BUILDEXE)

dist: clean libco-$(version).src.tar.gz

libco-$(version).src.tar.gz:
	@find . -type f | grep -v CVS | grep -v .svn | sed s:^./:libco-$(version)/: > MANIFEST
	@(cd ..; ln -s libco_pub libco-$(version))
	(cd ..; tar cvf - `cat libco_pub/MANIFEST` | gzip > libco_pub/libco-$(version).src.tar.gz)
	@(cd ..; rm libco-$(version))

clean:
	$(CLEAN) *.o $(PROGS)
	rm -fr MANIFEST lib solib libco-$(version).src.tar.gz libco-$(version)

