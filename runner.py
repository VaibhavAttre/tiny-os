#!/usr/bin/env python3
import os, sys, json, subprocess, time, selectors, uuid, argparse, signal, math
from statistics import mean, pstdev

ARTIFACTS_DIR = "artifacts"
DEFAULT_TIMEOUT_SEC = 20

IGNORE_METRIC_KEYS = {"ticks", "version"}

def percentile(sorted_vals, p):
    if not sorted_vals:
        return None
    if p <= 0:
        return sorted_vals[0]
    if p >= 100:
        return sorted_vals[-1]
    n = len(sorted_vals)
    pos = (p / 100.0) * (n - 1)
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    if lo == hi:
        return sorted_vals[lo]
    frac = pos - lo
    return sorted_vals[lo] * (1.0 - frac) + sorted_vals[hi] * frac

def load_json_if_exists(path):
    try:
        with open(path) as f:
            return json.load(f)
    except Exception:
        return None

def save_json(path, obj):
    with open(path, "w") as f:
        json.dump(obj, f, indent=2, sort_keys=True)

def build_cmd(base_cmd_list, workload):
    cmd = list(base_cmd_list)
    if workload:
        cmd.append(f"WORKLOAD={workload}")
    return cmd

def run_once(run_dir, timeout_sec, qemu_cmd, workload):
    os.makedirs(run_dir, exist_ok=True)

    start = time.time()

    in_metrics = False
    metrics_lines = []

    saw_ready = False
    saw_done = False
    done_code = None
    log_lines = []

    jproc = subprocess.Popen(
        qemu_cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        start_new_session=True,
        env={**os.environ, **({"TINYOS_WORKLOAD": workload} if workload else {})},
    )

    sel = selectors.DefaultSelector()
    sel.register(jproc.stdout, selectors.EVENT_READ)

    err = None
    metrics_path = os.path.join(run_dir, "metrics.json")
    kernel_log_path = os.path.join(run_dir, "kernel.log")
    meta_path = os.path.join(run_dir, "run_meta.json")

    try:
        while True:
            if time.time() - start > timeout_sec:
                raise TimeoutError("Test timed out")

            events = sel.select(timeout=0.1)
            for key, _ in events:
                line = key.fileobj.readline()
                if not line:
                    break

                log_lines.append(line)

                if "METRICS_BEGIN" in line:
                    in_metrics = True
                    metrics_lines = []
                    continue

                if "METRICS_END" in line:
                    in_metrics = False
                    with open(metrics_path, "w") as f:
                        f.write("".join(metrics_lines))
                    continue

                if in_metrics:
                    metrics_lines.append(line)
                    continue

                sys.stdout.write(line)
                sys.stdout.flush()

                if (not saw_ready) and "READY" in line:
                    saw_ready = True

                if "DONE" in line:
                    parts = line.strip().split()
                    if len(parts) >= 2 and parts[0] == "DONE":
                        saw_done = True
                        try:
                            done_code = int(parts[1])
                        except Exception:
                            done_code = None
                        break

            if saw_done:
                break

    except Exception as e:
        err = str(e)
        with open(os.path.join(run_dir, "runner_error.txt"), "w") as f:
            f.write(err + "\n")

    finally:
        with open(kernel_log_path, "w") as f:
            f.writelines(log_lines)

        meta = {
            "cmd": qemu_cmd,
            "saw_ready": saw_ready,
            "saw_done": saw_done,
            "done_code": done_code,
            "timeout_sec": timeout_sec,
            "wall_ms": int((time.time() - start) * 1000),
            "error": err,
            "workload": workload,
        }
        save_json(meta_path, meta)

        try:
            os.killpg(jproc.pid, signal.SIGTERM)
        except Exception:
            pass

        try:
            jproc.wait(timeout=1.0)
        except Exception:
            try:
                os.killpg(jproc.pid, signal.SIGKILL)
            except Exception:
                pass

    metrics = load_json_if_exists(metrics_path)
    return meta, metrics

def aggregate_trials(trial_results):
    oks = [t for t in trial_results if t["ok"]]
    fails = [t for t in trial_results if not t["ok"]]

    wall_ms = [t["meta"]["wall_ms"] for t in oks if isinstance(t["meta"].get("wall_ms"), int)]

    series = {}
    for t in oks:
        m = t.get("metrics") or {}
        for k, v in m.items():
            if k in IGNORE_METRIC_KEYS:
                continue
            if isinstance(v, (int, float)):
                series.setdefault(k, []).append(float(v))

    def stats(vals):
        if not vals:
            return None
        s = sorted(vals)
        return {
            "n": len(s),
            "mean": mean(s),
            "stdev": pstdev(s) if len(s) > 1 else 0.0,
            "min": s[0],
            "p50": percentile(s, 50),
            "p95": percentile(s, 95),
            "max": s[-1],
        }

    wall_stats = stats([float(x) for x in wall_ms]) if wall_ms else None
    cv = None
    if wall_stats and wall_stats["mean"] and wall_stats["mean"] != 0:
        cv = wall_stats["stdev"] / wall_stats["mean"]

    agg = {
        "trials_total": len(trial_results),
        "trials_ok": len(oks),
        "trials_failed": len(fails),
        "wall_ms": wall_stats,
        "wall_cv": cv,
        "metrics": {k: stats(vs) for k, vs in sorted(series.items())},
        "failed_indices": [t["trial_index"] for t in fails],
    }
    return agg

def stability_status(cv, warn_cv, fail_cv):
    if cv is None:
        return {"status": "unknown", "pass": False}
    if cv > fail_cv:
        return {"status": "fail", "pass": False}
    if cv > warn_cv:
        return {"status": "warn", "pass": True}
    return {"status": "pass", "pass": True}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", default=None, help="Experiment folder name under artifacts/")
    ap.add_argument("--trials", type=int, default=1, help="Number of runs to execute")
    ap.add_argument("--timeout-sec", type=int, default=DEFAULT_TIMEOUT_SEC)
    ap.add_argument("--cmd", default="make run-fresh", help='Command to run (default: "make run-fresh")')
    ap.add_argument("--workload", default=None, help="Workload selector string")
    ap.add_argument("--warn-cv", type=float, default=0.05)
    ap.add_argument("--fail-cv", type=float, default=0.10)
    args = ap.parse_args()

    exp_id = args.label or str(uuid.uuid4())[:8]
    exp_dir = os.path.join(ARTIFACTS_DIR, exp_id)

    trials_dir = os.path.join(exp_dir, "trials")
    os.makedirs(trials_dir, exist_ok=True)

    base_cmd = args.cmd.split()
    qemu_cmd = build_cmd(base_cmd, args.workload)

    trial_results = []
    for i in range(args.trials):
        run_dir = os.path.join(trials_dir, f"{i:03d}")
        print(f"\n=== Trial {i+1}/{args.trials} -> {run_dir} ===\n")

        meta, metrics = run_once(run_dir, args.timeout_sec, qemu_cmd, args.workload)
        ok = bool(meta.get("saw_done")) and (meta.get("done_code") == 0) and (meta.get("error") is None)

        trial_results.append({
            "trial_index": i,
            "ok": ok,
            "meta": meta,
            "metrics": metrics,
        })

    summary = aggregate_trials(trial_results)
    st = stability_status(summary.get("wall_cv"), args.warn_cv, args.fail_cv)
    summary["stability"] = {
        "status": st["status"],
        "pass": st["pass"],
        "warn_cv": args.warn_cv,
        "fail_cv": args.fail_cv,
    }
    summary["workload"] = args.workload
    summary["cmd"] = qemu_cmd

    save_json(os.path.join(exp_dir, "summary.json"), summary)
    save_json(os.path.join(exp_dir, "trials_index.json"), {
        "exp_id": exp_id,
        "cmd": qemu_cmd,
        "timeout_sec": args.timeout_sec,
        "workload": args.workload,
        "warn_cv": args.warn_cv,
        "fail_cv": args.fail_cv,
        "trials": [
            {
                "trial_index": t["trial_index"],
                "ok": t["ok"],
                "wall_ms": t["meta"].get("wall_ms"),
                "done_code": t["meta"].get("done_code"),
                "error": t["meta"].get("error"),
                "dir": os.path.join("trials", f"{t['trial_index']:03d}"),
            }
            for t in trial_results
        ],
    })

    print(f"\nSaved experiment artifacts to {exp_dir}\n")

    if summary["trials_ok"] == 0:
        sys.exit(1)
    if not summary["stability"]["pass"]:
        sys.exit(2)
    sys.exit(0)

if __name__ == "__main__":
    main()
