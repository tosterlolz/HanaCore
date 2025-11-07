#include "../../filesystem/fat32.hpp"

extern "C" void print(const char*);

extern "C" void builtin_touch_cmd(const char* arg) {
    if (!arg || arg[0] == '\0') {
        print("usage: touch <file>\n");
        return;
    }

    int rc = hanacore::fs::fat32_create_file(arg);
    if (rc == 0) {
        print("touch: ok\n");
    } else {
        print("touch: failed\n");
    }
}
