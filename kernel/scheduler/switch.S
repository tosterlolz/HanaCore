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

    # NOTE: fxsave disabled temporarily for debugging
    # fxsave  (%rdx)

    # Push callee-saved registers of old task onto its stack
    pushq   %rbp
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15

    # Save old RSP value
    movq    %rsp, (%rdi)

    # Load new RSP value
    movq    (%rsi), %rsp

    # NOTE: fxrstor disabled temporarily for debugging
    # fxrstor (%rcx)

    # Pop callee-saved registers for the new task
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    popq    %rbp

    ret

    .size context_switch, .-context_switch
