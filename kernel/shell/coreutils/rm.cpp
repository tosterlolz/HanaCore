#include "../../filesystem/vfs.hpp"

extern "C" void print(const char*);

extern "C" void builtin_rm_cmd(const char* arg) {
    if (!arg || arg[0] == '\0') {
        print("usage: rm <file>\n");
        return;
    }

    int rc = hanacore::fs::unlink(arg);
    if (rc == 0) {
        print("rm: ok\n");
    } else {
        print("rm: failed\n");
    }
}
