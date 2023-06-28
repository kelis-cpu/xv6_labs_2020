// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct bbucket {
    struct spinlock lock;
    struct buf head;
};

struct {
    struct buf buf[NBUF];
    struct bbucket bbucket[NBUCKET];
} bcache;

void binit(void) {
    struct buf *b;
    char lockname[10];

    // 初始化每一个bucket的lock
    for (int i = 0; i < NBUCKET; i++) {
        snprintf(lockname, sizeof(lockname), "bcache_%d", i);
        initlock(&bcache.bbucket[i].lock, lockname);
        bcache.bbucket[i].head.prev = &bcache.bbucket[i].head;
        bcache.bbucket[i].head.next = &bcache.bbucket[i].head;
    }

    // initlock(&bcache.lock, "bcache");
    //  头插法,将所有buf都创建到bbucket[0]
    for (b = bcache.buf; b < bcache.buf + NBUF; b++) {
        b->next = bcache.bbucket[0].head.next;
        b->prev = &bcache.bbucket[0].head;
        initsleeplock(&b->lock, "buffer");

        bcache.bbucket[0].head.next->prev = b;
        bcache.bbucket[0].head.next = b;
    }
}
static inline uint buckethash(uint blockno) { return blockno % NBUCKET; }
// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *bget(uint dev, uint blockno) {
    struct buf *b;

    uint bucketid = buckethash(blockno);

    // acquire(&bcache.lock);
    acquire(&bcache.bbucket[bucketid].lock);

    // block已经被缓存
    for (b = bcache.bbucket[bucketid].head.next;
         b != &bcache.bbucket[bucketid].head; b = b->next) {
        if (b->dev == dev && b->blockno == blockno) {
            ++b->refcnt;
            acquire(&tickslock);
            b->used_timestamp = ticks;
            release(&tickslock);
            release(&bcache.bbucket[bucketid].lock);
            acquiresleep(&b->lock);
            return b;
        }
    }

    // 未缓存，LRU淘汰最不经常使用页面
    struct buf *temp;
    b = 0;
    for (int i = 0, id = bucketid; i < NBUCKET; i++, id = (id + 1) % NBUCKET) {
        // 当前遍历的是其他bucket
        if (id != bucketid) {
            acquire(&bcache.bbucket[id].lock);
        }
        // 寻找一个bucket中最近最久未使用的buf
        for (temp = bcache.bbucket[id].head.next;
             temp != &bcache.bbucket[id].head; temp = temp->next) {
            if (temp->refcnt == 0 &&
                (b == 0 || temp->used_timestamp < b->used_timestamp))
                b = temp;
        }
        if (b) {
            // 在其他bucket中找到最近最久未使用buf，需要将其移动到bucketid中
            if (id != bucketid) {
                // 从原bucket中删除
                b->next->prev = b->prev;
                b->prev->next = b->next;
                // 头插法，插入到bucketid中
                b->next = bcache.bbucket[bucketid].head.next;
                b->prev = &bcache.bbucket[bucketid].head;
                bcache.bbucket[bucketid].head.next->prev = b;
                bcache.bbucket[bucketid].head.next = b;
                release(&bcache.bbucket[id].lock);
            }
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            b->refcnt = 1;
            acquire(&tickslock);
            b->used_timestamp = ticks;
            release(&tickslock);
            release(&bcache.bbucket[bucketid].lock);
            acquiresleep(&b->lock);
            return b;
        } else {
            // 该bucket中未找到
            if (id != bucketid) {
                release(&bcache.bbucket[id].lock);
            }
        }
    }

    panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *bread(uint dev, uint blockno) {
    struct buf *b;

    b = bget(dev, blockno);
    if (!b->valid) {
        virtio_disk_rw(b, 0);
        b->valid = 1;
    }
    return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b) {
    if (!holdingsleep(&b->lock)) panic("bwrite");
    virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void brelse(struct buf *b) {
    if (!holdingsleep(&b->lock)) panic("brelse");
    uint bucketid = buckethash(b->blockno);

    releasesleep(&b->lock);

    acquire(&bcache.bbucket[bucketid].lock);
    b->refcnt--;
    acquire(&tickslock);
    b->used_timestamp = ticks;
    release(&tickslock);

    release(&bcache.bbucket[bucketid].lock);
}

void bpin(struct buf *b) {
    uint id = buckethash(b->blockno);
    acquire(&bcache.bbucket[id].lock);
    b->refcnt++;
    release(&bcache.bbucket[id].lock);
}

void bunpin(struct buf *b) {
    uint id = buckethash(b->blockno);
    acquire(&bcache.bbucket[id].lock);
    b->refcnt--;
    release(&bcache.bbucket[id].lock);
}
