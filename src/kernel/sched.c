#include "kernel/sched.h"
#include "kernel/kalloc.h"
#include "kernel/printf.h"
#include "riscv.h"
#include "timer.h"
#include "kernel/vm.h"
#include "sv39.h"
#include "mmu.h"
#include "kernel/string.h"
#include "kernel/vm.h"
#include "kernel/memlayout.h"
#include "kernel/elf.h"

/*
https://danielmangum.com/posts/risc-v-bytes-timer-interrupts/
https://book.rvemu.app/hardware-components/03-csrs.html
*/

#define USER_TEXT_VA 0x0UL
#define USER_STACK_TOP TRAPFRAME
#define USER_STACK_BASE (USER_STACK_TOP - PGSIZE)

volatile int need_switch = 0;
volatile int in_scheduler = 0;
static struct proc procs[NPROC];
static struct context scheduler_context;
static struct proc * curr = 0;

#define TRACE_N 512

enum {
    TR_PICK = 1,
    TR_YIELD = 2,
    TR_SLEEP = 3,
    TR_WAKEUP = 4,
    TR_PREEMPT = 5,
    TR_SYSCALL = 6,
    TR_IDLE = 7
};

struct trace_evt {

    uint64_t tick;
    int from_id;
    int to_id;
    uint8_t from_user;
    uint8_t to_user;
    uint8_t type;
    uint8_t pad;
    uint32_t arg;
};

static struct trace_evt trace_buffer[TRACE_N];
static volatile uint32_t trace_w = 0;
static volatile uint32_t trace_r = 0;
static volatile uint32_t trace_drops = 0;
static int trace_enabled = 1;


extern void usertrapret();

static inline char tag_pid(int id, uint8_t is_user) {
    if (id < 0) return '#';  // scheduler / none
    if (is_user) return 'A' + (id % 26); // U-procs: A, B, C...
    return 'a' + (id % 26); // K-threads: a, b, c...
}

static inline void trace_log(uint8_t type, struct proc * from, struct proc * to, uint32_t arg) {

    if(!trace_enabled) return;

    int wason = (r_sstatus() & SSTATUS_SIE) != 0;
    sstatus_disable_sie();

    uint32_t i = trace_w++ % TRACE_N;
    trace_buffer[i].tick = ticks;
    trace_buffer[i].from_id = from ? from->id : -1;
    trace_buffer[i].to_id = to ? to->id : -1;
    trace_buffer[i].from_user = from ? (uint8_t)(from->user) : 0;
    trace_buffer[i].to_user = to ? (uint8_t)(to->user) : 0;
    trace_buffer[i].type = type;
    trace_buffer[i].arg = arg;
    if(wason) sstatus_enable_sie();
}

static void fmt_pid(char * out, int16_t id, uint8_t is_user) {

    if(id < 0) {
        out[0] = 'S';
        out[1] = 'C';
        out[2] = 'H';
        out[3] = '\0';
        return;
    }

    out[0] = is_user ? 'U' : 'K';
    out[1] = '0' + (id % 10);
    out[2] = '\0';
}

static void firstrun() {
    usertrapret();
    for(;;) {asm volatile("wfi");}
}

static int premission_from_elf_flags(uint32_t flags) {

    int perm = PTE_U | PTE_A;
    if(flags & PF_R) perm |= PTE_R;
    if(flags & PF_W) perm |= (PTE_W | PTE_D);
    if(flags & PF_X) perm |= PTE_X;
    return perm;
}

static int load(pagetable_t pt, uint8_t * img, uint64_t len, uint64_t * entry_out) {

    if(len > PGSIZE) return -1;
    void * page = kalloc();
    if(!page) return -1;
    memzero(page, PGSIZE);
    memcopy(page, img, len);
    if(vm_map(pt, 0, (uint64_t)page, PGSIZE, PTE_R | PTE_X | PTE_U | PTE_A) < 0) {
        kfree(page);
        return -1;
    }
    *entry_out = 0;
    return 0;
}

static int load_elf(pagetable_t pt, uint8_t * img, uint64_t len, uint64_t * entry_out) {

    Elf64_Ehdr eh;
    if(len < sizeof(eh)) return -1;
    memcopy(&eh, img, sizeof(eh));

    if(eh.e_ident[EI_MAG0] != ELFMAG0 ||
       eh.e_ident[EI_MAG1] != ELFMAG1 ||
       eh.e_ident[EI_MAG2] != ELFMAG2 ||
       eh.e_ident[EI_MAG3] != ELFMAG3) {
        return -1;
    }

    if(eh.e_ident[EI_CLASS] != ELFCLASS64) return -1;
    if(eh.e_ident[EI_DATA] != ELFDATA2LSB) return -1;
    if(eh.e_machine != EM_RISCV) return -1;

    if(eh.e_phoff == 0 || eh.e_phnum == 0) return -1;
    if(eh.e_phentsize < sizeof(Elf64_Phdr)) return -1;
    if (eh.e_phoff + (uint64_t)eh.e_phnum * (uint64_t)eh.e_phentsize > len) return -1;

    for(uint16_t i = 0; i < eh.e_phnum; ++i) {

        uint64_t off = eh.e_phoff + (uint64_t)i * (uint64_t)eh.e_phentsize;
        Elf64_Phdr ph;
        memcopy(&ph, img + off, sizeof(ph));

        if(ph.p_type != PT_LOAD) continue;
        if(ph.p_memsz == 0) continue;
        if(ph.p_filesz > ph.p_memsz) return -1;
        if(ph.p_offset + ph.p_filesz > len) return -1;

        uint64_t seg_start = ph.p_vaddr;
        uint64_t seg_end = ph.p_vaddr + ph.p_memsz;
        if(seg_end < seg_start) return -1;
        if(seg_end >= TRAPFRAME) return -1;

        int perm = premission_from_elf_flags(ph.p_flags);
        uint64_t a = PGRDOWN(seg_start);
        uint64_t b = PGRUP(seg_end);

        for(uint64_t va = a; va < b; va += PGSIZE) {

            void * page = kalloc();
            if(!page) return -1;
            memzero(page, PGSIZE);

            if(vm_map(pt, va, (uint64_t)page, PGSIZE, perm) < 0) {
                kfree(page);
                return -1;
            }
                
            /*
                file-backed:  [0x1050 -------------------- 0x2850)
                page:                       [0x2000 ----------- 0x3000)
                overlap:                     [0x2000 ---- 0x2850)

            */

            uint64_t file_lo = ph.p_vaddr;
            uint64_t file_hi = ph.p_vaddr + ph.p_filesz;
            uint64_t page_lo = va;
            uint64_t page_hi = va + PGSIZE;
            uint64_t copy_lo = (file_lo > page_lo) ? file_lo : page_lo;
            uint64_t copy_hi = (file_hi < page_hi) ? file_hi : page_hi;
            if(copy_hi > copy_lo) {
                
                uint64_t src_off = ph.p_offset + (copy_lo - file_lo);
                uint64_t dst_off = copy_lo - page_lo;
                memcopy((uint8_t*)page + dst_off, img + src_off, copy_hi - copy_lo);
            }
        }
    }
    *entry_out = eh.e_entry;
    return 0;
}

static void pt_freewalk(pagetable_t pt) {

    for(int i = 0; i < 512; ++i) {

        pte_t pte = pt[i];
        if((pte & PTE_V) == 0) continue;
        
        if((pte & (PTE_R | PTE_W | PTE_X)) == 0) {
            pagetable_t child = (pagetable_t)PTE2PA(pte);
            pt_freewalk(child); 
            kfree((void*)child);
            pt[i] = 0;
        }
        else {
            
            if(pte & PTE_U) {
                void * page = (void*)PTE2PA(pte);
                kfree(page);
            }
            pt[i] = 0;
        }
    }
}       

static void freeproc(struct proc * proc) {

    if(proc->ucode) {
        kfree(proc->ucode);
        proc->ucode = 0;
    }
    if(proc->ustack) {
        kfree(proc->ustack);
        proc->ustack = 0;
    }
    if(proc->pagetable) {
        pt_freewalk(proc->pagetable);
        kfree((void*)proc->pagetable);
        proc->pagetable = 0;
    }
    if(proc->tf) {
        kfree((void*)proc->tf);
        proc->tf = 0;
    }
    if(proc->kstack_base) {
        kfree(proc->kstack_base);
        proc->kstack_base = 0;
        proc->kstack_top = 0;
    }    
    // Reset (match struct proc exactly)
    proc->state = UNUSED;
    proc->killed = 0;
    proc->exit_status = 0;

    proc->start = 0;
    proc->chan = 0;

    proc->user = 0;
    proc->uentry = 0;
    proc->usp = 0;
    memzero(&proc->ctx, sizeof(proc->ctx));
    memzero(&proc->st, sizeof(proc->st));
}

void proc_kill(struct proc *p, int status) {
    if (!p) return;
    p->killed = 1;
    p->exit_status = status;
}

void proc_exit(int status) {
    struct proc *p = getmyproc();
    if (!p) panic("proc_exit: no current proc\n");

    sstatus_disable_sie();
    in_scheduler = 1;

    p->killed = 1;
    p->exit_status = status;
    p->state = ZOMBIE;
    
    swtch(&p->ctx, &scheduler_context);
    panic("proc_exit: returned\n");
}

//bootstrap for first time proc is run
static void kthread_trampoline() {

    void (*func)(void) = curr->start;
    sstatus_enable_sie();
    func();
    
    proc_exit(0);

    panic("kthread_trampoline: returned from proc_exit\n");
    for(;;) {
        asm volatile("wfi");
    }
}

static void user_trampoline() {

    uint64_t status = r_sstatus();
    status &= ~SSTATUS_SPP; //set to user mode
    status &= ~SSTATUS_SIE;
    status |= SSTATUS_SPIE; //enable interrupts on return to user mode
    w_sstatus(status);

    w_sepc(curr->uentry);
    
    kprintf("user_trampoline: satp=%p uentry=%p usp=%p\n",
        (void*)read_csr(satp), (void*)curr->uentry, (void*)curr->usp);

    //siwtch to user stack 
    asm volatile(
        "mv sp, %0\n"
        "sret\n"
        : 
        : "r"(curr->usp)
        : "memory"
    );

    for(;;) {
        asm volatile("wfi");
    }
}

struct proc * getmyproc() {
    return curr;
}

void sched_init() {

    for(int i = 0; i < NPROC; ++i) {

        procs[i].state = UNUSED;
        procs[i].id = 0;
        procs[i].killed = 0;
        procs[i].exit_status = 0;
        procs[i].ucode = 0;
        procs[i].ustack = 0;
        procs[i].start = 0;
        procs[i].kstack_base = 0;
        procs[i].kstack_top = 0;
        procs[i].chan = 0;
        procs[i].ctx.ra = 0;
        procs[i].ctx.sp = 0;
        procs[i].ctx.s0 = 0;
        procs[i].ctx.s1 = 0;
        procs[i].ctx.s2 = 0;
        procs[i].ctx.s3 = 0;
        procs[i].ctx.s4 = 0;
        procs[i].ctx.s5 = 0;
        procs[i].ctx.s6 = 0;
        procs[i].ctx.s7 = 0;
        procs[i].ctx.s8 = 0;
        procs[i].ctx.s9 = 0;
        procs[i].ctx.s10 = 0;
        procs[i].ctx.s11 = 0;

        procs[i].st = (struct sched_stats){0};
    }
}

int sched_create_kthread(void (*func)(void)) {

    for(int i = 0; i < NPROC; ++i) {

        if (procs[i].state == UNUSED) {

            void * stack_base = kalloc();
            if(!stack_base) {
                panic("sched create failed\n");
                return -1;
            }            

            procs[i].kstack_base = stack_base;
            procs[i].kstack_top = (uint64_t)stack_base + KSTACK_SIZE;
            procs[i].start = func;
            procs[i].chan = 0;
            procs[i].id = i;
            *(struct proc **) stack_base = &procs[i];
            //kprintf("[proc %d] kstack_base=%p kstack_top=%p\n",i, procs[i].kstack_base, (void*)procs[i].kstack_top);

            procs[i].ctx.sp = procs[i].kstack_top;
            procs[i].ctx.ra = (uint64_t)kthread_trampoline;
            procs[i].state = RUNNABLE;
            return 0;
        }
    }
    return -1;
}

static void do_yield(int restore_sie, int preempt) {
    if (!curr) { need_switch = 0; return; }

    sstatus_disable_sie();

    if (preempt) curr->st.involuntary_yields++;
    else curr->st.voluntary_yields++;

    need_switch = 0;
    curr->state = RUNNABLE;

    trace_log(preempt ? TR_PREEMPT : TR_YIELD, curr, 0, 0);
    swtch(&curr->ctx, &scheduler_context);

    if (restore_sie) sstatus_enable_sie();
}


void yield() {


    int restore = (r_sstatus() & SSTATUS_SIE) != 0;
    do_yield(restore, need_switch);
}

void yield_from_trap(int preempt) {
    int restore = (r_sstatus() & SSTATUS_SPIE) != 0; 
    do_yield(0, preempt);
}

void sleep(void * chan) {

    if(!curr) return;

    int wasinterrupton = (r_sstatus() & SSTATUS_SIE) != 0;

    sstatus_disable_sie();

    curr->st.sleep_calls++;
    curr->st.sleep_start_tick = ticks;

    curr->chan = chan;
    curr->state = SLEEPING;
    need_switch = 0;
    swtch(&curr->ctx, &scheduler_context);
    curr->chan = 0;
    if(curr->st.sleep_start_tick) {
        curr->st.slept_ticks_total += (ticks - curr->st.sleep_start_tick);
        curr->st.sleep_start_tick = 0;
    }
    if(wasinterrupton) sstatus_enable_sie();
}

void wakeup(void * chan) {

    int wasinterrupton = (r_sstatus() & SSTATUS_SIE) != 0;

    sstatus_disable_sie();
    for(int i = 0; i < NPROC; ++i) {

        if(procs[i].state == SLEEPING && procs[i].chan == chan) {
            procs[i].state = RUNNABLE;
            procs[i].chan = 0;
            
            trace_log(TR_WAKEUP, 0, &procs[i], 0);

            procs[i].st.wakeups++;
            procs[i].st.last_wakeup_tick = ticks;
        }
    }

    if(wasinterrupton) sstatus_enable_sie();
}

//Round robin for now
void scheduler() {

    static struct proc * last = 0;

    for(;;) {
        int ran = 0;
        in_scheduler = 1;
        sstatus_disable_sie();
        for(int i = 0; i < NPROC; ++i) {

            if (procs[i].state != RUNNABLE) continue;
            ran = 1;
            curr = &procs[i];
            curr->state = RUNNING;

            trace_log(TR_PICK, last, curr, 0);
            last = curr;

            write_csr(sscratch, curr->kstack_top);

            curr->st.ctx_in++;
            if(curr->st.last_wakeup_tick) {
                curr->st.wake_latency_total += (ticks - curr->st.last_wakeup_tick);
                curr->st.wake_latency_events++;
                curr->st.last_wakeup_tick = 0;
            }

            vm_switch(kvmpagetable());
            //write_csr(sscratch, curr->kstack_top);

            in_scheduler = 0;
            //sstatus_enable_sie();
            swtch(&scheduler_context, &curr->ctx);
            //sstatus_disable_sie();
            in_scheduler = 1;
            
            if(curr && curr->state == ZOMBIE) {
                freeproc(curr);
            }
            
            //after it yeilds 
            curr = 0;
        }

        if(!ran) {
            last = 0;
            uint64_t sp;
            asm volatile("mv %0, sp" : "=r"(sp));
            write_csr(sscratch, sp);
            vm_switch(kvmpagetable());

            trace_log(TR_IDLE, 0, 0, 0);

            sstatus_enable_sie();
            asm volatile("wfi");
        }
    }
}

void sched_on_tick() {

    if(curr && curr->state == RUNNING) {
        curr->st.run_ticks++;
    }
}

void sched_tick() {

    if((ticks% QUANT_TICKS) != 0) return;

    need_switch = 1;
}



int sched_trace_dump_n(int max) {
    uint32_t end;

    // snapshot writer position with interrupts off
    int wason = (r_sstatus() & SSTATUS_SIE) != 0;
    sstatus_disable_sie();
    end = trace_w;
    if (wason) sstatus_enable_sie();

    uint32_t avail = end - trace_r;   
    if (avail > TRACE_N) {
        uint32_t drop = avail - TRACE_N;
        trace_r = end - TRACE_N;
        trace_drops += drop;
        avail = TRACE_N;
    }

    kprintf("\nTRACE now=%d (r=%u w=%u end=%u drops=%u avail=%u)\n",
            (int)ticks,
            (unsigned)trace_r,
            (unsigned)trace_w,
            (unsigned)end,
            (unsigned)trace_drops,
            (unsigned)avail);

    int n = 0;
    while (n < max) {
        struct trace_evt e;

        int wason2 = (r_sstatus() & SSTATUS_SIE) != 0;
        sstatus_disable_sie();

        if (trace_r == end) {
            if (wason2) sstatus_enable_sie();
            break;
        }

        e = trace_buffer[trace_r % TRACE_N];
        trace_r++;

        if (wason2) sstatus_enable_sie();

        char f = tag_pid(e.from_id, e.from_user);
        char t = tag_pid(e.to_id,   e.to_user);

        switch (e.type) {
            case TR_PICK:
                kprintf("[%c] -> [%c]  pick  (tick=%d)\n", f, t, (int)e.tick);
                break;
            case TR_YIELD:
                kprintf("[%c]           yield (tick=%d)\n", f, (int)e.tick);
                break;
            case TR_PREEMPT:
                kprintf("[%c]           preempt (tick=%d)\n", f, (int)e.tick);
                break;
            case TR_SLEEP:
                kprintf("[%c]           sleep(%d) (tick=%d)\n",
                        f, (int)e.arg, (int)e.tick);
                break;
            case TR_WAKEUP:
                kprintf("[%c]           wake (tick=%d)\n", t, (int)e.tick);
                break;
            case TR_SYSCALL: {
                int num = (int)(e.arg >> 16);
                int arg = (int)(e.arg & 0xFFFF);
                kprintf("[%c]           syscall=%d arg=%d (tick=%d)\n",
                        f, num, arg, (int)e.tick);
                break;
            }
            case TR_IDLE:
                kprintf("[#]           idle(wfi) (tick=%d)\n", (int)e.tick);
                break;
            default:
                kprintf("[?]           ??? type=%d (tick=%d)\n", (int)e.type, (int)e.tick);
                break;
        }

        n++;
    }

    return n;
}



void sched_trace_dump(void) {
    uint32_t end;

    int wason = (r_sstatus() & SSTATUS_SIE) != 0;
    sstatus_disable_sie();
    end = trace_w;      
    if (wason) sstatus_enable_sie();

    while (1) {
        struct trace_evt e;
        int empty = 0;

        int wason2 = (r_sstatus() & SSTATUS_SIE) != 0;
        sstatus_disable_sie();

        if (trace_r == end) empty = 1;
        else {
            e = trace_buffer[trace_r % TRACE_N];
            trace_r++;
        }

        if (wason2) sstatus_enable_sie();
        if (empty) break;

        char f = tag_pid(e.from_id, e.from_user);
        char t = tag_pid(e.to_id,   e.to_user);

        switch (e.type) {
            case TR_PICK:
                kprintf("[%c] -> [%c]  pick  (tick=%d)\n", f, t, (int)e.tick);
                break;
            case TR_YIELD:
                kprintf("[%c]           yield (tick=%d)\n", f, (int)e.tick);
                break;
            case TR_PREEMPT:
                kprintf("[%c]           preempt (tick=%d)\n", f, (int)e.tick);
                break;
            case TR_SLEEP:
                kprintf("[%c]           sleep(%d) (tick=%d)\n", f, (int)e.arg, (int)e.tick);
                break;
            case TR_WAKEUP:
                kprintf("[%c]           wake (tick=%d)\n", t, (int)e.tick);
                break;
            case TR_SYSCALL: {
                int num = (int)(e.arg >> 16);
                int arg = (int)(e.arg & 0xFFFF);
                kprintf("[%c]           syscall=%d arg=%d (tick=%d)\n", f, num, arg, (int)e.tick);
                break;
            }
            case TR_IDLE:
                kprintf("[#]           idle(wfi) (tick=%d)\n", (int)e.tick);
                break;
            default:
                kprintf("[?]           ??? (tick=%d)\n", (int)e.tick);
                break;
        }
    }
}
	
void sched_dump() {

    kprintf("CSV\n");
    kprintf("id,run_ticks,ctx_in,preemptions,voluntary_yields,sleep_calls,wakeups_received,slept_ticks_total,avg_wake_latency_ticks\n");
    for (int i=0; i<NPROC; i++) {
        if (procs[i].kstack_base == 0) continue; // allocated thread
        uint64_t avg = 0;
        if (procs[i].st.wake_latency_events)
            avg = procs[i].st.wake_latency_total / procs[i].st.wake_latency_events;

        kprintf("%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
            procs[i].id,
            (int)procs[i].st.run_ticks,
            (int)procs[i].st.ctx_in,
            (int)procs[i].st.involuntary_yields,
            (int)procs[i].st.voluntary_yields,
            (int)procs[i].st.sleep_calls,
            (int)procs[i].st.wakeups,
            (int)procs[i].st.slept_ticks_total,
            (int)avg
        );
    }
    kprintf("CSV_END\n");
}

void sleep_ticks(uint64_t t) {

    if(t == 0) return;
    uint64_t target = ticks + t;
    trace_log(TR_SLEEP, curr, 0, (uint32_t)t);
    while((int64_t)(ticks - target) < 0) {
        sleep((void*)&ticks);
    }
}

void sleep_until(uint64_t t) {
    while ((int64_t)(ticks - t) < 0) {
        sleep((void*)&ticks);
    }
}

void sleep_ms(uint64_t ms) {

    uint64_t t = (ms * HZ+999)/1000;
    if(t == 0) t = 1;
    sleep_ticks(t);
}

int sched_create_userproc(const void * code, uint64_t sz) {

    const uint8_t * img = (const uint8_t *)code;

    for(int i = 0; i < NPROC; ++i) {

        if (procs[i].state == UNUSED) {

            //kprintf("Checkpoint A\n");

            void * stack_base = kalloc();
            if(!stack_base) {
                panic("sched create userproc failed\n");
                return -1;
            }       
            //kprintf("Checkpoint A2\n");
     

            procs[i].kstack_base = stack_base;
            procs[i].kstack_top = (uint64_t)stack_base + KSTACK_SIZE;
            procs[i].id = i;
            *(struct proc **) stack_base = &procs[i];

            //kprintf("[proc %d] kstack_base=%p kstack_top=%p\n", i, procs[i].kstack_base, (void*)procs[i].kstack_top);

            pagetable_t pt = uvmcreate();
            if(!pt) {
                kfree(stack_base);
                return -1;
            }

            procs[i].tf = (struct trapframe *)kalloc();
            if(!procs[i].tf) {
                kfree(stack_base);
                kfree((void*)pt);
                return -1;
            }
            memzero(procs[i].tf, PGSIZE);
            
            if(vm_map(pt, TRAPFRAME, (uint64_t)procs[i].tf, PGSIZE, PTE_R | PTE_W | PTE_A | PTE_D) < 0) {
                kfree(stack_base);
                kfree((void*)pt);
                kfree((void*)procs[i].tf);
                return -1;
            }
            

           //kprintf("Checkpoint B\n");

            void * ustack = kalloc();
            if(!ustack) {
                pt_freewalk(pt);
                kfree((void*)pt);
                kfree((void*)procs[i].tf);
                if(ustack) kfree(ustack);
                kfree(stack_base);
                return -1;
            }
            
            memzero(ustack, PGSIZE);

            if(vm_map(pt, USER_STACK_BASE, (uint64_t)ustack, PGSIZE, PTE_R | PTE_W | PTE_U | PTE_A | PTE_D) < 0) {
                
                kfree(ustack);
                pt_freewalk(pt);
                kfree((void*)procs[i].tf);
                kfree(stack_base);
                kfree((void*)pt);
                procs[i].tf = 0;
                return -1;
            }

            uint64_t entry = 0;
            int ok;
            if(sz >= 4 && img[0] == 0x7F && img[1] == 'E' && img[2] == 'L' && img[3] == 'F') {
                ok = load_elf(pt, (uint8_t*)img, sz, &entry);
            }
            else {
                ok = load(pt, (uint8_t*)img, sz, &entry);
            }
            if(ok < 0) {
                pt_freewalk(pt);
                kfree(stack_base);
                kfree((void*)pt);
                kfree((void*)procs[i].tf);
                return -1;
            }   
            
            procs[i].tf->epc = entry;
            procs[i].tf->sp  = USER_STACK_TOP;
            
            procs[i].pagetable = pt;
            procs[i].user = 1;
            procs[i].uentry = entry;
            procs[i].usp = USER_STACK_TOP;

            procs[i].ctx.sp = procs[i].kstack_top;
            procs[i].ctx.ra = (uint64_t)firstrun;   

            procs[i].state = RUNNABLE;
                

           // kprintf("Checkpoint C\n");

            dump_pte(pt, USER_TEXT_VA);
            dump_pte(pt, USER_STACK_BASE);
           
            return 0;
        }
    }   
    
    return -1;
}

int proc_exec(struct proc * p, const uint8_t * code, uint64_t sz) {
    if (!p || !p->user) return -1;

    pagetable_t newpt = uvmcreate();
    if (!newpt) return -1;

    if(vm_map(newpt, TRAPFRAME, (uint64_t)p->tf, PGSIZE, PTE_R | PTE_W | PTE_A | PTE_D) < 0) {
        kfree((void*)newpt);
        return -1;
    }

    void * ustack = kalloc();
    if(!ustack) {
        pt_freewalk(newpt);
        kfree((void*)newpt);
        return -1;
    }

    memzero(ustack, PGSIZE);

    if(vm_map(newpt, USER_STACK_BASE, (uint64_t)ustack, PGSIZE, PTE_R | PTE_W | PTE_U | PTE_A | PTE_D) < 0) {
        //kfree(ustack);
        pt_freewalk(newpt);
        kfree((void*)newpt);
        return -1;
    }

    uint64_t entry = 0;
    int ok  = 0;
    if(sz >= 4 && code[0] == 0x7F && code[1] == 'E' && code[2] == 'L' && code[3] == 'F') {
        ok = load_elf(newpt, (uint8_t*)code, sz, &entry);
    }
    else {
        ok = load(newpt, (uint8_t*)code, sz, &entry);
    }

    if(ok < 0) {
        //kfree(ustack);
        pt_freewalk(newpt);
        kfree((void*)newpt);
        return -1;
    }

    pagetable_t oldpt = p->pagetable;
    p->pagetable = newpt;
    sfence_vma();
    p->uentry = entry;
    p->usp = USER_STACK_TOP;
    p->tf->epc = entry;
    p->tf->sp  = USER_STACK_TOP;

    if(oldpt) {
        pt_freewalk(oldpt);
        kfree((void*)oldpt);
    }
    return 0;
}

void sched_trace_syscall(uint64_t num, uint64_t arg) {
    
    struct proc * p = getmyproc();
    if(!p) return;
    uint32_t x = ((uint32_t)num << 16) | (uint32_t)(arg & 0xFFFF);
    trace_log(TR_SYSCALL, p, 0, x);
}

void sched_trace_state(uint32_t *r, uint32_t *w) {
    int wason = (r_sstatus() & SSTATUS_SIE) != 0;
    sstatus_disable_sie();

    if (r) *r = trace_r;
    if (w) *w = trace_w;

    if (wason) sstatus_enable_sie();
}
