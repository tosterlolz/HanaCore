#pragma once
#include <stddef.h>

// Lightweight adapter layer for optional lwIP integration.
// If lwIP is vendored and linked into the kernel build, define HAVE_LWIP
// in the build and provide a real implementation of `http_fetch_via_lwip`.

extern "C" {
// Fetch a URL via lwIP and write the response body to `out_path` on FAT32.
// Returns 0 on success, negative on failure.
int http_fetch_via_lwip(const char* url, const char* out_path);
}
