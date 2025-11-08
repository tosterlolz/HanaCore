#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/filesystem/vfs.hpp"
#include "../../kernel/filesystem/hanafs.hpp"
#include "../../kernel/filesystem/devfs.hpp"
#include "../../kernel/filesystem/procfs.hpp"
#include "../../kernel/mem/heap.hpp"

extern "C" void print(const char* s) { printf("%s", s); }

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Starting filesystem UBSAN test harness\n");

    // Initialize VFS and HanaFS
    hanacore::fs::vfs_init();
    if (hanacore::fs::hanafs_init() != 0) {
        printf("hanafs_init failed\n");
        return 1;
    }
    hanacore::fs::procfs_init();
    hanacore::fs::devfs_init();

    // Create a test file and read it back via VFS
    const char* path = "/fs_ubsan_test.txt";
    const char* payload = "hello ubsan\n";
    if (hanacore::fs::hanafs_write_file(path, payload, strlen(payload)) != 0) {
        printf("hanafs_write_file failed\n");
        return 1;
    }

    size_t out_len = 0;
    void* data = hanacore::fs::vfs_get_file_alloc(path, &out_len);
    if (!data) {
        printf("vfs_get_file_alloc returned NULL\n");
    } else {
        printf("Read back %zu bytes: %.*s\n", out_len, (int)out_len, (char*)data);
        hanacore::mem::kfree(data);
    }

    // List mounts
    hanacore::fs::vfs_list_mounts([](const char* line){ printf("%s\n", line); });

    printf("filesystem UBSAN test harness completed\n");
    return 0;
}
