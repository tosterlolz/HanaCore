    .section .text
    .global isr_common_stub
isr_common_stub:
    /* Minimal stub: simply return from interrupt. Caller must ensure stack is sane. */
    iretq
