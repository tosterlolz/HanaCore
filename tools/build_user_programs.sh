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
