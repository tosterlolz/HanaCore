#include "commands.hpp"
#include "../utils/logger.hpp"
#include "../userland/users.hpp"
#include "../userland/elf_loader.hpp"
#include "../filesystem/vfs.hpp"
#include "../filesystem/hanafs.hpp"
#include "../filesystem/fat32.hpp"
#include "../mem/heap.hpp"
#include "../scheduler/scheduler.hpp"
#include "../../third_party/limine/limine.h"
#include <cstdio>
#include <cstring>

extern "C" {
    void print(const char*);
    int memcmp(const void* s1, const void* s2, size_t n);
}

extern volatile struct limine_module_request module_request;
extern volatile struct limine_hhdm_request limine_hhdm_request;

namespace hanacore {
namespace shell {
namespace commands {

int cmd_help(const char* args) {
    (void)args;  // unused
    print("\n=== Available Commands ===\n");
    print("help              - Show this help message\n");
    print("exit              - Exit the shell\n");
    print("logout            - Exit the shell\n");
    print("clear             - Clear the screen\n");
    print("echo <text>       - Echo text to screen\n");
    print("whoami            - Display current user info\n");
    print("pwd               - Print working directory\n");
    print("cd <path>         - Change directory (stub)\n");
    print("ls [path]         - List directory contents\n");
    print("lsblk            - List block devices and mounts\n");
    print("version           - Show system version\n");
    print("\n");
    return 0;
}

int cmd_echo(const char* args) {
    if (args && args[0] != '\0') {
        print(args);
        print("\n");
    } else {
        print("\n");
    }
    return 0;
}

int cmd_whoami(const char* args) {
    (void)args;  // unused
    // Get current user from context
    hanacore::users::uid_t uid = hanacore::users::get_current_uid();
    hanacore::users::gid_t gid = hanacore::users::get_current_gid();
    const char* username = hanacore::users::get_username_by_uid(uid);
    
    if (username) {
        print(username);
    } else {
        print("unknown");
    }
    print(" (uid=");
    char uidstr[16];
    snprintf(uidstr, sizeof(uidstr), "%u", uid);
    print(uidstr);
    print(", gid=");
    char gidstr[16];
    snprintf(gidstr, sizeof(gidstr), "%u", gid);
    print(gidstr);
    print(")\n");
    return 0;
}

int cmd_version(const char* args) {
    (void)args;  // unused
    print("HanaCore vI dont remmeber lmao ");
    print(__DATE__);
    print(" ");
    print(__TIME__);
    print("\n");
    return 0;
}

int cmd_pwd(const char* args) {
    (void)args;  // unused
    print("/\n");  // For now, always return root
    return 0;
}

int cmd_cd(const char* args) {
    if (!args || args[0] == '\0') {
        print("cd: missing argument\n");
        return 1;
    }
    print("cd: directory changing not yet implemented\n");
    return 1;
}

int cmd_clear(const char* args) {
    (void)args;  // unused
    print("\033[2J");    // ANSI: clear screen
    print("\033[H");     // ANSI: move cursor to home
    return 0;
}

int cmd_exec_external(const char* cmdname, const char* args) {
    if (!cmdname || cmdname[0] == '\0') return -1;
    
    // Try to find and execute from Limine modules
    if (module_request.response) {
        volatile struct limine_module_response* resp = module_request.response;
        for (uint64_t i = 0; i < resp->module_count; ++i) {
            volatile struct limine_file* mod = resp->modules[i];
            const char* path = (const char*)(uintptr_t)mod->path;
            
            // Convert path using HHDM if needed
            if (path && limine_hhdm_request.response) {
                uint64_t hoff = limine_hhdm_request.response->offset;
                if ((uint64_t)path < hoff) path = (const char*)((uintptr_t)path + hoff);
            }
            if (!path) continue;
            
            // Extract filename from path
            const char* filename = path;
            for (const char* p = path; *p; p++) {
                if (*p == '/') filename = p + 1;
            }
            
            // Check if this module matches the command name
            if (strcmp(filename, cmdname) == 0) {
                // Found matching module - try to load as ELF
                uintptr_t addr = (uintptr_t)mod->address;
                if (limine_hhdm_request.response) {
                    uint64_t off = limine_hhdm_request.response->offset;
                    if ((uint64_t)addr < off) addr = (uintptr_t)(off + addr);
                }
                
                void* entry = elf64_load_from_memory((const void*)addr, (size_t)mod->size);
                if (entry) {
                    void (*entry_fn)(void) = (void(*)(void))entry;
                    entry_fn();
                    return 0;
                } else {
                    print("Failed to load ELF: ");
                    print(cmdname);
                    print("\n");
                    return 1;
                }
            }
        }
    }
    
    // As a fallback, try to execute from /bin/<cmdname> using VFS
    // NOTE: executing user-mode ELF binaries directly from the kernel is unsafe
    // because they expect a user-mode environment (syscalls, stack, TSS, paging).
    // For now, avoid attempting to run binaries loaded from VFS and return a
    // clear error instead of crashing. In the future implement proper exec/spawn.
    if (cmdname && cmdname[0] != '\0') {
        char binpath[256];
        snprintf(binpath, sizeof(binpath), "/bin/%s", cmdname);
        size_t len = 0;
        void* data = ::vfs_get_file_alloc(binpath, &len);
        if (data && len > 0) {
            // Try to load ELF into memory (elf loader uses bump allocator)
            void* entry = elf64_load_from_memory(data, len);
            // free the VFS buffer returned by vfs_get_file_alloc
            hanacore::mem::kfree(data);
            if (!entry) {
                print("Failed to load ELF from ");
                print(binpath);
                print("\n");
                return 1;
            }

            // Create a user task to run the ELF entry. Use a modest user stack.
            const size_t USER_STACK = 16 * 1024;
            int pid = hanacore::scheduler::create_user_task(entry, USER_STACK);
            if (pid == 0) {
                print("Failed to create user task for ");
                print(binpath);
                print("\n");
                return 1;
            }

            char tmp[32];
            snprintf(tmp, sizeof(tmp), "Started pid=%d\n", pid);
            print(tmp);
            return 0;
        }
        if (data) hanacore::mem::kfree(data);
    }

    // Command not found
    print("Command not found: ");
    print(cmdname);
    print("\n");
    return 127;
}

// Helper callback for ls command
static struct {
    int count;
} ls_context = {0};

static void ls_callback(const char* name) {
    if (name) {
        print(name);
        print("\n");
        ls_context.count++;
    }
}

int cmd_ls(const char* args) {
    const char* path = "/";
    
    // Parse arguments
    if (args && args[0] != '\0') {
        path = args;
    }
    
    ls_context.count = 0;
    
    // Use VFS to list the directory
    int rc = ::vfs_list_dir(path, ls_callback);
    
    if (rc != 0) {
        print("Cannot list directory: ");
        print(path);
        print(" (rc=");
        char tmp[16];
        snprintf(tmp, sizeof(tmp), "%d", rc);
        print(tmp);
        print(")\n");
        return 1;
    }
    
    if (ls_context.count == 0) {
        print("(empty directory)\n");
    }
    
    return 0;
}

// Collector callback: store lines into a provided buffer (non-printing)
struct lsblk_collector_t {
    char (*lines)[128];
    int max_lines;
    int count;
};

static void lsblk_collect_cb(const char* line, void* ctx) {
    if (!line || !ctx) return;
    lsblk_collector_t* col = (lsblk_collector_t*)ctx;
    if (col->count >= col->max_lines) return;
    // copy up to 127 chars and NUL-terminate
    int i = 0;
    for (; i + 1 < 128 && line[i]; ++i) col->lines[col->count][i] = line[i];
    col->lines[col->count][i] = '\0';
    col->count++;
}

// Adapter wrappers to match existing C callbacks that don't accept context.
// We'll use small trampoline where necessary: a global pointer used only
// while making the synchronous call into the filesystem.
static lsblk_collector_t* g_lsblk_col = nullptr;
static void lsblk_trampoline_cb(const char* line) {
    if (!g_lsblk_col) return;
    lsblk_collect_cb(line, g_lsblk_col);
}

int cmd_lsblk(const char* args) {
    (void)args;
    print("=== lsblk ===\n");

    // Prepare small collector buffer on stack
    char lines[64][128];
    lsblk_collector_t col;
    col.lines = lines;
    col.max_lines = 64;
    col.count = 0;

    // Collect VFS mounts
    print("-- VFS mounts --\n");
    g_lsblk_col = &col;
    ::vfs_list_mounts(lsblk_trampoline_cb);
    g_lsblk_col = nullptr;

    // FAT32 summary (only if initialized)
    print("-- FAT32 summary --\n");
    if (hanacore::fs::fat32_ready) {
        g_lsblk_col = &col;
        hanacore::fs::fat32_get_summary(lsblk_trampoline_cb);
        g_lsblk_col = nullptr;
    } else {
        // Not initialized => record message
        if (col.count < col.max_lines) {
            snprintf(col.lines[col.count], 128, "(FAT32 not initialized)");
            col.count++;
        }
    }

    // Now print all collected lines (single-threaded, safe)
    for (int i = 0; i < col.count; ++i) {
        print(col.lines[i]);
        print("\n");
    }

    return 0;
}

} // namespace commands
} // namespace shell
} // namespace hanacore
