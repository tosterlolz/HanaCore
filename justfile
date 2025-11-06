set shell := ["bash", "-cu"]

arch := "i686-elf"
# Use string concatenation so the variable expands to e.g. i686-elf-g++
cxx := arch + "-g++"
as := "nasm"

kernel_sources := "kernel/kernel.cpp kernel/screen.cpp"
kernel_objs := "build/kernel.o build/screen.o"
boot_obj := "build/boot.bin"

@default:
    just build

build-kernel:
    mkdir -p build
    {{cxx}} -ffreestanding -m32 -c kernel/kernel.cpp -o build/kernel.o
    {{cxx}} -ffreestanding -m32 -c kernel/screen.cpp -o build/screen.o

build-boot:
    mkdir -p build
    {{as}} -f bin boot/boot.asm -o {{boot_obj}}

build: build-boot build-kernel
    echo "🌸 Linking HanaCore OS..."
    cat {{boot_obj}} build/kernel.o build/screen.o > build/HanaCore.bin
    echo "✅ HanaCore built -> build/HanaCore.bin"

run:
    echo "🌸 Starting HanaCore in QEMU..."
    qemu-system-i386 -drive format=raw,file=build/HanaCore.bin

clean:
    rm -rf build
    echo "🧹 Cleaned build files."
