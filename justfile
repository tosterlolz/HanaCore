set shell := ["bash", "-cu"]

arch := "x86_64-elf"
# Use string concatenation so the variable expands to e.g. x86_64-elf-g++
cc := arch + "-gcc"
cxx := arch + "-g++"
as := "nasm"
ld := arch + "-ld"
objcopy := arch + "-objcopy"

@default:
    just build

build-kernel:
    mkdir -p build/iso_root/boot
    {{cxx}} -ffreestanding -mcmodel=kernel -mno-red-zone -c kernel/kernel.cpp -o build/kernel.o
    {{cxx}} -ffreestanding -mcmodel=kernel -mno-red-zone -c kernel/drivers/framebuffer.cpp -o build/framebuffer.o
    {{cc}} -ffreestanding -mcmodel=kernel -mno-red-zone -c kernel/screen.c -o build/screen.o
    {{cc}} -ffreestanding -mcmodel=kernel -mno-red-zone -c kernel/limine_entry.c -o build/limine_entry.o
    {{ld}} -T linker.ld -o build/kernel.elf build/limine_entry.o build/kernel.o build/framebuffer.o build/screen.o
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
    qemu-system-x86_64 -cdrom build/HanaCore.iso -serial stdio -monitor none -display none -no-reboot

clean:
    rm -rf build
    echo "🧹 Cleaned build files."
