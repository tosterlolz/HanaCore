    .text
    .section .text
    .global syscall_entry
    .type syscall_entry,@function
syscall_entry:
    /* Swap GS to kernel GS base */
    swapgs

    /* Save user RIP (in rcx) and RFLAGS (in r11) pushed by syscall */
    push %r11
    push %rcx

    /* Save caller-saved callee registers we will clobber */
    push %r15
    push %r14
    push %r13
    push %r12
    push %rbx

    /* Create a frame for C call - keep stack 16-byte aligned */
    mov %rsp, %rbp

    /* Call into C dispatcher: syscall_dispatch(num, a, b, c) */
    /* Registers already: rax=num, rdi=a, rsi=b, rdx=c */
    call syscall_dispatch

    /* Restore stack/frame and registers */
    mov %rbp, %rsp
    pop %rbx
    pop %r12
    pop %r13
    pop %r14
    pop %r15

    /* Restore saved user RIP/RFLAGS */
    pop %rcx
    pop %r11

    swapgs
    /* Return to user via sysretq */
    sysretq

.size syscall_entry, .-syscall_entry
