#include "kernel/file.h"
#include "kernel/fs.h"
#include "kernel/fs_tree.h"
#include "kernel/printf.h"
#include <drivers/uart.h>

static struct file ftable[NFILE];

#define NDEV 10
static struct device devsw[NDEV];

static int consoleread(int minor, char *dst, int n);
static int consolewrite(int minor, char *src, int n);

void fileinit(void) {
    for (int i = 0; i < NFILE; i++) {
        ftable[i].type = FD_NONE;
        ftable[i].ref = 0;
    }
}

void devinit(void) {
    devsw[CONSOLE].read = consoleread;
    devsw[CONSOLE].write = consolewrite;
}

struct file *filealloc(void) {
    for (int i = 0; i < NFILE; i++) {
        if (ftable[i].ref == 0) {
            ftable[i].ref = 1;
            ftable[i].type = FD_NONE;
            ftable[i].readable = 0;
            ftable[i].writable = 0;
            ftable[i].major = 0;
            ftable[i].minor = 0;
            ftable[i].ip = 0;
            ftable[i].off = 0;
            ftable[i].tree_ino = 0;
            return &ftable[i];
        }
    }
    return 0;
}

struct file *filedup(struct file *f) {
    if (f->ref < 1) {
        kprintf("filedup: ref < 1\n");
        return 0;
    }
    f->ref++;
    return f;
}

void fileclose(struct file *f) {
    if (f->ref < 1) {
        kprintf("fileclose: ref < 1\n");
        return;
    }

    f->ref--;
    if (f->ref > 0) {
        return;
    }

    int type = f->type;
    struct inode *ip = f->ip;

    f->type = FD_NONE;
    f->ip = 0;

    if (type == FD_INODE && ip) {
        iput(ip);
    }
}

int fileread(struct file *f, char *addr, int n) {
    if (!f->readable) {
        return -1;
    }

    if (f->type == FD_DEVICE) {
        if (f->major < 0 || f->major >= NDEV) {
            return -1;
        }
        if (!devsw[f->major].read) {
            return -1;
        }
        return devsw[f->major].read(f->minor, addr, n);
    }

    if (f->type == FD_INODE) {
        ilock(f->ip);
        int r = readi(f->ip, addr, f->off, n);
        if (r > 0) {
            f->off += r;
        }
        iunlock(f->ip);
        return r;
    }

    if (f->type == FD_TREE) {
        int r = fs_tree_file_read(f->tree_ino, f->off, addr, (uint32_t)n);
        if (r > 0) {
            f->off += (uint32_t)r;
        }
        return r;
    }

    return -1;
}

int filewrite(struct file *f, char *addr, int n) {
    if (!f->writable) {
        return -1;
    }

    if (f->type == FD_DEVICE) {
        if (f->major < 0 || f->major >= NDEV) {
            return -1;
        }
        if (!devsw[f->major].write) {
            return -1;
        }
        return devsw[f->major].write(f->minor, addr, n);
    }

    if (f->type == FD_INODE) {
        ilock(f->ip);
        int r = writei(f->ip, addr, f->off, n);
        if (r > 0) {
            f->off += r;
        }
        iunlock(f->ip);
        return r;
    }

    if (f->type == FD_TREE) {
        kprintf("filewrite: FD_TREE ino=%u off=%u n=%d\n",
                f->tree_ino, f->off, n);
        int r = fs_tree_file_write(f->tree_ino, f->off, addr, (uint32_t)n);
        if (r > 0) {
            f->off += (uint32_t)r;
        }
        return r;
    }

    return -1;
}

static int consoleread(int minor, char *dst, int n) {
    (void)minor;
    int i;
    for (i = 0; i < n; i++) {
        int c = uart_getc();
        if (c < 0) break; // No more input
        dst[i] = (char)c;
        if (c == '\n' || c == '\r') {
            i++;
            break;
        }
    }
    return i;
}

static int consolewrite(int minor, char *src, int n) {
    (void)minor;
    for (int i = 0; i < n; i++) {
        uart_putc(src[i]);
    }
    return n;
}
