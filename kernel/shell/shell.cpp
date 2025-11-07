#include "shell.hpp"
#include "../filesystem/fat32.hpp"
#include "../userland/elf_loader.hpp"
#include "../drivers/screen.hpp"
#include <stddef.h>
#include <string.h>
#include "../libs/libc.h"
#include "../mem/heap.hpp"

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
}

static char cwd[256] = "/";
static char current_drive = '0';

static void print_prompt() {
    char d[3] = { current_drive, ':', '\0' };
    print(d);
    print(cwd);
    print("$ ");
}

static void build_path(char* out, size_t out_size, const char* arg) {
    size_t pos = 0;
    out[pos++] = current_drive;
    out[pos++] = ':';

    if (!arg || *arg == '\0') {
        size_t i = 0;
        while (i + pos < out_size - 1 && cwd[i]) out[pos++] = cwd[i++];
        out[pos] = '\0';
        return;
    }

    if (((arg[0] >= '0' && arg[0] <= '9')) && arg[1] == ':') {
        char d = arg[0]; 
        out[0] = d; out[1] = ':'; pos = 2;
        size_t i = 2;
        while (pos + i < out_size - 1 && arg[i]) out[pos + i] = arg[i++];
        out[pos + i] = '\0';
        return;
    }

    if (arg[0] == '/') {
        size_t i = 0;
        while (pos + i < out_size - 1 && arg[i]) out[pos + i] = arg[i++];
        out[pos + i] = '\0';
        return;
    }

    size_t len = 0;
    while (cwd[len]) ++len;
    for (size_t i = 0; i < len && pos + i < out_size - 1; ++i) out[pos + i] = cwd[i];
    pos += len;
    if (pos > 2 && out[pos - 1] != '/' && pos < out_size - 1) out[pos++] = '/';
    size_t i = 0;
    while (pos + i < out_size - 1 && arg[i]) { out[pos + i] = arg[i]; ++i; }
    out[pos + i] = '\0';
}

namespace hanacore {
    namespace shell {
        void shell_main(void) {
            char buf[128];
            size_t pos = 0;
            print("Welcome to HanaShell!\n");
            print_prompt();

continue_main_loop:
            while (1) {
                char c = keyboard_poll_char();

                if (c == '\n' || c == '\r') {
                    buf[pos] = '\0';
                    print("\n");
                    if (pos == 0) { print_prompt(); continue; }

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
                            print("Piping is not supported yet\n");
                            pos = 0;
                            print_prompt();
                            goto continue_main_loop;
                        }
                        if (i + 1 < pos && buf[i] == '>' && buf[i+1] == '>') {
                            print("Append redirection (>>) is not supported yet\n");
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
                            strncpy(cwd, &tmp[2], sizeof(cwd) - 1);
                            cwd[sizeof(cwd)-1] = '\0';
                            if (strlen(cwd) == 0) strcpy(cwd, "/");
                            if (arg[1] == ':') current_drive = (arg[0] >= 'a') ? arg[0] - 'a' + 'A' : arg[0];
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
                    if (strcmp(cmd, "pwd") == 0) { char path[260]; path[0]=current_drive; path[1]=':'; strncpy(&path[2], cwd, sizeof(path)-3); path[sizeof(path)-1]='\0'; print(path); print("\n"); pos=0; print_prompt(); continue; }
                    if (strcmp(cmd, "clear") == 0) { clear_screen(); pos=0; print_prompt(); continue; }
                    if (strcmp(cmd, "echo") == 0) { if(arg && *arg) print(arg); print("\n"); pos=0; print_prompt(); continue; }

                    // Execute /bin/<cmd>
                    char fullpath[256];
                    sprintf(fullpath, "/bin/%s", cmd);
                    print("Trying to execute "); print(fullpath); print("\n");
                    size_t fsize = 0;
                    void* data = hanacore::fs::fat32_get_file_alloc(fullpath, &fsize);
                    if (data) {
                        print("Loaded file from FAT32 (size: ");
                        char numbuf[32]; size_t n=0; size_t tmp=fsize;
                        if(tmp==0){n=1;numbuf[0]='0';}
                        while(tmp>0 && n<sizeof(numbuf)-1){numbuf[n++]='0'+tmp%10; tmp/=10;}
                        for(size_t i=0;i<n/2;i++){char t=numbuf[i]; numbuf[i]=numbuf[n-1-i]; numbuf[n-1-i]=t;}
                        numbuf[n]='\0'; print(numbuf); print(")\n");

                        void* entry = elf64_load_from_memory(data,fsize);
                        if(entry){ void (*e)(void)=(void(*)(void))entry; e(); print("Returned from ELF program\n"); }
                        else { print("ELF load failed\n"); }
                    } else { print("File not found in rootfs: "); print(fullpath); print("\n"); }

                    pos = 0;
                    print_prompt();
                } else {
                    if (c=='\b'){if(pos>0){--pos; print("\b \b");}}
                    else if(c>=32){if(pos<sizeof(buf)-1){buf[pos++]=c; char tmp[2]={c,'\0'}; print(tmp);}}
                }
            }
        }
    }
}
