/*
 * @Author: kelise
 * @Date: 2023-05-12 22:11:48
 * @LastEditors: error: git config user.name & please set dead value or install git
 * git git git git git git git git git git git git git git git git git git git
 * git git git git git
 * @LastEditTime: 2023-05-13 04:19:01
 * @Description: file content
 */
#include "kernel/types.h"
#include "user/user.h"

int read_first(int *fd, int *first) {
    if (read(fd[0], first, sizeof(int)) == sizeof(int)) {
        fprintf(1, "prime %d\n", *first);
        return 1;
    }
    return 0;
}

void transport_data(int *l_pipe, int *r_pipe, int first) {
    int data;

    while (read(l_pipe[0], &data, sizeof(int)) == sizeof(int)) {
        if (data % first) write(r_pipe[1], &data, sizeof(int));
    }

    close(l_pipe[0]);
    close(r_pipe[1]);
}

void primes(int *l_pipe) {
    int first;

    if (read_first(l_pipe, &first)) {
        int r_pipe[2];
        pipe(r_pipe);

        transport_data(l_pipe, r_pipe, first);

        if (fork() == 0) {
            primes(r_pipe);
        }
        close(r_pipe[0]);
        wait(0);
        exit(0);
    }
    exit(0);
}

int main(int argc, char const *argv[]) {
    int l_pipe[2];

    pipe(l_pipe);

    // parent process
    for (int i = 2; i <= 35; ++i) {
        write(l_pipe[1], &i, sizeof(int));
    }

    // child process
    if (fork() == 0) {
        close(l_pipe[1]);
        primes(l_pipe);
    }
    close(l_pipe[0]);
    close(l_pipe[1]);
    wait(0);
    exit(0);
}
