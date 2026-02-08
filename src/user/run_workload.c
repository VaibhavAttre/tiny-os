#include <stdint.h>
#include <kernel/syscall.h>
#include <kernel/metrics.h>

static char g_workload[32];

static const char *workload_name(void) { return g_workload; }


static int streq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

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

static inline long sys_write(int fd, const void *buf, long n) {
    return sys_call(SYSCALL_WRITE, fd, (long)buf, n, 0, 0, 0);
}
static inline long sys_sleep(long t) { return sys_call(SYSCALL_SLEEP, t, 0, 0, 0, 0, 0); }
static inline long sys_exit(int c) { return sys_call(SYSCALL_EXIT, c, 0, 0, 0, 0, 0); }

static inline long sys_get_metrics(struct tiny_metrics *out, long out_sz) {
    return sys_call(SYSCALL_GET_METRICS, (long)out, out_sz, 0, 0, 0, 0);
}

static int ustrlen(const char *s){ int n=0; while(s[n]) n++; return n; }
static void uputs(const char *s){ sys_write(1, s, ustrlen(s)); }

static void append(char *buf, int *pos, const char *s) {
    while (*s) buf[(*pos)++] = *s++;
}

static inline long sys_get_workload(char *buf, long sz) {
    return sys_call(SYSCALL_GET_WORKLOAD, (long)buf, sz, 0, 0, 0, 0);
}


static void append_u64(char *buf, int *pos, uint64_t x) {
    char tmp[32];
    int n = 0;
    if (x == 0) { buf[(*pos)++] = '0'; return; }
    while (x) { tmp[n++] = (char)('0' + (x % 10)); x /= 10; }
    while (n--) buf[(*pos)++] = tmp[n];
}

int main() {
    uputs("READY\n");

    for (int i = 0; i < (int)sizeof(g_workload); i++) g_workload[i] = 0;

    long n = sys_get_workload(g_workload, sizeof(g_workload));
    if (n <= 0) {
        uputs("get_workload FAILED\n");
        // fallback
        g_workload[0]='b'; g_workload[1]='a'; g_workload[2]='s';
        g_workload[3]='e'; g_workload[4]='l'; g_workload[5]='i';
        g_workload[6]='n'; g_workload[7]='e'; g_workload[8]=0;
    } else {
        uputs("get_workload bytes=");
        // (print n...)
        uputs("\n");
    }

    uputs("WORKLOAD=");
    uputs(g_workload);
    uputs("\n");




    int exit_code = 0;

    long sleep_t = 10;
    if (streq(workload_name(), "smoke")) {
        sleep_t = 10;
    } else if (streq(workload_name(), "sleep50")) {
        sleep_t = 50;
    } else if (streq(workload_name(), "fail")) {
        sleep_t = 0;
        exit_code = 1;
    } else {
        sleep_t = 0;
        exit_code = 2;
    }

    if (sleep_t > 0) sys_sleep(sleep_t);




    struct tiny_metrics m;
    for (int i = 0; i < (int)sizeof(m); i++) ((char*)&m)[i] = 0;

    sys_get_metrics(&m, sizeof(m));

    char out[512];
    int p = 0;


    append(out, &p, "{\n  \"workload\": \"");
    append(out, &p, workload_name());
    append(out, &p, "\",");

    append(out, &p, "\n  \"workload_version\": 1,");
    append(out, &p, "\n  \"sleep_ticks\": ");
    append_u64(out, &p, (uint64_t)sleep_t);
    append(out, &p, ",");


    append(out, &p, "\n  \"version\": ");
    append_u64(out, &p, m.version);
    append(out, &p, ",\n  \"ticks\": ");
    append_u64(out, &p, m.ticks);


    append(out, &p, ",\n  \"syscall_enter\": ");
    append_u64(out, &p, m.syscall_enter);
    append(out, &p, ",\n  \"syscall_exit\": ");
    append_u64(out, &p, m.syscall_exit);

    append(out, &p, ",\n  \"ctx_switches\": ");
    append_u64(out, &p, m.context_switches);

    append(out, &p, ",\n  \"page_faults\": ");
    append_u64(out, &p, m.page_faults);

    append(out, &p, ",\n  \"disk_reads\": ");
    append_u64(out, &p, m.disk_reads);
    append(out, &p, ",\n  \"disk_writes\": ");
    append_u64(out, &p, m.disk_writes);
    append(out, &p, ",\n  \"disk_read_bytes\": ");
    append_u64(out, &p, m.disk_read_bytes);
    append(out, &p, ",\n  \"disk_write_bytes\": ");
    append_u64(out, &p, m.disk_write_bytes);

    append(out, &p, "\n}\n");

    uputs("METRICS_BEGIN\n");
    sys_write(1, out, p);
    uputs("METRICS_END\n");
    uputs("DONE ");
    char codebuf[32];
    int q = 0;
    append_u64(codebuf, &q, (uint64_t)exit_code);
    codebuf[q++] = '\n';
    sys_write(1, codebuf, q);

    sys_exit(exit_code);

    return 0;
}
