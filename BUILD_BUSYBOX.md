Building BusyBox for HanaCore
=================================

This repository includes a helper script to build BusyBox and install
its applets into `rootfs_src/bin` so the project's `mkrootfs.sh` will
pick them up into the bootable rootfs image.

Basic usage
-----------

From the repository root:

```fish
./tools/build_busybox.sh
./tools/mkrootfs.sh build rootfs_src
```

- The script builds BusyBox statically (CONFIG_STATIC=y) by default.
  You may need a static-capable toolchain on your host. Adjust
  `CONFIG_EXTRA_CFLAGS` or the build process in `tools/build_busybox.sh`
  if you need cross-compilation or musl-based linking.

