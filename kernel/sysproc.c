/*
 * @Author: kelise
 * @Date: 2023-06-07 11:43:14
 * @LastEditors: kelis-cpu
 * @LastEditTime: 2023-07-02 10:34:58
 * @Description: file content
 */
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"

extern struct {
    struct spinlock lock;
    struct file file[NFILE];
} ftable;

uint64 sys_exit(void) {
    int n;
    if (argint(0, &n) < 0) return -1;
    exit(n);
    return 0;  // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_fork(void) { return fork(); }

uint64 sys_wait(void) {
    uint64 p;
    if (argaddr(0, &p) < 0) return -1;
    return wait(p);
}

uint64 sys_sbrk(void) {
    int addr;
    int n;

    if (argint(0, &n) < 0) return -1;
    addr = myproc()->sz;
    if (growproc(n) < 0) return -1;
    return addr;
}

uint64 sys_sleep(void) {
    int n;
    uint ticks0;

    if (argint(0, &n) < 0) return -1;
    acquire(&tickslock);
    ticks0 = ticks;
    while (ticks - ticks0 < n) {
        if (myproc()->killed) {
            release(&tickslock);
            return -1;
        }
        sleep(&ticks, &tickslock);
    }
    release(&tickslock);
    return 0;
}

uint64 sys_kill(void) {
    int pid;

    if (argint(0, &pid) < 0) return -1;
    return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
    uint xticks;

    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
    return xticks;
}

void *sys_mmap() {
    uint64 addr;
    int length, prot, flags, fd;
    int offset;

    int i;

    struct vma *vma_;

    if (argaddr(0, &addr) < 0 || argint(1, &length) < 0 ||
        argint(2, &prot) < 0 || argint(3, &flags) < 0 || argint(4, &fd) < 0 ||
        argint(5, &offset) < 0) {
        panic("mmap get arg fail\n");
        return MAP_FAILED;
    }

    if (length <= 0) {
        panic("mmap length should > 0\n");
    }
    if (addr != 0) {
        printf("addr is not zero\n");
        return MAP_FAILED;
    }
    struct proc *p = myproc();
    // 寻找空闲vma
    for (i = 0; i < MAXVMA; i++) {
        if (p->vmas[i].used == 0) {
            p->vmas[i].used = 1;
            vma_ = &p->vmas[i];
            break;
        }
    }
    if (i == MAXVMA) {
        printf("no free vma\n");
        return MAP_FAILED;
    }
    if (fd >= NOFILE || !p->ofile[fd]) {
        printf("no open file\n");
        return MAP_FAILED;
    }
    vma_->mapfile = p->ofile[fd];
    // 文件打开为不可写，map不可为可写
    if (vma_->mapfile->writable == 0 && (prot & PROT_WRITE) &&
        (flags & MAP_SHARED))
        return MAP_FAILED;
    // 增加文件引用
    filedup(vma_->mapfile);

    vma_->addr = p->sz;     // 挂载地址
    vma_->length = length;  // 挂载文件长度
    vma_->prot = prot;
    vma_->flags = flags;
    vma_->offset = offset;
    vma_->fd = fd;
    p->sz += length;
    return (void *)vma_->addr;
}
int sys_munmap() {
    uint64 addr;
    int length, i;
    struct proc *p;
    if (argaddr(0, &addr) < 0 || argint(1, &length) < 0) return -1;
    p = myproc();
    for (i = 0; i < MAXVMA; i++) {
        if (p->vmas[i].used && addr >= p->vmas[i].addr &&
            p->vmas[i].length + p->vmas[i].addr >= addr + length) {
            // 写回
            if ((p->vmas[i].prot & PROT_WRITE) &&
                (p->vmas[i].flags & MAP_SHARED)) {
                filewrite(p->vmas[i].mapfile, addr, length);
            }
            uvmunmap(p->pagetable, addr, length / PGSIZE, 1);
            if (length == p->vmas[i].length) {
                // 减少文件引用
                fileclose(p->vmas[i].mapfile);
                p->vmas[i].used = 0;
            }
            return 0;
        }
    }
    return -1;
}