
#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="./build"
SRC_DIR="./rootfs_src"
IMG="$BUILD_DIR/rootfs.img"
SIZE_MB=8
TMPDIR="$(mktemp -d)"

cleanup() {
    if mountpoint -q "$TMPDIR/mnt" 2>/dev/null; then
        echo "mkrootfs: unmounting $TMPDIR/mnt"
        sudo umount "$TMPDIR/mnt" || true
    fi
    rm -rf "$TMPDIR"
}
trap cleanup EXIT

echo "mkrootfs: build dir=${BUILD_DIR}, src=${SRC_DIR}, img=${IMG}"
mkdir -p "$BUILD_DIR"

# -------- Allocate blank image --------
if command -v fallocate >/dev/null 2>&1; then
    echo "mkrootfs: allocating ${SIZE_MB}MiB image with fallocate"
    fallocate -l "${SIZE_MB}M" "$IMG"
    sync
else
    echo "mkrootfs: fallocate not found, using dd"
    dd if=/dev/zero of="$IMG" bs=1M count=${SIZE_MB} status=none
fi

# -------- Format as FAT32 only --------
if command -v mkfs.fat >/dev/null 2>&1; then
    echo "mkrootfs: formatting image as FAT32 (mkfs.fat)"
    mkfs.fat -F 32 -n ROOTFS "$IMG" >/dev/null
    sync
    # Ensure image file exists and is accessible before mtools
    if [ ! -f "$IMG" ]; then
        echo "mkrootfs: ERROR: image file $IMG does not exist after formatting"
        exit 1
    fi
else
    echo "mkrootfs: mkfs.fat not found — please install dosfstools"
    exit 1
fi

# -------- Copy files into FAT32 image --------
if command -v mcopy >/dev/null 2>&1 && command -v mmd >/dev/null 2>&1; then
    echo "mkrootfs: using mtools (mcopy/mmd)"
    export MTOOLS_SKIP_CHECK=1
    export MTOOLS_NO_VFAT=1
    echo "mkrootfs: copying files from ${SRC_DIR}/ recursively"
    if ! mcopy -i "$IMG" -s "$SRC_DIR/*" ::/; then
        echo "mkrootfs: mtools failed, falling back to sudo loop-mount for FAT32 image"
        MNT="${TMPDIR}/mnt"
        mkdir -p "$MNT"
        if sudo mount -o loop "$IMG" "$MNT" 2>/dev/null; then
            echo "mkrootfs: mounted image at $MNT"
            sudo mkdir -p "$MNT"
            echo "mkrootfs: copying files from ${SRC_DIR}/"
            if ! sudo cp -a "$SRC_DIR/." "$MNT/" 2>/dev/null; then
                echo "mkrootfs: cp -a failed (owner preserve) — retrying without ownership preservation"
                sudo cp -a -r --no-preserve=ownership "$SRC_DIR/." "$MNT/" || true
            fi
                echo "mkrootfs: contents of $MNT after copy:"
                sudo ls -lR "$MNT"
                sync
                sudo umount "$MNT"
                sync
        else
            echo "mkrootfs: failed to mount FAT32 image"
            exit 1
        fi
    fi
else
    echo "mkrootfs: mtools not available; falling back to sudo loop-mount for FAT32 image"
    MNT="${TMPDIR}/mnt"
    mkdir -p "$MNT"
    if sudo mount -o loop "$IMG" "$MNT" 2>/dev/null; then
        echo "mkrootfs: mounted image at $MNT"
        sudo mkdir -p "$MNT"
        echo "mkrootfs: copying files from ${SRC_DIR}/"
        if ! sudo cp -a "$SRC_DIR/." "$MNT/" 2>/dev/null; then
            ls $MNT
            ls $MNT/bin
            echo "mkrootfs: cp -a failed (owner preserve) — retrying without ownership preservation"
            sudo cp -a -r --no-preserve=ownership "$SRC_DIR/." "$MNT/" || true
        fi
            echo "mkrootfs: contents of $MNT after copy:"
            sudo ls -lR "$MNT"
            sync
            sudo umount "$MNT"
    else
        echo "mkrootfs: failed to mount FAT32 image"
        exit 1
    fi
fi

echo "mkrootfs: created ${IMG} successfully (${SIZE_MB}MiB FAT32)"

# Copy rootfs.img into ISO directory for Limine
iso_root="./build/cmake/iso_root/"
mkdir -p "$iso_root"
cp "$IMG" "$iso_root/rootfs.img"
echo "mkrootfs: copied rootfs.img to $iso_root/rootfs.img for ISO inclusion"

# Verify that /bin/hcsh exists in the image using mdir
if command -v mdir >/dev/null 2>&1; then
    if mdir -i "$IMG" ::/bin | grep -q hcsh; then
        echo "mkrootfs: verified /bin/hcsh exists in rootfs.img"
    else
        echo "mkrootfs: WARNING: /bin/hcsh NOT found in rootfs.img!"
    fi
else
    echo "mkrootfs: mdir not available, cannot verify /bin/hcsh in image"
fi
echo "mkrootfs: copied rootfs.img to $iso_root/rootfs.img for ISO inclusion"