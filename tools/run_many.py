#!/usr/bin/env python3
import json, os, sys, time, subprocess

DEFAULT_WORKLOADS = ["smoke", "sleep50"]

def run_one(workload: str) -> int:

    return subprocess.call(["./tools/run_one.py", workload])

def load_json(path: str):
    with open(path, "r") as f:
        return json.load(f)

def main():
    workloads = sys.argv[1:] if len(sys.argv) > 1 else DEFAULT_WORKLOADS

    os.makedirs("artifacts", exist_ok=True)

    results = []
    any_fail = False

    for w in workloads:
        rc = run_one(w)
        meta_path = os.path.join("artifacts", w, "run_meta.json")
        metrics_path = os.path.join("artifacts", w, "metrics.json")

        meta = None
        metrics = None

        try:
            meta = load_json(meta_path)
        except Exception as e:
            meta = {"workload": w, "ok": False, "error": f"missing run_meta.json: {e}"}

        try:
            metrics = load_json(metrics_path)
        except Exception:
            metrics = None

        row = {
            "workload": w,
            "ok": bool(meta.get("ok", False)) and (rc == 0),
            "run_one_rc": rc,
            "done_code": meta.get("done_code"),
            "runner_rc": meta.get("runner_rc"),
            "error": meta.get("error"),
            "metrics": metrics,  
        }

        if not row["ok"]:
            any_fail = True

        results.append(row)

    index = {
        "timestamp_unix": int(time.time()),
        "workloads": workloads,
        "results": results,
        "ok": (not any_fail),
    }

    with open("artifacts/index.json", "w") as f:
        json.dump(index, f, indent=2)

    if index["ok"]:
        print("OK: all workloads passed. Wrote artifacts/index.json")
        sys.exit(0)
    else:
        print("FAIL: some workloads failed. See artifacts/index.json")
        sys.exit(1)

if __name__ == "__main__":
    main()
