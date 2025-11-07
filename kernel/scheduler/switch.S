# context_switch.S
# GNU assembler version for HanaCore scheduler
# 
# void context_switch(uint64_t **old_sp_ptr, uint64_t **new_sp_ptr,
#                     void *old_fx, void *new_fx);
#
# Arguments:
#   rdi = &old_sp_ptr
#   rsi = &new_sp_ptr
#   rdx = old_fx
#   rcx = new_fx
#
# Callee-saved registers (System V): rbx, rbp, r12–r15
# Stack layout after switch (for new task):
#   [r15][r14][r13][r12][rbx][rbp][ret_addr]
#   so context_switch pops these and returns into ret_addr (task_trampoline)

    .section .text
    .global context_switch
    .type context_switch, @function
context_switch:

    # Save FPU/SSE state to *old_fx
    fxsave  (%rdx)

    pushq   %rbp
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15
    pushq   %rdi
    pushq   %rsi
    pushq   %rdx
    pushq   %rcx
    pushq   %r8
    pushq   %r9
    pushq   %r10
    pushq   %r11
    pushq   %rax

    # Save old RSP value (pointing to the bottom of pushed register frame)
    movq    %rsp, (%rdi)

    # Load new RSP value
    movq    (%rsi), %rsp

    # Restore FPU/SSE state of new task
    fxrstor (%rcx)

    # Pop registers in the reverse order to restore the new task's context
    popq    %rax
    popq    %r11
    popq    %r10
    popq    %r9
    popq    %r8
    popq    %rcx
    popq    %rdx
    popq    %rsi
    popq    %rdi
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    popq    %rbp

    ret

    .size context_switch, .-context_switch
