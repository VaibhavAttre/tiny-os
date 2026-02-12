#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys
import time
sys.path.append(os.path.dirname(__file__))
from manifest_utils import get_git_info, get_tool_versions


def safe_read_json(path: str):
    try:
        with open(path, "r") as f:
            return json.load(f)
    except Exception:
        return None


def run_one(workload: str, repdir: str, timeout: float | None, kernel, disk) -> dict:
    os.makedirs(repdir, exist_ok=True)
    cmd = ["./tools/run_one.py", workload, "--out", repdir]
    if timeout is not None:
        cmd += ["--timeout", str(timeout)]
    if kernel and disk:
        cmd += ["--kernel", kernel, "--disk", disk]

    p = subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    return {
        "workload": workload,
        "run_one_rc": p.returncode,
        "stdout": p.stdout,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("workloads", nargs="*", help="workloads to run (default: smoke sleep50)")
    ap.add_argument("--out", default=None, help="output directory (default: artifacts/<run_id>)")
    ap.add_argument("--repeat", type=int, default=1, help="repeat each workload N times")
    ap.add_argument("--json", action="store_true", help="print compact summary JSON to stdout")
    ap.add_argument("--latest", action="store_true", help="write artifacts/LATEST pointing to this run dir")
    ap.add_argument("--timeout", type=float, default=None, help="pass timeout through to run_one.py")
    ap.add_argument("--fail-fast", action="store_true", help="stop early after first failure")
    ap.add_argument("--kernel", default=None, help="Prebuilt kernel.elf path")
    ap.add_argument("--disk", default=None, help="Prebuilt disk.img path")

    args = ap.parse_args()

    workloads = args.workloads if args.workloads else ["smoke", "sleep50"]
    if args.repeat < 1:
        print("--repeat must be >= 1", file=sys.stderr)
        return 2

    run_id = time.strftime("%Y%m%d_%H%M%S")
    outbase = args.out if args.out else os.path.join("artifacts", run_id)
    os.makedirs(outbase, exist_ok=True)

    results = []
    overall_ok = True
    stop = False

    for w in workloads:
        for rep in range(1, args.repeat + 1):
            repdir = os.path.join(outbase, w, f"rep{rep}")
            r = run_one(w, repdir, args.timeout, args.kernel, args.disk)

            meta = safe_read_json(os.path.join(repdir, "run_meta.json")) or {}
            metrics = safe_read_json(os.path.join(repdir, "metrics.json"))

            ok = bool(meta.get("ok")) and (r["run_one_rc"] == 0)
            if not ok:
                overall_ok = False
                if args.fail_fast:
                    stop = True

            results.append({
                "workload": w,
                "rep": rep,
                "ok": ok,
                "run_one_rc": r["run_one_rc"],
                "runner_stdout_tail": "\n".join((r.get("stdout") or "").splitlines()[-6:]),
                "done_code": meta.get("done_code"),
                "runner_rc": meta.get("runner_rc"),
                "terminated_by_runner": meta.get("terminated_by_runner"),
                "timed_out": meta.get("timed_out"),
                "error": meta.get("error"),
                "metrics": metrics,
                "outdir": repdir,
            })

            if stop:
                break
        if stop:
            break

    index = {
        "run_id": run_id,
        "timestamp_unix": int(time.time()),
        "outbase": outbase,
        "workloads": workloads,
        "repeat": args.repeat,
        "timeout_sec": args.timeout,
        "fail_fast": args.fail_fast,
        "results": results,
        "ok": overall_ok,
        "schema_version": 1,
    }

    with open(os.path.join(outbase, "index.json"), "w") as f:
        json.dump(index, f, indent=2, sort_keys=True)
 
    run_manifest = {
        "schema_version": 1,
        "run_id": run_id,
        "created_at_unix": index["timestamp_unix"],

        "git": get_git_info(),
        "tools": get_tool_versions(),

        "run": {
            "workloads": workloads,
            "repeat": args.repeat,
            "timeout_sec": args.timeout,
            "fail_fast": args.fail_fast,
        },

        "artifacts": {
            "index": "index.json",
  
            "outbase": outbase,
        },
    }

    with open(os.path.join(outbase, "manifest.json"), "w") as f:
        json.dump(run_manifest, f, indent=2, sort_keys=True)


    subprocess.check_call(["./tools/validate_run.py", outbase])

    if args.latest:
        os.makedirs("artifacts", exist_ok=True)
        with open(os.path.join("artifacts", "LATEST"), "w") as f:
            f.write(outbase + "\n")

    if overall_ok:
        print(f"OK: all workloads passed. Wrote {outbase}/index.json")
        rc = 0
    else:
        print(f"FAIL: some workloads failed. Wrote {outbase}/index.json")
        rc = 1

    if args.json:
        print(json.dumps({"ok": overall_ok, "outbase": outbase, "run_id": run_id}))

    return rc


if __name__ == "__main__":
    sys.exit(main())
