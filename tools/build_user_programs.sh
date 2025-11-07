#!/usr/bin/env bash
set -euo pipefail

# Simple helper script to build the example userland program(s)
# Requires x86_64-elf-gcc toolchain in PATH.

OUT_DIR="build/userbin"
mkdir -p "$OUT_DIR"
mkdir -p rootfs_src/bin

echo "Building hello_hana user program..."

CC=${CC:-x86_64-elf-gcc}

${CC} -ffreestanding -nostdlib -nostartfiles -static \
    -o "$OUT_DIR/hello_hana.elf" \
    userland/crt0.S userland/libhana.c test_programs/hello_hana.c

echo "Copying into rootfs_src/bin/..."
cp "$OUT_DIR/hello_hana.elf" rootfs_src/bin/hello_hana
echo "Done: rootfs_src/bin/hello_hana"
