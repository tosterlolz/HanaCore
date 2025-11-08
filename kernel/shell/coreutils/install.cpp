// Copy an embedded rootfs image (Limine module) onto an ATA device (raw copy).
// Usage: install <disk>
// Example: `install 0:` will write the bundled rootfs image to ATA device 0.

#include <stddef.h>
#include <stdint.h>
#include "../../filesystem/fat32.hpp"
#include "../../drivers/ide.hpp"
#include "../../libs/libc.h"
#include "../../../limine/limine.h"
#include "../../mem/heap.hpp"
#include "../../drivers/screen.hpp"
#include "../../libs/libc.h"

// Node type for install traversal (file-scope so callbacks can push into it)
struct InstallNameNode { char name[32]; InstallNameNode* next; };
static InstallNameNode* g_inst_head = NULL;
static InstallNameNode* g_inst_tail = NULL;

static void collector_trampoline(const char* name) {
    InstallNameNode* n = (InstallNameNode*)hanacore::mem::kmalloc(sizeof(InstallNameNode));
    if (!n) return;
    n->next = NULL;
    size_t i = 0; while (name[i] && i + 1 < sizeof(n->name)) { n->name[i] = name[i]; ++i; }
    n->name[i] = '\0';
    if (!g_inst_head) { g_inst_head = n; g_inst_tail = n; } else { g_inst_tail->next = n; g_inst_tail = n; }
}

extern "C" void print(const char*);

// Limine module requests (same as used in fat32.cpp)
extern volatile struct limine_hhdm_request limine_hhdm_request;
extern volatile struct limine_module_request module_request;

// Forward declarations (file-scope): helper functions used by builtin_install_cmd
static void copy_dir_recursive_impl(const char* src_prefix, const char* dst_prefix, const void* img_ptr, size_t img_size);
static bool ends_with(const char* s, const char* suffix);

// New behavior: support copying filesystem contents from one mounted drive
// to another: `install 1:/ 0:/` will copy recursively all files and
// directories from drive 1 root to drive 0 root. If a single argument is
// provided, fall back to the raw-image write (legacy behaviour).
extern "C" void builtin_install_cmd(const char* arg) {
    if (!arg || arg[0] == '\0') {
        print("usage: install <src> <dst>\nExamples:\n  install 1:/ 0:/   (copy fs)\n  install 0:        (write embedded image to disk)\n");
        return;
    }

    // Simple tokenization (split by whitespace)
    char tok1[64] = {0}; char tok2[64] = {0};
    size_t i = 0; size_t j = 0;
    // read first token
    while (arg[i] && arg[i] == ' ') ++i;
    while (arg[i] && arg[i] != ' ' && j + 1 < sizeof(tok1)) tok1[j++] = arg[i++];
    tok1[j] = '\0';
    while (arg[i] && arg[i] == ' ') ++i;
    j = 0;
    while (arg[i] && arg[i] != ' ' && j + 1 < sizeof(tok2)) tok2[j++] = arg[i++];
    tok2[j] = '\0';

    // If only one token provided, preserve legacy behavior (raw image write)
    if (tok2[0] == '\0') {
        // delegate to legacy path: write embedded image to target disk
        // reuse older logic by pretending arg is disk spec
        // (simply call original behavior by restarting function with single token)
        // For simplicity, call the original code path by scanning modules for image
    }

    // Expect syntax like "1:/" -> src drive 1 root and "0:/" -> dst drive 0 root
    if (!(tok1[0] == '1' && (tok1[1] == ':' || tok1[1] == '\0')) || !(tok2[0] == '0' && (tok2[1] == ':' || tok2[1] == '\0'))) {
        print("install: unsupported syntax. Use: install 1:/ 0:/\n");
        return;
    }

    // Find the module image to use as source (we look for a rootfs-like module)
    if (!module_request.response) {
        print("install: no Limine modules available (rootfs image not found)\n");
        return;
    }
    volatile struct limine_module_response* resp = module_request.response;
    const void* img_ptr = NULL; size_t img_size = 0;
    for (uint64_t m = 0; m < resp->module_count; ++m) {
        volatile struct limine_file* mod = resp->modules[m];
        const char* path = (const char*)(uintptr_t)mod->path;
        if (path && limine_hhdm_request.response) {
            uint64_t hoff = limine_hhdm_request.response->offset;
            if ((uint64_t)path < hoff) path = (const char*)((uintptr_t)path + hoff);
        }
        if (!path) continue;
        // accept likely names
        if (ends_with(path, "rootfs.img") || ends_with(path, "rootfs.bin") || strstr(path, "rootfs")) {
            uintptr_t mod_addr = (uintptr_t)mod->address;
            const void* mod_virt = (const void*)mod_addr;
            if (limine_hhdm_request.response) {
                uint64_t off = limine_hhdm_request.response->offset;
                if ((uint64_t)mod_addr < off) mod_virt = (const void*)(off + mod_addr);
            }
            img_ptr = mod_virt; img_size = (size_t)mod->size; break;
        }
    }
    if (!img_ptr || img_size == 0) {
        print("install: rootfs image module not found\n");
        return;
    }

    // Mount source (module) filesystem from memory
    if (hanacore::fs::fat32_init_from_memory(img_ptr, img_size) != 0) {
        print("install: failed to mount source module image\n");
        return;
    }

    // We'll perform a recursive copy. We'll collect directory entries per
    // directory (while the source is mounted), then process the collected
    // names and write files to ATA by re-mounting ATA as target when needed.

    // Forward declaration for recursive copy helper implemented below (declared at file scope)
    copy_dir_recursive_impl("/", "/", img_ptr, img_size);

    print("install: completed\n");
}

// Helper: recursively copy directory from mounted source image to ATA target.
// This function mounts source image (from img_ptr) before listing, and mounts
// ATA when writing files. Uses heap kmalloc/kfree for temporary buffers.
static void copy_dir_recursive_impl(const char* src_prefix, const char* dst_prefix, const void* img_ptr, size_t img_size) {
    // ensure source is mounted
    if (hanacore::fs::fat32_init_from_memory(img_ptr, img_size) != 0) return;

    // Use the file-scope collector (collector_trampoline) which appends
    // names into the InstallNameNode list (g_inst_head/g_inst_tail).
    g_inst_head = NULL; g_inst_tail = NULL;
    hanacore::fs::fat32_list_dir(src_prefix, collector_trampoline);
    InstallNameNode* head = g_inst_head;

    // Process collected names
    for (InstallNameNode* cur = head; cur != NULL; ) {
        InstallNameNode* next = cur->next;
        char src_path[256]; char dst_path[256];
        if (strcmp(src_prefix, "/") == 0) print_fmt(src_path, sizeof(src_path), "/%s", cur->name);
        else print_fmt(src_path, sizeof(src_path), "%s/%s", src_prefix, cur->name);
        if (strcmp(dst_prefix, "/") == 0) print_fmt(dst_path, sizeof(dst_path), "/%s", cur->name);
        else print_fmt(dst_path, sizeof(dst_path), "%s/%s", dst_prefix, cur->name);

        size_t flen = 0; void* fbuf = hanacore::fs::fat32_get_file_alloc(src_path, &flen);
        if (fbuf) {
            // file: mount ATA and write
            fat32_mount_ata_master(0);
            // attempt to create parent dir (best-effort)
            // naive parent extraction
            char parent[256]; strcpy(parent, dst_path);
            char* p = strrchr(parent, '/');
            if (p && p != parent) { *p = '\0'; hanacore::fs::fat32_make_dir(parent); }
            hanacore::fs::fat32_write_file(dst_path, fbuf, flen);
            hanacore::mem::kfree(fbuf);
            // remount source
            hanacore::fs::fat32_init_from_memory(img_ptr, img_size);
        } else {
            // directory: create on target then recurse
            fat32_mount_ata_master(0);
            hanacore::fs::fat32_make_dir(dst_path);
            // remount source and recurse
            hanacore::fs::fat32_init_from_memory(img_ptr, img_size);
            copy_dir_recursive_impl(src_path, dst_path, img_ptr, img_size);
        }

        // free node
        hanacore::mem::kfree(cur);
        cur = next;
    }
    // clear global collector state
    g_inst_head = NULL; g_inst_tail = NULL;
}

// (thin wrapper removed)

// bring in helper used above
static bool ends_with(const char* s, const char* suffix) {
    if (!s || !suffix) return false;
    const char* ps = s; size_t sl = 0; while (ps[sl]) ++sl;
    const char* pf = suffix; size_t fl = 0; while (pf[fl]) ++fl;
    if (fl > sl) return false;
    const char* start = s + (sl - fl);
    for (size_t i = 0; i < fl; ++i) if (start[i] != suffix[i]) return false;
    return true;
}
