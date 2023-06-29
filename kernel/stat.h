/*
 * @Author: kelise
 * @Date: 2023-05-13 21:53:50
 * @LastEditors: kelis-cpu
 * @LastEditTime: 2023-06-29 17:40:01
 * @Description: file content
 */
#define T_DIR 1      // Directory
#define T_FILE 2     // File
#define T_DEVICE 3   // Device
#define T_SYMLINK 4  // symbolic link

struct stat {
    int dev;      // File system's disk device
    uint ino;     // Inode number
    short type;   // Type of file
    short nlink;  // Number of links to file
    uint64 size;  // Size of file in bytes
};
