#include "shell.hpp"
#include "../filesystem/fat32.hpp"
#include "../filesystem/hanafs.hpp"
#include "../userland/elf_loader.hpp"
#include "../drivers/screen.hpp"
#include <stddef.h>
#include <string.h>
#include "../libs/libc.h"
#include "../mem/heap.hpp"
#include "../tty/tty.hpp"

extern "C" {
    void print(const char*);
    char keyboard_poll_char(void);
    void builtin_ls_cmd(const char* path);
    void builtin_lsblk_cmd(const char* arg);
    void builtin_format_cmd(const char* arg);
    void builtin_install_cmd(const char* arg);
    void builtin_mkdir_cmd(const char* arg);
    void builtin_rmdir_cmd(const char* arg);
    void builtin_touch_cmd(const char* arg);
    void builtin_rm_cmd(const char* arg);
    void builtin_fetch_cmd(const char* arg);
    void builtin_cat_cmd(const char* arg);
}

static char cwd[256] = "/";

static void print_prompt() {
    // Use TTY for prompt output so it can be redirected/overridden later
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
            tty_write("Welcome to HanaShell!\n");
            print_prompt();

continue_main_loop:
            while (1) {
                char c = tty_poll_char();

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
                            pos = 0;
                            print_prompt();
                            goto continue_main_loop;
                        }
                        if (i + 1 < pos && buf[i] == '>' && buf[i+1] == '>') {
                            tty_write("Append redirection (>>) is not supported yet\n");
                            pos = 0;
                            print_prompt();
                            goto continue_main_loop;
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

                    if (strcmp(cmd, "ls") == 0) { char path[256]; build_path(path, sizeof(path), arg); builtin_ls_cmd(path); pos=0; print_prompt(); continue; }
                    if (strcmp(cmd, "lsblk") == 0) { builtin_lsblk_cmd(NULL); pos=0; print_prompt(); continue; }
                    if (strcmp(cmd, "format") == 0) { builtin_format_cmd(arg); pos=0; print_prompt(); continue; }
                    if (strcmp(cmd, "install") == 0) { builtin_install_cmd(arg); pos=0; print_prompt(); continue; }
                    if (strcmp(cmd, "mkdir") == 0) { builtin_mkdir_cmd(arg); pos=0; print_prompt(); continue; }
                    if (strcmp(cmd, "rmdir") == 0) { builtin_rmdir_cmd(arg); pos=0; print_prompt(); continue; }
                    if (strcmp(cmd, "touch") == 0) { builtin_touch_cmd(arg); pos=0; print_prompt(); continue; }
                    if (strcmp(cmd, "rm") == 0) { builtin_rm_cmd(arg); pos=0; print_prompt(); continue; }
                    if (strcmp(cmd, "cat") == 0) { char path[256]; build_path(path, sizeof(path), arg); builtin_cat_cmd(path); pos=0; print_prompt(); continue; }
                    if (strcmp(cmd, "pwd") == 0) { tty_write(cwd); tty_write("\n"); pos=0; print_prompt(); continue; }
                    if (strcmp(cmd, "clear") == 0) { clear_screen(); pos=0; print_prompt(); continue; }
            		if (strcmp(cmd, "echo") == 0) { if(arg && *arg) tty_write(arg); tty_write("\n"); pos=0; print_prompt(); continue; }
					if (strcmp(cmd, "help") == 0) {
						print("HanaShell built-in commands:\n");
						print("  cd <path>        Change directory\n");
						print("  ls [path]       List directory contents\n");
						print("  lsblk           List block devices\n");
						print("  format <dev>    Format device (e.g., 0:)\n");
						print("  install <src>   Install OS from FAT32 path\n");
						print("  mkdir <path>    Create directory\n");
						print("  rmdir <path>    Remove directory\n");
                        print("  touch <file>    Create empty file\n");
                        print("  rm <file>       Remove file\n");
                        print("  cat <file>      Print file contents\n");
						print("  pwd             Print working directory\n");
						print("  clear           Clear the screen\n");
						print("  echo <text>     Print text to console\n");
						print("  help            Show this help message\n"); pos=0; print_prompt(); continue;
					}


                    // Execute /bin/<cmd>
                    char fullpath[256];
                    sprintf(fullpath, "/bin/%s", cmd);
                        tty_write("Trying to execute "); tty_write(fullpath); tty_write("\n");
                    size_t fsize = 0;
                        void* data = hanacore::fs::hanafs_get_file_alloc(fullpath, &fsize);
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
