# -------- Ensure shell is present --------
if [ ! -x "$SRC_DIR/rootfs_src/bin/hcsh" ]; then
    echo "[WARNING] rootfs_src/bin/hcsh is missing or not executable! Shell will not boot automatically."
else
    echo "[INFO] Found shell: rootfs_src/bin/hcsh"
fi
#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="./build"
SRC_DIR="./rootfs_src"
IMG="$BUILD_DIR/rootfs.img"
SIZE_MB=4
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
else
    echo "mkrootfs: fallocate not found, using dd"
    dd if=/dev/zero of="$IMG" bs=1M count=${SIZE_MB} status=none
fi

# -------- Format as FAT32 --------
if command -v mkfs.vfat >/dev/null 2>&1; then
    echo "mkrootfs: formatting image as FAT32 (mkfs.vfat)"
    mkfs.vfat -F 32 "$IMG" >/dev/null
elif command -v mkdosfs >/dev/null 2>&1; then
    echo "mkrootfs: formatting image as FAT32 (mkdosfs)"
    mkdosfs -F 32 "$IMG" >/dev/null
else
    echo "mkrootfs: mkfs.vfat/mkdosfs not found — please install dosfstools"
    exit 1
fi

# -------- Copy files into image --------
USE_MTOOLS_FALLBACK=0
if command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
    echo "mkrootfs: using sudo loop-mount to populate image (non-interactive)"
    MNT="${TMPDIR}/mnt"
    mkdir -p "$MNT"
    if sudo mount -o loop "$IMG" "$MNT" 2>/dev/null; then
        echo "mkrootfs: mounted image at $MNT"
        sudo mkdir -p "$MNT"
        if [ -d "$SRC_DIR/bin" ]; then
            echo "mkrootfs: copying files from ${SRC_DIR}/"
            # Try to copy preserving attributes; if preserving ownership fails
            # (non-root filesystem), fall back to copying without preserving owner.
            if ! sudo cp -a "$SRC_DIR/." "$MNT/" 2>/dev/null; then
                echo "mkrootfs: cp -a failed (owner preserve) — retrying without ownership preservation"
                sudo cp -a --no-preserve=ownership "$SRC_DIR/." "$MNT/" || true
            fi
        else
            echo "mkrootfs: no bin/ found in rootfs_src; creating README"
            echo "HanaCore rootfs image" | sudo tee "$MNT/README.txt" >/dev/null
        fi
        sync
        sudo umount "$MNT"
    else
        echo "mkrootfs: failed to mount with sudo; switching to mtools fallback"
        USE_MTOOLS_FALLBACK=1
    fi
else
    echo "mkrootfs: no sudo access, using mtools fallback"
    USE_MTOOLS_FALLBACK=1
fi

# -------- MTOOLS fallback (no root needed) --------
if [ "$USE_MTOOLS_FALLBACK" -eq 1 ]; then
    if command -v mcopy >/dev/null 2>&1 && command -v mmd >/dev/null 2>&1; then
        echo "mkrootfs: using mtools (mcopy/mmd)"
        export MTOOLS_SKIP_CHECK=1
        mmd -i "$IMG" ::/ || true
        if [ -d "$SRC_DIR/bin" ]; then
            echo "mkrootfs: copying files from ${SRC_DIR}/"
            (cd "$SRC_DIR" && \
                find . -type d -print0 | while IFS= read -r -d '' d; do
                    [[ "$d" == "." ]] && continue
                    mmd -i "$IMG" ::"/$d" || true
                done)
            (cd "$SRC_DIR" && \
                find . -type f -print0 | while IFS= read -r -d '' f; do
                    parentdir=$(dirname "$f")
                    if [ "$parentdir" != "." ]; then
                        mmd -i "$IMG" ::"/$parentdir" || true
                    fi
                    mcopy -n -D A -i "$IMG" "$f" ::"/$f" || true
                done)
        else
            echo "mkrootfs: no bin/ found in rootfs_src; creating README"
            echo "HanaCore rootfs image" > "$TMPDIR/README.txt"
            mcopy -i "$IMG" "$TMPDIR/README.txt" ::/README.txt
        fi
    else
        echo "mkrootfs: mtools not available; cannot populate image"
        exit 1
    fi
fi

echo "mkrootfs: created ${IMG} successfully (${SIZE_MB}MiB FAT32)"
