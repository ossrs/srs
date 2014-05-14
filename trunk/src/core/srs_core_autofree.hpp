/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

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

#ifndef SRS_CORE_AUTO_FREE_HPP
#define SRS_CORE_AUTO_FREE_HPP

/*
#include <srs_core_autofree.hpp>
*/

#include <srs_core.hpp>

/**
* auto free the instance in the current scope, for instance, MyClass* ptr,
* which is a ptr and this class will:
*       1. free the ptr.
*       2. set ptr to NULL.
* Usage:
*       MyClass* po = new MyClass();
*       // ...... use po
*       SrsAutoFree(MyClass, po);
*/
#define SrsAutoFree(className, instance) \
    __SrsAutoFree<className> _auto_free_##instance(&instance)
template<class T>
class __SrsAutoFree
{
private:
    T** ptr;
public:
    /**
    * auto delete the ptr.
    */
    __SrsAutoFree(T** _ptr) {
        ptr = _ptr;
    }
    
    virtual ~__SrsAutoFree() {
        if (ptr == NULL || *ptr == NULL) {
            return;
        }
        
        delete *ptr;
        
        *ptr = NULL;
    }
};

/**
* auto free the array ptrs, for example, MyClass* msgs[10],
* which stores 10 MyClass* objects, this class will:
*       1. free each MyClass* in array.
*       2. free the msgs itself.
*       3. set msgs to NULL.
* @remark, MyClass* msgs[] equals to MyClass**, the ptr array equals ptr to ptr.
* Usage:
*       MyClass* msgs[10];
*       // ...... use msgs.
*       SrsAutoFreeArray(MyClass, msgs, 10);
*/
#define SrsAutoFreeArray(className, instance, size) \
    __SrsAutoFreeArray<className> _auto_free_array_##instance(&instance, size)
template<class T>
class __SrsAutoFreeArray
{
private:
    T*** ptr;
    int size;
public:
    /**
    * auto delete the ptr array.
    */
    __SrsAutoFreeArray(T*** _ptr, int _size) {
        ptr = _ptr;
        size = _size;
    }
    
    virtual ~__SrsAutoFreeArray() {
        if (ptr == NULL || *ptr == NULL) {
            return;
        }
        
        T** arr = *ptr;
        for (int i = 0; i < size; i++) {
            T* pobj = arr[i];
            if (pobj) {
                delete pobj;
                arr[i] = NULL;
            }
        }
        
        delete arr;
        
        *ptr = NULL;
    }
};

#endif