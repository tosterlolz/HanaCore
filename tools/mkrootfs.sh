#!/usr/bin/env bash
set -euo pipefail

# args: <build_dir> <source_dir>
BUILD_DIR="$1"
SRC_DIR="$2"
IMG="$BUILD_DIR/rootfs.img"
SIZE_MB=32
TMPDIR="$(mktemp -d)"

echo "mkrootfs: build dir=${BUILD_DIR}, src=${SRC_DIR}, img=${IMG}"

# create blank image
if command -v fallocate >/dev/null 2>&1; then
    echo "mkrootfs: allocating ${SIZE_MB}MiB image with fallocate"
    fallocate -l ${SIZE_MB}M "$IMG"
else
    echo "mkrootfs: fallocate not found, using dd"
    dd if=/dev/zero of="$IMG" bs=1M count=${SIZE_MB} status=none
fi

# format as FAT32
if command -v mkfs.vfat >/dev/null 2>&1; then
    echo "mkrootfs: formatting image as FAT32 (mkfs.vfat)"
    mkfs.vfat -F 32 "$IMG"
elif command -v mkdosfs >/dev/null 2>&1; then
    echo "mkrootfs: formatting image as FAT32 (mkdosfs)"
    mkdosfs -F 32 "$IMG"
else
    echo "mkrootfs: mkfs.vfat/mkdosfs not found — image will not be a valid FAT filesystem"
    echo "mkrootfs: please install dosfstools or provide a prebuilt rootfs.img"
    exit 0
fi

# Populate image: prefer mtools (no root required)
if command -v mcopy >/dev/null 2>&1 && command -v mmd >/dev/null 2>&1; then
    echo "mkrootfs: using mtools (mcopy/mmd) to populate image"
    # create /boot inside image (do not pre-create /bin to avoid mtools prompting)
    mmd -i "$IMG" ::/boot || true
    MTOOLS_MCOPY_FLAGS="-n" # do not overwrite existing files (non-interactive)
    # if a rootfs_src dir exists, copy its contents; otherwise create README
        if [ -d "$SRC_DIR/rootfs_src" ]; then
        echo "mkrootfs: copying files from ${SRC_DIR}/rootfs_src into image"
        # copy all files and dirs under rootfs_src to image root
        (cd "$SRC_DIR/rootfs_src" && find . -type d -print0 | while IFS= read -r -d '' d; do
            # create directories in image
            [[ "$d" == "." ]] && continue
            mmd -i "$IMG" ::"/$d" || true
        done)
        (cd "$SRC_DIR/rootfs_src" && find . -type f -print0 | while IFS= read -r -d '' f; do
            # copy file preserving relative path
            parentdir=$(dirname "$f")
            if [ "$parentdir" != "." ]; then
                mmd -i "$IMG" ::"/$parentdir" || true
            fi
            # use -n to avoid interactive overwrite prompts
            mcopy -i "$IMG" $MTOOLS_MCOPY_FLAGS "$f" ::"/$f" || true
        done)
    else
        echo "mkrootfs: no rootfs_src found; creating README"
        echo "HanaCore rootfs image" > "$TMPDIR/README.txt"
        mcopy -i "$IMG" $MTOOLS_MCOPY_FLAGS "$TMPDIR/README.txt" ::/README.txt || true
    fi

else
    # fallback: try to mount image (requires sudo/loop support)
    echo "mkrootfs: mtools not found. Trying to mount image (requires sudo)."
    MNT="${TMPDIR}/mnt"
    mkdir -p "$MNT"
    if sudo mount -o loop "$IMG" "$MNT"; then
        echo "mkrootfs: mounted image to $MNT"
        # ensure /bin exists
        sudo mkdir -p "$MNT/bin"
        if [ -d "$SRC_DIR/rootfs_src" ]; then
            echo "mkrootfs: copying files from ${SRC_DIR}/rootfs_src"
            # copy files, avoid clobbering existing files
            sudo cp -a "$SRC_DIR/rootfs_src/." "$MNT/" || true
        else
            echo "mkrootfs: no rootfs_src found; creating README"
            echo "HanaCore rootfs image" | sudo tee "$MNT/README.txt" >/dev/null
        fi
        sync
        sudo umount "$MNT"
    else
        echo "mkrootfs: failed to mount image. Install mtools or run mkrootfs manually."
        exit 0
    fi
fi

rm -rf "$TMPDIR"

echo "mkrootfs: created $IMG"
