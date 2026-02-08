#!/usr/bin/env python3
"""
tools/diff_metrics.py

Compare two run_many.py runs (index.json files). Aggregates per-workload metrics
across reps (mean/min/max), then prints deltas between Run A and Run B.

Usage:
  ./tools/diff_metrics.py artifacts/<runA>/index.json artifacts/<runB>/index.json
  ./tools/diff_metrics.py ... --verbose
  ./tools/diff_metrics.py ... --show-all
  ./tools/diff_metrics.py ... --json > diff.json
"""

import argparse
import json
import math
import os
import re
import sys
from typing import Any, Dict, List, Tuple, Optional

IGNORE_KEYS = {"workload"}  # printed separately
REP_DIR_RE = re.compile(r"^rep(\d+)$")


def load_json(path: str) -> Dict[str, Any]:
    with open(path, "r") as f:
        return json.load(f)


def is_number(x: Any) -> bool:
    return isinstance(x, (int, float)) and not isinstance(x, bool)


def fmt_delta(a: float, b: float) -> str:
    d = b - a
    # avoid "-0"
    if abs(d) < 0.5e-6:
        d = 0.0
    if abs(a) < 0.5e-9:
        return f"{d:+.2f} (n/a)"
    pct = (d / a) * 100.0
    return f"{d:+.2f} ({pct:+.1f}%)"


def find_workloads(run_root: str, index_obj: Dict[str, Any]) -> List[str]:
    # Prefer what index says, but fall back to scanning folders.
    wls = index_obj.get("workloads")
    if isinstance(wls, list) and all(isinstance(x, str) for x in wls):
        return sorted(set(wls))

    # fallback: directories under run_root that look like workloads
    out = []
    for name in os.listdir(run_root):
        p = os.path.join(run_root, name)
        if name == "index.json":
            continue
        if os.path.isdir(p) and not name.startswith("."):
            out.append(name)
    return sorted(out)


def list_rep_dirs(workload_dir: str) -> List[str]:
    reps = []
    if not os.path.isdir(workload_dir):
        return reps
    for name in os.listdir(workload_dir):
        if REP_DIR_RE.match(name) and os.path.isdir(os.path.join(workload_dir, name)):
            reps.append(name)
    # stable order rep0, rep1, ...
    reps.sort(key=lambda s: int(REP_DIR_RE.match(s).group(1)))  # type: ignore
    return reps


def load_rep(workload_dir: str, rep: str) -> Tuple[bool, Optional[int], Optional[str], Optional[Dict[str, Any]]]:
    """
    Returns (ok, done_code, error, metrics_dict)
    """
    rep_dir = os.path.join(workload_dir, rep)
    meta_path = os.path.join(rep_dir, "run_meta.json")
    metrics_path = os.path.join(rep_dir, "metrics.json")

    ok = True
    done = None
    err = None

    if os.path.exists(meta_path):
        try:
            meta = load_json(meta_path)
            # run_one.py writes these keys
            if "ok" in meta and isinstance(meta["ok"], bool):
                ok = bool(meta["ok"])
            if "done_code" in meta and (meta["done_code"] is None or isinstance(meta["done_code"], int)):
                done = meta["done_code"]
            if "error" in meta and (meta["error"] is None or isinstance(meta["error"], str)):
                err = meta["error"]
        except Exception as e:
            ok = False
            err = f"failed to read run_meta.json: {e}"

    metrics = None
    if os.path.exists(metrics_path):
        try:
            metrics = load_json(metrics_path)
        except Exception as e:
            ok = False
            err = f"failed to read metrics.json: {e}"
            metrics = None
    else:
        ok = False
        err = "missing metrics.json"

    # if done_code is missing, treat as bad rep for aggregation
    if done is None:
        ok = False
        if err is None:
            err = "missing done_code"

    return ok, done, err, metrics


def aggregate_workload(run_root: str, workload: str) -> Dict[str, Any]:
    """
    Returns:
      {
        "workload": str,
        "n_reps": int,
        "n_ok": int,
        "bad_reps": [ {rep, error, done_code}, ... ],
        "stats": { metric_name: {"mean": float, "min": float, "max": float} },
      }
    """
    wdir = os.path.join(run_root, workload)
    reps = list_rep_dirs(wdir)

    bad_reps = []
    ok_metrics: List[Dict[str, Any]] = []

    for rep in reps:
        ok, done, err, metrics = load_rep(wdir, rep)
        if ok and metrics is not None:
            ok_metrics.append(metrics)
        else:
            bad_reps.append({"rep": rep, "done_code": done, "error": err})

    # collect numeric keys across ok reps
    keys = set()
    for m in ok_metrics:
        for k, v in m.items():
            if k in IGNORE_KEYS:
                continue
            if is_number(v):
                keys.add(k)

    stats: Dict[str, Dict[str, float]] = {}
    for k in sorted(keys):
        vals = []
        for m in ok_metrics:
            v = m.get(k)
            if is_number(v):
                vals.append(float(v))
        if not vals:
            continue
        stats[k] = {
            "mean": sum(vals) / float(len(vals)),
            "min": min(vals),
            "max": max(vals),
        }

    return {
        "workload": workload,
        "n_reps": len(reps),
        "n_ok": len(ok_metrics),
        "bad_reps": bad_reps,
        "stats": stats,
    }


def compare_workloads(a: Dict[str, Any], b: Dict[str, Any], weird_pct: float, show_all: bool) -> Tuple[int, int, List[Dict[str, Any]]]:
    """
    Returns (n_changed, n_weird, changes_list)
    Each change element includes metric name and stats/delta.
    """
    a_stats = a.get("stats", {})
    b_stats = b.get("stats", {})
    keys = sorted(set(a_stats.keys()) | set(b_stats.keys()))

    changes = []
    n_changed = 0
    n_weird = 0

    for k in keys:
        sa = a_stats.get(k)
        sb = b_stats.get(k)
        if sa is None or sb is None:
            # metric missing from one side: always show (important)
            changes.append({
                "name": k,
                "missing": True,
                "a": sa,
                "b": sb,
                "weird": True,
            })
            n_changed += 1
            n_weird += 1
            continue

        a_mean = float(sa["mean"])
        b_mean = float(sb["mean"])
        delta = b_mean - a_mean
        if abs(delta) < 0.5e-6:
            delta = 0.0

        if (not show_all) and delta == 0.0:
            continue

        pct = None
        if abs(a_mean) >= 0.5e-9:
            pct = (delta / a_mean) * 100.0

        weird = False
        if delta != 0.0:
            n_changed += 1
            # if a_mean is 0, treat as weird by default (n/a pct)
            if pct is None or abs(pct) >= weird_pct:
                weird = True
                n_weird += 1

        changes.append({
            "name": k,
            "a_mean": a_mean, "b_mean": b_mean,
            "delta": delta, "pct": pct,
            "a_min": float(sa["min"]), "a_max": float(sa["max"]),
            "b_min": float(sb["min"]), "b_max": float(sb["max"]),
            "weird": weird,
        })

    return n_changed, n_weird, changes


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("a_index", help="artifacts/<runA>/index.json")
    ap.add_argument("b_index", help="artifacts/<runB>/index.json")
    ap.add_argument("--weird-pct", type=float, default=20.0, help="flag changes >= this percent (default 20)")
    ap.add_argument("--verbose", action="store_true", help="print per-run rep health and ranges")
    ap.add_argument("--show-all", action="store_true", help="print metrics even if unchanged")
    ap.add_argument("--json", action="store_true", help="print machine-readable JSON summary to stdout")
    args = ap.parse_args()

    a_index = load_json(args.a_index)
    b_index = load_json(args.b_index)

    a_root = os.path.dirname(os.path.abspath(args.a_index))
    b_root = os.path.dirname(os.path.abspath(args.b_index))

    a_wls = find_workloads(a_root, a_index)
    b_wls = find_workloads(b_root, b_index)
    all_wls = sorted(set(a_wls) | set(b_wls))

    # Run summaries
    def count_ok(idx: Dict[str, Any]) -> Tuple[Optional[bool], Optional[int], Optional[int]]:
        ok = idx.get("ok")
        if not isinstance(ok, bool):
            ok = None
        # some index.json has results[] list
        res = idx.get("results")
        if isinstance(res, list):
            total = len(res)
            good = sum(1 for r in res if isinstance(r, dict) and r.get("ok") is True)
            return ok, good, total
        return ok, None, None

    a_ok, a_good, a_total = count_ok(a_index)
    b_ok, b_good, b_total = count_ok(b_index)

    if not args.json:
        print(f"Run A: {args.a_index}")
        if a_good is not None:
            print(f"  ok={a_ok} ({a_good}/{a_total} workloads ok)")
        else:
            print(f"  ok={a_ok} workloads={len(a_wls)}")
        print(f"Run B: {args.b_index}")
        if b_good is not None:
            print(f"  ok={b_ok} ({b_good}/{b_total} workloads ok)")
        else:
            print(f"  ok={b_ok} workloads={len(b_wls)}")
        print()

    summary = {
        "run_a": args.a_index,
        "run_b": args.b_index,
        "weird_pct": args.weird_pct,
        "ok": True,
        "workloads": [],
    }

    overall_ok = True

    for wl in all_wls:
        in_a = wl in a_wls
        in_b = wl in b_wls

        entry: Dict[str, Any] = {"workload": wl, "ok": True, "changes": []}

        if not in_a or not in_b:
            entry["ok"] = False
            entry["error"] = f"present: A={in_a} B={in_b}"
            summary["workloads"].append(entry)
            summary["ok"] = False
            overall_ok = False
            if not args.json:
                print(f"== {wl} ==")
                print(f"  ERROR: {entry['error']}")
                print()
            continue

        a_agg = aggregate_workload(a_root, wl)
        b_agg = aggregate_workload(b_root, wl)

        # workload ok means: at least 1 ok rep and no missing done_code issues
        wl_ok = (a_agg["n_ok"] > 0) and (b_agg["n_ok"] > 0) and (len(a_agg["bad_reps"]) == 0) and (len(b_agg["bad_reps"]) == 0)
        if not wl_ok:
            entry["ok"] = False
            overall_ok = False
            summary["ok"] = False

        n_changed, n_weird, changes = compare_workloads(a_agg, b_agg, args.weird_pct, args.show_all)
        entry["n_changed"] = n_changed
        entry["n_weird"] = n_weird
        entry["a"] = {"n_reps": a_agg["n_reps"], "n_ok": a_agg["n_ok"], "bad_reps": a_agg["bad_reps"]}
        entry["b"] = {"n_reps": b_agg["n_reps"], "n_ok": b_agg["n_ok"], "bad_reps": b_agg["bad_reps"]}
        entry["changes"] = changes

        summary["workloads"].append(entry)

        if args.json:
            continue

        print(f"== {wl} ==")
        if args.verbose:
            print(f"  reps A: {a_agg['n_ok']}/{a_agg['n_reps']} ok")
            if a_agg["bad_reps"]:
                for br in a_agg["bad_reps"]:
                    print(f"    A bad {br['rep']}: done={br['done_code']} err={br['error']}")
            print(f"  reps B: {b_agg['n_ok']}/{b_agg['n_reps']} ok")
            if b_agg["bad_reps"]:
                for br in b_agg["bad_reps"]:
                    print(f"    B bad {br['rep']}: done={br['done_code']} err={br['error']}")

        if not changes:
            print("  (no aggregate metric changes)")
            print()
            continue

        for ch in changes:
            if ch.get("missing"):
                print(f"  {ch['name']}: missing on one side (A={ch.get('a') is not None}, B={ch.get('b') is not None})  [WEIRD]")
                continue
            name = ch["name"]
            a_mean = ch["a_mean"]
            b_mean = ch["b_mean"]
            line = f"  {name} mean: {a_mean:.2f} → {b_mean:.2f} ({fmt_delta(a_mean, b_mean)})"
            if args.verbose:
                line += f" | range A [{ch['a_min']:.0f},{ch['a_max']:.0f}] B [{ch['b_min']:.0f},{ch['b_max']:.0f}]"
            if ch.get("weird"):
                line += "  [WEIRD]"
            print(line)
        print()

    if args.json:
        print(json.dumps(summary, indent=2))
        return 0 if summary["ok"] else 1

    return 0 if overall_ok else 1


if __name__ == "__main__":
    sys.exit(main())
    