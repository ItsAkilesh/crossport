/*
 * File: shim/setjmp.c
 * Purpose: setjmp and longjmp for System V code, saving the callee-saved registers, stack pointer and return address
 * Author: Akilesh
 * Date: 2026-10-28
 *
 * Only the first 64 bytes of the caller's 200-byte jmp_buf are used. There is no signal mask to save.
 */

#include "libc_abi.h"

/* The exported names are the aliases below; each label is its own entry point into the same code. */
LIBC_FN int sysv__setjmp(void *environment);
LIBC_FN int sysv_setjmp(void *environment);
LIBC_FN int sysv___sigsetjmp(void *environment, int save_mask);
LIBC_FN NORETURN void sysv_longjmp(void *environment, int value);
LIBC_FN NORETURN void sysv__longjmp(void *environment, int value);
LIBC_FN NORETURN void sysv_siglongjmp(void *environment, int value);
LIBC_FN NORETURN void sysv___longjmp_chk(void *environment, int value);

__asm__(".text\n"
        ".globl sysv__setjmp\n"
        ".globl sysv_setjmp\n"
        ".globl sysv___sigsetjmp\n"
        "sysv__setjmp:\n"
        "sysv_setjmp:\n"
        "sysv___sigsetjmp:\n"
        "    mov %rbx, 0(%rdi)\n"
        "    mov %rbp, 8(%rdi)\n"
        "    mov %r12, 16(%rdi)\n"
        "    mov %r13, 24(%rdi)\n"
        "    mov %r14, 32(%rdi)\n"
        "    mov %r15, 40(%rdi)\n"
        "    lea 8(%rsp), %rdx\n"
        "    mov %rdx, 48(%rdi)\n"
        "    mov (%rsp), %rdx\n"
        "    mov %rdx, 56(%rdi)\n"
        "    xor %eax, %eax\n"
        "    ret\n"
        ".globl sysv_longjmp\n"
        ".globl sysv__longjmp\n"
        ".globl sysv_siglongjmp\n"
        ".globl sysv___longjmp_chk\n"
        "sysv_longjmp:\n"
        "sysv__longjmp:\n"
        "sysv_siglongjmp:\n"
        "sysv___longjmp_chk:\n"
        "    mov %esi, %eax\n"
        "    test %eax, %eax\n"
        "    jnz 1f\n"
        "    inc %eax\n"
        "1:  mov 0(%rdi), %rbx\n"
        "    mov 8(%rdi), %rbp\n"
        "    mov 16(%rdi), %r12\n"
        "    mov 24(%rdi), %r13\n"
        "    mov 32(%rdi), %r14\n"
        "    mov 40(%rdi), %r15\n"
        "    mov 48(%rdi), %rsp\n"
        "    jmp *56(%rdi)\n");
