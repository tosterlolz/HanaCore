#include "../../filesystem/fat32.hpp"
#include <stddef.h>
#include <string.h>
#include "../../../limine/limine.h"
#include <stdint.h>
#include "../../net/lwip_wrapper.hpp"

extern "C" void print(const char*);

// Simple fetch builtin: supports copying from FAT32 paths like "1:/path/file"
// Usage: fetch <src-url> -o <dest-path>
// If src is http(s) the command currently prints an informational message.
extern "C" void builtin_fetch_cmd(const char* arg) {
    if (!arg || *arg == '\0') {
        print("fetch: missing url\n");
        return;
    }

    // Parse arguments: split by spaces, look for -o
    char buf[256]; size_t bl = 0;
    while (bl + 1 < sizeof(buf) && arg[bl]) { buf[bl] = arg[bl]; ++bl; }
    buf[bl] = '\0';

    // Tokenize
    const char* src = NULL;
    const char* out = NULL;
    char tok1[128]; tok1[0]='\0';
    char tok2[128]; tok2[0]='\0';
    // very small tokenizer
    size_t i = 0; size_t t = 0;
    // first token
    while (i < bl && buf[i] == ' ') ++i;
    size_t s = 0;
    while (i < bl && buf[i] != ' ' && s + 1 < sizeof(tok1)) tok1[s++] = buf[i++];
    tok1[s] = '\0';
    src = tok1;
    // find -o
    while (i < bl && buf[i] == ' ') ++i;
    while (i < bl) {
        if (buf[i] == '-' && i + 1 < bl && buf[i+1] == 'o') {
            i += 2; while (i < bl && buf[i] == ' ') ++i;
            size_t o = 0;
            while (i < bl && buf[i] != ' ' && o + 1 < sizeof(tok2)) tok2[o++] = buf[i++];
            tok2[o] = '\0'; out = tok2; break;
        }
        // skip token
        while (i < bl && buf[i] != ' ') ++i; while (i < bl && buf[i] == ' ') ++i;
    }

    if (!src || !src[0]) { print("fetch: invalid src\n"); return; }

    if (strncmp(src, "http://", 7) == 0 || strncmp(src, "https://", 8) == 0) {
        // If lwIP is available and linked, try a real HTTP fetch first.
        if (out && out[0]) {
            int r = http_fetch_via_lwip(src, out);
            if (r == 0) {
                print("fetch: written successfully (via lwIP)\n");
                return;
            }
        }
        // Try to satisfy HTTP requests by looking up a Limine module whose
        // path matches the requested host/path. This allows embedding HTTP
        // payloads into the ISO as modules and then fetching them at runtime.
        // URL format: http://host[:port]/path
        const char* p = src;
        const char* scheme_end = strstr(p, "://");
        if (!scheme_end) { print("fetch: invalid URL\n"); return; }
        p = scheme_end + 3; // now at host[:port]/path
        // extract host
        char host[128]; size_t hi = 0;
        while (*p && *p != ':' && *p != '/' && hi + 1 < sizeof(host)) host[hi++] = *p++;
        host[hi] = '\0';
        // skip optional :port
        if (*p == ':') { while (*p && *p != '/') ++p; }
        // extract path
        char path[256]; size_t pi = 0;
        if (*p == '/') ++p; // skip leading /
        while (*p && pi + 1 < sizeof(path)) path[pi++] = *p++;
        path[pi] = '\0';

        // Construct candidate module suffixes to match against limine module paths.
        // Try: host/path, path, filename
        char cand1[320]; cand1[0] = '\0';
        if (host[0] && path[0]) {
            // host + '/' + path
            size_t off = 0;
            for (size_t i = 0; i < hi; ++i) cand1[off++] = host[i];
            if (off + 1 < sizeof(cand1)) cand1[off++] = '/';
            for (size_t i = 0; i < pi && off + 1 < sizeof(cand1); ++i) cand1[off++] = path[i];
            cand1[off] = '\0';
        }

        char cand2[256]; cand2[0] = '\0';
        // just path
        for (size_t i = 0; i < pi && i + 1 < sizeof(cand2); ++i) cand2[i] = path[i]; cand2[pi] = '\0';

        char filename[128]; filename[0] = '\0';
        // filename part (after last '/')
        size_t last_sl = 0;
        for (size_t i = 0; i < pi; ++i) if (path[i] == '/') last_sl = i + 1;
        size_t fi = 0;
        for (size_t i = last_sl; i < pi && fi + 1 < sizeof(filename); ++i) filename[fi++] = path[i];
        filename[fi] = '\0';

        // Search Limine modules for a matching module path
        extern volatile struct limine_module_request module_request;
        extern volatile struct limine_hhdm_request limine_hhdm_request;
        if (module_request.response) {
            volatile struct limine_module_response* resp = module_request.response;
            for (uint64_t mi = 0; mi < resp->module_count; ++mi) {
                volatile struct limine_file* mod = resp->modules[mi];
                const char* mpath = (const char*)(uintptr_t)mod->path;
                if (mpath && limine_hhdm_request.response) {
                    uint64_t hoff = limine_hhdm_request.response->offset;
                    if ((uint64_t)mpath < hoff) mpath = (const char*)((uintptr_t)mpath + hoff);
                }
                if (!mpath) continue;
                // check suffix matches
                size_t ml = 0; while (mpath[ml]) ++ml;
                // helper: ends_with
                auto ends_with = [&](const char* s, const char* suf)->bool{
                    if (!s || !suf) return false;
                    size_t sl = 0; while (s[sl]) ++sl;
                    size_t fl = 0; while (suf[fl]) ++fl;
                    if (fl > sl) return false;
                    const char* start = s + (sl - fl);
                    for (size_t k = 0; k < fl; ++k) if (start[k] != suf[k]) return false;
                    return true;
                };
                if ((cand1[0] && ends_with(mpath, cand1)) || (cand2[0] && ends_with(mpath, cand2)) || (filename[0] && ends_with(mpath, filename))) {
                    // found module; get its virtual address
                    uintptr_t mod_addr = (uintptr_t)mod->address;
                    const void* mod_virt = (const void*)mod_addr;
                    if (limine_hhdm_request.response) {
                        uint64_t off = limine_hhdm_request.response->offset;
                        if ((uint64_t)mod_addr < off) mod_virt = (const void*)(off + mod_addr);
                    }
                    size_t msize = (size_t)mod->size;
                    if (!out || !out[0]) { print("fetch: no -o destination provided; use -o <dest-path>\n"); return; }
                    int rc = hanacore::fs::fat32_write_file(out, mod_virt, msize);
                    if (rc == 0) print("fetch: written successfully (from module)\n"); else print("fetch: failed to write destination\n");
                    return;
                }
            }
        }

        print("fetch: HTTP module not found in ISO modules; include the file as a module or use FAT path\n");
        return;
    }

    // Assume FAT32 path; use fat32_get_file_alloc
    size_t len = 0;
    void* data = hanacore::fs::fat32_get_file_alloc(src, &len);
    if (!data) {
        print("fetch: source not found on FAT32 filesystem\n");
        return;
    }

    if (!out || !out[0]) {
        print("fetch: no -o destination provided; use -o <dest-path> (e.g. 0:/out.bin)\n");
        return;
    }

    int rc = hanacore::fs::fat32_write_file(out, data, len);
    if (rc == 0) {
        print("fetch: written successfully\n");
    } else {
        print("fetch: failed to write destination\n");
    }
}
