#include <stdint.h>
#include <kernel/syscall.h>
#include <kernel/fs.h>

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
static inline long sys_snapshot(void) {
    return sys_call(SYSCALL_SNAPSHOT, 0, 0, 0, 0, 0, 0);
}
static inline long sys_subvol_set(long id) {
    return sys_call(SYSCALL_SUBVOL_SET, id, 0, 0, 0, 0, 0);
}
static inline long sys_getcwd(char *buf, long n) {
    return sys_call(SYSCALL_GETCWD, (long)buf, n, 0, 0, 0, 0);
}
static inline long sys_chdir(const char *path) {
    return sys_call(SYSCALL_CHDIR, (long)path, 0, 0, 0, 0, 0);
}
static inline long sys_exec(const char *path) {
    return sys_call(SYSCALL_EXEC, (long)path, 0, 0, 0, 0, 0);
}
static inline long sys_fork(void) {
    return sys_call(SYSCALL_FORK, 0, 0, 0, 0, 0, 0);
}
static inline long sys_wait(long *status) {
    return sys_call(SYSCALL_WAIT, (long)status, 0, 0, 0, 0, 0);
}
struct stat {
    uint16_t type;
    uint16_t nlink;
    uint32_t size;
    uint32_t ino;
};
static inline long sys_fstat(int fd, struct stat *st) {
    return sys_call(SYSCALL_FSTAT, fd, (long)st, 0, 0, 0, 0);
}
static inline long sys_sleep(long ticks) {
    return sys_call(SYSCALL_SLEEP, ticks, 0, 0, 0, 0, 0);
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
static void uputnum(uint64_t v) {
    char buf[32];
    int i = 0;
    if (v == 0) {
        sys_write(1, "0", 1);
        return;
    }
    while (v > 0 && i < (int)sizeof(buf)) {
        buf[i++] = '0' + (v % 10);
        v /= 10;
    }
    while (i > 0) {
        i--;
        sys_write(1, &buf[i], 1);
    }
}

static int read_line(char *buf, int max) {
    int i = 0;
    while (i < max - 1) {
        char c = 0;
        long r = sys_read(0, &c, 1);
        if (r <= 0) {
            sys_sleep(1);
            continue;
        }
        if (c == '\r') {
            c = '\n';
        }
        if (c == '\n') {
            sys_write(1, "\n", 1);
            break;
        }
        if (c == 0x7f || c == 0x08) { // backspace/delete
            if (i > 0) {
                i--;
                sys_write(1, "\b \b", 3);
            }
            continue;
        }
        buf[i++] = c;
        sys_write(1, &c, 1); // echo input
    }
    buf[i] = 0;
    return i;
}

static int split_args(char *line, char *argv[], int max) {
    int argc = 0;
    while (*line && argc < max) {
        while (*line == ' ' || *line == '\t') line++;
        if (!*line) break;
        argv[argc++] = line;
        while (*line && *line != ' ' && *line != '\t') line++;
        if (*line) *line++ = 0;
    }
    return argc;
}

static void cmd_help(void) {
    uputs("Commands: help pwd cd ls mkdir touch cat write rm mv clone snapshot subvol exec exit\n");
    uputs("Built-ins run in child (except cd/exit). External: try /bin/<cmd> or /path\n");
}

static void cmd_pwd(void) {
    char cwd[128];
    if (sys_getcwd(cwd, sizeof(cwd)) < 0) {
        uputs("pwd: failed\n");
        return;
    }
    uputs(cwd);
    uputs("\n");
}

static void cmd_ls(const char *path, int longfmt) {
    uint64_t cookie = 0;
    char name[32];
    while (sys_readdir(path, &cookie, name, sizeof(name)) == 0) {
        if (!longfmt) {
            uputs(name);
            uputs("\n");
            continue;
        }
        char full[128];
        int i = 0;
        if (path[0] == 0 || (path[0] == '.' && path[1] == 0)) {
            for (; name[i] && i < (int)sizeof(full) - 1; i++) {
                full[i] = name[i];
            }
        } else if (path[0] == '/' && path[1] == 0) {
            full[i++] = '/';
            int j = 0;
            while (name[j] && i < (int)sizeof(full) - 1) {
                full[i++] = name[j++];
            }
        } else {
            int j = 0;
            while (path[j] && i < (int)sizeof(full) - 1) {
                full[i++] = path[j++];
            }
            if (i < (int)sizeof(full) - 1 && full[i - 1] != '/') {
                full[i++] = '/';
            }
            j = 0;
            while (name[j] && i < (int)sizeof(full) - 1) {
                full[i++] = name[j++];
            }
        }
        full[i] = 0;
        long fd = sys_open(full, O_TREE);
        if (fd < 0) {
            uputs("? 0 0 ");
            uputs(name);
            uputs("\n");
            continue;
        }
        struct stat st;
        if (sys_fstat((int)fd, &st) < 0) {
            sys_close((int)fd);
            uputs("? 0 0 ");
            uputs(name);
            uputs("\n");
            continue;
        }
        sys_close((int)fd);
        char t = '?';
        if (st.type == T_DIR) t = 'd';
        else if (st.type == T_FILE) t = 'f';
        sys_write(1, &t, 1);
        sys_write(1, " ", 1);
        uputnum(st.size);
        sys_write(1, " ", 1);
        uputnum(st.ino);
        sys_write(1, " ", 1);
        uputs(name);
        uputs("\n");
    }
}

static void cmd_touch(const char *path) {
    long fd = sys_open(path, O_TREE | O_CREATE);
    if (fd < 0) {
        uputs("touch: failed\n");
        return;
    }
    sys_close(fd);
}

static void cmd_cat(const char *path) {
    long fd = sys_open(path, O_TREE);
    if (fd < 0) {
        uputs("cat: open failed\n");
        return;
    }
    char buf[128];
    for (;;) {
        long n = sys_read(fd, buf, sizeof(buf));
        if (n <= 0) break;
        sys_write(1, buf, n);
    }
    sys_close(fd);
    uputs("\n");
}

static void cmd_write(const char *path, const char *text) {
    long fd = sys_open(path, O_TREE | O_CREATE | O_WRONLY);
    if (fd < 0) {
        uputs("write: open failed\n");
        return;
    }
    sys_write(fd, text, ustrlen(text));
    sys_close(fd);
}

static void cmd_rm(const char *path) {
    if (sys_unlink(path) < 0) {
        uputs("rm: failed\n");
    }
}

static void cmd_mv(const char *oldp, const char *newp) {
    if (sys_rename(oldp, newp) < 0) {
        uputs("mv: failed\n");
    }
}

static void cmd_clone(const char *src, const char *dst) {
    if (sys_clone(src, dst) < 0) {
        uputs("clone: failed\n");
    }
}

static void cmd_snapshot(void) {
    long id = sys_snapshot();
    if (id < 0) {
        uputs("snapshot: failed\n");
        return;
    }
    uputs("snapshot id=");
    char num[32];
    int i = 0;
    long v = id;
    if (v == 0) num[i++] = '0';
    while (v > 0 && i < (int)sizeof(num) - 1) {
        num[i++] = '0' + (v % 10);
        v /= 10;
    }
    for (int j = i - 1; j >= 0; j--) {
        sys_write(1, &num[j], 1);
    }
    uputs("\n");
}

static void cmd_subvol(const char *idstr) {
    long id = 0;
    while (*idstr) {
        if (*idstr < '0' || *idstr > '9') break;
        id = id * 10 + (*idstr - '0');
        idstr++;
    }
    if (sys_subvol_set(id) < 0) {
        uputs("subvol: failed\n");
    }
}

static int run_builtin(int argc, char *argv[]) {
    const char *cmd = argv[0];
    if(ustreq(cmd, "done")) {
        uputs("DONE 0\n");
        return 0;
    }

    if (ustreq(cmd, "help")) { cmd_help(); return 0; }
    if (ustreq(cmd, "pwd")) { cmd_pwd(); return 0; }
    if (ustreq(cmd, "ls")) {
        int longfmt = 0;
        const char *p = ".";
        if (argc > 1 && ustreq(argv[1], "-l")) {
            longfmt = 1;
            if (argc > 2) p = argv[2];
        } else if (argc > 1) {
            p = argv[1];
        }
        cmd_ls(p, longfmt);
        return 0;
    }
    if (ustreq(cmd, "mkdir")) {
        if (argc < 2) { uputs("mkdir: missing path\n"); return 0; }
        if (sys_mkdir(argv[1]) < 0) uputs("mkdir: failed\n");
        return 0;
    }
    if (ustreq(cmd, "touch")) {
        if (argc < 2) { uputs("touch: missing path\n"); return 0; }
        cmd_touch(argv[1]);
        return 0;
    }
    if (ustreq(cmd, "cat")) {
        if (argc < 2) { uputs("cat: missing path\n"); return 0; }
        cmd_cat(argv[1]);
        return 0;
    }
    if (ustreq(cmd, "write")) {
        if (argc < 3) { uputs("write: missing args\n"); return 0; }
        cmd_write(argv[1], argv[2]);
        return 0;
    }
    if (ustreq(cmd, "rm")) {
        if (argc < 2) { uputs("rm: missing path\n"); return 0; }
        cmd_rm(argv[1]);
        return 0;
    }
    if (ustreq(cmd, "mv")) {
        if (argc < 3) { uputs("mv: missing args\n"); return 0; }
        cmd_mv(argv[1], argv[2]);
        return 0;
    }
    if (ustreq(cmd, "clone")) {
        if (argc < 3) { uputs("clone: missing args\n"); return 0; }
        cmd_clone(argv[1], argv[2]);
        return 0;
    }
    if (ustreq(cmd, "snapshot")) { cmd_snapshot(); return 0; }
    if (ustreq(cmd, "subvol")) {
        if (argc < 2) { uputs("subvol: missing id\n"); return 0; }
        cmd_subvol(argv[1]);
        return 0;
    }
    if (ustreq(cmd, "exec")) {
        if (argc < 2) { uputs("exec: missing path\n"); return 0; }
        if (sys_exec(argv[1]) < 0) uputs("exec: failed\n");
        return 0;
    }
    return -1;
}

static const char *tests[] = { "/bin/testC", "/bin/testD", "/bin/testE", "/bin/testF" };

int main() {

    long pid = sys_fork();
    if (pid == 0) {
        // child
        sys_exec("/bin/run_workload");
        // only reached if exec fails
        uputs("DONE 127\n");
        sys_exit(127);
    }

    if (pid < 0) {
        uputs("DONE 127\n");
        sys_exit(127);
    }

    long st = 0;
    sys_wait(&st);

    // If your wait() returns raw exit code already, keep it simple:
    uputs("DONE ");
    uputnum((uint64_t)st);
    uputs("\n");
    sys_exit((int)st);
}

/*
int main(void) {

    uputs("READY\n");  
    long wpid = sys_fork();
    if (wpid == 0) {
        // child
        sys_exec("/bin/run_workload");
        uputs("init: exec /bin/run_workload failed\n");
        sys_exit(1);
    } else if (wpid > 0) {
        // parent waits for workload to finish
        long st = 0;
        sys_wait(&st);
        uputs("init: run_workload finished\n");
    } else {
        uputs("init: fork failed, skipping run_workload\n");
    }


    uputs("running user tests...\n");
    for (int i = 0; i < (int)(sizeof(tests) / sizeof(tests[0])); i++) {
        uputs("fork/exec ");
        uputs(tests[i]);
        uputs("\n");
        long pid = sys_fork();
        if (pid == 0) {
            uputs("child exec ");
            uputs(tests[i]);
            uputs("\n");
            sys_exec(tests[i]);
            uputs("exec failed\n");
            sys_exit(1);
        } else if (pid > 0) {
            long status = 0;
            uputs("parent wait\n");
            sys_wait(&status);
        } else {
            uputs("fork failed\n");
        }
    }

    uputs("tiny-os shell (type 'help')\n");
    uputs("READY\n");
    for (;;) {
        char cwd[128];
        if (sys_getcwd(cwd, sizeof(cwd)) < 0) {
            cwd[0] = '/';
            cwd[1] = 0;
        }
        uputs(cwd);
        uputs(" $ ");

        char line[128];
        if (read_line(line, sizeof(line)) <= 0) continue;

        char *argv[8];
        int argc = split_args(line, argv, 8);
        if (argc == 0) continue;

        if (ustreq(argv[0], "exit")) {
            sys_exit(0);
        }
        if (ustreq(argv[0], "cd")) {
            if (argc < 2) { uputs("cd: missing path\n"); continue; }
            if (sys_chdir(argv[1]) < 0) uputs("cd: failed\n");
            continue;
        }

        long pid = sys_fork();
        if (pid == 0) {
            if (run_builtin(argc, argv) == 0) {
                sys_exit(0);
            }

            if (argv[0][0] == '/') {
                sys_exec(argv[0]);
            } else {
                char path[64];
                path[0] = '/'; path[1] = 'b'; path[2] = 'i'; path[3] = 'n'; path[4] = '/';
                int i = 0;
                while (argv[0][i] && i < (int)sizeof(path) - 6) {
                    path[5 + i] = argv[0][i];
                    i++;
                }
                path[5 + i] = 0;
                sys_exec(path);
            }
            uputs("unknown cmd\n");
            sys_exit(1);
        } else if (pid > 0) {
            long status = 0;
            sys_wait(&status);
        } else {
            uputs("fork failed\n");
        }
    }
    return 0;
}
*/