/*
 * @Author: kelise
 * @Date: 2023-05-12 19:26:16
 * @LastEditors: error: git config user.name & please set dead value or install git
 * git git git git git git git git git git
 * @LastEditTime: 2023-05-12 21:26:41
 * @Description: file content
 */
#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char const *argv[]) {
    if (argc < 2) {
        fprintf(2, "please pass the sleep time\n");
        exit(-1);
    }

    sleep(atoi(argv[1]));
    exit(0);
}
