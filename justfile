set shell := ["bash", "-cu"]

# Use cross-toolchain for freestanding higher-half kernel
prefix := "x86_64-elf"
cc := prefix + "-gcc"
cxx := prefix + "-g++"
as := "nasm"
ld := prefix + "-ld"
objcopy := prefix + "-objcopy"

@default:
    just build

build-kernel:
    mkdir -p build/iso_root/boot
    {{cxx}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-exceptions -fno-rtti -fno-stack-protector -c kernel/kernel.cpp -o build/kernel.o
    {{cxx}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-exceptions -fno-rtti -fno-stack-protector -c kernel/drivers/framebuffer.cpp -o build/framebuffer.o
    {{cxx}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-exceptions -fno-rtti -fno-stack-protector -c kernel/arch/gdt.cpp -o build/gdt.o
    {{cxx}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-exceptions -fno-rtti -fno-stack-protector -c kernel/arch/idt.cpp -o build/idt.o
    {{cxx}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-stack-protector -c kernel/drivers/screen.cpp -o build/screen.o
    {{cxx}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-exceptions -fno-rtti -fno-stack-protector -c kernel/drivers/keyboard.cpp -o build/keyboard.o
    {{cxx}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-stack-protector -c kernel/utils/logger.cpp -o build/logger.o
    # Build built-in userland shell (linked into kernel as fallback)
    {{cxx}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-exceptions -fno-rtti -fno-stack-protector -c userland/shell.cpp -o build/shell.o
    # Compile Flanterm C sources separately so the kernel TU remains C++ only
    {{cc}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-stack-protector -c flanterm/src/flanterm.c -o build/flanterm.o
    {{cc}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-stack-protector -c flanterm/src/flanterm_backends/fb.c -o build/flanterm_fb.o
    {{cc}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-stack-protector -c boot/limine_entry.c -o build/limine_entry.o
    {{cc}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-stack-protector -c kernel/libs/nanoprintf.c -o build/nanoprintf.o
    {{cc}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-stack-protector -c kernel/libs/libc.c -o build/libc.o
    {{ld}} -m elf_x86_64 -T linker.ld -o build/kernel.elf build/limine_entry.o build/kernel.o build/gdt.o build/idt.o build/framebuffer.o build/screen.o build/keyboard.o build/shell.o build/logger.o build/nanoprintf.o build/libc.o build/flanterm.o build/flanterm_fb.o
    cp build/kernel.elf build/iso_root/boot/kernel

build-image: build-kernel
    mkdir -p build/iso_root/boot
    # Copy Limine files
    cp limine-bootloader/limine-bios.sys build/iso_root/boot/
    cp limine-bootloader/limine-bios-cd.bin build/iso_root/boot/
    cp cfg/limine.conf build/iso_root/boot/
    # Create ISO
    xorriso -as mkisofs -b boot/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table build/iso_root -o build/HanaCore.iso 2>/dev/null || echo "xorriso failed, trying without it..."
    # Install Limine to ISO
    limine-bootloader/limine bios-install build/HanaCore.iso 2>/dev/null || echo "Failed to install Limine (might need xorriso)"

build: build-image
    echo "✅ HanaCore ISO built -> build/HanaCore.iso"

run: build
    echo "🌸 Starting HanaCore in QEMU..."
    qemu-system-x86_64 -cdrom build/HanaCore.iso

run-cli: build
    echo "🌸 Starting HanaCore in QEMU (serial/stdio)..."
    qemu-system-x86_64 -cdrom build/HanaCore.iso -serial stdio -no-shutdown -no-reboot

debug: build
        echo "🐛 Starting HanaCore in QEMU (debug mode). Logs: /tmp/serial.log /tmp/debug.log"
        qemu-system-x86_64 -cdrom build/HanaCore.iso -m 512 \
            -serial file:/tmp/serial.log \
            -debugcon file:/tmp/debug.log \
            -no-reboot -no-shutdown -s -S -display gtk &
        sleep 0.5
        gdb -q build/kernel.elf \
            -ex "target remote :1234" \
            -ex "set confirm off" \
            -ex "break kernel_main" \
            -ex "break clear_screen" \
            -ex "break framebuffer_init" \
            -ex "break flanterm_fb_init" \
            -ex "continue"
        pkill qemu

clean:
    rm -rf build
    echo "🧹 Cleaned build files."
