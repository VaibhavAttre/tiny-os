#include "kernel/syscall.h"
#include "kernel/trapframe.h"
#include "kernel/printf.h"
#include "kernel/sched.h"
#include "timer.h"
#include <drivers/uart.h>
#include "riscv.h"
#include "sv39.h"
#include "kernel/current.h"
#include <stdint.h>
#include "user_test.h"

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
            int which = (int)tf->a0;

            const uint8_t *img = 0;
            uint64_t sz = 0;

            if (which == 0) { img = (const uint8_t*)userA_elf; sz = userA_elf_len; }
            else if (which == 1) { img = (const uint8_t*)userB_elf; sz = userB_elf_len; }
            else { tf->a0 = (uint64_t)-1; break; }

            int r = proc_exec(p, img, sz);
            tf->a0 = (r < 0) ? (uint64_t)-1 : 0;
            break;
        }

        default: {
            kprintf("Unknown syscall num: %d\n", (int)syscall_num);
            tf->a0 = -1;
            break;
        }
    }
}