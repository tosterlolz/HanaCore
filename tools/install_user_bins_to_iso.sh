#!/usr/bin/env bash
set -euo pipefail

ROOTFS_SRC="$(pwd)/rootfs_src"
ISO_ROOT="$(pwd)/build/cmake/iso_root"

mkdir -p "$ISO_ROOT/bin"

if [ -d "$ROOTFS_SRC/bin" ]; then
  echo "Copying user programs into ISO root..."
  cp -a "$ROOTFS_SRC/bin/"* "$ISO_ROOT/bin/"
else
  echo "No user programs found in $ROOTFS_SRC/bin"
  exit 1
fi

# If hanabox exists, create a busybox symlink to it
if [ -f "$ISO_ROOT/bin/hanabox" ]; then
  ln -sf hanabox "$ISO_ROOT/bin/busybox"
  echo "Created symlink busybox -> hanabox"
fi

echo "Installed user bins into $ISO_ROOT/bin"
