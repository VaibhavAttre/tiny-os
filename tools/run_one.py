#!/usr/bin/env python3
import json, os, re, subprocess, sys, time, signal

BEGIN = "METRICS_BEGIN"
END   = "METRICS_END"

DONE_RE = re.compile(r"\bDONE\s+(-?\d+)\b")

def extract_metrics(log: str) -> dict:
    i = log.find(BEGIN)
    j = log.find(END)
    if i < 0 or j < 0 or j < i:
        raise ValueError("couldn't find METRICS_BEGIN / METRICS_END")
    payload = log[i + len(BEGIN): j].strip()
    return json.loads(payload)

def extract_done_code(log: str) -> int:
    m = DONE_RE.search(log)
    if not m:
        raise ValueError("could not find DONE <code>")
    return int(m.group(1))

def runproc(workload: str):
    """
    Runs: make run-fresh WORKLOAD=<workload>
    Captures output until DONE appears, then kills the whole process group.
    Returns (rc, full_log).
    rc will often be -15 (SIGTERM) because we intentionally stop QEMU.
    """
    # Start make in its own process group so we can kill the whole tree (make + qemu).
    p = subprocess.Popen(
        ["make", "run-fresh", f"WORKLOAD={workload}"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        preexec_fn=os.setsid,  # new process group (POSIX)
    )

    out_lines = []
    done_code = None

    assert p.stdout is not None
    for line in p.stdout:
        out_lines.append(line)
        m = DONE_RE.search(line)
        if m:
            done_code = int(m.group(1))
            # Keep reading a tiny bit more? not necessary; break immediately.
            break

    # If we saw DONE, stop the whole group (make + qemu)
    if done_code is not None:
        try:
            os.killpg(p.pid, signal.SIGTERM)
        except Exception:
            pass

    # Drain any remaining buffered output quickly
    try:
        rest = p.stdout.read()
        if rest:
            out_lines.append(rest)
    except Exception:
        pass

    # Wait for process to exit
    try:
        rc = p.wait(timeout=3)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except Exception:
            pass
        rc = p.wait()

    return rc, "".join(out_lines)

def main():
    if len(sys.argv) != 2:
        print("usage: tools/run_one.py <workload>")
        sys.exit(2)

    workload = sys.argv[1]
    rc, log = runproc(workload)

    done = None
    metrics = None
    err = None

    try:
        done = extract_done_code(log)
        metrics = extract_metrics(log)
    except Exception as e:
        err = str(e)

    outdir = os.path.join("artifacts", workload)
    os.makedirs(outdir, exist_ok=True)

    with open(os.path.join(outdir, "kernel.log"), "w") as f:
        f.write(log)

    # Success criteria: we parsed DONE=0 and metrics successfully
    ok = (err is None and done == 0)

    run_meta = {
        "workload": workload,
        "runner_rc": rc,                       # usually -15 because we SIGTERM the group
        "done_code": done,
        "ok": ok,
        "error": err,
        "timestamp_unix": int(time.time()),
        "terminated_by_runner": (done == 0 and rc in (-15, 0)),  # informational
    }

    with open(os.path.join(outdir, "run_meta.json"), "w") as f:
        json.dump(run_meta, f, indent=2)

    if metrics is not None:
        with open(os.path.join(outdir, "metrics.json"), "w") as f:
            json.dump(metrics, f, indent=2)

    if ok:
        print(f"OK: wrote {outdir}/metrics.json")
        sys.exit(0)
    else:
        print("FAIL")
        sys.exit(1)

if __name__ == "__main__":
    main()
