set shell := ["bash", "-cu"]

arch := "i686-elf"
# Use string concatenation so the variable expands to e.g. i686-elf-g++
cxx := arch + "-g++"
as := "nasm"
ld := arch + "-ld"
objcopy := arch + "-objcopy"

kernel_sources := "kernel/kernel.cpp kernel/screen.cpp"
kernel_objs := "build/kernel.o build/screen.o"
boot_obj := "build/boot.bin"

@default:
    just build

build-kernel:
    mkdir -p build
    {{cxx}} -ffreestanding -m32 -c kernel/kernel.cpp -o build/kernel.o
    {{cxx}} -ffreestanding -m32 -c kernel/screen.cpp -o build/screen.o
    {{ld}} -T linker.ld -m elf_i386 -o build/kernel.elf build/kernel.o build/screen.o
    {{objcopy}} -O binary build/kernel.elf build/kernel.bin

build-boot:
    mkdir -p build
    # Assemble stage 1 boot sector (512 bytes, exactly)
    {{as}} -f bin boot/boot1.asm -o build/boot1.bin
    # Assemble stage 2 bootloader
    {{as}} -f bin boot/boot2.asm -o build/boot2.bin
    # Pad stage 2 to 512 bytes (or more as needed)
    dd if=build/boot2.bin of=build/boot2.padded bs=512 count=1 conv=sync 2>/dev/null
    # Combine: boot sector + stage 2 + kernel
    cat build/boot1.bin build/boot2.padded > build/boot_combined.bin

build: build-kernel build-boot build-image
    echo "✅ HanaCore built -> build/HanaCore.bin"

build-image: build-kernel build-boot
    mkdir -p build
    cat build/boot1.bin build/boot2.padded build/kernel.bin > build/HanaCore.bin
    echo "✅ HanaCore built ($(stat -c%s build/HanaCore.bin) bytes) -> build/HanaCore.bin"

run:
    echo "🌸 Starting HanaCore in QEMU..."
    qemu-system-i386 -drive format=raw,file=build/HanaCore.bin

run-cli:
    echo "🌸 Starting HanaCore in QEMU (serial/stdio)..."
    qemu-system-i386 -drive format=raw,file=build/HanaCore.bin -serial stdio -monitor none -display none -no-reboot

clean:
    rm -rf build
    echo "🧹 Cleaned build files."
