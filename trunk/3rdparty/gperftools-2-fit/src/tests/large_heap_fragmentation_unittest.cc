// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

// This is a unit test for exercising fragmentation of large (over 1
// meg) page spans. It makes sure that allocations/releases of
// increasing memory chunks do not blowup memory
// usage. See also https://code.google.com/p/gperftools/issues/detail?id=368


#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>

#include "base/logging.h"
#include "common.h"
#include <gperftools/malloc_extension.h>


int main (int argc, char** argv) {
  for (int pass = 1; pass <= 3; pass++) {
    size_t size = 100*1024*1024;
    while (size < 500*1024*1024) {
      void *ptr = malloc(size);
      free(ptr);
      size += 20000;

      size_t heap_size = static_cast<size_t>(-1);
      MallocExtension::instance()->GetNumericProperty("generic.heap_size",
                                                      &heap_size);


      CHECK_LT(heap_size, 1*1024*1024*1024);
    }
  }

  printf("PASS\n");
  return 0;
}
