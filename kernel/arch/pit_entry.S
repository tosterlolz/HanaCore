    .text
    .globl pit_entry
    .type pit_entry,@function
    pit_entry:
        # Save general purpose registers. We'll restore them before returning
        # so the interrupted context is preserved.
        push %rax
        push %rbx
        push %rcx
        push %rdx
        push %rsi
        push %rdi
        push %rbp
        push %r8
        push %r9
        push %r10
        push %r11
        push %r12
        push %r13
        push %r14
        push %r15

        # Call the C-level PIT handler.
        call pit_isr

        # Restore registers in reverse order.
        pop %r15
        pop %r14
        pop %r13
        pop %r12
        pop %r11
        pop %r10
        pop %r9
        pop %r8
        pop %rbp
        pop %rdi
        pop %rsi
        pop %rdx
        pop %rcx
        pop %rbx
        pop %rax

        iretq
