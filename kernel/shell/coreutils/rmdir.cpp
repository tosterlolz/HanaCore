#include "../../filesystem/vfs.hpp"

extern "C" void print(const char*);

extern "C" void builtin_rmdir_cmd(const char* arg) {
    if (!arg || arg[0] == '\0') {
        print("usage: rmdir <directory>\n");
        return;
    }

    int rc = hanacore::fs::vfs_remove_dir(arg);
    if (rc == 0) {
        print("rmdir: ok\n");
    } else {
        print("rmdir: failed\n");
    }
}
