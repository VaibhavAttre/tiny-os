#include "kernel/syscall.h"
#include "kernel/trapframe.h"
#include "kernel/printf.h"
#include "kernel/sched.h"
#include "kernel/file.h"
#include "kernel/fs.h"
#include "kernel/fs_tree.h"
#include "kernel/tree.h"
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

        sched_trace_syscall(syscall_num, tf->a0);
    }

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

            char path[128];
            if (copyinstr(p->pagetable, path, upath, sizeof(path)) < 0) {
                kprintf("sys_exec: copyinstr failed pid=%d upath=%p\n",
                        p ? p->id : -1, (void*)upath);
                tf->a0 = (uint64_t)-1;
                break;
            }
            kprintf("sys_exec: pid=%d path='%s'\n", p ? p->id : -1, path);

            fs_tree_init();
            uint32_t ino = 0;
            uint16_t type = 0;
            uint64_t size = 0;
            const uint32_t exec_max_pages = 64;
            const uint64_t exec_max_bytes = (uint64_t)exec_max_pages * PGSIZE;
            int use_tree = (fs_tree_lookup_path_at(p->tree_cwd, path, &ino) == 0);
            if (use_tree) {
                if (fs_tree_get_inode(ino, &type, &size) < 0 || type != T_FILE) {
                    tf->a0 = (uint64_t)-1;
                    break;
                }
                if (size == 0 || size > exec_max_bytes) {
                    tf->a0 = (uint64_t)-1;
                    break;
                }
                uint32_t npages = (uint32_t)((size + PGSIZE - 1) / PGSIZE);
                uint8_t *buf = (uint8_t *)kalloc_n(npages);
                if (!buf) {
                    tf->a0 = (uint64_t)-1;
                    break;
                }
                int n = fs_tree_file_read(ino, 0, buf, (uint32_t)size);
                if (n != (int)size) {
                    kfree_n(buf, npages);
                    kprintf("sys_exec: read failed n=%d size=%u\n", n, (unsigned)size);
                    tf->a0 = (uint64_t)-1;
                    break;
                }
                int r = proc_exec(p, buf, (uint64_t)size);
                kfree_n(buf, npages);
                kprintf("sys_exec: proc_exec r=%d\n", r);
                tf->a0 = (r < 0) ? (uint64_t)-1 : 0;
                break;
            }

            struct inode *ip = namei(path);
            if (ip == 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            ilock(ip);
            if (ip->type != T_FILE) {
                iunlock(ip);
                iput(ip);
                tf->a0 = (uint64_t)-1;
                break;
            }

            uint32_t sz = ip->size;
            if (sz == 0 || sz > exec_max_bytes) {
                iunlock(ip);
                iput(ip);
                tf->a0 = (uint64_t)-1;
                break;
            }

            uint32_t npages = (uint32_t)((sz + PGSIZE - 1) / PGSIZE);
            uint8_t *buf = (uint8_t*)kalloc_n(npages);
            if (!buf) {
                iunlock(ip);
                iput(ip);
                tf->a0 = (uint64_t)-1;
                break;
            }

            int n = readi(ip, buf, 0, sz);
            iunlock(ip);
            iput(ip);
            if (n != (int)sz) {
                kfree_n(buf, npages);
                tf->a0 = (uint64_t)-1;
                break;
            }

            int r = proc_exec(p, buf, sz);
            kfree_n(buf, npages);
            tf->a0 = (r < 0) ? (uint64_t)-1 : 0;
            break;
        }

        case SYSCALL_READ: {
            struct proc *p = myproc();
            int fd = (int)tf->a0;
            uint64_t uaddr = tf->a1;
            uint64_t n = tf->a2;

            if (fd < 0 || fd >= NOFILE || !p->ofile[fd]) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            int64_t total = 0;
            while (n > 0) {
                uint64_t chunk = (n < COPYBUF_SIZE) ? n : COPYBUF_SIZE;
                int r = fileread(p->ofile[fd], copybuf, (int)chunk);
                if (r < 0) {
                    tf->a0 = (total > 0) ? (uint64_t)total : (uint64_t)-1;
                    break;
                }
                if (r == 0) break; // EOF

                if (copyout(p->pagetable, uaddr, copybuf, (uint64_t)r) < 0) {
                    tf->a0 = (uint64_t)-1;
                    break;
                }
                total += r;
                uaddr += (uint64_t)r;
                n -= (uint64_t)r;
                if (r < (int)chunk) break; // Short read
            }
            tf->a0 = (uint64_t)total;
            break;
        }

        case SYSCALL_WRITE: {
            struct proc *p = myproc();
            int fd = (int)tf->a0;
            uint64_t uaddr = tf->a1;
            uint64_t n = tf->a2;

            if (fd < 0 || fd >= NOFILE || !p->ofile[fd]) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            int64_t total = 0;
            while (n > 0) {
                uint64_t chunk = (n < COPYBUF_SIZE) ? n : COPYBUF_SIZE;

            if (copyin(p->pagetable, copybuf, uaddr, chunk) < 0) {
                kprintf("sys_write: copyin failed pid=%d uaddr=%p n=%ld\n",
                        p ? p->id : -1, (void*)uaddr, (long)n);
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

            char path[128];
            if (copyinstr(p->pagetable, path, upath, sizeof(path)) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            fs_tree_init();
            kprintf("sys_open: O_TREE path='%s' flags=%x\n", path, flags);
            uint32_t tino = 0;
            int r;
            if (flags & O_CREATE) {
                r = fs_tree_create_file_at(p->tree_cwd, path, &tino);
            } else {
                r = fs_tree_lookup_path_at(p->tree_cwd, path, &tino);
            }
            kprintf("sys_open: O_TREE r=%d ino=%u\n", r, tino);
            if (r < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            if ((flags & O_TRUNC) &&
                (flags & (O_WRONLY | O_RDWR))) {
                if (fs_tree_truncate(tino, 0) < 0) {
                    tf->a0 = (uint64_t)-1;
                    break;
                }
            }

            struct file *f = filealloc();
            if (f == 0) {
                kprintf("sys_open: O_TREE filealloc failed\n");
                tf->a0 = (uint64_t)-1;
                break;
            }
            kprintf("sys_open: O_TREE filealloc ok\n");

            int fd = fdalloc(f);
            if (fd < 0) {
                kprintf("sys_open: O_TREE fdalloc failed\n");
                fileclose(f);
                tf->a0 = (uint64_t)-1;
                break;
            }
            kprintf("sys_open: O_TREE fdalloc ok fd=%d\n", fd);

            f->type = FD_TREE;
            f->tree_ino = tino;
            f->off = 0;
            f->readable = !(flags & O_WRONLY);
            f->writable = (flags & O_WRONLY) || (flags & O_RDWR);

            tf->a0 = (uint64_t)fd;
            kprintf("sys_open: O_TREE fd=%d\n", fd);
            break;
        }

        case SYSCALL_CLONE: {
            struct proc *p = myproc();
            uint64_t usrc = tf->a0;
            uint64_t udst = tf->a1;

            char src[128], dst[128];
            if (copyinstr(p->pagetable, src, usrc, sizeof(src)) < 0 ||
                copyinstr(p->pagetable, dst, udst, sizeof(dst)) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            fs_tree_init();
            if (fs_tree_clone_path_at(p->tree_cwd, src, dst) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }
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
            uint64_t uaddr = tf->a0; // User address for status, or 0

            int status = 0;
            int pid = proc_wait(&status);

            if (pid > 0 && uaddr != 0) {
                copyout(p->pagetable, uaddr, (char*)&status, sizeof(status));
            }

            tf->a0 = (uint64_t)pid;
            break;
        }

        case SYSCALL_MKDIR: {
            struct proc *p = myproc();
            uint64_t upath = tf->a0;
            (void)tf->a1;

            char path[128];
            if (copyinstr(p->pagetable, path, upath, sizeof(path)) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            fs_tree_init();
            if (fs_tree_create_dir_at(p->tree_cwd, path) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }

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

            fs_tree_init();
            uint32_t ino = 0;
            uint16_t type = 0;
            if (fs_tree_lookup_path_at(p->tree_cwd, path, &ino) < 0 ||
                fs_tree_get_inode(ino, &type, 0) < 0 ||
                type != T_DIR) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            p->tree_cwd = ino;
            tf->a0 = 0;
            break;
        }

        case SYSCALL_GETCWD: {
            struct proc *p = myproc();
            uint64_t ubuf = tf->a0;
            uint64_t size = tf->a1;

            if (size < 2) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            fs_tree_init();
            uint32_t cur = p->tree_cwd ? p->tree_cwd : 1;
            char buf[128];
            memzero(buf, sizeof(buf));

            if (cur == 1) {
                buf[0] = '/';
                buf[1] = '\0';
            } else {
                int end = (int)sizeof(buf) - 1;
                buf[end] = '\0';
                while (cur != 1) {
                    uint32_t parent = 0;
                    if (fs_tree_get_parent(cur, &parent) < 0 || parent == 0) {
                        tf->a0 = (uint64_t)-1;
                        break;
                    }
                    char name[32];
                    memzero(name, sizeof(name));
                    if (fs_tree_dir_find_name(parent, cur, name, sizeof(name)) < 0) {
                        tf->a0 = (uint64_t)-1;
                        break;
                    }
                    int n = 0;
                    while (name[n] && n < (int)sizeof(name)) n++;
                    if (n <= 0 || end - n - 1 < 0) {
                        tf->a0 = (uint64_t)-1;
                        break;
                    }
                    end -= n;
                    memmove(buf + end, name, (uint64_t)n);
                    end--;
                    buf[end] = '/';
                    cur = parent;
                }
                if (tf->a0 == (uint64_t)-1) {
                    break;
                }
                memmove(buf, buf + end, (uint64_t)(sizeof(buf) - end));
            }

            uint64_t len = 0;
            while (len + 1 < sizeof(buf) && buf[len] != 0) len++;
            len += 1;
            if (len > size) len = size;

            if (copyout(p->pagetable, ubuf, buf, len) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            tf->a0 = (uint64_t)ubuf;
            break;
        }

        case SYSCALL_UNLINK: {
            struct proc *p = myproc();
            uint64_t upath = tf->a0;
            (void)tf->a1;

            char path[128];
            if (copyinstr(p->pagetable, path, upath, sizeof(path)) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            fs_tree_init();
            if (fs_tree_unlink_path_at(p->tree_cwd, path) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            tf->a0 = 0;
            break;
        }

        case SYSCALL_TRUNCATE: {
            struct proc *p = myproc();
            uint64_t upath = tf->a0;
            uint64_t usize = tf->a1;
            (void)tf->a2;

            char path[128];
            if (copyinstr(p->pagetable, path, upath, sizeof(path)) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            fs_tree_init();
            uint32_t ino = 0;
            if (fs_tree_lookup_path_at(p->tree_cwd, path, &ino) < 0 ||
                fs_tree_truncate(ino, usize) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            tf->a0 = 0;
            break;
        }

        case SYSCALL_FSTAT: {
            struct proc *p = myproc();
            int fd = (int)tf->a0;
            uint64_t ust = tf->a1; // User struct stat pointer

            if (fd < 0 || fd >= NOFILE || !p->ofile[fd]) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            struct file *f = p->ofile[fd];

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
            } else if (f->type == FD_TREE) {
                uint16_t type = 0;
                uint64_t size = 0;
                if (fs_tree_get_inode(f->tree_ino, &type, &size) < 0) {
                    tf->a0 = (uint64_t)-1;
                    break;
                }
                st.type = type;
                st.nlink = 1;
                st.size = (uint32_t)size;
                st.ino = f->tree_ino;
            } else if (f->type == FD_DEVICE) {
                st.type = 0; // Special device
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

        case SYSCALL_READDIR: {
            struct proc *p = myproc();
            uint64_t upath = tf->a0;
            uint64_t ucookie = tf->a1;
            uint64_t uname = tf->a2;
            uint64_t uname_len = tf->a3;
            (void)tf->a4;

            char path[128];
            if (copyinstr(p->pagetable, path, upath, sizeof(path)) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            uint64_t cookie = 0;
            if (copyin(p->pagetable, (char *)&cookie, ucookie, sizeof(cookie)) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            fs_tree_init();
            uint32_t ino = 0;
            if (path[0] == 0 || (path[0] == '.' && path[1] == 0)) {
                ino = p->tree_cwd;
            } else if (path[0] == '/' && path[1] == 0) {
                ino = 1;
            } else {
                if (fs_tree_lookup_path_at(p->tree_cwd, path, &ino) < 0) {
                    tf->a0 = (uint64_t)-1;
                    break;
                }
            }

            char name[32];
            uint32_t out_ino = 0;
            if (fs_tree_readdir(ino, &cookie, name, sizeof(name), &out_ino) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            uint64_t nlen = uname_len;
            if (nlen > sizeof(name)) nlen = sizeof(name);
            if (copyout(p->pagetable, uname, name, nlen) < 0 ||
                copyout(p->pagetable, ucookie, (char *)&cookie, sizeof(cookie)) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            tf->a0 = 0;
            break;
        }

        case SYSCALL_RENAME: {
            struct proc *p = myproc();
            uint64_t uold = tf->a0;
            uint64_t unew = tf->a1;
            (void)tf->a2;

            char oldpath[128];
            char newpath[128];
            if (copyinstr(p->pagetable, oldpath, uold, sizeof(oldpath)) < 0 ||
                copyinstr(p->pagetable, newpath, unew, sizeof(newpath)) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            fs_tree_init();
            if (fs_tree_rename_path_at(p->tree_cwd, oldpath, newpath) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }

            tf->a0 = 0;
            break;
        }

        case SYSCALL_SNAPSHOT: {
            uint64_t id = 0;
            tree_init();
            if (tree_subvol_create(&id) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            tf->a0 = id;
            break;
        }

        case SYSCALL_SUBVOL_SET: {
            uint64_t id = tf->a0;
            struct proc *p = myproc();
            tree_init();
            if (tree_subvol_set_current(id) < 0) {
                tf->a0 = (uint64_t)-1;
                break;
            }
            if (p) {
                p->subvol_id = id;
            }
            tf->a0 = 0;
            break;
        }

        default: {
            kprintf("Unknown syscall num: %d\n", (int)syscall_num);
            tf->a0 = -1;
            break;
        }
    }
}
