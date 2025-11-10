#!/usr/bin/env bash
set -euo pipefail

# Build BusyBox for HanaCore
# This script downloads, configures, and builds BusyBox statically
# and installs its applets into rootfs_src/bin

ROOTDIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOTDIR/build/busybox_build"
ROOTFS_BIN="$ROOTDIR/rootfs_src/bin"
BUSYBOX_VERSION="${BUSYBOX_VERSION:-1.35.0}"

mkdir -p "$BUILD_DIR" "$ROOTFS_BIN"

echo "=== Building BusyBox $BUSYBOX_VERSION for HanaCore ==="

# Check if busybox source exists, if not download it
if [ ! -d "$BUILD_DIR/busybox-$BUSYBOX_VERSION" ]; then
    echo "Downloading BusyBox $BUSYBOX_VERSION..."
    cd "$BUILD_DIR"
    
    if ! command -v wget &> /dev/null && ! command -v curl &> /dev/null; then
        echo "Error: wget or curl required to download BusyBox" >&2
        exit 1
    fi
    
    if command -v wget &> /dev/null; then
        wget -q "https://busybox.net/downloads/busybox-$BUSYBOX_VERSION.tar.bz2"
    else
        curl -sLO "https://busybox.net/downloads/busybox-$BUSYBOX_VERSION.tar.bz2"
    fi
    
    tar xf "busybox-$BUSYBOX_VERSION.tar.bz2"
    cd "$ROOTDIR"
fi

cd "$BUILD_DIR/busybox-$BUSYBOX_VERSION"

echo "Configuring BusyBox..."

# Create a minimal .config for BusyBox
# This enables common applets useful for system administration
cat > .config << 'EOF'
CONFIG_STATIC=y
CONFIG_INSTALL_APPLET_SYMLINKS=y
CONFIG_INSTALL_APPLET_HARDLINKS=n

# Core utilities
CONFIG_CAT=y
CONFIG_ECHO=y
CONFIG_FALSE=y
CONFIG_TRUE=y
CONFIG_BASENAME=y
CONFIG_DIRNAME=y
CONFIG_LS=y
CONFIG_CP=y
CONFIG_MV=y
CONFIG_RM=y
CONFIG_MKDIR=y
CONFIG_RMDIR=y
CONFIG_PWD=y
CONFIG_TOUCH=y
CONFIG_WC=y
CONFIG_HEAD=y
CONFIG_TAIL=y
CONFIG_GREP=y
CONFIG_SED=y
CONFIG_SORT=y
CONFIG_UNIQ=y
CONFIG_CUT=y
CONFIG_TR=y
CONFIG_FIND=y
CONFIG_XARGS=y

# Shell utilities
CONFIG_SH=y
CONFIG_HUSH=y
CONFIG_BASH_IS_HUSH=n

# Text editors
CONFIG_VI=y

# File commands
CONFIG_FILE=y

# System utilities
CONFIG_WHOAMI=y
CONFIG_GROUPS=y
CONFIG_ID=y
CONFIG_UNAME=y
CONFIG_UPTIME=y
CONFIG_DATE=y
CONFIG_TIME=y
CONFIG_KILL=y
CONFIG_KILLALL=y
CONFIG_PS=y
CONFIG_TOP=n

# Hardware/system
CONFIG_DMESG=y
CONFIG_LSMOD=y
CONFIG_INSMOD=y
CONFIG_RMMOD=y

# Compression
CONFIG_GZIP=y
CONFIG_GUNZIP=y
CONFIG_TAR=y

# Network (disabled for now - kernel doesn't support networking)
CONFIG_NETSTAT=n
CONFIG_ROUTE=n
CONFIG_PING=n
CONFIG_IFCONFIG=n

# Don't include modules we don't need
CONFIG_FEATURE_EXTRA_QUIET=y
CONFIG_FEATURE_VERBOSE_USAGE=n

# Minimal debugging
CONFIG_DEBUG=n
CONFIG_DEBUG_PESSIMIZE=n
EOF

echo "Building BusyBox..."
make CONFIG_PREFIX="$ROOTFS_BIN" -j$(nproc) 2>&1 | tail -20

echo "Installing BusyBox..."
make CONFIG_PREFIX="$ROOTFS_BIN" install 2>&1 | tail -10

# The main busybox binary should be at $ROOTFS_BIN/bin/busybox
if [ -f "$ROOTFS_BIN/bin/busybox" ]; then
    echo "Successfully built busybox at $ROOTFS_BIN/bin/busybox"
    # Create symlink in rootfs root
    ln -sf bin/busybox "$ROOTFS_BIN/busybox" 2>/dev/null || true
    echo ""
    echo "=== BusyBox applets installed ==="
    ls -la "$ROOTFS_BIN/bin/" | head -20
    echo "..."
    echo ""
    echo "BusyBox build complete!"
else
    echo "Error: BusyBox build failed - busybox binary not found" >&2
    exit 1
fi

cd "$ROOTDIR"
