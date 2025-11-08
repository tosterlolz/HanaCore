#include "shell.hpp"
#include "../filesystem/ext3.hpp"
#include "../filesystem/hanafs.hpp"
#include "../filesystem/ramfs.hpp"
#include "../filesystem/vfs.hpp"
#include "../filesystem/fat32.hpp"
#include "../userland/elf_loader.hpp"
#include "../drivers/screen.hpp"
#include "../scheduler/scheduler.hpp"
#include "../userland/users.hpp"
#include <stddef.h>
#include <string.h>
#include <ctype.h>
#include "../libs/libc.h"
#include "../mem/heap.hpp"
#include "../tty/tty.hpp"

extern "C" {
    void print(const char*);
    char keyboard_poll_char(void);
    void builtin_ls_cmd(const char* path);
    void builtin_lsblk_cmd(const char* arg);
    void builtin_install_cmd(const char* arg);
    void builtin_mkdir_cmd(const char* arg);
    void builtin_rmdir_cmd(const char* arg);
    void builtin_touch_cmd(const char* arg);
    void builtin_rm_cmd(const char* arg);
    void builtin_fetch_cmd(const char* arg);
    void builtin_cat_cmd(const char* arg);
    void builtin_mount_cmd(const char* arg);
    void builtin_fs_cmd(const char* arg);
    void builtin_wm_cmd(const char* arg);
    const char* get_current_username();
}

static char cwd[256] = "/";

// Simple registry for shell builtins that will be spawned as separate tasks.
typedef void (*builtin_func_t)(const char*);
struct builtin_reg { char name[32]; builtin_func_t func; };
static builtin_reg g_builtin_table[32];
static int g_builtin_count = 0;

extern "C" void register_shell_cmd(const char* name, void (*func)(const char*)) {
    if (!name || !func) return;
    if (g_builtin_count >= (int)(sizeof(g_builtin_table)/sizeof(g_builtin_table[0]))) return;
    int i = g_builtin_count++;
    int j = 0; while (j + 1 < (int)sizeof(g_builtin_table[i].name) && name[j]) { g_builtin_table[i].name[j] = name[j]; ++j; }
    g_builtin_table[i].name[j] = '\0';
    g_builtin_table[i].func = func;
}

// Spawn a registered builtin by name. Returns pid (>0) on success or -1.
extern "C" int spawn_registered_cmd(const char* name, const char* arg);

extern "C" void shell_builtin_wrapper(void* v);

extern "C" int spawn_registered_cmd(const char* name, const char* arg) {
    if (!name) return -1;
    builtin_func_t fn = NULL;
    for (int i = 0; i < g_builtin_count; ++i) {
        if (strcmp(g_builtin_table[i].name, name) == 0) { fn = g_builtin_table[i].func; break; }
    }
    if (!fn) return -1;

    struct ctx { builtin_func_t f; char* a; };
    struct ctx* c = (struct ctx*)hanacore::mem::kmalloc(sizeof(struct ctx));
    if (!c) return -1;
    c->f = fn;
    if (arg) {
        size_t L = strlen(arg) + 1;
        c->a = (char*)hanacore::mem::kmalloc(L);
        if (!c->a) { hanacore::mem::kfree(c); return -1; }
        for (size_t k = 0; k < L; ++k) c->a[k] = arg[k];
    } else c->a = NULL;

    int pid = hanacore::scheduler::create_task_with_arg((void(*)(void*))shell_builtin_wrapper, (void*)c);
    return pid > 0 ? pid : -1;
}

extern "C" void shell_builtin_wrapper(void* v) {
    if (!v) return;
    struct { builtin_func_t f; char* a; } *c = (decltype(c))v;
    if (c->f) c->f(c->a);
    if (c->a) hanacore::mem::kfree(c->a);
    hanacore::mem::kfree(c);
    if (hanacore::scheduler::current_task) hanacore::scheduler::current_task->state = hanacore::scheduler::TASK_DEAD;
    hanacore::scheduler::sched_yield();
}

// mount builtin implementation
static void build_path(char* out, size_t out_size, const char* arg);
extern "C" void builtin_mount_cmd(const char* arg) {
    if (!arg || *arg == '\0') {
        print("Usage: mount <src> <dst>\n");
        return;
    }
    // parse src and dst
    const char* s = arg;
    // skip leading spaces
    while (*s == ' ') ++s;
    const char* sp = s;
    while (*sp && *sp != ' ') ++sp;
    size_t slen = (size_t)(sp - s);
    if (slen == 0) { print("Usage: mount <src> <dst>\n"); return; }
    // skip spaces to dst
    const char* d = sp;
    while (*d == ' ') ++d;
    if (!*d) { print("Usage: mount <src> <dst>\n"); return; }

    char src[128]; size_t i=0;
    for (; i < sizeof(src)-1 && i < slen; ++i) {
        src[i] = s[i];
    }
    src[i] = '\0';

    char dst[256]; // expand relative to cwd
    // build_path expects the argument token (relative or absolute)
    build_path(dst, sizeof(dst), d);

    // Basic heuristics: cdrom -> hanafs ISO mount (delegate to hanafs::fs::mount)
    if (strncmp(src, "/dev/cdrom", 9) == 0 || strstr(src, "cdrom") != NULL) {
        print("Mounting CD-ROM via HanaFS ISO mount...\n");
    int rc = ramfs_mount_iso_drive(1, dst);
        if (rc == 0) print("Mounted CD-ROM to "); else print("Mount failed: ");
        print(dst); print("\n");
        return;
    }

    // ATA/SD devices -> try FAT32 mount first (legacy rootfs), then ext3 stub.
    if (strncmp(src, "/dev/sda", 8) == 0 || strncmp(src, "/dev/hda", 8) == 0 || strstr(src, "sda") != NULL) {
        print("Attempting FAT32 mount from ATA...\n");
    int rc = fat32_mount_ata_master(0);
        if (rc == 0) {
            // Register the vfs mount so the mounted filesystem appears at dst
            hanacore::fs::register_mount("fat32", dst);
            print("Mounted FAT32 device to "); print(dst); print("\n");
            return;
        }
        // Fallback to ext3 (stub) if FAT32 mount failed
        print("FAT32 mount failed, trying ext3 (stub)...\n");
        rc = ext3::mount(0, dst);
        if (rc == 0) print("Mounted device to "); else print("Mount failed: ");
        print(dst); print("\n");
        return;
    }

    // Fallback: attempt hanafs ISO mount with numeric drive if given like '1'
    if (isdigit((unsigned char)src[0])) {
        int drv = src[0] - '0';
    int rc = ramfs_mount_iso_drive(drv, dst);
    if (rc == 0) {
        // ISO was mounted into HanaFS under the mountpoint; register it with VFS
        hanacore::fs::register_mount("hanafs", dst);
    }
        if (rc == 0) print("Mounted drive to "); else print("Mount failed: ");
        print(dst); print("\n");
        return;
    }

    print("mount: unsupported source or filesystem (supported: /dev/cdrom, /dev/sda*)\n");
}

static void print_prompt() {
    // Use TTY for prompt output so it can be redirected/overridden later
    const char* username = get_current_username();
    if (username && username[0]) {
        tty_write(username);
        tty_write("@hana:");
    }
    tty_write(cwd);
    tty_write("$ ");
}

static void build_path(char* out, size_t out_size, const char* arg) {
    // If no arg, return cwd. If arg starts with '/', it's absolute.
    if (!out || out_size == 0) return;
    if (!arg || *arg == '\0') {
        size_t i = 0;
        while (i + 1 < out_size && cwd[i]) { out[i] = cwd[i]; ++i; }
        out[i] = '\0';
        return;
    }
    if (arg[0] == '/') {
        size_t i = 0;
        while (i + 1 < out_size && arg[i]) { out[i] = arg[i]; ++i; }
        out[i] = '\0';
        return;
    }
    // relative path: join cwd and arg
    size_t len = 0;
    while (cwd[len]) ++len;
    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 1 < out_size; ++i) out[pos++] = cwd[i];
    if (pos == 0) { if (pos + 1 < out_size) out[pos++] = '/'; }
    if (pos > 0 && out[pos-1] != '/' && pos + 1 < out_size) out[pos++] = '/';
    size_t i = 0;
    while (pos + i + 1 < out_size && arg[i]) { out[pos + i] = arg[i]; ++i; }
    out[pos + i] = '\0';
}

// Simple persistent history: keep a small in-memory ring and flush to
// ATA master as /.hcshhistory after each entered command. This is a
// minimal, robust approach that avoids complex filesystem state.
static const size_t HCSH_HIST_MAX = 64;
static const size_t HCSH_LINE_LEN = 128;
static char hcsh_history[HCSH_HIST_MAX][HCSH_LINE_LEN];
static size_t hcsh_hist_count = 0;
static size_t hcsh_hist_head = 0; // next write position (ring)

// History browsing state: -1 when not browsing, otherwise index into
// 0..(hcsh_hist_count-1) where 0 is the oldest entry.
static int hcsh_hist_pos = -1;
// Saved edit buffer when entering browsing so it can be restored
static char hcsh_saved_buf[HCSH_LINE_LEN];
static size_t hcsh_saved_pos = 0;
static int hcsh_saved_has = 0;

static void hcsh_append_history(const char* line) {
    if (!line) return;
    // store into ring buffer
    size_t i = 0;
    while (i + 1 < HCSH_LINE_LEN && line[i]) { hcsh_history[hcsh_hist_head][i] = line[i]; ++i; }
    hcsh_history[hcsh_hist_head][i] = '\0';
    hcsh_hist_head = (hcsh_hist_head + 1) % HCSH_HIST_MAX;
    if (hcsh_hist_count < HCSH_HIST_MAX) ++hcsh_hist_count;

    // Serialize and write to disk (overwrite file each time)
    size_t total = 0;
    size_t cap = hcsh_hist_count * (HCSH_LINE_LEN + 1) + 16;
    char* buf = (char*)hanacore::mem::kmalloc(cap);
    if (!buf) return;
    size_t pos = 0;
    // oldest entry index
    size_t start = (hcsh_hist_count == HCSH_HIST_MAX) ? hcsh_hist_head : 0;
    for (size_t n = 0; n < hcsh_hist_count; ++n) {
        size_t idx = (start + n) % HCSH_HIST_MAX;
        size_t j = 0;
        while (j + pos + 1 < cap && hcsh_history[idx][j]) { buf[pos++] = hcsh_history[idx][j]; ++j; }
        if (pos < cap) buf[pos++] = '\n';
    }
    total = pos;

        // Write history into HanaFS (in-memory) so the built-in shell can persist
        // history without relying on FAT/ATA. HanaFS is initialized at boot.
        hanacore::fs::hanafs_write_file("/hcsh_history", buf, total);
    hanacore::mem::kfree(buf);
}

// Get history entry by logical index 0..(hcsh_hist_count-1). Returns
// pointer to internal buffer or NULL if out of range.
static const char* hcsh_get_entry_by_index(size_t idx) {
    if (hcsh_hist_count == 0 || idx >= hcsh_hist_count) return NULL;
    size_t start = (hcsh_hist_count == HCSH_HIST_MAX) ? hcsh_hist_head : 0;
    size_t real = (start + idx) % HCSH_HIST_MAX;
    return hcsh_history[real];
}

// Redraw current input buffer: return cursor to line start, print prompt,
// clear to end of line, then write provided buffer.
static void hcsh_redraw_input(const char* buf, size_t pos) {
    // Carriage return + prompt, then clear to EOL using ANSI ESC[K
    tty_write("\r");
    print_prompt();
    tty_write("\x1b[K");
    if (buf && pos > 0) {
        // ensure null-terminated for tty_write
        char tmp[HCSH_LINE_LEN + 1];
        size_t copy = (pos < HCSH_LINE_LEN) ? pos : (HCSH_LINE_LEN - 1);
        for (size_t i = 0; i < copy; ++i) tmp[i] = buf[i];
        tmp[copy] = '\0';
        tty_write(tmp);
    }
}

namespace hanacore {
    namespace shell {
        void shell_main(void) {
            char buf[128];
            size_t pos = 0;
            // Initialize TTY and greet
            tty_init();
            tty_write("Welcome to HanaCore built-in shell! if you see this, the /bin/hcsh could not start!\n");
            // Register built-in commands to be spawned as tasks (do it once)
            static int builtins_registered = 0;
            if (!builtins_registered) {
                register_shell_cmd("ls", builtin_ls_cmd);
                register_shell_cmd("lsblk", builtin_lsblk_cmd);
                register_shell_cmd("install", builtin_install_cmd);
                register_shell_cmd("mkdir", builtin_mkdir_cmd);
                register_shell_cmd("rmdir", builtin_rmdir_cmd);
                register_shell_cmd("touch", builtin_touch_cmd);
                register_shell_cmd("rm", builtin_rm_cmd);
                register_shell_cmd("cat", builtin_cat_cmd);
                register_shell_cmd("mount", builtin_mount_cmd);
                register_shell_cmd("fs", builtin_fs_cmd);
                register_shell_cmd("wm", builtin_wm_cmd);
                builtins_registered = 1;
            }
            print_prompt();

            while (1) {
                char c = tty_poll_char();
                
                if (c == 0) continue;

                if (c == '\n' || c == '\r') {
                    buf[pos] = '\0';
                    tty_write("\n");
                    if (pos == 0) { print_prompt(); continue; }

                    // Append entered command to history and persist
                    hcsh_append_history(buf);
                    // Reset browsing state after a new entry
                    hcsh_hist_pos = -1;
                    hcsh_saved_has = 0;

                    size_t cmdlen = 0;
                    while (cmdlen < pos && buf[cmdlen] != ' ') ++cmdlen;
                    char cmd[32];
                    if (cmdlen >= sizeof(cmd)) cmdlen = sizeof(cmd) - 1;
                    for (size_t i = 0; i < cmdlen; ++i) cmd[i] = buf[i];
                    cmd[cmdlen] = '\0';
                    const char* arg = (cmdlen + 1 < pos) ? &buf[cmdlen + 1] : NULL;

                    // Simple detection for piping / redirection tokens
                    for (size_t i = 0; i < pos; ++i) {
                        if (buf[i] == '|') {
                            tty_write("Piping is not supported yet\n");
                        }
                    }

                    // Commands
                    if (strcmp(cmd, "cd") == 0) {
                        if (!arg || *arg == '\0') { cwd[0] = '/'; cwd[1] = '\0'; }
                        else if (arg[0] == '.' && arg[1] == '.' && (arg[2] == '\0' || arg[2] == '/')) {
                            size_t len = strlen(cwd);
                            if (len > 1) {
                                size_t p = len - 1;
                                while (p > 0 && cwd[p] != '/') --p;
                                cwd[p] = '\0';
                                if (p == 0) { cwd[0] = '/'; cwd[1] = '\0'; }
                            }
                        } else {
                            char tmp[256];
                            build_path(tmp, sizeof(tmp), arg);
                            // ensure we copy the whole path
                            strncpy(cwd, tmp, sizeof(cwd) - 1);
                            cwd[sizeof(cwd)-1] = '\0';
                            if (strlen(cwd) == 0) strcpy(cwd, "/");
                        }
                        pos = 0;
                        print_prompt();
                        continue;
                    }

                    // Helper macro to spawn a command and wait for it to complete
                    // Supports Ctrl+C to kill the spawned task
                    #define SPAWN_CMD_WAIT(name, arg_val) do { \
                    int cmd_pid = spawn_registered_cmd(name, arg_val); \
                    if (cmd_pid > 0) { \
                        while (cmd_pid > 0) { \
                            hanacore::scheduler::Task* t = hanacore::scheduler::find_task_by_pid(cmd_pid); \
                            if (!t || t->state == hanacore::scheduler::TASK_DEAD) { \
                                break; \
                            } \
                            char _c = tty_poll_char(); \
                            if (_c == 3) { tty_write("^C\n"); hanacore::scheduler::kill_task(cmd_pid); break; } \
                            hanacore::scheduler::schedule_next(); \
                        } \
                    } \
                    pos = 0; \
                    print_prompt(); \
                    /* Instead of continue, restart the shell loop */ \
                    shell_main(); \
                    return; \
                } while(0);

                    if (strcmp(cmd, "ls") == 0) { 
                        char path[256]; 
                        build_path(path, sizeof(path), arg); 
                        SPAWN_CMD_WAIT("ls", path);
                    }
                    if (strcmp(cmd, "lsblk") == 0) {
                        SPAWN_CMD_WAIT("lsblk", arg);
                    }
                    if (strcmp(cmd, "format") == 0) { 
                        SPAWN_CMD_WAIT("format", arg);
                    }
                    if (strcmp(cmd, "install") == 0) { 
                        SPAWN_CMD_WAIT("install", arg);
                    }
                    if (strcmp(cmd, "mkdir") == 0) { 
                        SPAWN_CMD_WAIT("mkdir", arg);
                    }
                    if (strcmp(cmd, "rmdir") == 0) { 
                        SPAWN_CMD_WAIT("rmdir", arg);
                    }
                    if (strcmp(cmd, "touch") == 0) { 
                        SPAWN_CMD_WAIT("touch", arg);
                    }
                    if (strcmp(cmd, "rm") == 0) { 
                        SPAWN_CMD_WAIT("rm", arg);
                    }
                    if (strcmp(cmd, "fs") == 0) { builtin_fs_cmd(arg); pos=0; print_prompt(); continue; }
                    if (strcmp(cmd, "cat") == 0) { 
                        char path[256]; 
                        build_path(path, sizeof(path), arg); 
                        SPAWN_CMD_WAIT("cat", path);
                    }
                    if (strcmp(cmd, "mount") == 0) { 
                        SPAWN_CMD_WAIT("mount", arg);
                    }
                    if (strcmp(cmd, "pwd") == 0) { tty_write(cwd); tty_write("\n"); pos=0; print_prompt(); continue; }
                    if (strcmp(cmd, "clear") == 0) { clear_screen(); pos=0; print_prompt(); continue; }
            		if (strcmp(cmd, "echo") == 0) { if(arg && *arg) tty_write(arg); tty_write("\n"); pos=0; print_prompt(); continue; }
					if (strcmp(cmd, "help") == 0) {
						print("HanaShell built-in commands:\n");
						print("  cd <path>          Change directory\n");
						print("  ls [path]          List directory contents\n");
						print("  lsblk              List block devices\n");
						print("  fs <fs> <mnt>s     Format device (e.g., 0:)\n");
						print("  install <src>      Install OS from FAT32 path\n");
						print("  mkdir <path>       Create directory\n");
						print("  rmdir <path>       Remove directory\n");
                        print("  touch <file>       Create empty file\n");
                        print("  rm <file>          Remove file\n");
                        print("  cat <file>         Print file contents\n");
						print("  pwd                Print working directory\n");
						print("  clear              Clear the screen\n");
						print("  echo <text>        Print text to console\n");
                        print("  mount <src> <dst>  Mount filesystem from source to destination\n");
                        print("  fs <cmd> [args]    Filesystem management (mount|list|format|info)\n");
                        print("  wm                 Start a simple window manager\n");
						print("  help               Show this help message\n"); pos=0; print_prompt(); continue;
					}
                    if (strcmp(cmd, "wm") == 0) {
                        builtin_wm_cmd(arg);
                        pos=0; print_prompt(); continue;
                    }


                    // Execute /bin/<cmd>
                    char fullpath[256];
                    sprintf(fullpath, "/bin/%s", cmd);
                    tty_write("Trying to execute "); tty_write(fullpath); tty_write("\n");
                    size_t fsize = 0;
                    void* data = ::vfs_get_file_alloc(fullpath, &fsize);
                    if (data) {
                        tty_write("Loaded file from FAT32 (size: ");
                        char numbuf[32]; size_t n=0; size_t tmp=fsize;
                        if(tmp==0){n=1;numbuf[0]='0';}
                        while(tmp>0 && n<sizeof(numbuf)-1){numbuf[n++]='0'+tmp%10; tmp/=10;}
                        for(size_t i=0;i<n/2;i++){char t=numbuf[i]; numbuf[i]=numbuf[n-1-i]; numbuf[n-1-i]=t;}
                        numbuf[n]='\0'; tty_write(numbuf); tty_write(")\n");

                        void* entry = elf64_load_from_memory(data,fsize);
                        if(entry){ void (*e)(void)=(void(*)(void))entry; e(); tty_write("Returned from ELF program\n"); }
                        else { tty_write("ELF load failed\n"); }
                    } else { tty_write("File not found in rootfs: "); tty_write(fullpath); tty_write("\n"); }

                    pos = 0;
                    print_prompt();
                } else {
                    if (c=='\b'){
                        if(pos>0){--pos; tty_write("\b \b");}
                    } else if (c == 12) { // Ctrl+L -> clear screen
                        clear_screen();
                        pos = 0;
                        print_prompt();
                    } else if (c == 27) {
                        // Escape sequence: expect '[' or 'O' then 'A' (up) or 'B' (down)
                        char c2 = tty_poll_char();
                        if (c2 == '[' || c2 == 'O') {
                            char c3 = tty_poll_char();
                            if (c3 == 'A') {
                                // Up arrow: move to newer (most recent) then older
                                if (hcsh_hist_count > 0) {
                                    // save current edit buffer on first entry into browsing
                                    if (hcsh_hist_pos == -1 && !hcsh_saved_has) {
                                        size_t si = 0;
                                        while (si + 1 < HCSH_LINE_LEN && si < pos) { hcsh_saved_buf[si] = buf[si]; ++si; }
                                        hcsh_saved_buf[si] = '\0';
                                        hcsh_saved_pos = pos;
                                        hcsh_saved_has = 1;
                                    }
                                    if (hcsh_hist_pos == -1) hcsh_hist_pos = (int)hcsh_hist_count - 1;
                                    else if (hcsh_hist_pos > 0) --hcsh_hist_pos;
                                    const char* h = hcsh_get_entry_by_index((size_t)hcsh_hist_pos);
                                    if (h) {
                                        // copy into input buffer
                                        size_t i = 0;
                                        while (i + 1 < sizeof(buf) && h[i]) { buf[i] = h[i]; ++i; }
                                        pos = i; buf[pos] = '\0';
                                        hcsh_redraw_input(buf, pos);
                                    }
                                }
                            } else if (c3 == 'B') {
                                // Down arrow: move to newer entry or exit browsing
                                if (hcsh_hist_count > 0 && hcsh_hist_pos != -1) {
                                    if ((size_t)hcsh_hist_pos + 1 < hcsh_hist_count) {
                                        ++hcsh_hist_pos;
                                        const char* h = hcsh_get_entry_by_index((size_t)hcsh_hist_pos);
                                        if (h) {
                                            size_t i = 0;
                                            while (i + 1 < sizeof(buf) && h[i]) { buf[i] = h[i]; ++i; }
                                            pos = i; buf[pos] = '\0';
                                            hcsh_redraw_input(buf, pos);
                                        }
                                    } else {
                                        // gone past newest: restore saved buffer if any, otherwise clear
                                        hcsh_hist_pos = -1;
                                        if (hcsh_saved_has) {
                                            size_t i = 0;
                                            while (i + 1 < sizeof(buf) && hcsh_saved_buf[i]) { buf[i] = hcsh_saved_buf[i]; ++i; }
                                            pos = (hcsh_saved_pos < sizeof(buf)) ? hcsh_saved_pos : i;
                                            buf[pos] = '\0';
                                            hcsh_saved_has = 0;
                                        } else {
                                            pos = 0; buf[0] = '\0';
                                        }
                                        hcsh_redraw_input(buf, pos);
                                    }
                                }
                            }
                        }
                    } else if(c>=32){
                        // typing cancels browsing session
                        hcsh_hist_pos = -1;
                        if(pos<sizeof(buf)-1){buf[pos++]=c; char tmp[2]={c,'\0'}; tty_write(tmp);} 
                    }
                }
            }
        }
    }
}
