# tiny-os Scheduler Metrics — v1 Graphs Analysis

This README documents what the **v1 scheduler dashboard** plots mean, what the current results imply about the system, and what limitations exist before using these metrics to compare future scheduler algorithms.

> **Data source:** periodic `sched_dump()` snapshots printed from the kernel, parsed into per-window deltas (e.g., `d_run_ticks`, `d_ctx_in`, etc.) and then graphed.

---

## Overview

The v1 dashboard is primarily measuring **scheduler behavior under a mixed workload** (CPU-bound + interactive sleepers). Paging/VM is only indirectly represented (unless you add fault/VM counters). The headline outcomes from the current plots are:

- **CPU stays saturated** (util ~ 1.0 across the run)
- **CPU time distribution is stable** but dominated by CPU-heavy threads
- **Fairness fluctuates** significantly over time
- **Context-switch overhead is high**
- **Preemption appears effectively off**, while voluntary yields dominate

This means the system currently behaves more like:

> **cooperative scheduling + sleep-driven switching**, not true preemptive RR

…unless your timer trap is explicitly forcing a yield on quantum expiry.

---

## Graph-by-Graph Interpretation

### 1) `SchedulerScore (0–100)` (Top-left)

**What it is:** A composite per-window score combining:
- utilization
- fairness
- overhead proxy
- latency proxy / sleep-wake balance (depending on your script)

**What it shows (current run):**
- Mean ~ **62.8/100**, p10 ~ **59**, std ~ **4.6**
- Score is fairly stable, with moderate oscillation

**What it implies:**
- The system is not “unstable” (no huge spikes), but the score isn’t high because fairness and overhead are not great.
- Score variation is mainly driven by **fairness swings** and **scheduler churn**.

**Limitations:**
- This score is **workload-dependent**. A yield-heavy workload can make overhead look “worse” even if the scheduler algorithm is fine.
- v1 score is **scheduler-centric**; it does not meaningfully incorporate paging/VM costs yet.

---

### 2) `CPU utilization (tot_run / window)` (Top-right)

**What it is:**  
`util = Σ(d_run_ticks across threads) / snapshot_window_ticks`

**What it shows (current run):**
- A flat line at **1.0**

**What it implies:**
- CPU is always busy: there is always a runnable thread.
- Given your workload includes CPU burners, this is expected and good.

**When it would be suspicious:**
- If you expected idle windows (e.g., all threads sleeping), or if `run_ticks` accounting is broken.
- In this workload, flat 1.0 is plausible.

---

### 3) `Fairness (Jain among active share > threshold)` (Middle-left)

**What it is:** Jain fairness index computed per window over the “active” set (threads above a cpu_share threshold).

- `Jain = (Σx)^2 / (n * Σ(x^2))`
- 1.0 = perfect fairness among the considered set

**What it shows (current run):**
- Large swings (roughly **0.45 → 0.95**)

**What it implies:**
- Sometimes CPU is split fairly across the active set.
- Other times, one thread dominates the window’s CPU time.

**Why this matters:**
- Under true preemptive RR with equal runnable CPU-bound threads, fairness should be much steadier.
- Large swings are consistent with either:
  - active set changing quickly, *and/or*
  - scheduling being mostly cooperative (threads only switch when they yield/sleep)

---

### 4) `Overhead proxy = 1 / (1 + ctx/tick)` (Middle-right)

**What it is:** A decreasing function of context switch rate:
- More switches per tick → lower proxy → more overhead

**What it shows (current run):**
- Roughly **0.035–0.039**, relatively flat

**Interpretation:**
- This corresponds to very high `ctx/tick` (order-of-magnitude tens per tick).
- In “real OS” terms, this is high overhead, but for this toy workload it can be expected because:
  - many threads call `sleep_ticks()` frequently (sleep → scheduler → wake → scheduler)
  - some threads may also call `yield()` often

**What it implies:**
- The run is measuring **scheduler churn** more than forced time-slice scheduling.
- Overhead metrics must be compared carefully across workloads.

---

### 5) `Preempt vs yield rates` (Bottom-left)

**What it is:** Per-window rates:
- `preempt_rate = d_preemptions / d_tick`
- `vyield_rate  = d_voluntary_yields / d_tick`

**What it shows (current run):**
- **preempt/tick ≈ ~0**
- **vyield/tick ≈ high** (dominant)

**This is the most important v1 result.**

**What it implies:**
- The system is not forcing context switches on quantum expiry.
- The scheduler is behaving like a **cooperative scheduler**:
  - switching primarily happens when threads call `yield()` or `sleep()`

**Likely cause:**
- `sched_tick()` sets a flag (`need_switch = 1`) but no code in the timer trap path actually *forces* a switch.
- If you want true preemption, the trap handler typically needs to call `yield()` (or equivalent) when quantum is up, while in a running thread context.

---

### 6) `CPU share per window (stacked)` (Bottom-right)

**What it is:** Per-window `cpu_share = d_run_ticks / Σ(d_run_ticks)` stacked across threads.

**What it shows (current run):**
- Two threads dominate most windows (expected CPU-heavy “batch” threads).
- Interactive + IO-ish threads contribute small slices (because they sleep frequently).

**What it implies:**
- This matches the workload design: CPU-heavy threads should get most CPU.
- If true preemptive RR were active, the CPU-heavy threads would typically split more evenly and the distribution would be less dependent on voluntary yields.

---

## System Effectiveness Summary (v1)

### Strengths
- **Throughput / saturation:** CPU utilization at ~1.0 indicates no idle gaps under load.
- **Stability:** Metrics are not chaotic; the system is consistent.
- **Better workload realism than v0:** The mixed CPU + interactive + IO-ish pattern produces meaningful CPU-share dynamics.

### Weaknesses / Downfalls
- **Preemption is effectively not happening** (preempt rate ~0).  
  This limits fairness guarantees and makes comparisons against future schedulers misleading.
- **High scheduler churn / overhead** (very high ctx switching rate).  
  This may be workload-driven, but still needs separation into voluntary vs involuntary overhead.
- **Fairness swings** suggest CPU distribution is heavily driven by thread behavior rather than scheduler policy.

---

## Notes on Comparing Future Scheduler Algorithms

Before using v1 metrics to compare algorithms (RR vs MLFQ vs CFS-like, etc.), it helps to:

1. **Make preemption real**
   - Ensure the timer interrupt path causes a context switch on quantum expiry.

2. **Separate voluntary vs involuntary overhead**
   - Track (and plot) context switches originating from:
     - sleep/wakeup
     - voluntary yield
     - timer preemption

3. **Add latency/response metrics for interactive threads**
   - wake-to-run latency distribution (p50/p95)
   - runnable wait time per thread

4. **Add VM/paging metrics if you want “whole-system” effectiveness**
   - page fault counts (when you add faults)
   - TLB-related metrics (later SMP)
   - allocator latency / memory pressure signals

---

## v1 Takeaway

The v1 dashboard suggests the system is **stable and saturated**, but is currently dominated by **cooperative switching** (yield/sleep) rather than true **preemptive RR**, which is why fairness and CPU share depend strongly on thread behavior.

This is a solid baseline for instrumentation—but turning on real preemption and refining overhead accounting will make future scheduler comparisons far more meaningful.
