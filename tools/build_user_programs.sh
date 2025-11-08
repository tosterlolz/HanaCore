#!/usr/bin/env bash
set -euo pipefail

ROOTDIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="$ROOTDIR/build/userbin"
ISO_OUT="$ROOTDIR/build/userprogram.iso"
ROOTFS_SRC="$ROOTDIR/rootfs_src"

mkdir -p "$OUT_DIR"
mkdir -p "$ROOTFS_SRC/bin"

echo "Building hello_hana user program..."

CC=${CC:-x86_64-elf-gcc}

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "Warning: compiler '$CC' not found in PATH. Set CC to a suitable cross-compiler or install it." >&2
fi

$CC -ffreestanding -nostdlib -nostartfiles -static \
        -o "$OUT_DIR/hello_hana.elf" \
        userland/crt0.S userland/libhana.c test_programs/hello_hana.c

echo "Copying into $ROOTFS_SRC/bin/..."
cp -f "$OUT_DIR/hello_hana.elf" "$ROOTFS_SRC/bin/hello_hana"
echo "Done: $ROOTFS_SRC/bin/hello_hana"

echo "Building hanabox user program..."
$CC -ffreestanding -nostdlib -nostartfiles -static \
        -o "$OUT_DIR/hanabox.elf" \
        userland/crt0.S userland/libhana.c userland/hanabox.c
cp -f "$OUT_DIR/hanabox.elf" "$ROOTFS_SRC/bin/hanabox"
echo "Done: $ROOTFS_SRC/bin/hanabox"

echo "Building hcsh user program..."
$CC -ffreestanding -nostdlib -nostartfiles -static \
    -o "$OUT_DIR/hcsh.elf" \
    userland/crt0.S userland/libhana.c userland/hcsh.c
cp -f "$OUT_DIR/hcsh.elf" "$ROOTFS_SRC/bin/hcsh"
echo "Done: $ROOTFS_SRC/bin/hcsh"

# Locate an ISO creation tool
ISO_TOOL=""
if command -v xorriso >/dev/null 2>&1; then ISO_TOOL="xorriso"; fi
if [ -z "$ISO_TOOL" ] && command -v genisoimage >/dev/null 2>&1; then ISO_TOOL="genisoimage"; fi
if [ -z "$ISO_TOOL" ] && command -v mkisofs >/dev/null 2>&1; then ISO_TOOL="mkisofs"; fi

if [ -z "$ISO_TOOL" ]; then
    echo "No ISO tool found (xorriso/genisoimage/mkisofs). Install one to create ISOs." >&2
    exit 1
fi

echo "Creating ISO image with $ISO_TOOL..."
case "$ISO_TOOL" in
    xorriso)
        xorriso -as mkisofs -iso-level 3 -volid "HanaCore" -output "$ISO_OUT" "$ROOTFS_SRC"
        ;;
    genisoimage)
        genisoimage -iso-level 3 -volid "HanaCore" -o "$ISO_OUT" "$ROOTFS_SRC"
        ;;
    mkisofs)
        mkisofs -iso-level 3 -volid "HanaCore" -o "$ISO_OUT" "$ROOTFS_SRC"
        ;;
esac

chmod 644 "$ISO_OUT" || true
echo "ISO created at $ISO_OUT"

# Try to create a FAT32 rootfs image usable as a Limine module (optional).
# This requires host tools: mkfs.fat/mkfs.vfat and mtools (mcopy). We create
# a small image, format it FAT32, and copy the contents of rootfs_src into
# the image if possible. The resulting file is installed into the CMake
# iso staging area so the kernel can see a Limine module named `rootfs.img`.
ISO_ROOT_DIR="$ROOTDIR/build/cmake/iso_root"
mkdir -p "$ISO_ROOT_DIR"
ROOTFS_IMG="$ISO_ROOT_DIR/rootfs.img"

echo "Attempting to create FAT32 rootfs image at $ROOTFS_IMG"
IMG_SIZE_MB=16
MKFS_TOOL=""
if command -v mkfs.fat >/dev/null 2>&1; then MKFS_TOOL=mkfs.fat; fi
if [ -z "$MKFS_TOOL" ] && command -v mkfs.vfat >/dev/null 2>&1; then MKFS_TOOL=mkfs.vfat; fi
if [ -z "$MKFS_TOOL" ]; then
    echo "mkfs.fat/mkfs.vfat not found; skipping rootfs.img creation" >&2
else
    dd if=/dev/zero of="$ROOTFS_IMG" bs=1M count=$IMG_SIZE_MB status=none
    "$MKFS_TOOL" -F 32 "$ROOTFS_IMG" >/dev/null 2>&1 || {
        echo "mkfs failed; removing image" >&2; rm -f "$ROOTFS_IMG"; exit 0;
    }
    # If mtools is available, use mcopy to populate the image without root.
    if command -v mcopy >/dev/null 2>&1; then
        # copy tree recursively (-s), use -i to specify image
        # mcopy expects files; we copy each file under rootfs_src into the root of image
        (cd "$ROOTFS_SRC" && find . -type d -print | sed 's|^\./||' | while read -r d; do
            if [ -n "$d" ]; then mmd -i "$ROOTFS_IMG" "/$d" >/dev/null 2>&1 || true; fi
        done)
        (cd "$ROOTFS_SRC" && find . -type f -print | sed 's|^\./||' | while read -r f; do
            mcopy -i "$ROOTFS_IMG" "$f" ::"/$f" >/dev/null 2>&1 || true
        done)
        echo "Populated $ROOTFS_IMG with $ROOTFS_SRC using mtools"
    else
        echo "mcopy not found; created empty FAT32 image at $ROOTFS_IMG (not populated)" >&2
    fi
fi
