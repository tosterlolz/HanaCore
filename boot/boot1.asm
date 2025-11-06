; HanaCore Bootloader - Stage 1 (MBR)
; 512-byte boot sector at 0x7c00
; Loads Stage 2 from image and jumps to it

[bits 16]
[org 0x7c00]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7c00

    ; Print single character to indicate stage 1 running
    mov ax, 0x0e31              ; '1' in teletype mode
    int 0x10

    ; Calculate source address: 0x7c00 + 512 = 0x7e00
    ; But stage2 should load to 0x7e00, so source in memory image
    ; Image layout: [boot1: 0-512] [boot2: 512-1024] [kernel: 1024+]
    ; At runtime: boot1 is at 0x7c00-0x7dff
    ;            boot2 is at 0x7e00-0x7fff (already in place!)
    ; So we don't need to copy - it's already there!
    
    ; Just jump to stage 2
    jmp 0x7e00

    ; Should not return
    cli
    hlt

; Padding to 510 bytes, then boot signature
times 510 - ($ - $$) db 0
dw 0xaa55
