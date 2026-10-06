; SPDX-License-Identifier: MIT
; Copyright (c) 2013-2026 The SRS Authors

; Native Windows x64 (MSVC, MASM) context switch, the WIN64 platform, not CYGWIN64.
; Assemble with: ml64 -nologo -c -Fomd_win64.o md_win64.asm
;
; The Win64 ABI callee-saved (nonvolatile) registers are rbx, rbp, rdi, rsi, r12-r15,
; xmm6-xmm15, the MXCSR control bits and the x87 control word, so all are saved. The
; TIB stack bounds (StackBase gs:[8], StackLimit gs:[16], DeallocationStack gs:[1478h])
; are switched too, because the exception unwinder, __chkstk, and stack walks reject a
; stack outside these bounds.
; @see https://learn.microsoft.com/en-us/cpp/build/x64-software-conventions#register-usage
;
; The jmpbuf layout, in bytes, must match MD_GET_SP and _st_jmp_buf_t in md.h:
;       0 rbx, 8 rbp, 16 rdi, 24 rsi, 32-56 r12-r15, 64 rsp (slot 8), 72 pc (slot 9),
;       80 StackBase, 88 StackLimit, 96 DeallocationStack, 104 mxcsr, 108 x87 control word,
;       128-287 xmm6-xmm15 (unaligned, 16 bytes each).
JB_RBX      EQU 0
JB_RBP      EQU 8
JB_RDI      EQU 16
JB_RSI      EQU 24
JB_R12      EQU 32
JB_R13      EQU 40
JB_R14      EQU 48
JB_R15      EQU 56
JB_RSP      EQU 64
JB_PC       EQU 72
JB_STKBASE  EQU 80
JB_STKLIMIT EQU 88
JB_STKDEALL EQU 96
JB_MXCSR    EQU 104
JB_FPUCW    EQU 108
JB_XMM6     EQU 128

; The TIB (NT_TIB of the TEB) offsets from gs.
TIB_STKBASE  EQU 8
TIB_STKLIMIT EQU 16
TIB_STKDEALL EQU 1478h

.code

; int _st_md_cxt_save(_st_jmp_buf_t env), env is rcx. Returns 0.
_st_md_cxt_save PROC
    mov [rcx+JB_RBX], rbx
    mov [rcx+JB_RBP], rbp
    mov [rcx+JB_RDI], rdi
    mov [rcx+JB_RSI], rsi
    mov [rcx+JB_R12], r12
    mov [rcx+JB_R13], r13
    mov [rcx+JB_R14], r14
    mov [rcx+JB_R15], r15
    ; The SP of the caller, after this function returns.
    lea rdx, [rsp+8]
    mov [rcx+JB_RSP], rdx
    ; The PC this function returns to.
    mov rdx, [rsp]
    mov [rcx+JB_PC], rdx
    ; The TIB stack bounds.
    mov rdx, gs:[TIB_STKBASE]
    mov [rcx+JB_STKBASE], rdx
    mov rdx, gs:[TIB_STKLIMIT]
    mov [rcx+JB_STKLIMIT], rdx
    mov rdx, gs:[TIB_STKDEALL]
    mov [rcx+JB_STKDEALL], rdx
    ; The floating-point control state.
    stmxcsr dword ptr [rcx+JB_MXCSR]
    fnstcw word ptr [rcx+JB_FPUCW]
    movdqu [rcx+JB_XMM6+0], xmm6
    movdqu [rcx+JB_XMM6+16], xmm7
    movdqu [rcx+JB_XMM6+32], xmm8
    movdqu [rcx+JB_XMM6+48], xmm9
    movdqu [rcx+JB_XMM6+64], xmm10
    movdqu [rcx+JB_XMM6+80], xmm11
    movdqu [rcx+JB_XMM6+96], xmm12
    movdqu [rcx+JB_XMM6+112], xmm13
    movdqu [rcx+JB_XMM6+128], xmm14
    movdqu [rcx+JB_XMM6+144], xmm15
    xor eax, eax
    ret
_st_md_cxt_save ENDP

; void _st_md_cxt_restore(_st_jmp_buf_t env, int val), env is rcx, val is edx.
; Jumps back to the save of env, which then returns val, or 1 if val is 0.
_st_md_cxt_restore PROC
    mov rbx, [rcx+JB_RBX]
    mov rbp, [rcx+JB_RBP]
    mov rdi, [rcx+JB_RDI]
    mov rsi, [rcx+JB_RSI]
    mov r12, [rcx+JB_R12]
    mov r13, [rcx+JB_R13]
    mov r14, [rcx+JB_R14]
    mov r15, [rcx+JB_R15]
    mov r8, [rcx+JB_STKBASE]
    mov gs:[TIB_STKBASE], r8
    mov r8, [rcx+JB_STKLIMIT]
    mov gs:[TIB_STKLIMIT], r8
    mov r8, [rcx+JB_STKDEALL]
    mov gs:[TIB_STKDEALL], r8
    ldmxcsr dword ptr [rcx+JB_MXCSR]
    fldcw word ptr [rcx+JB_FPUCW]
    movdqu xmm6, [rcx+JB_XMM6+0]
    movdqu xmm7, [rcx+JB_XMM6+16]
    movdqu xmm8, [rcx+JB_XMM6+32]
    movdqu xmm9, [rcx+JB_XMM6+48]
    movdqu xmm10, [rcx+JB_XMM6+64]
    movdqu xmm11, [rcx+JB_XMM6+80]
    movdqu xmm12, [rcx+JB_XMM6+96]
    movdqu xmm13, [rcx+JB_XMM6+112]
    movdqu xmm14, [rcx+JB_XMM6+128]
    movdqu xmm15, [rcx+JB_XMM6+144]
    ; Return val, or 1 if val is 0.
    mov eax, 1
    test edx, edx
    cmovne eax, edx
    ; Restore SP and jump to the saved PC.
    mov r8, [rcx+JB_PC]
    mov rsp, [rcx+JB_RSP]
    jmp r8
_st_md_cxt_restore ENDP

EXTERN _st_thread_main:PROC

; void _st_md_thread_start(void), the entry of a new thread, never returns.
; MD_INIT_THREAD_ENTRY in md.h points the PC of the new thread's jmpbuf here, and its SP at a
; null return address, 8 mod 16 as at any function entry, so _st_md_cxt_restore enters this
; function as if called from address 0, which ends stack walks. The thread no longer resumes
; after _st_md_cxt_save in st_thread_create, so it does not depend on how the compiler uses
; that frame or its registers after save returns twice (/Od, /O2, /GL).
_st_md_thread_start PROC FRAME
    ; The 32-byte home space for the callee, and 8 bytes to align rsp to 16 at the call.
    sub rsp, 40
    .allocstack 40
    .endprolog
    call _st_thread_main
    ; _st_thread_main calls st_thread_exit, which never returns.
    int 3
_st_md_thread_start ENDP

END
