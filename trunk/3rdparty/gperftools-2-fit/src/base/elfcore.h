// -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil -*-
/* Copyright (c) 2005-2008, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ---
 * Author: Markus Gutschke, Carl Crous
 */

#ifndef _ELFCORE_H
#define _ELFCORE_H
#ifdef __cplusplus
extern "C" {
#endif

/* We currently only support x86-32, x86-64, ARM, MIPS, PPC on Linux.
 * Porting to other related platforms should not be difficult.
 */
#if (defined(__i386__) || defined(__x86_64__) || defined(__arm__) || \
     defined(__mips__) || defined(__PPC__)) && defined(__linux)

#include "config.h"

#include <stdarg.h>
#include <stdint.h>
#include <sys/types.h>


/* Define the DUMPER symbol to make sure that there is exactly one
 * core dumper built into the library.
 */
#define DUMPER "ELF"

/* By the time that we get a chance to read CPU registers in the
 * calling thread, they are already in a not particularly useful
 * state. Besides, there will be multiple frames on the stack that are
 * just making the core file confusing. To fix this problem, we take a
 * snapshot of the frame pointer, stack pointer, and instruction
 * pointer at an earlier time, and then insert these values into the
 * core file.
 */

#if defined(__i386__) || defined(__x86_64__)
  typedef struct i386_regs {    /* Normal (non-FPU) CPU registers            */
  #ifdef __x86_64__
    #define BP rbp
    #define SP rsp
    #define IP rip
    uint64_t  r15,r14,r13,r12,rbp,rbx,r11,r10;
    uint64_t  r9,r8,rax,rcx,rdx,rsi,rdi,orig_rax;
    uint64_t  rip,cs,eflags;
    uint64_t  rsp,ss;
    uint64_t  fs_base, gs_base;
    uint64_t  ds,es,fs,gs;
  #else
    #define BP ebp
    #define SP esp
    #define IP eip
    uint32_t  ebx, ecx, edx, esi, edi, ebp, eax;
    uint16_t  ds, __ds, es, __es;
    uint16_t  fs, __fs, gs, __gs;
    uint32_t  orig_eax, eip;
    uint16_t  cs, __cs;
    uint32_t  eflags, esp;
    uint16_t  ss, __ss;
  #endif
  } i386_regs;
#elif defined(__arm__)
  typedef struct arm_regs {     /* General purpose registers                 */
    #define BP uregs[11]        /* Frame pointer                             */
    #define SP uregs[13]        /* Stack pointer                             */
    #define IP uregs[15]        /* Program counter                           */
    #define LR uregs[14]        /* Link register                             */
    long uregs[18];
  } arm_regs;
#elif defined(__mips__)
  typedef struct mips_regs {
    unsigned long pad[6];       /* Unused padding to match kernel structures */
    unsigned long uregs[32];    /* General purpose registers.                */
    unsigned long hi;           /* Used for multiplication and division.     */
    unsigned long lo;
    unsigned long cp0_epc;      /* Program counter.                          */
    unsigned long cp0_badvaddr;
    unsigned long cp0_status;
    unsigned long cp0_cause;
    unsigned long unused;
  } mips_regs;
#elif defined (__PPC__)
  typedef struct ppc_regs {
    #define SP uregs[1]         /* Stack pointer                             */
    #define IP rip              /* Program counter                           */
    #define LR lr               /* Link register                             */
    unsigned long uregs[32];	/* General Purpose Registers - r0-r31.       */
    double        fpr[32];	/* Floating-Point Registers - f0-f31.        */
    unsigned long rip;		/* Program counter.                          */
    unsigned long msr;
    unsigned long ccr;
    unsigned long lr;
    unsigned long ctr;
    unsigned long xeq;
    unsigned long mq;
  } ppc_regs;
#endif

#endif  // __linux and various arches

#ifdef __cplusplus
}
#endif
#endif /* _ELFCORE_H */
