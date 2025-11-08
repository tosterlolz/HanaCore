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

     /* Call into C dispatcher: syscall_dispatch(num, a1, a2, a3, a4, a5)
         Syscall calling convention: rax=num, rdi=a1, rsi=a2, rdx=a3,
         r10=a4, r8=a5, r9=a6
         C call convention expects args in rdi,rsi,rdx,rcx,r8,r9.
         We'll forward up to 5 user args (a1..a5) into the C call registers.
         Use saved callee registers (r11..r15, rbx) as temporaries (they were
         pushed above), so it's safe to clobber them here.
     */
      /* move user args into temporaries */
      mov %rdi, %r11    /* t1 = a1 */
      mov %rsi, %r12    /* t2 = a2 */
      mov %rdx, %r13    /* t3 = a3 */
      mov %r10, %r14    /* t4 = a4 */
      mov %r8,  %r15    /* t5 = a5 */
      mov %r9,  %rbx    /* t6 = a6 (use rbx temporary) */
      /* place 6th argument on stack as 7th arg for C call */
      sub $8, %rsp
      mov %rbx, (%rsp)
      /* arrange C call args: rdi=num, rsi=a1, rdx=a2, rcx=a3, r8=a4, r9=a5
          (the 7th arg a6 is on stack)
      */
      mov %rax, %rdi
      mov %r11, %rsi
      mov %r12, %rdx
      mov %r13, %rcx
      mov %r14, %r8
      mov %r15, %r9
      call syscall_dispatch
      add $8, %rsp

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
