#!/usr/bin/env bash
set -euo pipefail

# Simple helper script to build the example userland program(s)
# Requires x86_64-elf-gcc and xorriso in PATH.

OUT_DIR="build/userbin"
ISO_OUT="build/userprogram.iso"
ROOTFS_SRC="rootfs_src"

mkdir -p "$OUT_DIR"
mkdir -p "$ROOTFS_SRC/bin"

echo "Building hello_hana user program..."

CC=${CC:-x86_64-elf-gcc}

${CC} -ffreestanding -nostdlib -nostartfiles -static \
    -o "$OUT_DIR/hello_hana.elf" \
    userland/crt0.S userland/libhana.c test_programs/hello_hana.c

echo "Copying into $ROOTFS_SRC/bin/..."
cp "$OUT_DIR/hello_hana.elf" "$ROOTFS_SRC/bin/hello_hana"
echo "Done: $ROOTFS_SRC/bin/hello_hana"

echo "Building hanabox user program..."
${CC} -ffreestanding -nostdlib -nostartfiles -static \
    -o "$OUT_DIR/hanabox.elf" \
    userland/crt0.S userland/libhana.c userland/hanabox.c
cp "$OUT_DIR/hanabox.elf" "$ROOTFS_SRC/bin/hanabox"
echo "Done: $ROOTFS_SRC/bin/hanabox"

# --- Simple ISO creation ---
echo "Creating ISO image with xorriso..."
xorriso -as mkisofs \
    -iso-level 3 \
    -volid "HanaCore" \
    -output "$ISO_OUT" \
    "$ROOTFS_SRC"

echo "ISO created at $ISO_OUT"
