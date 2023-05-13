#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fs.h"

void find(char *src, char *dst) {
    char buf[512], *p;
    int fd;
    struct dirent de;
    struct stat st;

    if ((fd = open(src, 0)) < 0) {
        fprintf(2, "find: cannot open %s\n", src);
        return;
    }

    if (fstat(fd, &st) < 0) {
        fprintf(2, "find: cannot stat %s\n", src);
        close(fd);
        return;
    }

    if (st.type != T_DIR) {
        fprintf(2, "the first arg must be directory\n");
        return;
    }
    // directory too long
    if (strlen(src) + 1 + DIRSIZ + 1 > sizeof buf) {
        fprintf(2, "find: path too long\n");
        return;
    }
    strcpy(buf, src);
    p = buf + strlen(buf);
    *p++ = '/';

    while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0) continue;

        memmove(p, de.name, DIRSIZ);

        p[DIRSIZ] = 0;

        if (stat(buf, &st) < 0) {
            fprintf(2, "find: cannot stat %s\n", buf);
            continue;
        }

        if (st.type == T_DIR && strcmp(p, ".") != 0 && strcmp(p, "..") != 0) {
            find(buf, dst);
        } else if (strcmp(dst, p) == 0) {
            printf("%s\n", buf);
        }
    }
    close(fd);
}

int main(int argc, char *argv[]) {
    // command format: find [src] [dst]
    if (argc < 3) fprintf(2, "find [src] [dst]\n");
    find(argv[1], argv[2]);
    exit(0);
}