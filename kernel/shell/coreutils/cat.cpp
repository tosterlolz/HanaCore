#include "../../filesystem/vfs.hpp"
#include "../../mem/heap.hpp"

extern "C" void print(const char*);
extern "C" void tty_write(const char* s);

extern "C" void builtin_cat_cmd(const char* arg) {
    if (!arg || arg[0] == '\0') {
        print("usage: cat <file>\n");
        return;
    }

    size_t len = 0;
    void* data = hanacore::fs::vfs_get_file_alloc(arg, &len);
    if (!data) {
        print("cat: file not found\n");
        return;
    }

    // Emit in small chunks so we don't need a large temporary buffer.
    const char* bytes = (const char*)data;
    size_t off = 0;
    char tmp[128];
    while (off < len) {
        size_t n = (len - off) < (sizeof(tmp) - 1) ? (len - off) : (sizeof(tmp) - 1);
        for (size_t i = 0; i < n; ++i) tmp[i] = bytes[off + i];
        tmp[n] = '\0';
        tty_write(tmp);
        off += n;
    }
    // Ensure trailing newline for nicer display if file doesn't end with one.
    tty_write("\n");

    hanacore::mem::kfree(data);
}
