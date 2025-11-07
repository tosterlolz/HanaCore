    .text
    .globl context_switch
    .type context_switch,@function
// void context_switch(uint64_t **old_sp_ptr, uint64_t **new_sp_ptr)
context_switch:
    // Save callee-saved registers on current stack
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    // Store current rsp into *old_sp_ptr (rdi holds pointer)
    mov %rsp, (%rdi)
    // Load new rsp from *new_sp_ptr (rsi holds pointer)
    mov (%rsi), %rsp
    // Restore callee-saved registers from new stack
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    ret
