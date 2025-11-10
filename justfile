set shell := ["bash", "-cu"]

# Use cross-toolchain for freestanding higher-half kernel
prefix := "x86_64-elf"
cc := prefix + "-gcc"

@default:
	just build

build-kernel:
	# Default to using the native toolchain (gcc/g++) unless CROSS_COMPILE is
	# explicitly provided. Setting CROSS_COMPILE to empty lets CMake pick the
	# host compilers.
	if [ -z "$$CROSS_COMPILE" ]; then export CROSS_COMPILE=; fi; \
	cmake -S . -B build/cmake || true; \
	cmake --build build/cmake --target kernel -- -j$(nproc)

# Build optional userland and install files and produce an ISO
build: build-kernel
	if [ -z "$$CROSS_COMPILE" ]; then export CROSS_COMPILE=; fi; \
	cmake -S . -B build/cmake || true; \
	# Build kernel already done by dependency; optionally build userland modules
	cmake --build build/cmake --target hello_module_install || true; \
	# Collect kernel and limine files into iso_root
	cmake --build build/cmake --target install_iso || true; \
	# Build user programs and populate rootfs_src so they are included in the ISO
	./tools/build_user_programs.sh || true; \
	# Build initrd.tar automatically and copy to ISO root
	./tools/mkrootfs.sh || true; \
	# Ensure boot dir exists
	mkdir -p build/cmake/iso_root/boot; \
	# Create ISO from CMake staging area (xorriso/genisoimage/mkisofs optional)
	if command -v xorriso >/dev/null 2>&1; then \
		xorriso -as mkisofs -b boot/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table build/cmake/iso_root -o build/HanaCore.iso 2>/dev/null || true; \
	elif command -v genisoimage >/dev/null 2>&1; then \
		genisoimage -b boot/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table -o build/HanaCore.iso build/cmake/iso_root || true; \
	elif command -v mkisofs >/dev/null 2>&1; then \
		mkisofs -b boot/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table -o build/HanaCore.iso build/cmake/iso_root || true; \
	else \
		echo "No ISO tool (xorriso/genisoimage/mkisofs) found; cannot create ISO"; \
	fi; \
	if [ -f build/HanaCore.iso ]; then \
		echo "🌸 Built HanaCore ISO at build/HanaCore.iso"; \
	else \
		echo "⚠️  ISO not created. Please install one of: xorriso, genisoimage, or mkisofs"; \
		exit 1; \
	fi

# Build only user programs and package them into an ISO (fast path)
build-userprograms:
	@echo "Building user programs and creating userprogram.iso..."
	./tools/build_user_programs.sh

install-userprograms: build-userprograms
	@echo "🌸 Built userprogram.iso at build/userprogram.iso"

run: build
	@echo "🌸 Starting HanaCore in QEMU..."
	qemu-system-x86_64 -cdrom build/HanaCore.iso

run-cli: build
	@echo "🌸 Starting HanaCore in QEMU (serial/stdio)..."
	qemu-system-x86_64 -cdrom build/HanaCore.iso -serial stdio -no-shutdown -no-reboot

debug: build
	@echo "🐛 Starting HanaCore in QEMU (debug mode). Logs: /tmp/serial.log /tmp/debug.log"
	qemu-system-x86_64 -cdrom build/HanaCore.iso -m 512 \
		-serial file:/tmp/serial.log \
		-debugcon file:/tmp/debug.log \
		-no-reboot -no-shutdown -s -S -display gtk &
	sleep 0.5
	gdb -q build/cmake/kernel.elf \
		-ex "target remote :1234" \
		-ex "set confirm off" \
		-ex "break kernel_main" \
		-ex "break clear_screen" \
		-ex "break framebuffer_init" \
		-ex "break flanterm_fb_init" \
		-ex "continue"
	pkill qemu || true

clean:
	rm -rf build build/cmake
	rm -rf rootfs_src
	@echo "🧹 Cleaned build files."