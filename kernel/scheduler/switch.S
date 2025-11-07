; context_switch.asm
; void context_switch(uint64_t **old_sp_ptr, uint64_t **new_sp_ptr)
; rdi = &old_sp
; rsi = &new_sp

global context_switch
section .text
context_switch:
    ; Save callee-saved registers
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp     ; save old rsp
    mov rsp, [rsi]     ; load new rsp

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
