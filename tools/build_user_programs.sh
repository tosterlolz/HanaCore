
#!/usr/bin/env bash
set -euo pipefail

ROOTDIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="$ROOTDIR/build/userbin"
ROOTFS_BIN="$ROOTDIR/rootfs_src/bin"

CC=${CC:-gcc}
mkdir -p "$OUT_DIR" "$ROOTFS_BIN"

declare -A programs=(
    [hello_hana]="test_programs/hello_hana.c"
    [hanabox]="userland/hanabox.c"
    [hcsh_user]="userland/shell/hcsh.c"
)

for name in "${!programs[@]}"; do
    src="${programs[$name]}"
    echo "Building $name..."
    $CC -ffreestanding -nostdlib -nostartfiles -static \
        -o "$OUT_DIR/$name.elf" \
        userland/crt0.S userland/libhana.c "$src"
    if [ ! -f "$OUT_DIR/$name.elf" ]; then
        echo "Error: failed to build $name" >&2
        exit 1
    fi
    cp "$OUT_DIR/$name.elf" "$ROOTFS_BIN/$name"
    chmod +x "$ROOTFS_BIN/$name"
done

# Canonical shell name for kernel
if [ -f "$ROOTFS_BIN/hcsh_user" ]; then
    cp "$ROOTFS_BIN/hcsh_user" "$ROOTFS_BIN/hcsh"
    chmod +x "$ROOTFS_BIN/hcsh"
    cp "$ROOTFS_BIN/hcsh" "$ROOTFS_BIN/HCSH"
    chmod +x "$ROOTFS_BIN/HCSH"
fi

echo "User programs copied to $ROOTFS_BIN:"
ls -l "$ROOTFS_BIN"