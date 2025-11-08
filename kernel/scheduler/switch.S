.section .text
.global context_switch
.type context_switch, @function

context_switch:
    # Save callee-saved registers of old task
    pushq   %rbp
    pushq   %rbx
    pushq   %r12
    pushq   %r13
    pushq   %r14
    pushq   %r15

    # Save old RSP into *old_sp_ptr (in %rdi)
    movq    %rsp, (%rdi)

    # Load new RSP from *new_sp_ptr (in %rsi)
    movq    (%rsi), %rsp

    # Restore registers for new task
    popq    %r15
    popq    %r14
    popq    %r13
    popq    %r12
    popq    %rbx
    popq    %rbp

    ret
.size context_switch, .-context_switch
