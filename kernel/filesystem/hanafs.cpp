#include "hanafs.hpp"
#include "../api/hanaapi.h"
#include "../mem/heap.hpp"
#include "../drivers/ide.hpp"
#include <string.h>
#include "../utils/logger.hpp"
#include "../libs/libc.h"
#include <stdint.h>
// ATA helpers (provided by IDE driver). These are weak symbols defined in
// filesystem/fat32.cpp as fallbacks; declare them here for use.
extern "C" int ata_read_sector(uint32_t lba, void* buf);
extern "C" int ata_write_sector(uint32_t lba, const void* buf);
extern "C" int ata_read_sector_drive(uint32_t drive, uint32_t lba, void* buf);
extern "C" int32_t ata_get_sector_count_drive(int drive);

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

// Directory iterator object used by hanafs_opendir/hanafs_readdir
struct HanaDirObj {
    char* ipath; // internal path used for matching (e.g., /drv0/foo)
    size_t ipath_len;
    HanaEntry* next; // next entry to inspect
};

// Helper to create a hana_dirent from entry name and type
static struct hana_dirent* make_dirent(const char* name, int is_dir) {
    struct hana_dirent* de = (struct hana_dirent*)hanacore::mem::kmalloc(sizeof(struct hana_dirent));
    if (!de) return NULL;
    de->d_ino = 0;
    de->d_type = (uint8_t)(is_dir ? 1 : 0);
    size_t i = 0; while (i + 1 < sizeof(de->d_name) && name[i]) { de->d_name[i] = name[i]; ++i; }
    de->d_name[i] = '\0';
    return de;
}

// If path begins with a drive prefix like '0:/', remove the leading "N:" so
// HanaFS operates on a single unified namespace. This modifies buf in-place.
// Parse optional drive prefix like 'N:...' at start of buf. If present,
// returns drive number (0..9) and shifts the string left to remove the
// prefix. If no prefix, returns -1 and leaves buf unchanged. sz is the
// buffer size in bytes.
static int parse_drive_prefix_inplace(char* buf, size_t sz) {
    if (!buf || sz < 3) return -1;
    if (buf[0] >= '0' && buf[0] <= '9' && buf[1] == ':') {
        int drv = (int)(buf[0] - '0');
        size_t l = strlen(buf);
        size_t i = 0;
        for (; i + 2 <= l; ++i) buf[i] = buf[i+2];
        if (i < sz) buf[i] = '\0'; else buf[sz-1] = '\0';
        return drv;
    }
    return -1;
}

// Build an internal path that includes a drive namespace so HanaFS can
// keep separate trees per drive. out will contain the internal path and
// always be NUL-terminated. Example: path="/foo" drive=1 -> out="/drv1/foo".
static void build_internal_path(const char* path, int drive, char* out, size_t out_sz) {
    if (!path || !out || out_sz == 0) return;
    // If drive is negative, treat as legacy (no drive) and copy path as-is.
    if (drive < 0) {
        size_t i = 0;
        while (i + 1 < out_sz && path[i]) { out[i] = path[i]; ++i; }
        out[i] = '\0';
        return;
    }
    // prefix "/drvN"
    int n = snprintf(out, out_sz, "/drv%d", drive);
    if (n < 0) { if (out_sz) out[0] = '\0'; return; }
    size_t pos = (size_t)n;
    // ensure there's a separating slash
    if (pos + 1 < out_sz && path[0] == '/') {
        // concat directly
        size_t i = 0;
        while (pos + i + 1 < out_sz && path[i]) { out[pos + i] = path[i]; ++i; }
        out[pos + i] = '\0';
    } else {
        // path might not start with '/', so add one
        if (pos + 1 < out_sz) out[pos++] = '/';
        size_t i = 0;
        while (pos + i + 1 < out_sz && path[i]) { out[pos + i] = path[i]; ++i; }
        out[pos + i] = '\0';
    }
}

extern "C" int hanafs_init(void) {
    // create root entry
    if (g_head) return 0;
    // attempt to load from ATA-backed image first; if this succeeds,
    // keep the loaded tree and do not create an additional /drv0 entry.
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
    // Ensure basic top-level directories exist for a consistent namespace
    // when starting with an in-memory filesystem. Do NOT create `/drv0`
    // unless an ATA-backed image was actually loaded or explicitly
    // mounted; mapping root paths into /drv0 by default confused
    // visible mount points.
    hanafs_make_dir("/bin");
    hanafs_make_dir("/dev");
    hanafs_make_dir("/home");
    return 0;
}

extern "C" int hanafs_write_file(const char* path, const void* buf, size_t len) {
    if (!path) return -1;
    // copy path into temp buffer to normalize
    size_t psz = strlen(path) + 2;
    char* pbuf = (char*)hanacore::mem::kmalloc(psz);
    if (!pbuf) return -1;
    for (size_t i = 0; i < psz-1; ++i) pbuf[i] = path[i]; pbuf[psz-1] = '\0';
    // support optional drive prefix like '0:/' and normalize; keep track of drive
    int drv = parse_drive_prefix_inplace(pbuf, psz);
    normalize_path_inplace(pbuf, psz);
    // build internal storage path including drive namespace (only when
    // a drive prefix was provided). If drv < 0, build_internal_path will
    // copy the path unchanged.
    char ipath[512]; build_internal_path(pbuf, drv, ipath, sizeof(ipath));

    // remove existing
    HanaEntry* prev = NULL; HanaEntry* cur = g_head;
    while (cur) {
        if (strcmp(cur->path, ipath) == 0) { remove_entry_node(prev, cur); break; }
        prev = cur; cur = cur->next;
    }

    // create new entry
    HanaEntry* e = (HanaEntry*)hanacore::mem::kmalloc(sizeof(HanaEntry));
    if (!e) { hanacore::mem::kfree(pbuf); return -1; }
    memset(e, 0, sizeof(HanaEntry));
    e->path = strdup_k(ipath); e->is_dir = 0; e->len = len; e->next = g_head; g_head = e;
    if (len > 0 && buf) {
        e->data = (uint8_t*)hanacore::mem::kmalloc(len);
        if (!e->data) { remove_entry_node(NULL, e); return -1; }
        for (size_t i = 0; i < len; ++i) e->data[i] = ((const uint8_t*)buf)[i];
    } else {
        e->data = NULL; e->len = 0;
    }
    // Attempt to persist to ATA; log result but don't fail the write on
    // persistence errors (so HanaFS remains usable even without ATA).
    if (hanafs_persist_to_ata() == 0) {
        hanacore::utils::log_info_cpp("[HanaFS] persisted file %s to ATA", ipath);
    } else {
        hanacore::utils::log_info_cpp("[HanaFS] failed to persist file %s to ATA", ipath);
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

// Create an empty HanaFS filesystem and write it to ATA starting at
// HANAFS_PERSIST_LBA. Drive_number is currently unused (placeholder).
extern "C" int hanafs_format_ata_master(int drive_number) {
    (void)drive_number;
    // clear any existing entries
    while (g_head) { HanaEntry* n = g_head->next; remove_entry_node(NULL, g_head); g_head = n; }
    // create fresh root
    HanaEntry* root = (HanaEntry*)hanacore::mem::kmalloc(sizeof(HanaEntry));
    if (!root) return -1;
    memset(root, 0, sizeof(HanaEntry));
    root->path = strdup_k("/"); if (!root->path) { hanacore::mem::kfree(root); return -1; }
    root->is_dir = 1; root->next = NULL; g_head = root;

    int rc = hanafs_persist_to_ata();
    if (rc == 0) {
        g_loaded_from_ata = 1;
        return 0;
    }
    return -1;
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

// Report mounts for tools like lsblk. The callback receives a human-readable
// line per mount. Return 0 on success, -1 on error.
extern "C" int hanafs_list_mounts(void (*cb)(const char* line)) {
    if (!cb) return -1;
    char line[128];
    // If HanaFS was loaded from ATA persistence area, report it as mounted
    // on ATA master (drive 0). Otherwise report as in-memory (no drive).
    // Report ATA-backed image for drive 0 when loaded.
    if (g_loaded_from_ata) {
        snprintf(line, sizeof(line), "HanaFS mount: [0: ATA image -> LBA=%u]", (unsigned)HANAFS_PERSIST_LBA);
        cb(line);
    } else {
        snprintf(line, sizeof(line), "HanaFS mount: [in-memory]");
        cb(line);
    }

    // Additionally, enumerate any `/drvN` namespaces that exist in the
    // in-memory tree so tools like lsblk can display per-drive mounts
    // (for example, /drv1 for a CD-ROM that was mounted).
    // Build a small bitmap of seen drive numbers (0-9 supported here).
    int seen[10] = {0,0,0,0,0,0,0,0,0,0};
    for (HanaEntry* e = g_head; e; e = e->next) {
        if (!e->path) continue;
        if (strncmp(e->path, "/drv", 4) == 0) {
            // expect "/drvN" where N is a digit
            if (e->path[4] >= '0' && e->path[4] <= '9') {
                int d = e->path[4] - '0';
                if (!seen[d]) {
                    seen[d] = 1;
                    // produce a friendly line; for drive 0 we already
                    // reported the ATA image above, so skip duplicate.
                    if (d == 0) continue;
                    snprintf(line, sizeof(line), "HanaFS mount: [/drv%d]", d);
                    // For simplicity just show the mount point and drive number.
                    cb(line);
                }
            }
        }
    }
    return 0;
}

extern "C" void* hanafs_get_file_alloc(const char* path, size_t* out_len) {
    if (!path || !out_len) return NULL;
    // Accept optional 'N:/...' drive prefix and normalize. Resolve per-drive
    // internal path first, then fall back to legacy (no-drive) entries for
    // drive 0 for compatibility.
    size_t psz = strlen(path) + 2;
    char* pbuf = (char*)hanacore::mem::kmalloc(psz);
    if (!pbuf) return NULL;
    for (size_t i = 0; i < psz-1; ++i) pbuf[i] = path[i]; pbuf[psz-1] = '\0';
    int drv = parse_drive_prefix_inplace(pbuf, psz);
    normalize_path_inplace(pbuf, psz);
    char ipath[512]; build_internal_path(pbuf, drv, ipath, sizeof(ipath));
    HanaEntry* e = find_entry(ipath);
    // do not implicitly fall back to /drv0 for bare paths; callers
    // should explicitly reference `0:/` when they want ATA-backed
    // drive contents. This keeps mount points distinct.
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
    hanacore::mem::kfree(pbuf);
    return buf;
}

extern "C" int hanafs_list_dir(const char* path, void (*cb)(const char* name)) {
    if (!path || !cb) return -1;
    // normalize path
    size_t psz = strlen(path) + 2;
    char* pbuf = (char*)hanacore::mem::kmalloc(psz);
    if (!pbuf) return -1;
    for (size_t i = 0; i < psz-1; ++i) pbuf[i] = path[i]; pbuf[psz-1] = '\0';
    int drv = -1;
    drv = parse_drive_prefix_inplace(pbuf, psz);
    normalize_path_inplace(pbuf, psz);
    size_t plen = strlen(pbuf);
    char ipath[512]; build_internal_path(pbuf, drv, ipath, sizeof(ipath));
    size_t iplen = strlen(ipath);
    // iterate entries and find immediate children for either internal namespace
    // or legacy (no-drive) namespace when appropriate.
    for (HanaEntry* e = g_head; e; e = e->next) {
        const char* candidate = NULL;
        size_t cand_len = 0;
        if (strncmp(e->path, ipath, iplen) == 0) {
            // internal match
            candidate = e->path + iplen;
            cand_len = strlen(e->path) - iplen;
        }
        else if (drv < 0 && strncmp(e->path, pbuf, plen) == 0) {
            // legacy (no-drive) match when the caller did not specify a
            // drive prefix.
            candidate = e->path + plen;
            cand_len = strlen(e->path) - plen;
        } else {
            continue;
        }
        if (candidate[0] == '/') ++candidate;
        const char* slash = strchr(candidate, '/');
        if (slash) continue;
        cb(candidate);
    }
    hanacore::mem::kfree(pbuf);
    return 0;
}

extern "C" struct hana_dir* hanafs_opendir(const char* path) {
    if (!path) return NULL;
    size_t psz = strlen(path) + 2;
    char* pbuf = (char*)hanacore::mem::kmalloc(psz);
    if (!pbuf) return NULL;
    for (size_t i = 0; i < psz-1; ++i) pbuf[i] = path[i]; pbuf[psz-1] = '\0';
    int drv = parse_drive_prefix_inplace(pbuf, psz);
    normalize_path_inplace(pbuf, psz);
    char ipath[512]; build_internal_path(pbuf, drv, ipath, sizeof(ipath));
    HanaDirObj* d = (HanaDirObj*)hanacore::mem::kmalloc(sizeof(HanaDirObj));
    if (!d) { hanacore::mem::kfree(pbuf); return NULL; }
    d->ipath = strdup_k(ipath);
    d->ipath_len = strlen(d->ipath);
    d->next = g_head;
    hanacore::mem::kfree(pbuf);
    return (struct hana_dir*)d;
}

extern "C" struct hana_dirent* hanafs_readdir(struct hana_dir* dirp) {
    if (!dirp) return NULL;
    HanaDirObj* d = (HanaDirObj*)dirp;
    size_t iplen = d->ipath_len;
    for (HanaEntry* e = d->next; e; e = e->next) {
        d->next = e->next;
        if (strcmp(e->path, d->ipath) == 0) continue;
        if (strncmp(e->path, d->ipath, iplen) != 0) continue;
        const char* rest = e->path + iplen;
        if (*rest == '/') ++rest;
        const char* slash = strchr(rest, '/');
        if (slash) continue; // not immediate child
        return make_dirent(rest, e->is_dir);
    }
    return NULL;
}

extern "C" int hanafs_closedir(struct hana_dir* dirp) {
    if (!dirp) return -1;
    HanaDirObj* d = (HanaDirObj*)dirp;
    if (d->ipath) hanacore::mem::kfree(d->ipath);
    hanacore::mem::kfree(d);
    return 0;
}

extern "C" int hanafs_create_file(const char* path) {
    return hanafs_write_file(path, NULL, 0);
}

extern "C" int hanafs_unlink(const char* path) {
    if (!path) return -1;
    size_t psz = strlen(path) + 2;
    char* pbuf = (char*)hanacore::mem::kmalloc(psz);
    if (!pbuf) return -1;
    for (size_t i = 0; i < psz-1; ++i) pbuf[i] = path[i]; pbuf[psz-1] = '\0';
    int drv = parse_drive_prefix_inplace(pbuf, psz);
    normalize_path_inplace(pbuf, psz);
    char ipath[512]; build_internal_path(pbuf, drv < 0 ? 0 : drv, ipath, sizeof(ipath));
    HanaEntry* prev = NULL; HanaEntry* cur = g_head;
    while (cur) {
        if (strcmp(cur->path, ipath) == 0 || (drv < 0 && strcmp(cur->path, pbuf) == 0)) {
            remove_entry_node(prev, cur);
            // persist updated state
            if (hanafs_persist_to_ata() == 0) {
                hanacore::utils::log_info_cpp("[HanaFS] persisted unlink %s to ATA", ipath);
            } else {
                hanacore::utils::log_info_cpp("[HanaFS] failed to persist unlink %s to ATA", ipath);
            }
            hanacore::mem::kfree(pbuf);
            return 0;
        }
        prev = cur; cur = cur->next;
    }
    hanacore::mem::kfree(pbuf);
    return -1;
}

extern "C" int hanafs_make_dir(const char* path) {
    if (!path) return -1;
    // normalize
    size_t psz = strlen(path) + 2;
    char* pbuf = (char*)hanacore::mem::kmalloc(psz);
    if (!pbuf) return -1;
    for (size_t i = 0; i < psz-1; ++i) pbuf[i] = path[i]; pbuf[psz-1] = '\0';
    int drv = parse_drive_prefix_inplace(pbuf, psz);
    normalize_path_inplace(pbuf, psz);
    char ipath[512]; build_internal_path(pbuf, drv < 0 ? 0 : drv, ipath, sizeof(ipath));
    if (find_entry(ipath) || (drv < 0 && find_entry(pbuf))) { hanacore::mem::kfree(pbuf); return -1; }
    HanaEntry* e = (HanaEntry*)hanacore::mem::kmalloc(sizeof(HanaEntry));
    if (!e) { hanacore::mem::kfree(pbuf); return -1; }
    memset(e, 0, sizeof(HanaEntry));
    e->path = strdup_k(ipath); e->is_dir = 1; e->data = NULL; e->len = 0; e->next = g_head; g_head = e;
    // persist new directory to ATA
    if (hanafs_persist_to_ata() == 0) {
        // ok
    } else {
        hanacore::utils::log_info_cpp("[HanaFS] failed to persist mkdir %s to ATA", ipath);
    }
    return 0;
}

extern "C" int hanafs_remove_dir(const char* path) {
    // only remove empty directory
    if (!path) return -1;
    // normalize and check for children
    size_t psz = strlen(path) + 2;
    char* pbuf = (char*)hanacore::mem::kmalloc(psz);
    if (!pbuf) return -1;
    for (size_t i = 0; i < psz-1; ++i) pbuf[i] = path[i]; pbuf[psz-1] = '\0';
    int drv = parse_drive_prefix_inplace(pbuf, psz);
    normalize_path_inplace(pbuf, psz);
    char ipath[512]; build_internal_path(pbuf, drv < 0 ? 0 : drv, ipath, sizeof(ipath));
    size_t plen = strlen(pbuf);
    for (HanaEntry* e = g_head; e; e = e->next) {
        if (strcmp(e->path, ipath) == 0) continue;
        if (strncmp(e->path, ipath, strlen(ipath)) == 0 && e->path[strlen(ipath)] == '/') { hanacore::mem::kfree(pbuf); return -1; }
        if (drv <= 0 && strcmp(e->path, pbuf) == 0) continue;
        if (drv <= 0 && strncmp(e->path, pbuf, plen) == 0 && e->path[plen] == '/') { hanacore::mem::kfree(pbuf); return -1; }
    }
    int rc = hanafs_unlink(pbuf);
    hanacore::mem::kfree(pbuf);
    return rc;
}

extern "C" int hanafs_stat(const char* path, struct hana_stat* st) {
    if (!path || !st) return -1;
    size_t psz = strlen(path) + 2;
    char* pbuf = (char*)hanacore::mem::kmalloc(psz);
    if (!pbuf) return -1;
    for (size_t i = 0; i < psz-1; ++i) pbuf[i] = path[i]; pbuf[psz-1] = '\0';
    int drv = parse_drive_prefix_inplace(pbuf, psz);
    normalize_path_inplace(pbuf, psz);
    char ipath[512]; build_internal_path(pbuf, drv, ipath, sizeof(ipath));
    HanaEntry* e = find_entry(ipath);
    if (!e && drv < 0) e = find_entry(pbuf);
    if (!e) { hanacore::mem::kfree(pbuf); return -1; }
    // Fill a minimal hana_stat
    memset(st, 0, sizeof(*st));
    st->st_size = (uint64_t)e->len;
    st->st_nlink = 1;
    st->st_uid = 0; st->st_gid = 0;
    // minimal mode: S_IFDIR (0x4000) or S_IFREG (0x8000)
    st->st_mode = e->is_dir ? 0x4000 : 0x8000;
    st->st_mtime_ns = 0; st->st_atime_ns = 0; st->st_ctime_ns = 0;
    hanacore::mem::kfree(pbuf);
    return 0;
}

// Minimal ISO9660 mount support: mount CD-ROM contents into HanaFS under
// a given mount point (e.g., "/drv1"). This lightweight reader supports
// the primary volume descriptor and directory records (basic subset).
static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void parse_iso_dir_recursive(int drive, uint32_t lba, uint32_t size, const char* mount_prefix);

extern "C" int hanafs_mount_iso_drive(int drive, const char* mount_point) {
    if (!mount_point) return -1;
    uint8_t sec[512];
    // Primary Volume Descriptor is at sector 16
    if (ata_read_sector_drive(drive, 16, sec) != 0) return -1;
    if (memcmp(&sec[1], "CD001", 5) != 0) return -1;
    const uint8_t* rr = sec + 156; // root dir record
    uint32_t root_lba = le32(rr + 2);
    uint32_t root_size = le32(rr + 10);
    // Create mount point directory
    hanacore::fs::hanafs_make_dir(mount_point);
    // Recursively parse and populate HanaFS
    parse_iso_dir_recursive(drive, root_lba, root_size, mount_point);
    return 0;
}

static void make_path_join(char* out, size_t out_sz, const char* base, const char* name) {
    size_t b = 0; while (b + 1 < out_sz && base[b]) { out[b] = base[b]; ++b; }
    if (b == 0 || out[b-1] != '/') {
        if (b + 1 < out_sz) out[b++] = '/';
    }
    size_t i = 0; while (b + i + 1 < out_sz && name[i]) { out[b + i] = name[i]; ++i; }
    out[b + i] = '\0';
}

static void parse_iso_dir_recursive(int drive, uint32_t lba, uint32_t size, const char* mount_prefix) {
    if (size == 0) return;
    uint8_t* buf = (uint8_t*)hanacore::mem::kmalloc(size);
    if (!buf) return;
    uint32_t sectors = (size + 511) / 512;
    for (uint32_t i = 0; i < sectors; ++i) {
        if (ata_read_sector_drive(drive, lba + i, buf + i*512) != 0) { hanacore::mem::kfree(buf); return; }
    }
    uint32_t off = 0;
    while (off < size) {
        uint8_t len = buf[off];
        if (len == 0) { uint32_t next = ((off / 512) + 1) * 512; off = next; continue; }
        const uint8_t* r = buf + off;
        uint32_t rec_lba = le32(r + 2);
        uint32_t rec_size = le32(r + 10);
        uint8_t name_len = r[32];
        const char* name_ptr = (const char*)(r + 33);
        if (name_len == 1 && (uint8_t)name_ptr[0] <= 1) { off += len; continue; }
        char name[256]; size_t ni = 0;
        for (uint8_t j = 0; j < name_len && ni + 1 < sizeof(name); ++j) {
            char ch = name_ptr[j]; if (ch == ';') break; name[ni++] = ch;
        }
        name[ni] = '\0';
        uint8_t flags = r[25];
        char path[512]; make_path_join(path, sizeof(path), mount_prefix, name);
        if (flags & 0x02) {
            hanacore::fs::hanafs_make_dir(path);
            parse_iso_dir_recursive(drive, rec_lba, rec_size, path);
        } else {
            uint8_t* fbuf = (uint8_t*)hanacore::mem::kmalloc(rec_size ? rec_size : 1);
            if (fbuf) {
                uint32_t fsectors = (rec_size + 511) / 512;
                for (uint32_t si = 0; si < fsectors; ++si) {
                    if (ata_read_sector_drive(drive, rec_lba + si, fbuf + si*512) != 0) break;
                }
                hanacore::fs::hanafs_write_file(path, fbuf, rec_size);
                hanacore::mem::kfree(fbuf);
            }
        }
        off += len;
    }
    hanacore::mem::kfree(buf);
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
    int hanafs_persist_to_ata(void) { return ::hanafs_persist_to_ata(); }
    int hanafs_load_from_ata(void) { return ::hanafs_load_from_ata(); }
    int hanafs_list_mounts(void (*cb)(const char* line)) { return ::hanafs_list_mounts(cb); }
    int hanafs_format_ata_master(int drive_number) { return ::hanafs_format_ata_master(drive_number); }
} }
#endif
