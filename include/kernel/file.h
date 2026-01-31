#pragma once
#include <stdint.h>

#define NOFILE 16 // Max open files per process
#define NFILE 64 // Max open files system-wide

enum {
    FD_NONE = 0,
    FD_DEVICE, // console, etc.
    FD_INODE, // future: disk file
    FD_TREE, // FS-tree backed file
    FD_PIPE, // future: pipe
};

struct device {
    int (*read)(int minor, char *dst, int n);
    int (*write)(int minor, char *src, int n);
};

struct inode;

struct file {
    int type; // FD_NONE, FD_DEVICE, FD_INODE, etc.
    int ref; // reference count
    int readable;
    int writable;

    int major;
    int minor;

    struct inode *ip;
    uint32_t off; // file offset

    uint32_t tree_ino;
};

void fileinit(void);
struct file *filealloc(void);
struct file *filedup(struct file *f);
void fileclose(struct file *f);
int fileread(struct file *f, char *addr, int n);
int filewrite(struct file *f, char *addr, int n);

void devinit(void);
#define CONSOLE 1

struct proc;
int fdalloc(struct file *f); // Allocate FD in current process
void proc_fdinit(struct proc *p); // Initialize FDs for new process (console on 0/1/2)
