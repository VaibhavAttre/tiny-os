#pragma once
#include <stdint.h>

#define NOFILE 16   // Max open files per process
#define NFILE  64   // Max open files system-wide

// File types
enum {
    FD_NONE = 0,
    FD_DEVICE,      // console, etc.
    FD_INODE,       // future: disk file
    FD_TREE,        // FS-tree backed file
    FD_PIPE,        // future: pipe
};

// Device operations
struct device {
    int (*read)(int minor, char *dst, int n);
    int (*write)(int minor, char *src, int n);
};

struct inode;

// Open file structure
struct file {
    int type;           // FD_NONE, FD_DEVICE, FD_INODE, etc.
    int ref;            // reference count
    int readable;
    int writable;
    
    // For FD_DEVICE
    int major;
    int minor;
    
    // For FD_INODE
    struct inode *ip;
    uint32_t off;       // file offset

    // For FD_TREE
    uint32_t tree_ino;
};

// File table operations
void fileinit(void);
struct file *filealloc(void);
struct file *filedup(struct file *f);
void fileclose(struct file *f);
int fileread(struct file *f, char *addr, int n);
int filewrite(struct file *f, char *addr, int n);

// Device registration
void devinit(void);
#define CONSOLE 1

// Per-process FD helpers
struct proc;
int fdalloc(struct file *f);  // Allocate FD in current process
void proc_fdinit(struct proc *p);  // Initialize FDs for new process (console on 0/1/2)
