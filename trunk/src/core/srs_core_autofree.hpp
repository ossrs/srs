/**
 * The MIT License (MIT)
 *
 * Copyright (c) 2013-2018 Winlin
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef SRS_CORE_AUTO_FREE_HPP
#define SRS_CORE_AUTO_FREE_HPP

#include <srs_core.hpp>

/**
 * auto free the instance in the current scope, for instance, MyClass* ptr,
 * which is a ptr and this class will:
 *       1. free the ptr.
 *       2. set ptr to NULL.
 *
 * Usage:
 *       MyClass* po = new MyClass();
 *       // ...... use po
 *       SrsAutoFree(MyClass, po);
 *
 * Usage for array:
 *      MyClass** pa = new MyClass*[size];
 *      // ....... use pa
 *      SrsAutoFreeA(MyClass*, pa);
 *
 * @remark the MyClass can be basic type, for instance, SrsAutoFreeA(char, pstr),
 *      where the char* pstr = new char[size].
 */
#define SrsAutoFree(className, instance) \
impl_SrsAutoFree<className> _auto_free_##instance(&instance, false)
#define SrsAutoFreeA(className, instance) \
impl_SrsAutoFree<className> _auto_free_array_##instance(&instance, true)
template<class T>
class impl_SrsAutoFree
{
private:
    T** ptr;
    bool is_array;
public:
    /**
     * auto delete the ptr.
     */
    impl_SrsAutoFree(T** p, bool array) {
        ptr = p;
        is_array = array;
    }
    
    virtual ~impl_SrsAutoFree() {
        if (ptr == NULL || *ptr == NULL) {
            return;
        }
        
        if (is_array) {
            delete[] *ptr;
        } else {
            delete *ptr;
        }
        
        *ptr = NULL;
    }
};

#endif
