#!/usr/bin/env bash
set -euo pipefail

ROOTDIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="$ROOTDIR/build/userbin"

mkdir -p "$OUT_DIR"

echo "Building hello_hana user program..."

CC=${CC:-gcc}

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "Warning: compiler '$CC' not found in PATH. Set CC to a suitable cross-compiler or install it." >&2
fi

$CC -ffreestanding -nostdlib -nostartfiles -static \
        -o "$OUT_DIR/hello_hana.elf" \
        userland/crt0.S userland/libhana.c test_programs/hello_hana.c


echo "Building hanabox user program..."
$CC -ffreestanding -nostdlib -nostartfiles -static \
        -o "$OUT_DIR/hanabox.elf" \
        userland/crt0.S userland/libhana.c userland/hanabox.c

echo "Building hcsh program..."
$CC -ffreestanding -nostdlib -nostartfiles -static \
    -o "$OUT_DIR/hcsh_user.elf" \
    userland/crt0.S userland/libhana.c userland/shell/hcsh.c
# Ensure rootfs_src/bin exists and copy shell binary
ROOTFS_BIN="$ROOTDIR/rootfs_src/bin"
mkdir -p "$ROOTFS_BIN"
cp "$OUT_DIR/hcsh_user.elf" "$ROOTFS_BIN/HCSH"
chmod +x "$ROOTFS_BIN/HCSH"