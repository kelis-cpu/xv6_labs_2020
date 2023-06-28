/*
 * @Author: kelise
 * @Date: 2023-04-10 17:39:44
 * @LastEditors: kelis-cpu
 * @LastEditTime: 2023-06-28 11:26:31
 * @Description: file content
 */
struct buf {
    int valid;  // has data been read from disk?
    int disk;   // does disk "own" buf?
    uint dev;
    uint blockno;
    struct sleeplock lock;
    uint refcnt;
    struct buf *prev;  // LRU cache list
    struct buf *next;
    uchar data[BSIZE];
    uint used_timestamp;  // 该block上次被使用的时间
};
