/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <srs_core_mem_watch.hpp>

#ifdef SRS_AUTO_MEM_WATCH

#include <map>
#include <stdio.h>
using namespace std;

struct SrsMemoryObject
{
    void* ptr;
    std::string category;
    int size;
};

std::map<void*, SrsMemoryObject*> _srs_ptrs;

void srs_memory_watch(void* ptr, string category, int size)
{
    SrsMemoryObject* obj = NULL;
    
    std::map<void*, SrsMemoryObject*>::iterator it;
    if ((it = _srs_ptrs.find(ptr)) != _srs_ptrs.end()) {
        obj = it->second;
    } else {
        obj = new SrsMemoryObject();
        _srs_ptrs[ptr] = obj;
    }
    
    obj->ptr = ptr;
    obj->category = category;
    obj->size = size;
}

void srs_memory_unwatch(void* ptr)
{
    std::map<void*, SrsMemoryObject*>::iterator it;
    if ((it = _srs_ptrs.find(ptr)) != _srs_ptrs.end()) {
        SrsMemoryObject* obj = it->second;
        srs_freep(obj);
        
        _srs_ptrs.erase(it);
    }
}

void srs_memory_report()
{
    printf("srs memory watch leak report:\n");
    
    int total = 0;
    std::map<void*, SrsMemoryObject*>::iterator it;
    for (it = _srs_ptrs.begin(); it != _srs_ptrs.end(); ++it) {
        SrsMemoryObject* obj = it->second;
        printf("    %s: %#"PRIx64", %dB\n", obj->category.c_str(), (int64_t)obj->ptr, obj->size);
        total += obj->size;
    }
    
    printf("%d objects leak %dKB.\n", (int)_srs_ptrs.size(), total / 1024);
    printf("@remark use script to cleanup for memory watch: ./etc/init.d/srs stop\n");
}

#endif

