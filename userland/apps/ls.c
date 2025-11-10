#include "../hanaapi.h"
#include <string.h>

/* Simple `hls` for HanaCore: list directory entries using hana_opendir/hana_readdir
 * Usage: hls [path]
 */

int main(int argc, char **argv) {
    const char *path = ".";
    if (argc > 1) path = argv[1];

    hana_dir_t *d = hana_opendir(path);
    if ((long)d < 0) {
        const char *err = "hls: cannot open directory\n";
        size_t errlen = 0;
        while (err[errlen]) ++errlen;
        hana_write(2, err, errlen);
        hana_exit(1);
    }

    hana_dirent *ent;
    while ((ent = hana_readdir(d)) != (hana_dirent*)0) {
    size_t n = 0;
    while (n < sizeof(ent->d_name) && ent->d_name[n]) ++n;
        if (n == 0) continue; /* skip empty names */
        hana_write(1, ent->d_name, n);
        hana_write(1, "\n", 1);
    }

    hana_closedir(d);
    hana_exit(0);
}
