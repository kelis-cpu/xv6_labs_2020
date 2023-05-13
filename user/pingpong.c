
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char const *argv[]) {
    char buf[20] = {1};
    int fd[2];
    pipe(fd);

    if (fork() == 0) {
        // child process
        if (read(fd[0], buf, sizeof buf) > 0) {
            fprintf(1, "%d: received ping\n", getpid());
            write(fd[1], buf, 1);
            exit(0);
        }
    }
    // parent process
    write(fd[1], buf, 20);
    wait(0);
    if (read(fd[0], buf, sizeof buf) > 0) {
        fprintf(1, "%d: received pong\n", getpid());
        exit(0);
    }
    exit(0);
}
