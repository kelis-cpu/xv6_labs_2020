// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[];  // first address after kernel.
                    // defined by kernel.ld.

struct run {
    struct run *next;
};

struct {
    struct spinlock lock;
    struct run *freelist;
} kmem;
// cow code
struct {
    // 防止多CPU同时修改同一物理页的引用数
    struct spinlock lock;
    uint pref_cnt[PHYSTOP / PGSIZE];
} kcow;

void kinit() {
    initlock(&kmem.lock, "kmem");
    initlock(&kcow.lock, "kcow");
    freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end) {
    char *p;
    p = (char *)PGROUNDUP((uint64)pa_start);
    for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE) {
        kcow.pref_cnt[(uint64)p / PGSIZE] = 1;
        kfree(p);
    }
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {
    struct run *r;

    if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    // cow code
    acquire(&kcow.lock);
    if (--kcow.pref_cnt[(uint64)pa / PGSIZE] != 0) {
        release(&kcow.lock);
        return;
    }
    release(&kcow.lock);
    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    r = (struct run *)pa;

    acquire(&kmem.lock);
    r->next = kmem.freelist;
    kmem.freelist = r;
    release(&kmem.lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void) {
    struct run *r;

    acquire(&kmem.lock);
    r = kmem.freelist;
    if (r) {
        kmem.freelist = r->next;
        acquire(&kcow.lock);
        kcow.pref_cnt[(uint64)r / PGSIZE] = 1;
        release(&kcow.lock);
    }
    release(&kmem.lock);

    if (r) memset((char *)r, 5, PGSIZE);  // fill with junk
    return (void *)r;
}

// cow code
// 判断虚拟地址是否位于cow page
int check_cowpage(pagetable_t pgtb, uint64 va) {
    if (va >= MAXVA) return -1;
    pte_t *pte = walk(pgtb, va, 0);
    if (pte == 0) return -1;
    if ((*pte & PTE_V) == 0) return -1;
    return *pte & PTE_COW ? 0 : -1;
}
// 复制cow page，并返回复制的物理页的物理地址
void *alloc_cowpage(pagetable_t pgtb, uint64 va) {
    if (va % PGSIZE != 0) return 0;
    pte_t *pte = walk(pgtb, va, 0);
    uint64 pa = walkaddr(pgtb, va);
    if (pa == 0) return 0;
    uint flags = PTE_FLAGS(*pte);
    flags = (flags | PTE_W) & ~PTE_COW;
    // cow page引用为1，只需要更改读写位、cow位
    if (kcow.pref_cnt[pa / PGSIZE] == 1) {
        *pte = PA2PTE(pa) | flags;
        return (void *)pa;
    } else {
        char *mem = kalloc();
        if (mem == 0) return 0;
        memmove(mem, (char *)pa, PGSIZE);
        // va已经映射到pa，需要先将PTE_V位清除，否则会remap
        *pte &= ~PTE_V;
        if (mappages(pgtb, va, PGSIZE, (uint64)mem, flags) != 0) {
            kfree(mem);
            //*pte |= PTE_V;
            return 0;
        }
        // 减少cow page的引用
        kfree((char *)PGROUNDDOWN(pa));
        return mem;
    }
    return 0;
}