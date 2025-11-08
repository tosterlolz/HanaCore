#!/usr/bin/env bash
set -euo pipefail

# Builds BusyBox statically and installs into the ISO root's /bin directory.
# Assumptions:
# - You have development tools (gcc, make, wget, patch, musl or glibc) installed.
# - This script is run from the repository root.
# - The kernel's ISO root is at build/cmake/iso_root.

BB_VERSION=1.36.1
BB_TAR=busybox-${BB_VERSION}.tar.bz2
BB_URL=https://busybox.net/downloads/${BB_TAR}

ROOT_DIR=$(pwd)
BUILD_DIR=${ROOT_DIR}/build/busybox-build
ISO_ROOT=${ROOT_DIR}/build/cmake/iso_root

mkdir -p "$BUILD_DIR"
mkdir -p "$ISO_ROOT/bin"
cd "$BUILD_DIR"

if [ ! -f "$BB_TAR" ]; then
    echo "Downloading BusyBox ${BB_VERSION}..."
    wget -q --show-progress "$BB_URL"
fi

if [ ! -d "busybox-${BB_VERSION}" ]; then
    tar xjf "$BB_TAR"
fi
cd "busybox-${BB_VERSION}"

# Create a minimal .config for a static busybox
make defconfig >/dev/null
# Enable static build and install path
# The config file uses CONFIG_* names; use sed to enable CONFIG_STATIC
sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config || true
# Disable applets we might not need? keep defaults

# Build busybox
make -j"$(nproc)"
# Install busybox to the ISO root/bin
make CONFIG_PREFIX="${ISO_ROOT}" install

# Ensure busybox is executable
chmod +x "${ISO_ROOT}/bin/busybox"

cat > "${ISO_ROOT}/bin/README_busybox.txt" <<EOF
BusyBox ${BB_VERSION} installed by tools/build_busybox.sh
This provides many common Unix utilities as symlinks to /bin/busybox.
EOF

# Create symlinks for common applets (optional: rely on busybox --install)
cd "${ISO_ROOT}/bin"
./busybox --install -s .

echo "BusyBox installed into ${ISO_ROOT}/bin"
