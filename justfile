set shell := ["bash", "-cu"]

# Use cross-toolchain for freestanding higher-half kernel
prefix := "x86_64-elf"
cc := prefix + "-gcc"

@default:
	just build

# Configure and build the kernel using CMake. This will generate
# build/cmake and build the 'kernel' target which outputs build/cmake/kernel.elf
build-kernel:
	if [ -z "$$CROSS_COMPILE" ]; then export CROSS_COMPILE={{prefix}}-; fi; \
	cmake -S . -B build/cmake || true; \
	cmake --build build/cmake --target kernel -- -j$(nproc)

# Build optional userland and install files and produce an ISO
build-image: build-kernel
	# Configure and build via CMake; be tolerant of optional host tools
	if [ -z "$$CROSS_COMPILE" ]; then export CROSS_COMPILE={{prefix}}-; fi; \
	cmake -S . -B build/cmake || true; \
	# Build kernel already done by dependency; optionally build rootfs and userland modules
	cmake --build build/cmake --target mkrootfs || true; \
	cmake --build build/cmake --target hello_module_install || true; \
	# Collect kernel and limine files into iso_root
	cmake --build build/cmake --target install_iso || true; \
	# Ensure boot dir exists and include generated rootfs if present
	mkdir -p build/cmake/iso_root/boot; \
	if [ -f build/cmake/rootfs.img ]; then cp build/cmake/rootfs.img build/cmake/iso_root/boot/; fi; \
	# Create ISO from CMake staging area (xorriso optional)
	if command -v xorriso >/dev/null 2>&1; then \
		xorriso -as mkisofs -b boot/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table build/cmake/iso_root -o build/HanaCore.iso 2>/dev/null || true; \
	elif command -v genisoimage >/dev/null 2>&1; then \
		genisoimage -b boot/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table -o build/HanaCore.iso build/cmake/iso_root || true; \
	elif command -v mkisofs >/dev/null 2>&1; then \
		mkisofs -b boot/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table -o build/HanaCore.iso build/cmake/iso_root || true; \
	else \
		echo "No ISO tool (xorriso/genisoimage/mkisofs) found; skipping ISO creation"; \
	fi; \
	# Install Limine to ISO (best-effort)
	if [ -f build/HanaCore.iso ]; then limine-bootloader/limine bios-install build/HanaCore.iso 2>/dev/null || echo "Failed to install Limine (might need xorriso)"; fi

build: build-image
	if [ -f build/HanaCore.iso ]; then echo "✅ HanaCore ISO built -> build/HanaCore.iso"; else echo "⚠️ ISO not created (missing tool or error). Check build/cmake/iso_root"; fi

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
	@echo "🧹 Cleaned build files."