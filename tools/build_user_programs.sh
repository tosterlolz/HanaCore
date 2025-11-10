
#!/usr/bin/env bash
set -euo pipefail

ROOTDIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT_DIR="$ROOTDIR/build/userbin"
rootfs_BIN="$ROOTDIR/rootfs_src/bin"

CC=${CC:-gcc}
mkdir -p "$OUT_DIR" "$rootfs_BIN"

declare -A programs=(
    [hls]="userland/apps/ls.c"
    [sh]="userland/apps/sh.c"
    # keep these commented as historical entries - builds will be skipped if missing
    # [hello_hana]="test_programs/hello_hana.c"
    # [hanabox]="userland/hanabox.c"
)

for name in "${!programs[@]}"; do
    src="${programs[$name]}"
    if [ ! -f "$src" ]; then
        echo "Skipping $name: source $src not found"
        continue
    fi
    echo "Building $name..."
    $CC -ffreestanding -nostdlib -nostartfiles -static \
        -o "$OUT_DIR/$name.elf" \
        userland/crt0.S userland/libhana.c "$src"
    if [ ! -f "$OUT_DIR/$name.elf" ]; then
        echo "Error: failed to build $name" >&2
        exit 1
    fi
    cp "$OUT_DIR/$name.elf" "$rootfs_BIN/$name"
    chmod +x "$rootfs_BIN/$name"
done

echo "User programs copied to $rootfs_BIN:"
ls -l "$rootfs_BIN"