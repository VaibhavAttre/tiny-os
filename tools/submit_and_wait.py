#!/usr/bin/env python3
import argparse
import json
import os
import re
import subprocess
import sys

JOB_RE = re.compile(r'("run_id"\s*:\s*"[^"]+".*"created_at_unix"\s*:\s*\d+)', re.DOTALL)

def extract_job(stdout: str):
    """
    Try hard to find {run_id, created_at_unix} from send_job.py output.
    Best case: send_job prints a JSON object on one line.
    Fallback: search for a JSON-ish substring.
    """
    # First try: parse any JSON object per line
    for line in stdout.splitlines():
        line = line.strip()
        if line.startswith("{") and line.endswith("}"):
            try:
                obj = json.loads(line)
                if "run_id" in obj and "created_at_unix" in obj:
                    return obj["run_id"], int(obj["created_at_unix"])
            except Exception:
                pass

    # Fallback: regex find a JSON fragment
    m = JOB_RE.search(stdout)
    if m:
        frag = "{" + m.group(1) + "}"
        try:
            obj = json.loads(frag)
            return obj["run_id"], int(obj["created_at_unix"])
        except Exception:
            pass

    return None, None

def main():
    ap = argparse.ArgumentParser(description="Submit a job and wait for it to finish.")
    ap.add_argument("--region", default=os.environ.get("AWS_REGION", os.environ.get("AWS_DEFAULT_REGION", "us-west-2")))
    ap.add_argument("--table", default=os.environ.get("TINYOS_DDB_TABLE"))
    ap.add_argument("--queue-url", default=os.environ.get("TINYOS_SQS_QUEUE_URL"))

    # forward these to send_job.py (match your send_job flags)
    ap.add_argument("--workloads", nargs="+", default=["smoke"])
    ap.add_argument("--repeat", type=int, default=1)
    ap.add_argument("--timeout", type=int, default=30)
    ap.add_argument("--fail-fast", action="store_true")

    # wait options
    ap.add_argument("--poll-sec", type=float, default=2.0)
    ap.add_argument("--wait-timeout-sec", type=int, default=0)

    args = ap.parse_args()

    # 1) Submit
    cmd = [
        sys.executable, "tools/send_job.py",
        "--region", args.region,
        "--workloads", *args.workloads,
        "--repeat", str(args.repeat),
        "--timeout", str(args.timeout),
    ]
    if args.table:
        cmd += ["--table", args.table]
    if args.queue_url:
        cmd += ["--queue-url", args.queue_url]
    if args.fail_fast:
        cmd += ["--fail-fast"]

    print("[submit_and_wait] submitting:", " ".join(cmd))
    p = subprocess.run(cmd, text=True, capture_output=True)
    sys.stdout.write(p.stdout)
    sys.stderr.write(p.stderr)
    if p.returncode != 0:
        print("[submit_and_wait] ERROR: send_job.py failed", file=sys.stderr)
        return p.returncode

    run_id, created_at_unix = extract_job(p.stdout)
    if not run_id:
        print("[submit_and_wait] ERROR: could not extract run_id/created_at_unix from send_job output.", file=sys.stderr)
        print("Fix: have send_job.py print a JSON line like:", file=sys.stderr)
        print('  {"run_id":"...","created_at_unix":123}', file=sys.stderr)
        return 2

    print(f"[submit_and_wait] job: run_id={run_id} created_at_unix={created_at_unix}")

    # 2) Wait
    cmd2 = [
        sys.executable, "tools/wait_job.py",
        "--region", args.region,
        "--run-id", run_id,
        "--created-at-unix", str(created_at_unix),
        "--poll-sec", str(args.poll_sec),
    ]
    if args.table:
        cmd2 += ["--table", args.table]
    if args.wait_timeout_sec:
        cmd2 += ["--timeout-sec", str(args.wait_timeout_sec)]

    print("[submit_and_wait] waiting:", " ".join(cmd2))
    return subprocess.call(cmd2)

if __name__ == "__main__":
    raise SystemExit(main())
