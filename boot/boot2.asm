; HanaCore Bootloader - Stage 2
; Loaded at 0x7e00 by stage 1
; Initialize hardware, switch to protected mode, jump to kernel

[bits 16]
[org 0x7e00]

start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7e00

    ; Print '2' to indicate stage 2 running
    mov ax, 0x0e32
    int 0x10

    ; Print '.' to show we got past first BIOS call
    mov ax, 0x0e2e
    int 0x10

    ; Initialize serial
    call serial_init

    ; Print '3' after serial init
    mov ax, 0x0e33
    int 0x10

    ; Print to serial
    mov si, msg_start
    call serial_puts

    ; Enable A20
    call enable_a20

    ; Print '4' after A20
    mov ax, 0x0e34
    int 0x10

    mov si, msg_a20
    call serial_puts

    ; Load GDT
    lgdt [gdt_ptr]

    ; Print '5' after GDT load
    mov ax, 0x0e35
    int 0x10

    ; Enter protected mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Print '6' after CR0 change (might not show)
    mov ax, 0x0e36
    int 0x10

    ; Far jump to protected mode
    jmp dword 0x08:pm_start

; ====== ROUTINES ======

serial_init:
    mov dx, 0x3f9
    mov al, 0x00
    out dx, al

    mov dx, 0x3fb
    mov al, 0x80
    out dx, al

    mov dx, 0x3f8
    mov al, 0x01
    out dx, al

    mov dx, 0x3f9
    mov al, 0x00
    out dx, al

    mov dx, 0x3fb
    mov al, 0x03
    out dx, al

    mov dx, 0x3fa
    mov al, 0xc7
    out dx, al

    mov dx, 0x3fc
    mov al, 0x0b
    out dx, al
    ret

serial_puts:
    ; SI -> string, output to serial
.loop:
    lodsb
    test al, al
    jz .done

    ; Wait for ready
    mov dx, 0x3fd
.wait:
    in al, dx
    test al, 0x20
    jz .wait

    ; Send char
    mov al, byte [si - 1]
    mov dx, 0x3f8
    out dx, al
    jmp .loop

.done:
    ret

enable_a20:
    in al, 0x92
    or al, 0x02
    out 0x92, al
    ret

; ====== DATA ======

msg_start: db "STAGE2", 13, 10, 0
msg_a20: db "A20_OK", 13, 10, 0

align 8
gdt:
    dq 0
    dq 0x00cf9a000000ffff
    dq 0x00cf92000000ffff
gdt_end:

gdt_ptr:
    dw gdt_end - gdt - 1
    dd gdt

; ====== PROTECTED MODE ======

[bits 32]

pm_start:
    mov eax, 0x10
    mov ds, eax
    mov es, eax
    mov ss, eax
    mov esp, 0x90000

    ; Jump to kernel
    mov eax, 0x10000
    jmp eax

    cli
    hlt
