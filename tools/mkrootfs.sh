#!/usr/bin/env bash
set -euo pipefail

# --- konfiguracja ---
BUILD_DIR="./build"
SRC_DIR="./rootfs_src"
TMPDIR="$BUILD_DIR/tmp"
ISO_ROOT="$BUILD_DIR/iso_root"
IMG="$BUILD_DIR/rootfs.img"
SIZE_MB=16

mkdir -p "$BUILD_DIR"
mkdir -p "$TMPDIR/mnt"
mkdir -p "$ISO_ROOT"

echo "mkrootfs: creating blank rootfs.img ($SIZE_MB MB)"
dd if=/dev/zero of="$IMG" bs=1M count=$SIZE_MB status=progress

echo "mkrootfs: formatting as FAT32 filesystem"
# Create FAT32 filesystem with 4 sectors per cluster
mkfs.fat -F 32 -s 4 "$IMG"

echo "mkrootfs: mounting image"
sudo mount -o loop "$IMG" "$TMPDIR/mnt"

echo "mkrootfs: copying files from $SRC_DIR to image"
sudo cp -a "$SRC_DIR/." "$TMPDIR/mnt/"

sync
echo "mkrootfs: unmounting image"
sudo umount "$TMPDIR/mnt"

cp "$IMG" "$ISO_ROOT/rootfs.img"
echo "mkrootfs: copied rootfs.img to $ISO_ROOT/rootfs.img for ISO inclusion"
sudo umount $TMPDIR 2>/dev/null; echo "Unmounted successfully"
# Ensure bin/ exists in ISO root and copy binaries
mkdir -p "$ISO_ROOT/bin"
cp -a "$SRC_DIR/bin/." "$ISO_ROOT/bin/"

rm -rf "$TMPDIR"
