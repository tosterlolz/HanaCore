# BusyBox integration

This repository includes a helper script to download, build, and install BusyBox
as a static binary into the ISO root so the kernel can ship BusyBox utilities
in `/bin`.

Usage:

1. Install build dependencies on your host (gcc, make, wget, bzip2).
2. From the repository root run:

```bash
./tools/build_busybox.sh
just build
```

This will place `/bin/busybox` and its applet symlinks into `build/cmake/iso_root/bin`.

Notes & caveats:
- BusyBox is built using the host toolchain. Depending on libc differences,
  BusyBox may or may not run under this kernel's userland. If you get crashes,
  consider building BusyBox against the kernel's userland C library (libhana)
  or adjusting the syscall wrappers.
- The script uses BusyBox upstream tarball; if you want a different version,
  set `BB_VERSION` in the script.
