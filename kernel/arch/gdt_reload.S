    .section .text
    .global gdt_reload_segments
gdt_reload_segments:
    /* Perform far return trick to reload CS with selector 0x08 (GDT entry 1). */
    mov $0x08, %rax
    /* push selector and RIP for lretq */
    pushq $0x08
    lea 1f(%rip), %rax
    pushq %rax
    lretq
1:
    /* Reload data segment registers with selector 0x10 */
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs
    ret
