#include "../../filesystem/fat32.hpp"

extern "C" void print(const char*);

extern "C" void builtin_mkdir_cmd(const char* arg) {
    if (!arg || arg[0] == '\0') {
        print("usage: mkdir <directory>\n");
        return;
    }

    int rc = hanacore::fs::fat32_make_dir(arg);
    if (rc == 0) {
        print("mkdir: ok\n");
    } else {
        print("mkdir: failed\n");
    }
}
