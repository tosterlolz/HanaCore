#!/usr/bin/env bash
set -euo pipefail

# --- Configuration ---
BUILD_DIR="./build"
SRC_DIR="./rootfs_src"
TMPDIR="$BUILD_DIR/tmp"
ISO_ROOT="$BUILD_DIR/cmake/iso_root"
IMG="$BUILD_DIR/initrd.tar"
SIZE_MB=16
export SRC_DIR IMG TMPDIR ISO_ROOT

# Cleanup old state
rm -rf "$TMPDIR" "$IMG"

# Create directories
mkdir -p "$BUILD_DIR"
mkdir -p "$TMPDIR"
mkdir -p "$ISO_ROOT"

# Ensure rootfs_src exists with standard directories
echo "mkrootfs: preparing filesystem structure"
mkdir -p "$SRC_DIR/bin"
mkdir -p "$SRC_DIR/dev"
mkdir -p "$SRC_DIR/proc"
mkdir -p "$SRC_DIR/home"
mkdir -p "$SRC_DIR/etc"

echo "mkrootfs: creating initrd tarball from $SRC_DIR"
# Create a GNU tarball (initrd) containing the contents of rootfs_src
if [ ! -d "$SRC_DIR" ]; then
    echo "mkrootfs: ERROR - $SRC_DIR does not exist" >&2
    exit 1
fi

tar -C "$SRC_DIR" -cf "$IMG"  .

echo "mkrootfs: copying initrd to ISO root"
mkdir -p "$ISO_ROOT"
cp "$IMG" "$ISO_ROOT/initrd.tar"
echo "mkrootfs: copied initrd.tar to $ISO_ROOT/initrd.tar for ISO inclusion"

# Verify it was copied
if [ -f "$ISO_ROOT/initrd.tar" ]; then
    ls -lh "$ISO_ROOT/initrd.tar"
else
    echo "ERROR: initrd.tar not found in ISO root!" >&2
    exit 1
fi

rm -rf "$TMPDIR"
echo "mkrootfs: done"a
