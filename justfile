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
    # Kernel helper sources
    {{cxx}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-exceptions -fno-rtti -fno-stack-protector -c kernel/mem/bump_alloc.cpp -o build/bump_alloc.o
    {{cxx}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-exceptions -fno-rtti -fno-stack-protector -c kernel/userland/elf_loader.cpp -o build/elf_loader.o
    {{cxx}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-exceptions -fno-rtti -fno-stack-protector -c kernel/filesystem/ext2.cpp -o build/ext2.o
    {{cxx}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-exceptions -fno-rtti -fno-stack-protector -c kernel/userland/module_runner.cpp -o build/module_runner.o
    {{cxx}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-exceptions -fno-rtti -fno-stack-protector -c kernel/userland/syscalls.cpp -o build/syscalls.o
    # Compile Flanterm C sources separately so the kernel TU remains C++ only
    {{cc}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-stack-protector -c flanterm/src/flanterm.c -o build/flanterm.o
    {{cc}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-stack-protector -c flanterm/src/flanterm_backends/fb.c -o build/flanterm_fb.o
    {{cc}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-stack-protector -c boot/limine_entry.c -o build/limine_entry.o
    {{cc}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-stack-protector -c kernel/libs/nanoprintf.c -o build/nanoprintf.o
    {{cc}} -ffreestanding -nostdlib -mcmodel=kernel -mno-red-zone -fno-stack-protector -c kernel/libs/libc.c -o build/libc.o
    {{ld}} -m elf_x86_64 -T linker.ld -o build/kernel.elf build/limine_entry.o build/kernel.o build/gdt.o build/idt.o build/framebuffer.o build/screen.o build/keyboard.o build/shell.o build/logger.o build/bump_alloc.o build/elf_loader.o build/ext2.o build/module_runner.o build/syscalls.o build/nanoprintf.o build/libc.o build/flanterm.o build/flanterm_fb.o
    cp build/kernel.elf build/iso_root/boot/kernel

build-image: build-kernel mkrootfs
    mkdir -p build/iso_root/boot
    # Copy Limine files
    cp limine-bootloader/limine-bios.sys build/iso_root/boot/
    cp limine-bootloader/limine-bios-cd.bin build/iso_root/boot/
    cp cfg/limine.conf build/iso_root/boot/
    # If a generated rootfs image exists, include it as a module on the ISO
    if [ -f build/rootfs.img ]; then cp build/rootfs.img build/iso_root/boot/; fi
    # Create ISO
    xorriso -as mkisofs -b boot/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table build/iso_root -o build/HanaCore.iso 2>/dev/null || echo "xorriso failed, trying without it..."
    # Install Limine to ISO
    limine-bootloader/limine bios-install build/HanaCore.iso 2>/dev/null || echo "Failed to install Limine (might need xorriso)"

build: build-image
    echo "✅ HanaCore ISO built -> build/HanaCore.iso"


# Create a tiny ext2 rootfs image and install small /bin programs that the
# kernel's simple module runner (flat binaries) can execute. This target
# requires: nasm, mke2fs (or mkfs.ext2), and debugfs (from e2fsprogs).
mkrootfs:
    echo "-> Creating rootfs image (build/rootfs.img)"
    rm -rf build/rootfs_src build/tmp || true
    mkdir -p build/rootfs_src/bin build/tmp

    rm -f build/rootfs.img || true
    # Use mke2fs with -d to populate from a directory if available
    if command -v mke2fs >/dev/null 2>&1; then
        echo "-> Creating ext2 image from directory (mke2fs -d)"
        # mke2fs -d will copy files from the given directory into the new image
        mke2fs -q -F -t ext2 -d build/rootfs_src build/rootfs.img >/dev/null 2>&1 || true
    elif command -v mkfs.ext2 >/dev/null 2>&1; then
        echo "-> Creating ext2 image (mkfs.ext2) and falling back to debugfs copy"
        dd if=/dev/zero of=build/rootfs.img bs=1M count=4 >/dev/null 2>&1 || true
        mkfs.ext2 -q build/rootfs.img >/dev/null 2>&1 || true
    else
        echo "No mke2fs/mkfs.ext2 found; cannot create ext2 image"; exit 1
    fi

    # Assemble small flat binaries for /bin/hello and /bin/echo (NASM)
    echo "-> Assembling flat-binary /bin/hello and /bin/echo"
    printf $'bits 64\norg 0\nsection .text\nglobal _start\n_start:\n    mov rcx, 0\n    mov rsi, hello\n.write_loop:\n    mov al, [rsi+rcx]\n    cmp al, 0\n    je .done\n    mov dx, 0x3f8\n    out dx, al\n    inc rcx\n    jmp .write_loop\n.done:\n    ret\nsection .data\nhello: db '\''Hello from /bin/hello'\'', 10, 0\n' > build/tmp/hello.asm
    {{as}} -f bin build/tmp/hello.asm -o build/tmp/hello.bin || true
    printf $'bits 64\norg 0\nsection .text\nglobal _start\n_start:\n    mov rcx, 0\n    mov rsi, hello2\n.loop2:\n    mov al, [rsi+rcx]\n    cmp al, 0\n    je .done2\n    mov dx, 0x3f8\n    out dx, al\n    inc rcx\n    jmp .loop2\n.done2:\n    ret\nsection .data\nhello2: db '\''Hello from /bin/echo'\'', 10, 0\n' > build/tmp/echo.asm
    {{as}} -f bin build/tmp/echo.asm -o build/tmp/echo.bin || true

    # Also build tiny ELF utilities (ls, cat, echo) so the shell can load
    # ELF programs from the ext2 image. These are simple serial-printing
    # programs (placeholders for real coreutils).
    echo "-> Building tiny ELF /bin/ls, /bin/cat, /bin/echo"
    printf $'bits 64\nglobal _start\nsection .text\n_start:\n    mov rcx, 0\n    mov rsi, message\n.loop:\n    mov al, [rsi+rcx]\n    cmp al, 0\n    je .done\n    mov dx, 0x3f8\n    out dx, al\n    inc rcx\n    jmp .loop\n.done:\n    ret\nsection .data\nmessage: db '\''%s'\'', 10, 0\n' > build/tmp/elf_print.asm
    # Instead build small C coreutils and place them into build/rootfs_src/bin so
    # mke2fs -d will include them into the image.
    echo "-> Building C coreutils into build/tmp and copying into build/rootfs_src/bin"
    {{cc}} -ffreestanding -nostdlib -fno-asynchronous-unwind-tables -fno-exceptions -O2 -c userland/coreutils/ls.c -o build/tmp/ls.o || true
    {{ld}} -m elf_x86_64 -N -e _start -Ttext=0x0 -o build/tmp/ls.elf build/tmp/ls.o || true
    {{cc}} -ffreestanding -nostdlib -fno-asynchronous-unwind-tables -fno-exceptions -O2 -c userland/coreutils/cat.c -o build/tmp/cat.o || true
    {{ld}} -m elf_x86_64 -N -e _start -Ttext=0x0 -o build/tmp/cat.elf build/tmp/cat.o || true
    {{cc}} -ffreestanding -nostdlib -fno-asynchronous-unwind-tables -fno-exceptions -O2 -c userland/coreutils/echo.c -o build/tmp/echo.o || true
    {{ld}} -m elf_x86_64 -N -e _start -Ttext=0x0 -o build/tmp/echo.elf build/tmp/echo.o || true
    # Ensure rootfs_src/bin exists and copy built ELFs there so mke2fs -d picks them up
    cp build/tmp/ls.elf build/rootfs_src/bin/ls 2>/dev/null || true
    cp build/tmp/cat.elf build/rootfs_src/bin/cat 2>/dev/null || true
    cp build/tmp/echo.elf build/rootfs_src/bin/echo 2>/dev/null || true

    # If we created the image from `build/rootfs_src` (via mke2fs -d), files
    # should already be present. Otherwise, try debugfs as a fallback.
    if [ -f build/rootfs.img ] && command -v debugfs >/dev/null 2>&1 && ! command -v mke2fs >/dev/null 2>&1; then \
        debugfs -w -R "mkdir /bin" build/rootfs.img >/dev/null 2>&1 || true; \
        debugfs -w -R "write build/tmp/hello.bin /bin/hello" build/rootfs.img >/dev/null 2>&1 || true; \
        debugfs -w -R "write build/tmp/echo.bin /bin/echo" build/rootfs.img >/dev/null 2>&1 || true; \
        debugfs -w -R "write build/tmp/ls.elf /bin/ls" build/rootfs.img >/dev/null 2>&1 || true; \
        debugfs -w -R "write build/tmp/cat.elf /bin/cat" build/rootfs.img >/dev/null 2>&1 || true; \
        debugfs -w -R "write build/tmp/echo.elf /bin/echo_elf" build/rootfs.img >/dev/null 2>&1 || true; \
        debugfs -w -R "write /home/toster/HanaCore/rootfs.img /init" build/rootfs.img >/dev/null 2>&1 || true; \
    else \
        if [ -f build/rootfs.img ]; then echo "rootfs image created and populated from build/rootfs_src"; else echo "rootfs image missing"; fi; \
    fi
    mkrootfs:
    	@echo "-> Creating rootfs image (build/rootfs.img)"
    	rm -rf build/rootfs_src build/tmp || true
    	mkdir -p build/rootfs_src/bin build/tmp

    	rm -f build/rootfs.img || true
    	# Use mke2fs with -d to populate from a directory if available
    	if command -v mke2fs >/dev/null 2>&1; then
    		@echo "-> Creating ext2 image from directory (mke2fs -d)"
    		# mke2fs -d will copy files from the given directory into the new image
    		mke2fs -q -F -t ext2 -d build/rootfs_src build/rootfs.img >/dev/null 2>&1 || true
    	elif command -v mkfs.ext2 >/dev/null 2>&1; then
    		@echo "-> Creating ext2 image (mkfs.ext2) and falling back to debugfs copy"
    		dd if=/dev/zero of=build/rootfs.img bs=1M count=4 >/dev/null 2>&1 || true
    		mkfs.ext2 -q build/rootfs.img >/dev/null 2>&1 || true
    	else
    		echo "No mke2fs/mkfs.ext2 found; cannot create ext2 image"; exit 1
    	fi

    	# Assemble small flat binaries for /bin/hello and /bin/echo (NASM)
    	@echo "-> Assembling flat-binary /bin/hello and /bin/echo"
    	printf $'bits 64\norg 0\nsection .text\nglobal _start\n_start:\n    mov rcx, 0\n    mov rsi, hello\n.write_loop:\n    mov al, [rsi+rcx]\n    cmp al, 0\n    je .done\n    mov dx, 0x3f8\n    out dx, al\n    inc rcx\n    jmp .write_loop\n.done:\n    ret\nsection .data\nhello: db '\''Hello from /bin/hello'\'', 10, 0\n' > build/tmp/hello.asm
    	{{as}} -f bin build/tmp/hello.asm -o build/tmp/hello.bin || true
    	printf $'bits 64\norg 0\nsection .text\nglobal _start\n_start:\n    mov rcx, 0\n    mov rsi, hello2\n.loop2:\n    mov al, [rsi+rcx]\n    cmp al, 0\n    je .done2\n    mov dx, 0x3f8\n    out dx, al\n    inc rcx\n    jmp .loop2\n.done2:\n    ret\nsection .data\nhello2: db '\''Hello from /bin/echo'\'', 10, 0\n' > build/tmp/echo.asm
    	{{as}} -f bin build/tmp/echo.asm -o build/tmp/echo.bin || true

    	# Build small C coreutils and copy into build/rootfs_src/bin so mke2fs -d
    	# includes them into the image when available.
    	@echo "-> Building C coreutils into build/tmp and copying into build/rootfs_src/bin"
    	{{cc}} -ffreestanding -nostdlib -fno-asynchronous-unwind-tables -fno-exceptions -O2 -c userland/coreutils/ls.c -o build/tmp/ls.o || true
    	{{ld}} -m elf_x86_64 -N -e _start -Ttext=0x0 -o build/tmp/ls.elf build/tmp/ls.o || true
    	{{cc}} -ffreestanding -nostdlib -fno-asynchronous-unwind-tables -fno-exceptions -O2 -c userland/coreutils/cat.c -o build/tmp/cat.o || true
    	{{ld}} -m elf_x86_64 -N -e _start -Ttext=0x0 -o build/tmp/cat.elf build/tmp/cat.o || true
    	{{cc}} -ffreestanding -nostdlib -fno-asynchronous-unwind-tables -fno-exceptions -O2 -c userland/coreutils/echo.c -o build/tmp/echo.o || true
    	{{ld}} -m elf_x86_64 -N -e _start -Ttext=0x0 -o build/tmp/echo.elf build/tmp/echo.o || true
    	# Ensure rootfs_src/bin exists and copy built ELFs there so mke2fs -d picks them up
    	cp build/tmp/ls.elf build/rootfs_src/bin/ls 2>/dev/null || true
    	cp build/tmp/cat.elf build/rootfs_src/bin/cat 2>/dev/null || true
    	cp build/tmp/echo.elf build/rootfs_src/bin/echo 2>/dev/null || true

    	# If we created the image from `build/rootfs_src` (via mke2fs -d), files
    	# should already be present. Otherwise, try debugfs as a fallback.
    	if [ -f build/rootfs.img ] && command -v debugfs >/dev/null 2>&1 && ! command -v mke2fs >/dev/null 2>&1; then
    		debugfs -w -R "mkdir /bin" build/rootfs.img >/dev/null 2>&1 || true
    		debugfs -w -R "write build/tmp/hello.bin /bin/hello" build/rootfs.img >/dev/null 2>&1 || true
    		debugfs -w -R "write build/tmp/echo.bin /bin/echo" build/rootfs.img >/dev/null 2>&1 || true
    		debugfs -w -R "write build/tmp/ls.elf /bin/ls" build/rootfs.img >/dev/null 2>&1 || true
    		debugfs -w -R "write build/tmp/cat.elf /bin/cat" build/rootfs.img >/dev/null 2>&1 || true
    		debugfs -w -R "write build/tmp/echo.elf /bin/echo_elf" build/rootfs.img >/dev/null 2>&1 || true
    		debugfs -w -R "write /home/toster/HanaCore/rootfs.img /init" build/rootfs.img >/dev/null 2>&1 || true
    	else
    		if [ -f build/rootfs.img ]; then echo "rootfs image created and populated from build/rootfs_src"; else echo "rootfs image missing"; fi
    	fi

    # Build and include an ELF module (userland/hello_module.c) into the ISO
    echo "-> Building ELF module userland/hello_module.c"
    {{cc}} -ffreestanding -nostdlib -fno-asynchronous-unwind-tables -fno-exceptions -O2 -c userland/hello_module.c -o build/tmp/hello_module.o || true
    {{ld}} -m elf_x86_64 -N -e _start -Ttext=0x0 -o build/tmp/hello_module.elf build/tmp/hello_module.o || true
    # Copy ELF module to iso_root so Limine will expose it as a module
    cp build/tmp/hello_module.elf build/iso_root/boot/hello_module.elf || true


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
