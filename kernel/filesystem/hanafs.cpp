#include "hanafs.hpp"
#include "../mem/heap.hpp"
#include <string.h>
#include "../utils/logger.hpp"
#include "../libs/nanoprintf.h"
#include <stdint.h>
// ATA helpers (provided by IDE driver). These are weak symbols defined in
// filesystem/fat32.cpp as fallbacks; declare them here for use.
extern "C" int ata_read_sector(uint32_t lba, void* buf);
extern "C" int ata_write_sector(uint32_t lba, void* buf);

// Very small in-memory filesystem implementation. Not thread-safe. Uses
// simple linked-list of entries keyed by full path.

struct HanaEntry {
    char* path; // normalized absolute path (NUL-terminated)
    int is_dir;
    uint8_t* data;
    size_t len;
    HanaEntry* next;
};

static HanaEntry* g_head = NULL;
// Whether HanaFS was successfully loaded from ATA at init time.
static int g_loaded_from_ata = 0;

static char* strdup_k(const char* s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char* p = (char*)hanacore::mem::kmalloc(n);
    if (!p) return NULL;
    for (size_t i = 0; i < n; ++i) p[i] = s[i];
    return p;
}

static HanaEntry* find_entry(const char* path) {
    if (!path) return NULL;
    for (HanaEntry* e = g_head; e; e = e->next) {
        if (strcmp(e->path, path) == 0) return e;
    }
    return NULL;
}

static void remove_entry_node(HanaEntry* prev, HanaEntry* cur) {
    if (!cur) return;
    if (prev) prev->next = cur->next; else g_head = cur->next;
    if (cur->path) hanacore::mem::kfree(cur->path);
    if (cur->data) hanacore::mem::kfree(cur->data);
    hanacore::mem::kfree(cur);
}

static void normalize_path_inplace(char* buf, size_t sz) {
    // ensure leading '/'
    if (sz == 0 || !buf) return;
    if (buf[0] != '/') {
        // shift right
        size_t l = strlen(buf);
        if (l + 1 >= sz) return; // can't normalize
        for (size_t i = l + 1; i > 0; --i) buf[i] = buf[i-1];
        buf[0] = '/';
    }
    // remove trailing slashes (except root)
    size_t l = strlen(buf);
    while (l > 1 && buf[l-1] == '/') { buf[l-1] = '\0'; --l; }
}

extern "C" int hanafs_init(void) {
    // create root entry
    if (g_head) return 0;
    // attempt to load from ATA-backed image first; if this fails,
    // fall back to creating an empty in-memory root.
    if (hanafs_load_from_ata() == 0) {
        hanacore::utils::log_ok_cpp("[HanaFS] Loaded filesystem from ATA image");
        return 0;
    }
    HanaEntry* root = (HanaEntry*)hanacore::mem::kmalloc(sizeof(HanaEntry));
    if (!root) return -1;
    memset(root, 0, sizeof(HanaEntry));
    root->path = strdup_k("/");
    if (!root->path) { hanacore::mem::kfree(root); return -1; }
    root->is_dir = 1;
    root->next = NULL;
    g_head = root;
    return 0;
}

extern "C" int hanafs_write_file(const char* path, const void* buf, size_t len) {
    if (!path) return -1;
    // copy path into temp buffer to normalize
    size_t psz = strlen(path) + 2;
    char* pbuf = (char*)hanacore::mem::kmalloc(psz);
    if (!pbuf) return -1;
    for (size_t i = 0; i < psz-1; ++i) pbuf[i] = path[i]; pbuf[psz-1] = '\0';
    normalize_path_inplace(pbuf, psz);

    // remove existing
    HanaEntry* prev = NULL; HanaEntry* cur = g_head;
    while (cur) {
        if (strcmp(cur->path, pbuf) == 0) { remove_entry_node(prev, cur); break; }
        prev = cur; cur = cur->next;
    }

    // create new entry
    HanaEntry* e = (HanaEntry*)hanacore::mem::kmalloc(sizeof(HanaEntry));
    if (!e) { hanacore::mem::kfree(pbuf); return -1; }
    memset(e, 0, sizeof(HanaEntry));
    e->path = pbuf; e->is_dir = 0; e->len = len; e->next = g_head; g_head = e;
    if (len > 0 && buf) {
        e->data = (uint8_t*)hanacore::mem::kmalloc(len);
        if (!e->data) { remove_entry_node(NULL, e); return -1; }
        for (size_t i = 0; i < len; ++i) e->data[i] = ((const uint8_t*)buf)[i];
    } else {
        e->data = NULL; e->len = 0;
    }
    return 0;
}

// Simple on-disk persistence format and helpers.
// Layout: header (Magic "HANA" 4 bytes, version u32 LE, entry_count u32 LE, total_size u32 LE)
// followed by entry records:
// [u16 path_len][u8 is_dir][u32 data_len][path bytes (no NUL)][data bytes]
// The entire blob is written starting at HANAFS_PERSIST_LBA.

extern "C" int hanafs_persist_to_ata(void) {
    // ATA helpers are declared at top of file; call them directly.

    // compute size
    uint32_t entry_count = 0;
    size_t payload_len = 0;
    for (HanaEntry* e = g_head; e; e = e->next) {
        ++entry_count;
        size_t plen = strlen(e->path);
        payload_len += 2 + 1 + 4 + plen + (e->is_dir ? 0 : e->len);
    }

    const uint32_t hdr_sz = 4 + 4 + 4 + 4; // magic + ver + count + total
    size_t total = hdr_sz + payload_len;
    // allocate temporary buffer
    uint8_t* buf = (uint8_t*)hanacore::mem::kmalloc(total);
    if (!buf) return -1;
    size_t off = 0;
    // magic
    buf[off++] = 'H'; buf[off++] = 'A'; buf[off++] = 'N'; buf[off++] = 'A';
    // version
    uint32_t ver = 1; memcpy(buf + off, &ver, 4); off += 4;
    // entry count
    memcpy(buf + off, &entry_count, 4); off += 4;
    // total payload (remaining)
    uint32_t payload32 = (uint32_t)payload_len; memcpy(buf + off, &payload32, 4); off += 4;

    // write entries
    for (HanaEntry* e = g_head; e; e = e->next) {
        uint16_t plen = (uint16_t)strlen(e->path);
        buf[off++] = (uint8_t)(plen & 0xFF); buf[off++] = (uint8_t)((plen >> 8) & 0xFF);
        buf[off++] = (uint8_t)(e->is_dir ? 1 : 0);
        uint32_t dlen = (uint32_t)(e->is_dir ? 0 : (uint32_t)e->len);
        memcpy(buf + off, &dlen, 4); off += 4;
        // path bytes (no NUL)
        for (size_t i = 0; i < plen; ++i) buf[off++] = (uint8_t)e->path[i];
        // data bytes
        if (!e->is_dir && dlen) {
            for (uint32_t i = 0; i < dlen; ++i) buf[off++] = e->data[i];
        }
    }

    // write to ATA sectors starting at HANAFS_PERSIST_LBA
#ifndef HANAFS_PERSIST_LBA
#define HANAFS_PERSIST_LBA 2048
#endif
    uint32_t sector = HANAFS_PERSIST_LBA;
    size_t written = 0;
    uint8_t sector_buf[512];
    while (written < total) {
        size_t to_copy = (total - written) < sizeof(sector_buf) ? (total - written) : sizeof(sector_buf);
        // zero sector_buf then copy
        for (size_t i = 0; i < sizeof(sector_buf); ++i) sector_buf[i] = 0;
        for (size_t i = 0; i < to_copy; ++i) sector_buf[i] = buf[written + i];
        if (ata_write_sector(sector, sector_buf) != 0) {
            hanacore::mem::kfree(buf);
            return -1;
        }
        written += to_copy;
        ++sector;
    }
    hanacore::mem::kfree(buf);
    g_loaded_from_ata = 1;
    return 0;
}

extern "C" int hanafs_load_from_ata(void) {
    // ATA helpers are declared at top of file; call them directly.
#ifndef HANAFS_PERSIST_LBA
#define HANAFS_PERSIST_LBA 2048
#endif
    uint32_t sector = HANAFS_PERSIST_LBA;
    // read first sector to get header
    uint8_t sector_buf[512];
    if (ata_read_sector(sector, sector_buf) != 0) return -1;
    // check magic
    if (!(sector_buf[0]=='H' && sector_buf[1]=='A' && sector_buf[2]=='N' && sector_buf[3]=='A')) return -1;
    size_t off = 4;
    uint32_t ver; memcpy(&ver, sector_buf + off, 4); off += 4;
    uint32_t entry_count; memcpy(&entry_count, sector_buf + off, 4); off += 4;
    uint32_t payload32; memcpy(&payload32, sector_buf + off, 4); off += 4;
    size_t total = 4 + 4 + 4 + 4 + (size_t)payload32;
    // allocate buffer and read remaining sectors
    uint8_t* buf = (uint8_t*)hanacore::mem::kmalloc(total);
    if (!buf) return -1;
    // copy first sector content
    for (size_t i = 0; i < 512 && i < total; ++i) buf[i] = sector_buf[i];
    size_t read = 512;
    ++sector;
    while (read < total) {
        if (ata_read_sector(sector, sector_buf) != 0) { hanacore::mem::kfree(buf); return -1; }
        size_t to_copy = (total - read) < 512 ? (total - read) : 512;
        for (size_t i = 0; i < to_copy; ++i) buf[read + i] = sector_buf[i];
        read += to_copy; ++sector;
    }

    // parse buffer and rebuild in-memory structure
    size_t pos = 0;
    // skip magic/version/count/total
    pos += 4 + 4 + 4 + 4;
    // clear any existing entries
    while (g_head) { HanaEntry* n = g_head->next; remove_entry_node(NULL, g_head); g_head = n; }
    // create root
    HanaEntry* root = (HanaEntry*)hanacore::mem::kmalloc(sizeof(HanaEntry));
    if (!root) { hanacore::mem::kfree(buf); return -1; }
    memset(root,0,sizeof(HanaEntry)); root->path = strdup_k("/"); root->is_dir = 1; root->next = NULL; g_head = root;

    for (uint32_t ei = 0; ei < entry_count; ++ei) {
        if (pos + 2 + 1 + 4 > total) break;
        uint16_t plen = (uint16_t)(buf[pos] | (buf[pos+1]<<8)); pos += 2;
        uint8_t is_dir = buf[pos++];
        uint32_t dlen = 0; memcpy(&dlen, buf + pos, 4); pos += 4;
        if (pos + plen > total) break;
        char* path = (char*)hanacore::mem::kmalloc(plen + 1);
        if (!path) break;
        for (uint16_t i = 0; i < plen; ++i) path[i] = (char)buf[pos++]; path[plen] = '\0';
        // create entry
        HanaEntry* e = (HanaEntry*)hanacore::mem::kmalloc(sizeof(HanaEntry));
        if (!e) { hanacore::mem::kfree(path); break; }
        memset(e,0,sizeof(HanaEntry));
        e->path = path; e->is_dir = is_dir ? 1 : 0; e->len = dlen; e->next = g_head; g_head = e;
        if (!e->is_dir && dlen > 0) {
            e->data = (uint8_t*)hanacore::mem::kmalloc(dlen);
            if (!e->data) { remove_entry_node(NULL, e); break; }
            for (uint32_t i = 0; i < dlen; ++i) e->data[i] = buf[pos++];
        }
    }

    hanacore::mem::kfree(buf);
    return 0;
}

extern "C" void* hanafs_get_file_alloc(const char* path, size_t* out_len) {
    if (!path || !out_len) return NULL;
    HanaEntry* e = find_entry(path);
    if (!e || e->is_dir) return NULL;
    size_t n = e->len ? e->len : 0;
    void* buf = hanacore::mem::kmalloc(n ? n : 1);
    if (!buf) return NULL;
    if (n) {
        for (size_t i = 0; i < n; ++i) ((uint8_t*)buf)[i] = e->data[i];
    } else {
        ((uint8_t*)buf)[0] = 0;
    }
    *out_len = n;
    return buf;
}

extern "C" int hanafs_list_dir(const char* path, void (*cb)(const char* name)) {
    if (!path || !cb) return -1;
    // normalize path
    size_t psz = strlen(path) + 2;
    char* pbuf = (char*)hanacore::mem::kmalloc(psz);
    if (!pbuf) return -1;
    for (size_t i = 0; i < psz-1; ++i) pbuf[i] = path[i]; pbuf[psz-1] = '\0';
    normalize_path_inplace(pbuf, psz);
    size_t plen = strlen(pbuf);
    // iterate entries and find immediate children
    for (HanaEntry* e = g_head; e; e = e->next) {
        if (strcmp(e->path, pbuf) == 0) continue;
        // path must start with pbuf + '/' or be direct child
        if (strncmp(e->path, pbuf, plen) != 0) continue;
        const char* rest = e->path + plen;
        if (*rest == '/') ++rest;
        // only immediate children: no additional '/'
        const char* slash = strchr(rest, '/');
        if (slash) continue;
        cb(rest);
    }
    hanacore::mem::kfree(pbuf);
    return 0;
}

extern "C" int hanafs_create_file(const char* path) {
    return hanafs_write_file(path, NULL, 0);
}

extern "C" int hanafs_unlink(const char* path) {
    if (!path) return -1;
    HanaEntry* prev = NULL; HanaEntry* cur = g_head;
    while (cur) {
        if (strcmp(cur->path, path) == 0) { remove_entry_node(prev, cur); return 0; }
        prev = cur; cur = cur->next;
    }
    return -1;
}

extern "C" int hanafs_make_dir(const char* path) {
    if (!path) return -1;
    // normalize
    size_t psz = strlen(path) + 2;
    char* pbuf = (char*)hanacore::mem::kmalloc(psz);
    if (!pbuf) return -1;
    for (size_t i = 0; i < psz-1; ++i) pbuf[i] = path[i]; pbuf[psz-1] = '\0';
    normalize_path_inplace(pbuf, psz);
    if (find_entry(pbuf)) { hanacore::mem::kfree(pbuf); return -1; }
    HanaEntry* e = (HanaEntry*)hanacore::mem::kmalloc(sizeof(HanaEntry));
    if (!e) { hanacore::mem::kfree(pbuf); return -1; }
    memset(e, 0, sizeof(HanaEntry));
    e->path = pbuf; e->is_dir = 1; e->data = NULL; e->len = 0; e->next = g_head; g_head = e;
    return 0;
}

extern "C" int hanafs_remove_dir(const char* path) {
    // only remove empty directory
    if (!path) return -1;
    // check for children
    size_t plen = strlen(path);
    for (HanaEntry* e = g_head; e; e = e->next) {
        if (strcmp(e->path, path) == 0) continue;
        if (strncmp(e->path, path, plen) == 0 && e->path[plen] == '/') return -1; // not empty
    }
    return hanafs_unlink(path);
}

// C++ namespace wrappers so callers can use hanacore::fs::hanafs_* APIs.
#ifdef __cplusplus
namespace hanacore { namespace fs {
    int hanafs_init(void) { return ::hanafs_init(); }
    int hanafs_write_file(const char* path, const void* buf, size_t len) { return ::hanafs_write_file(path, buf, len); }
    void* hanafs_get_file_alloc(const char* path, size_t* out_len) { return ::hanafs_get_file_alloc(path, out_len); }
    int hanafs_list_dir(const char* path, void (*cb)(const char* name)) { return ::hanafs_list_dir(path, cb); }
    int hanafs_create_file(const char* path) { return ::hanafs_create_file(path); }
    int hanafs_unlink(const char* path) { return ::hanafs_unlink(path); }
    int hanafs_make_dir(const char* path) { return ::hanafs_make_dir(path); }
    int hanafs_remove_dir(const char* path) { return ::hanafs_remove_dir(path); }
} }
#endif
