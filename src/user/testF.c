#include <stdint.h>
#include <kernel/syscall.h>

static inline long sys_call(long n, long a0, long a1, long a2, long a3, long a4, long a5) {
    register long r0 asm("a0") = a0;
    register long r1 asm("a1") = a1;
    register long r2 asm("a2") = a2;
    register long r3 asm("a3") = a3;
    register long r4 asm("a4") = a4;
    register long r5 asm("a5") = a5;
    register long r7 asm("a7") = n;
    asm volatile("ecall" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7) : "memory");
    return r0;
}

static inline long sys_open(const char *path, int flags) {
    return sys_call(SYSCALL_OPEN, (long)path, flags, 0, 0, 0, 0);
}

static inline long sys_read(int fd, void *buf, long n) {
    return sys_call(SYSCALL_READ, fd, (long)buf, n, 0, 0, 0);
}

static inline long sys_write(int fd, const void *buf, long n) {
    return sys_call(SYSCALL_WRITE, fd, (long)buf, n, 0, 0, 0);
}

static inline long sys_close(int fd) {
    return sys_call(SYSCALL_CLOSE, fd, 0, 0, 0, 0, 0);
}

static inline long sys_mkdir(const char *path) {
    return sys_call(SYSCALL_MKDIR, (long)path, 0, 0, 0, 0, 0);
}

static inline long sys_unlink(const char *path) {
    return sys_call(SYSCALL_UNLINK, (long)path, 0, 0, 0, 0, 0);
}

static inline long sys_rename(const char *oldp, const char *newp) {
    return sys_call(SYSCALL_RENAME, (long)oldp, (long)newp, 0, 0, 0, 0);
}

static inline long sys_readdir(const char *path, uint64_t *cookie, char *name, long name_len) {
    return sys_call(SYSCALL_READDIR, (long)path, (long)cookie, (long)name, name_len, 0, 0);
}

static inline long sys_clone(const char *src, const char *dst) {
    return sys_call(SYSCALL_CLONE, (long)src, (long)dst, 0, 0, 0, 0);
}

static inline long sys_exit(int code) {
    return sys_call(SYSCALL_EXIT, code, 0, 0, 0, 0, 0);
}

static int ustrlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static int ustreq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static void uputs(const char *s) {
    sys_write(1, s, ustrlen(s));
}

static void die(const char *msg) {
    uputs("testF: FAIL: ");
    uputs(msg);
    uputs("\n");
    sys_exit(1);
}

int main(void) {
    char buf[64];

    sys_mkdir("/cfs");
    sys_mkdir("/cfs/dir");
    sys_unlink("/cfs/dir/file.txt");
    sys_unlink("/cfs/dir/file2.txt");
    sys_unlink("/cfs/reflink_src");
    sys_unlink("/cfs/reflink_dst");

    int fd = sys_open("/cfs/dir/file.txt", O_TREE | O_CREATE | O_RDWR);
    if (fd < 0) die("open file.txt");
    if (sys_write(fd, "hello", 5) != 5) die("write file.txt");
    sys_close(fd);

    fd = sys_open("/cfs/dir/file.txt", O_TREE);
    if (fd < 0) die("reopen file.txt");
    long n = sys_read(fd, buf, 5);
    sys_close(fd);
    if (n != 5) die("read file.txt");
    buf[5] = 0;
    if (!ustreq(buf, "hello")) die("verify file.txt");

    if (sys_rename("/cfs/dir/file.txt", "/cfs/dir/file2.txt") < 0) die("rename file");

    uint64_t cookie = 0;
    int found = 0;
    for (int i = 0; i < 16; i++) {
        char name[32];
        if (sys_readdir("/cfs/dir", &cookie, name, sizeof(name)) < 0) break;
        if (ustreq(name, "file2.txt")) {
            found = 1;
            break;
        }
    }
    if (!found) die("readdir file2.txt");

    if (sys_unlink("/cfs/dir/file2.txt") < 0) die("unlink file2.txt");

    fd = sys_open("/cfs/reflink_src", O_TREE | O_CREATE | O_WRONLY);
    if (fd < 0) die("open reflink_src");
    if (sys_write(fd, "AAAAA", 5) != 5) die("write reflink_src");
    sys_close(fd);

    if (sys_clone("/cfs/reflink_src", "/cfs/reflink_dst") < 0) die("clone reflink");

    fd = sys_open("/cfs/reflink_src", O_TREE | O_WRONLY);
    if (fd < 0) die("reopen reflink_src");
    if (sys_write(fd, "BBBBB", 5) != 5) die("overwrite reflink_src");
    sys_close(fd);

    fd = sys_open("/cfs/reflink_dst", O_TREE);
    if (fd < 0) die("open reflink_dst");
    n = sys_read(fd, buf, 5);
    sys_close(fd);
    if (n != 5) die("read reflink_dst");
    buf[5] = 0;
    if (!ustreq(buf, "AAAAA")) die("reflink dst changed");

    fd = sys_open("/cfs/reflink_src", O_TREE);
    if (fd < 0) die("open reflink_src final");
    n = sys_read(fd, buf, 5);
    sys_close(fd);
    if (n != 5) die("read reflink_src final");
    buf[5] = 0;
    if (!ustreq(buf, "BBBBB")) die("reflink src not updated");

    uputs("testF: PASS\n");
    sys_exit(0);
    return 0;
}
