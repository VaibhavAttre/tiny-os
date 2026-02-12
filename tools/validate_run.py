#!/usr/bin/env python3
import json
import os
import sys

REQUIRED_INDEX_KEYS = {
    "schema_version": int,
    "run_id": str,
    "timestamp_unix": int,
    "outbase": str,
    "workloads": list,
    "repeat": int,
    "results": list,
    "ok": bool,
}

REQUIRED_RESULT_KEYS = {
    "workload": str,
    "rep": int,
    "ok": bool,
    "run_one_rc": int,
    "done_code": (int, type(None)),
    "runner_rc": (int, type(None)),
    "terminated_by_runner": (bool, type(None)),
    "timed_out": (bool, type(None)),
    "error": (str, type(None)),
    "metrics": (dict, type(None)),
    "outdir": str,
}

REQUIRED_REP_FILES = ["kernel.log", "run_meta.json", "metrics.json"]

REQUIRED_METRICS_KEYS = {
    "schema_version": int,
    "workload": str,
    "version": int,
    "ticks": int,
    "ctx_switches": int,
    "disk_reads": int,
    "disk_writes": int,
    "disk_read_bytes": int,
    "disk_write_bytes": int,
    "page_faults": int,
    "syscall_enter": int,
    "syscall_exit": int,
}

def load_json(path: str):
    with open(path, "r") as f:
        return json.load(f)

def add_err(errors, msg):
    errors.append(msg)

def check_key_types(errors, obj, required, where):
    if not isinstance(obj, dict):
        add_err(errors, f"{where}: expected object, got {type(obj).__name__}")
        return
    for k, t in required.items():
        if k not in obj:
            add_err(errors, f"{where}: missing key '{k}'")
            continue
        if not isinstance(obj[k], t):
            # t might be a tuple of allowed types
            expected = t if isinstance(t, tuple) else (t,)
            exp_names = ", ".join(tt.__name__ for tt in expected)
            add_err(errors, f"{where}: key '{k}' type {type(obj[k]).__name__}, expected {exp_names}")

def main():
    if len(sys.argv) != 2:
        print("usage: validate_run.py <artifacts/run_dir>", file=sys.stderr)
        return 2

    run_dir = sys.argv[1]
    errors = []

    if not os.path.isdir(run_dir):
        print(f"ERROR: not a directory: {run_dir}", file=sys.stderr)
        return 1

    index_path = os.path.join(run_dir, "index.json")
    if not os.path.exists(index_path):
        print(f"ERROR: missing {index_path}", file=sys.stderr)
        return 1

    # ---- index.json ----
    try:
        index = load_json(index_path)
    except Exception as e:
        print(f"ERROR: failed to parse {index_path}: {e}", file=sys.stderr)
        return 1

    check_key_types(errors, index, REQUIRED_INDEX_KEYS, "index.json")

    if isinstance(index.get("schema_version"), int) and index["schema_version"] != 1:
        add_err(errors, f"index.json: unsupported schema_version {index['schema_version']} (expected 1)")

    # outbase consistency
    outbase = index.get("outbase")
    if isinstance(outbase, str):
        # outbase is usually run_dir; if it isn't, at least ensure it points to the same place
        if os.path.normpath(outbase) != os.path.normpath(run_dir):
            # not fatal, but warn as an error for strictness
            add_err(errors, f"index.json: outbase='{outbase}' does not match run_dir='{run_dir}'")

    results = index.get("results", [])
    if isinstance(results, list):
        for i, r in enumerate(results):
            where_r = f"index.json.results[{i}]"
            check_key_types(errors, r, REQUIRED_RESULT_KEYS, where_r)

            outdir = r.get("outdir")
            if not isinstance(outdir, str):
                continue

            # your run_many writes absolute-ish relative paths like artifacts/<run_id>/<w>/repN
            rep_dir = outdir if os.path.isabs(outdir) else outdir
            if not os.path.isdir(rep_dir):
                add_err(errors, f"{where_r}: outdir not found as directory: {outdir}")
                continue

            # required rep files
            for fname in REQUIRED_REP_FILES:
                p = os.path.join(rep_dir, fname)
                if not os.path.exists(p):
                    add_err(errors, f"{where_r}: missing file {p}")

            # metrics.json validation
            metrics_path = os.path.join(rep_dir, "metrics.json")
            if os.path.exists(metrics_path):
                try:
                    m = load_json(metrics_path)
                except Exception as e:
                    add_err(errors, f"{where_r}: failed to parse {metrics_path}: {e}")
                    m = None

                if isinstance(m, dict):
                    check_key_types(errors, m, REQUIRED_METRICS_KEYS, f"{where_r}.metrics.json")
                    if m.get("schema_version") != 1:
                        add_err(errors, f"{where_r}.metrics.json: schema_version {m.get('schema_version')} (expected 1)")
                    if isinstance(r.get("workload"), str) and m.get("workload") != r.get("workload"):
                        add_err(errors, f"{where_r}: workload mismatch (index={r.get('workload')} metrics.json={m.get('workload')})")

            # kernel.log should contain DONE marker
            klog = os.path.join(rep_dir, "kernel.log")
            if os.path.exists(klog):
                try:
                    txt = open(klog, "r", errors="ignore").read()
                    if "DONE " not in txt:
                        add_err(errors, f"{where_r}: kernel.log missing 'DONE <code>' marker")
                except Exception as e:
                    add_err(errors, f"{where_r}: failed reading {klog}: {e}")

            # run_meta.json should be parseable + contain ok boolean if present
            meta_path = os.path.join(rep_dir, "run_meta.json")
            if os.path.exists(meta_path):
                try:
                    meta = load_json(meta_path)
                except Exception as e:
                    add_err(errors, f"{where_r}: failed to parse {meta_path}: {e}")
                    meta = None
                if isinstance(meta, dict) and "ok" in meta and not isinstance(meta["ok"], bool):
                    add_err(errors, f"{where_r}: run_meta.json 'ok' must be bool")

    # Final verdict
    if errors:
        print("INVALID RUN BUNDLE:", file=sys.stderr)
        for e in errors:
            print(" - " + e, file=sys.stderr)
        return 1

    print("OK: run bundle valid")
    return 0

if __name__ == "__main__":
    sys.exit(main())
