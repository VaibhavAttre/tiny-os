import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

CSV_FILE = "sched_samples.csv"
OUT_PNG  = "dashboard.png"

# --- knobs ---
WARMUP_WINDOWS = 2
ACTIVE_SHARE_MIN = 0.02

LAT_SCALE = 20.0
CTX_PER_TICK_SCALE = 2.0
SLEEP_WAKE_DIFF_SCALE = 0.75

WEIGHTS = {"util":0.25, "fair":0.30, "over":0.25, "lat":0.15, "bal":0.05}

def clamp01(x): return np.minimum(1.0, np.maximum(0.0, x))
def safe_div(a,b): return np.where(b!=0, a/b, np.nan)
def exp_score(x, scale):
    x = np.maximum(0.0, x)
    return 100.0 * np.exp(-x/float(scale))

def jain_index(x):
    x = np.asarray(x, dtype=float)
    x = x[x > 0]
    if len(x) == 0:
        return np.nan
    return (x.sum()**2) / (len(x) * (x**2).sum())

# ---------------- load + deltas ----------------
df = pd.read_csv(CSV_FILE).sort_values(["id","tick"])
df["tick"] = df["tick"].astype(int)
df["id"]   = df["id"].astype(int)

cum_cols = ["run_ticks","ctx_in","preemptions","voluntary_yields","sleep_calls","wakeups_received","slept_ticks_total"]
for c in cum_cols:
    df["d_"+c] = df.groupby("id")[c].diff()
df["d_tick"] = df.groupby("id")["tick"].diff()

# window size
win = df.groupby("tick")["d_tick"].median().rename("window")
df = df.merge(win, on="tick", how="left")

# totals per window
tot = df.groupby("tick")[["d_run_ticks","d_ctx_in","d_preemptions","d_voluntary_yields","d_sleep_calls","d_wakeups_received"]].sum()
tot = tot.rename(columns={
    "d_run_ticks":"tot_run","d_ctx_in":"tot_ctx","d_preemptions":"tot_pre",
    "d_voluntary_yields":"tot_vy","d_sleep_calls":"tot_slp","d_wakeups_received":"tot_wak"
}).reset_index()
df = df.merge(tot, on="tick", how="left")

df["cpu_share"] = safe_div(df["d_run_ticks"], df["tot_run"])

# ---------------- per-window metrics ----------------
rows = []
for t, g in df.groupby("tick"):
    window = float(g["window"].iloc[0]) if np.isfinite(g["window"].iloc[0]) else np.nan
    if not np.isfinite(window) or window <= 0:
        continue

    tot_run = float(g["tot_run"].iloc[0]) if np.isfinite(g["tot_run"].iloc[0]) else 0.0
    tot_ctx = float(g["tot_ctx"].iloc[0]) if np.isfinite(g["tot_ctx"].iloc[0]) else 0.0
    tot_pre = float(g["tot_pre"].iloc[0]) if np.isfinite(g["tot_pre"].iloc[0]) else 0.0
    tot_vy  = float(g["tot_vy"].iloc[0])  if np.isfinite(g["tot_vy"].iloc[0])  else 0.0
    tot_slp = float(g["tot_slp"].iloc[0]) if np.isfinite(g["tot_slp"].iloc[0]) else 0.0
    tot_wak = float(g["tot_wak"].iloc[0]) if np.isfinite(g["tot_wak"].iloc[0]) else 0.0

    util = float(clamp01(tot_run / window))

    active = g.dropna(subset=["cpu_share","d_run_ticks"]).copy()
    active = active[(active["cpu_share"] > ACTIVE_SHARE_MIN) & (active["d_run_ticks"] > 0)]
    fair = jain_index(active["d_run_ticks"].to_numpy())

    ctx_per_tick = tot_ctx / window
    preempt_rate = tot_pre / window
    vyield_rate  = tot_vy  / window
    sleep_rate   = tot_slp / window
    wake_rate    = tot_wak / window
    bal_diff     = abs(sleep_rate - wake_rate)

    # latency proxy: weighted avg of avg_wake_latency_ticks (approx)
    lat = np.nan
    if "avg_wake_latency_ticks" in g.columns:
        w = np.nan_to_num(g["d_wakeups_received"].to_numpy(), nan=0.0)
        a = np.nan_to_num(g["avg_wake_latency_ticks"].to_numpy(), nan=0.0)
        if w.sum() > 0:
            lat = np.average(a, weights=w)

    rows.append(dict(
        tick=t, window=window, util=util, fair=fair, ctx_per_tick=ctx_per_tick,
        preempt_rate=preempt_rate, vyield_rate=vyield_rate,
        sleep_rate=sleep_rate, wake_rate=wake_rate, bal_diff=bal_diff, lat_proxy=lat
    ))

m = pd.DataFrame(rows).sort_values("tick")

# ---------------- score per window ----------------
m["score_util"] = 100.0 * clamp01(m["util"])
m["score_fair"] = 100.0 * clamp01(m["fair"].fillna(0.0))
m["score_over"] = exp_score(m["ctx_per_tick"], CTX_PER_TICK_SCALE)
m["score_lat"]  = exp_score(m["lat_proxy"].fillna(LAT_SCALE), LAT_SCALE)
m["score_bal"]  = exp_score(m["bal_diff"], SLEEP_WAKE_DIFF_SCALE)

wsum = sum(WEIGHTS.values())
w = {k: v/wsum for k,v in WEIGHTS.items()}
m["score"] = (
    w["util"]*m["score_util"] +
    w["fair"]*m["score_fair"] +
    w["over"]*m["score_over"] +
    w["lat"] *m["score_lat"]  +
    w["bal"] *m["score_bal"]
)

# steady-state overall number
ticks_sorted = m["tick"].to_list()
warm_idx = min(WARMUP_WINDOWS, max(0, len(ticks_sorted)-1))
warm_tick = ticks_sorted[warm_idx] if ticks_sorted else None
steady = m[m["tick"] >= warm_tick] if warm_tick is not None else m
overall = float(steady["score"].mean()) if len(steady) else float("nan")
p10     = float(steady["score"].quantile(0.10)) if len(steady) else float("nan")
std     = float(steady["score"].std()) if len(steady) else float("nan")

print(f"SchedulerScore = {overall:.2f}/100  (p10={p10:.2f}, std={std:.2f})")

# ---------------- CPU share stacked ----------------
piv = df.pivot_table(index="tick", columns="id", values="cpu_share", aggfunc="mean").fillna(0.0)

# ---------------- ONE dashboard PNG ----------------
fig, axs = plt.subplots(3, 2, figsize=(13, 8))
axs = axs.ravel()

axs[0].plot(m["tick"], m["score"])
axs[0].set_title(f"SchedulerScore (0-100)  mean={overall:.1f}  p10={p10:.1f}  std={std:.1f}")
axs[0].set_xlabel("tick"); axs[0].set_ylabel("score")

axs[1].plot(m["tick"], m["util"])
axs[1].set_title("CPU utilization (tot_run/window)")
axs[1].set_xlabel("tick"); axs[1].set_ylabel("util")

axs[2].plot(m["tick"], m["fair"])
axs[2].set_title(f"Fairness (Jain among active share>{ACTIVE_SHARE_MIN})")
axs[2].set_xlabel("tick"); axs[2].set_ylabel("fairness")

axs[3].plot(m["tick"], 1.0/(1.0 + m["ctx_per_tick"]))
axs[3].set_title("Overhead proxy 1/(1 + ctx/tick)")
axs[3].set_xlabel("tick"); axs[3].set_ylabel("proxy")

axs[4].plot(m["tick"], m["preempt_rate"], label="preempt/tick")
axs[4].plot(m["tick"], m["vyield_rate"],  label="vyield/tick")
axs[4].set_title("Preempt vs yield rates")
axs[4].set_xlabel("tick"); axs[4].set_ylabel("rate")
axs[4].legend(fontsize=8)

axs[5].stackplot(piv.index, piv.T.values, labels=[f"id {c}" for c in piv.columns])
axs[5].set_title("CPU share per window (stacked)")
axs[5].set_xlabel("tick"); axs[5].set_ylabel("share")
axs[5].legend(loc="upper left", ncol=2, fontsize=7)

plt.tight_layout()
plt.savefig(OUT_PNG, dpi=200)
print("Wrote:", OUT_PNG)
