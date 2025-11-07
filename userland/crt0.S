.text
.global _start
.type _start,@function
_start:
    /* Minimal crt0: set up argc/argv (0 / NULL) and call main
       The kernel's elf_loader will call this entry. */
    xor %rbp, %rbp
    xor %rdi, %rdi    /* argc = 0 */
    xor %rsi, %rsi    /* argv = NULL */
    call main
    /* On return, call hana_exit(returncode) */
    mov %rax, %rdi
    call hana_exit
.size _start, .-_start
