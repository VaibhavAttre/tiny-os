#!/usr/bin/env python3
import argparse
import json
import os
import re
import selectors
import signal
import subprocess
import sys
import time

BEGIN = "METRICS_BEGIN"
END = "METRICS_END"
DONE_RE = re.compile(r"\bDONE\s+(-?\d+)\b")

def get_git_info():

    try:
        commit = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
        dirty = subprocess.check_output(["git", "status", "--porcelain"], text=True).strip() != ""
        return {"commit": commit, "dirty": dirty}
    except Exception:
        return {"commit": "unknown", "dirty": None}


def get_tool_versions():
    out = {}
    try:

        qemu_ver = subprocess.check_output(["qemu-system-riscv64", "--version"], text=True).splitlines()[0].strip()
        out["qemu_system_riscv64"] = qemu_ver
    except Exception:
        out["qemu_system_riscv64"] = "unknown"
    return out


def extract_metrics(log: str):
    i = log.find(BEGIN)
    j = log.find(END)
    if i < 0 or j < 0 or j < i:
        raise ValueError("could not find METRICS_BEGIN/END")
    payload = log[i + len(BEGIN): j].strip()
    return json.loads(payload)


def extract_done_code(log: str):
    m = DONE_RE.search(log)
    if not m:
        raise ValueError("could not find DONE <code>")
    return int(m.group(1))


def write_json(path: str, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(obj, f, indent=2, sort_keys=True)
    os.replace(tmp, path)


def run_make(cmd, stop_on_done: bool, terminate_timeout_s: float, timeout_s: float | None):
    #cmd = ["make", "run-fresh", f"WORKLOAD={workload}"]

    p = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        start_new_session=True,
    )

    assert p.stdout is not None

    sel = selectors.DefaultSelector()
    sel.register(p.stdout, selectors.EVENT_READ)

    out_lines: list[str] = []
    done_code = None
    terminated_by_runner = False
    timed_out = False

    t0 = time.time()

    def kill_group(sig, wait_s=None):
        nonlocal terminated_by_runner
        terminated_by_runner = True
        try:
            os.killpg(p.pid, sig)
        except Exception:
            return
        if wait_s is not None:
            try:
                p.wait(timeout=wait_s)
            except Exception:
                return

    try:
        while True:
            if timeout_s is not None and (time.time() - t0) > timeout_s:
                timed_out = True
                kill_group(signal.SIGTERM, wait_s=terminate_timeout_s)
                if p.poll() is None:
                    kill_group(signal.SIGKILL, wait_s=1.0)
                break

            events = sel.select(timeout=0.1)
            if not events:
                if p.poll() is not None:
                    break
                continue

            for key, _ in events:
                line = key.fileobj.readline()
                if not line:
                    continue

                out_lines.append(line)
                sys.stdout.write(line)
                sys.stdout.flush()

                m = DONE_RE.search(line)
                if m:
                    done_code = int(m.group(1))
                    if stop_on_done:
                        kill_group(signal.SIGTERM, wait_s=terminate_timeout_s)
                        if p.poll() is None:
                            kill_group(signal.SIGKILL, wait_s=1.0)
                        return p.poll(), "".join(out_lines), done_code, terminated_by_runner, timed_out

    finally:
        try:
            sel.unregister(p.stdout)
        except Exception:
            pass

    if p.poll() is None:
        p.wait()

    return p.returncode, "".join(out_lines), done_code, terminated_by_runner, timed_out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("workload", help="workload name passed through to make/run_workload")
    ap.add_argument("--out", default=None, help="output directory for artifacts (default: artifacts/<workload>)")
    ap.add_argument("--no-terminate", action="store_true", help="do not terminate runner after DONE is seen")
    ap.add_argument("--timeout", type=float, default=None, help="kill the run if DONE not seen within this many seconds")
    ap.add_argument("--terminate-timeout", type=float, default=2.0, help="seconds to wait after terminate before kill")
    ap.add_argument("--kernel", default= None, help="default kernel to use (kernel.elf)")
    ap.add_argument("--disk", default= None, help="default disk.img")
    
    args = ap.parse_args()

    workload = args.workload
    outdir = args.out if args.out else os.path.join("artifacts", workload)
    os.makedirs(outdir, exist_ok=True)

    cmd = None
    if args.kernel and args.disk:
        
        disk_run = os.path.join(outdir, "disk.img")
        subprocess.check_call(["cp", args.disk, disk_run])

        cmd = [
            "qemu-system-riscv64",
            "-machine", "virt",
            "-smp", "1",
            "-bios", "none",
            "-kernel", args.kernel,
            "-append", workload,
            "-drive", f"file={disk_run},if=none,format=raw,id=hd0",
            "-device", "virtio-blk-device,drive=hd0",
            "-nographic",
        ]
    else:
        
        cmd = ["make", "run-fresh", f"WORKLOAD={workload}"]

    start_unix = time.time()
    rc, log, saw_done_code, terminated_by_runner, timed_out = run_make(
        cmd,
        workload,
        stop_on_done=(not args.no_terminate),
        terminate_timeout_s=args.terminate_timeout,
        timeout_s=args.timeout,
    )


    end_unix = time.time()

    done = None
    metrics = None
    err = None

    if timed_out:
        err = "timeout"

    if err is None:
        try:
            done = extract_done_code(log)
        except Exception as e:
            err = str(e)

    if err is None:
        try:
            metrics = extract_metrics(log)
        except Exception as e:
            err = str(e)

    with open(os.path.join(outdir, "kernel.log"), "w") as f:
        f.write(log)

    ok = (err is None) and (done == 0) and (not timed_out)

    meta = {
        "workload": workload,
        "start_unix": start_unix,
        "end_unix": end_unix,
        "wall_ms": int((end_unix - start_unix) * 1000),
        "cmd": cmd,
        "runner_rc": rc,
        "done_code": done,
        "terminated_by_runner": terminated_by_runner,
        "timed_out": timed_out,
        "ok": ok,
    }

    if err is not None:
        meta["error"] = err
    if timed_out:
        meta["timeout_sec"] = args.timeout

    write_json(os.path.join(outdir, "run_meta.json"), meta)

    if metrics is not None:
        if "schema_version" not in metrics:
            metrics = dict(metrics)
            metrics["schema_version"] = 1
        
        write_json(os.path.join(outdir, "metrics.json"), metrics)

    manifest = {

        "schema_version": 1, 
        "created_at_unix": start_unix,
        "workload": workload,
        "outdir": outdir,
        "git": get_git_info(),
        "tools": get_tool_versions(),
        
        "artifacts": {
            "kernel_log": "kernel.log",
            "run_meta": "run_meta.json",
            "metrics" : "metrics.json" if metrics is not None else None,
        },
    }

    write_json(os.path.join(outdir, "manifest.json"), manifest)

    print(f"OK: wrote {outdir}/metrics.json" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
