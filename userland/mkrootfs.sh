#!/bin/sh
# Simple helper to create an ext2 image from a directory.
# Usage: mkrootfs.sh <srcdir> <outimg>
set -e
SRC="$1"
OUT="$2"
if [ -z "$SRC" ] || [ -z "$OUT" ]; then
  echo "Usage: $0 <srcdir> <outimg>" >&2
  exit 2
fi
if command -v genext2fs >/dev/null 2>&1; then
  genext2fs -d "$SRC" -b 4096 "$OUT"
  exit $?
elif command -v mke2fs >/dev/null 2>&1; then
  # create a 16MiB image then populate it
  dd if=/dev/zero of="$OUT" bs=1M count=16
  mke2fs -q -t ext2 -d "$SRC" "$OUT"
  exit $?
else
  echo "Warning: no genext2fs or mke2fs found; rootfs.img will not be created" >&2
  # return success so build continues
  exit 0
fi
