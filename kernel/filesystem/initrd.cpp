#include "initrd.hpp"
#include "hanafs.hpp"
#include "../utils/logger.hpp"
#include "../../third_party/limine/limine.h"
#include "../libs/libc.h"

extern volatile struct limine_hhdm_request limine_hhdm_request;
extern volatile struct limine_module_request module_request;

static size_t parse_octal(const char* s, size_t n) {
    size_t v = 0;
    for (size_t i = 0; i < n && s[i]; ++i) {
        char c = s[i];
        if (c < '0' || c > '7') break;
        v = (v << 3) + (size_t)(c - '0');
    }
    return v;
}

// Minimal POSIX ustar header (we only need name, size, typeflag and prefix)
struct TarHeader {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};

static void ensure_parent_dirs(const char* path) {
    // Create parent dirs incrementally: for /a/b/c.txt create /a and /a/b
    size_t len = strlen(path);
    char tmp[512];
    if (len >= sizeof(tmp)) return;
    strcpy(tmp, path);
    // remove leading slash if present for iteration convenience
    size_t pos = 0;
    if (tmp[0] == '/') pos = 1;
    for (size_t i = pos; i < len; ++i) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            // build a dir path starting with '/'
            char dpath[512];
            if (pos == 1) snprintf(dpath, sizeof(dpath), "/%s", tmp + 1);
            else snprintf(dpath, sizeof(dpath), "%s", tmp);
            hanacore::fs::hanafs_make_dir(dpath);
            tmp[i] = '/';
        }
    }
}

static void copy_field_nul(const char* src, size_t src_n, char* out, size_t out_n) {
    size_t i = 0;
    for (; i < src_n && i + 1 < out_n; ++i) {
        char c = src[i];
        out[i] = c;
        if (c == '\0') break;
    }
    if (i == src_n || out[i] != '\0') {
        // ensure NUL termination
        out[(i < out_n) ? i : (out_n - 1)] = '\0';
    }
}

int hanacore::initrd::init_from_memory(const void* data, size_t size) {
    if (!data || size == 0) return -1;
    const uint8_t* p = (const uint8_t*)data;
    size_t off = 0;
    char tmp[256];

    while (off + 512 <= size) {
        const TarHeader* h = (const TarHeader*)(p + off);
        // If name is empty, end of archive
        if (h->name[0] == '\0') break;

        // Extract name and prefix safely (fields may not be NUL-terminated)
        char name_field[256];
        char prefix_field[256];
        copy_field_nul(h->name, sizeof(h->name), name_field, sizeof(name_field));
        copy_field_nul(h->prefix, sizeof(h->prefix), prefix_field, sizeof(prefix_field));

        // Build full name
        char fullname[512]; fullname[0] = '\0';
        if (prefix_field[0] != '\0') {
            snprintf(fullname, sizeof(fullname), "%s/%s", prefix_field, name_field);
        } else {
            snprintf(fullname, sizeof(fullname), "%s", name_field);
        }

        // Strip possible leading "./"
        const char* fn = fullname;
        if (fn[0] == '.' && fn[1] == '/') fn += 2;

        // Ignore empty or special '.' entries
        if (fn[0] == '\0' || (fn[0] == '.' && fn[1] == '\0')) {
            off += 512; // still need to advance
            continue;
        }

        // sanitize: ensure leading '/'
        char path[512];
        if (fn[0] == '/') snprintf(path, sizeof(path), "%s", fn);
        else snprintf(path, sizeof(path), "/%s", fn);

        // detect directory by typeflag or trailing '/'
        bool is_dir = (h->typeflag == '5');
        size_t plen = strlen(path);
        if (!is_dir && plen > 0 && path[plen - 1] == '/') {
            is_dir = true;
            // remove trailing slash
            if (plen < sizeof(path)) path[plen - 1] = '\0';
        }

        size_t fsize = parse_octal(h->size, sizeof(h->size));
        // Log small amount for diagnostics
        snprintf(tmp, sizeof(tmp), "[INITRD] entry: %s size=%zu type=%c", path, fsize, h->typeflag);
        hanacore::utils::log_info_cpp(tmp);

        off += 512;
        if (is_dir) {
            hanacore::fs::hanafs_make_dir(path);
        } else {
            if (fsize > 0) {
                if (off + fsize > size) return -1; // truncated
                // ensure parent dirs exist
                ensure_parent_dirs(path);
                // write file into HanaFS
                if (hanacore::fs::hanafs_write_file(path, p + off, fsize) != 0) {
                    hanacore::utils::log_info_cpp("[INITRD] failed to write file into HanaFS");
                    return -1;
                }
            } else {
                // empty file, ensure parent and create empty file
                ensure_parent_dirs(path);
                hanacore::fs::hanafs_create_file(path);
            }
        }

        // advance by file data rounded up to 512
        size_t data_blocks = (fsize + 511) / 512;
        off += data_blocks * 512;
    }

    return 0;
}

int hanacore::initrd::init_from_module(const char* module_name) {
    if (module_request.response && module_name) {
        volatile struct limine_module_response* resp = module_request.response;
        for (uint64_t i = 0; i < resp->module_count; ++i) {
            volatile struct limine_file* mod = resp->modules[i];
            const char* path = (const char*)(uintptr_t)mod->path;
            if (path && limine_hhdm_request.response) {
                uint64_t hoff = limine_hhdm_request.response->offset;
                if ((uint64_t)path < hoff) path = (const char*)((uintptr_t)path + hoff);
            }
            if (!path) continue;
            if (strcmp(path, module_name) == 0 || strcmp(path + (strlen(path) - strlen(module_name)), module_name) == 0) {
                uintptr_t mod_addr = (uintptr_t)mod->address;
                const void* mod_virt = (const void*)mod_addr;
                size_t mod_size = (size_t)mod->size;
                char tmp[256];
                snprintf(tmp, sizeof(tmp), "[INITRD] found module %s addr=%p size=%u", module_name, mod_virt, (unsigned)mod_size);
                hanacore::utils::log_info_cpp(tmp);
                return hanacore::initrd::init_from_memory(mod_virt, mod_size);
            }
        }
    }
    return -1;

}

extern "C" int initrd_init_from_module(const char* module_name) { return hanacore::initrd::init_from_module(module_name); }
extern "C" int initrd_init_from_memory(const void* data, size_t size) { return hanacore::initrd::init_from_memory(data, size); }
