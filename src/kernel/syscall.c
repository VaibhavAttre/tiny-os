#include "kernel/syscall.h"
#include "kernel/trapframe.h"
#include "kernel/printf.h"
#include "kernel/sched.h"
#include "kernel/file.h"
#include "kernel/fs.h"
#include "kernel/vm.h"
#include "kernel/string.h"
#include "kernel/kalloc.h"
#include "timer.h"
#include <drivers/uart.h>
#include "riscv.h"
#include "sv39.h"
#include "mmu.h"
#include "kernel/current.h"
#include <stdint.h>
#include "user_test.h"

// Temporary buffer for copying between user/kernel space
#define COPYBUF_SIZE 512
static char copybuf[COPYBUF_SIZE];

static void sys_sleep_ticks(uint64_t t) {

    if(t == 0) return;

    int x = (r_sstatus() & SSTATUS_SIE) != 0;
    sstatus_enable_sie();
    sleep_ticks(t);
    if(!x) sstatus_disable_sie();
}

void syscall_handler(struct trapframe * tf) {

    if (!tf) return;

    uint64_t syscall_num = tf->a7;

    if(syscall_num != SYSCALL_PUTC) {

        //uint32_t x = ((uint32_t)syscall_num << 16) | (uint32_t)(tf->a0 & 0xFFFF);
        //trace_log(TR_SYSCALL, myproc(), 0, x);
        sched_trace_syscall(syscall_num, tf->a0);
    }

    //kprintf("syscall num=%d\n", (int)syscall_num);

    switch(syscall_num) {

        case SYSCALL_PUTC: {
            uart_putc((char)(tf->a0 & 0xFF));
            tf->a0 = 0;
            break;
        }

        case SYSCALL_YIELD: {
            yield_from_trap(0);
            tf->a0 = 0;
            break;
        }

        case SYSCALL_TICKS: {
            tf->a0 = ticks;
            break;
        }

        case SYSCALL_SLEEP: {
            sys_sleep_ticks(tf->a0);
            tf->a0 = 0;
            break;
        }

        case SYSCALL_GETPID: {
            struct proc * p = myproc();
            if(!p) {
                tf->a0 = -1;
            } else {
                tf->a0 = p->id;
            }
            break;
        }

        case SYSCALL_EXIT: {
            proc_exit((int)tf->a0);
            break;
        }

        case SYSCALL_EXEC: {
            struct proc *p = myproc();
            uint64_t upath = tf->a0;
            
            // Copy path from userspace
            char path[128];
            if (copyinstr(p->pagetable, path, upath, sizeof(path)) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            // Open the executable file
            struct inode *ip = namei(path);
            if (ip == 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            ilock(ip);
            
            // Must be a regular file
            if (ip->type != T_FILE) {
                iunlock(ip);
                iput(ip);
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            // Read the entire file into a buffer
            uint32_t sz = ip->size;
            if (sz == 0 || sz > 64 * 1024) {  // Limit to 64KB
                iunlock(ip);
                iput(ip);
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            // Allocate buffer for file contents
            uint8_t *buf = (uint8_t*)kalloc();
            if (!buf) {
                iunlock(ip);
                iput(ip);
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            // For files larger than one page, need multiple pages
            // For now, just support files up to 4KB
            if (sz > PGSIZE) {
                kfree(buf);
                iunlock(ip);
                iput(ip);
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            int n = readi(ip, buf, 0, sz);
            iunlock(ip);
            iput(ip);
            
            if (n != (int)sz) {
                kfree(buf);
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            // Execute it
            int r = proc_exec(p, buf, sz);
            kfree(buf);
            
            tf->a0 = (r < 0) ? (uint64_t)-1 : 0;
            break;
        }

        case SYSCALL_READ: {
            struct proc *p = myproc();
            int fd = (int)tf->a0;
            uint64_t uaddr = tf->a1;
            uint64_t n = tf->a2;
            
            // Validate FD
            if (fd < 0 || fd >= NOFILE || !p->ofile[fd]) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            // Read in chunks using copybuf
            int64_t total = 0;
            while (n > 0) {
                uint64_t chunk = (n < COPYBUF_SIZE) ? n : COPYBUF_SIZE;
                int r = fileread(p->ofile[fd], copybuf, (int)chunk);
                if (r < 0) {
                    tf->a0 = (total > 0) ? (uint64_t)total : (uint64_t)-1;
                    break;
                }
                if (r == 0) break;  // EOF
                
                if (copyout(p->pagetable, uaddr, copybuf, (uint64_t)r) < 0) {
                    tf->a0 = (uint64_t)-1;
                    break;
                }
                total += r;
                uaddr += (uint64_t)r;
                n -= (uint64_t)r;
                if (r < (int)chunk) break;  // Short read
            }
            tf->a0 = (uint64_t)total;
            break;
        }

        case SYSCALL_WRITE: {
            struct proc *p = myproc();
            int fd = (int)tf->a0;
            uint64_t uaddr = tf->a1;
            uint64_t n = tf->a2;
            
            // Validate FD
            if (fd < 0 || fd >= NOFILE || !p->ofile[fd]) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            // Write in chunks using copybuf
            int64_t total = 0;
            while (n > 0) {
                uint64_t chunk = (n < COPYBUF_SIZE) ? n : COPYBUF_SIZE;
                
                if (copyin(p->pagetable, copybuf, uaddr, chunk) < 0) {
                    tf->a0 = (uint64_t)-1;
                    break;
                }
                
                int r = filewrite(p->ofile[fd], copybuf, (int)chunk);
                if (r < 0) {
                    tf->a0 = (total > 0) ? (uint64_t)total : (uint64_t)-1;
                    break;
                }
                total += r;
                uaddr += (uint64_t)r;
                n -= (uint64_t)r;
            }
            tf->a0 = (uint64_t)total;
            break;
        }

        case SYSCALL_CLOSE: {
            struct proc *p = myproc();
            int fd = (int)tf->a0;
            
            // Validate FD
            if (fd < 0 || fd >= NOFILE || !p->ofile[fd]) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            fileclose(p->ofile[fd]);
            p->ofile[fd] = 0;
            tf->a0 = 0;
            break;
        }

        case SYSCALL_OPEN: {
            struct proc *p = myproc();
            uint64_t upath = tf->a0;
            int flags = (int)tf->a1;
            
            // Copy path from userspace
            char path[128];
            if (copyinstr(p->pagetable, path, upath, sizeof(path)) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            struct inode *ip;
            
            if (flags & O_CREATE) {
                // Create file if it doesn't exist
                ip = create(path, T_FILE);
                if (ip == 0) {
                    tf->a0 = (uint64_t)-1;
                    break;
                }
                // create returns locked inode
            } else {
                // Open existing file
                ip = namei(path);
                if (ip == 0) {
                    tf->a0 = (uint64_t)-1;
                    break;
                }
                ilock(ip);
                
                // Can't open directory for writing
                if (ip->type == T_DIR && (flags & O_WRONLY || flags & O_RDWR)) {
                    iunlock(ip);
                    iput(ip);
                    tf->a0 = (uint64_t)-1;
                    break;
                }
            }
            
            // Allocate file structure
            struct file *f = filealloc();
            if (f == 0) {
                iunlock(ip);
                iput(ip);
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            // Allocate file descriptor
            int fd = fdalloc(f);
            if (fd < 0) {
                fileclose(f);
                iunlock(ip);
                iput(ip);
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            f->type = FD_INODE;
            f->ip = ip;
            f->off = 0;
            f->readable = !(flags & O_WRONLY);
            f->writable = (flags & O_WRONLY) || (flags & O_RDWR);
            
            iunlock(ip);
            
            tf->a0 = (uint64_t)fd;
            break;
        }

        case SYSCALL_CLONE: {
            // clone(src_path, dst_path) - CoW clone a file
            struct proc *p = myproc();
            uint64_t usrc = tf->a0;
            uint64_t udst = tf->a1;
            
            char src[128], dst[128];
            if (copyinstr(p->pagetable, src, usrc, sizeof(src)) < 0 ||
                copyinstr(p->pagetable, dst, udst, sizeof(dst)) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            // Open source file
            struct inode *srcip = namei(src);
            if (srcip == 0) {
                kprintf("clone: src '%s' not found\n", src);
                tf->a0 = (uint64_t)-1;
                break;
            }
            ilock(srcip);
            
            // Clone it
            struct inode *dstip = iclone(srcip);
            if (dstip == 0) {
                iunlock(srcip);
                iput(srcip);
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            // Link clone into filesystem
            char name[DIRENT_NAMELEN];
            struct inode *dp = nameiparent(dst, name);
            if (dp == 0) {
                iunlock(dstip);
                iput(dstip);
                iunlock(srcip);
                iput(srcip);
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            ilock(dp);
            if (dirlink(dp, name, dstip->inum) < 0) {
                iunlock(dp);
                iput(dp);
                iunlock(dstip);
                iput(dstip);
                iunlock(srcip);
                iput(srcip);
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            iunlock(dp);
            iput(dp);
            iunlock(dstip);
            iput(dstip);
            iunlock(srcip);
            iput(srcip);
            
            tf->a0 = 0;
            break;
        }

        case SYSCALL_FORK: {
            int pid = proc_fork();
            tf->a0 = (uint64_t)pid;
            break;
        }

        case SYSCALL_WAIT: {
            struct proc *p = myproc();
            uint64_t uaddr = tf->a0;  // User address for status, or 0
            
            int status = 0;
            int pid = proc_wait(&status);
            
            if (pid > 0 && uaddr != 0) {
                // Copy status to user space
                copyout(p->pagetable, uaddr, (char*)&status, sizeof(status));
            }
            
            tf->a0 = (uint64_t)pid;
            break;
        }

        case SYSCALL_MKDIR: {
            struct proc *p = myproc();
            uint64_t upath = tf->a0;
            
            char path[128];
            if (copyinstr(p->pagetable, path, upath, sizeof(path)) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            struct inode *ip = create(path, T_DIR);
            if (ip == 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            iunlock(ip);
            iput(ip);
            tf->a0 = 0;
            break;
        }

        case SYSCALL_CHDIR: {
            struct proc *p = myproc();
            uint64_t upath = tf->a0;
            
            char path[128];
            if (copyinstr(p->pagetable, path, upath, sizeof(path)) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            struct inode *ip = namei(path);
            if (ip == 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            ilock(ip);
            if (ip->type != T_DIR) {
                iunlock(ip);
                iput(ip);
                tf->a0 = (uint64_t)-1;
                break;
            }
            iunlock(ip);
            
            // Release old cwd and set new one
            if (p->cwd) {
                iput(p->cwd);
            }
            p->cwd = ip;
            
            tf->a0 = 0;
            break;
        }

        case SYSCALL_GETCWD: {
            struct proc *p = myproc();
            uint64_t ubuf = tf->a0;
            uint64_t size = tf->a1;
            
            if (!p->cwd || size < 2) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            // For now, just return "/" if at root, or "?" otherwise
            // A proper implementation would walk up the directory tree
            char buf[128];
            if (p->cwd->inum == ROOTINO) {
                buf[0] = '/';
                buf[1] = '\0';
            } else {
                buf[0] = '?';  // TODO: implement proper path reconstruction
                buf[1] = '\0';
            }
            
            uint64_t len = 2;
            if (len > size) len = size;
            
            if (copyout(p->pagetable, ubuf, buf, len) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            tf->a0 = (uint64_t)ubuf;  // Return pointer on success
            break;
        }

        case SYSCALL_UNLINK: {
            struct proc *p = myproc();
            uint64_t upath = tf->a0;
            
            char path[128];
            if (copyinstr(p->pagetable, path, upath, sizeof(path)) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            // Get parent directory and name
            char name[DIRENT_NAMELEN];
            struct inode *dp = nameiparent(path, name);
            if (dp == 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            ilock(dp);
            
            // Cannot unlink "." or ".."
            if (name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'))) {
                iunlock(dp);
                iput(dp);
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            // Look up the file
            uint32_t off;
            struct inode *ip = dirlookup(dp, name, &off);
            if (ip == 0) {
                iunlock(dp);
                iput(dp);
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            ilock(ip);
            
            // Cannot unlink a non-empty directory
            if (ip->type == T_DIR && ip->size > 2 * sizeof(struct dirent)) {
                iunlock(ip);
                iput(ip);
                iunlock(dp);
                iput(dp);
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            // Remove the directory entry
            struct dirent de;
            memzero(&de, sizeof(de));
            if (writei(dp, &de, off, sizeof(de)) != sizeof(de)) {
                iunlock(ip);
                iput(ip);
                iunlock(dp);
                iput(dp);
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            // Decrement link count
            ip->nlink--;
            iupdate(ip);
            
            iunlock(ip);
            iput(ip);
            iunlock(dp);
            iput(dp);
            
            tf->a0 = 0;
            break;
        }

        case SYSCALL_FSTAT: {
            struct proc *p = myproc();
            int fd = (int)tf->a0;
            uint64_t ust = tf->a1;  // User struct stat pointer
            
            if (fd < 0 || fd >= NOFILE || !p->ofile[fd]) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            struct file *f = p->ofile[fd];
            
            // Simple stat structure
            struct {
                uint16_t type;
                uint16_t nlink;
                uint32_t size;
                uint32_t ino;
            } st;
            
            if (f->type == FD_INODE) {
                ilock(f->ip);
                st.type = f->ip->type;
                st.nlink = f->ip->nlink;
                st.size = f->ip->size;
                st.ino = f->ip->inum;
                iunlock(f->ip);
            } else if (f->type == FD_DEVICE) {
                st.type = 0;  // Special device
                st.nlink = 1;
                st.size = 0;
                st.ino = 0;
            } else {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            if (copyout(p->pagetable, ust, (char*)&st, sizeof(st)) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            tf->a0 = 0;
            break;
        }

        case SYSCALL_DUP: {
            struct proc *p = myproc();
            int fd = (int)tf->a0;
            
            if (fd < 0 || fd >= NOFILE || !p->ofile[fd]) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            struct file *f = filedup(p->ofile[fd]);
            int newfd = fdalloc(f);
            if (newfd < 0) {
                fileclose(f);
                tf->a0 = (uint64_t)-1;
                break;
            }
            
            tf->a0 = (uint64_t)newfd;
            break;
        }

        default: {
            kprintf("Unknown syscall num: %d\n", (int)syscall_num);
            tf->a0 = -1;
            break;
        }
    }
}