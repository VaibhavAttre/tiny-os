#!/usr/bin/env python3
import os, sys, json, subprocess, time, selectors, uuid
import argparse
import signal

ARTIFACTS_DIR = "artifacts"
TIMEOUT_SEC = 20

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", default=None)
    args = ap.parse_args()

    run_id = args.label or str(uuid.uuid4())[:8]
    out_dir = os.path.join(ARTIFACTS_DIR, run_id)
    os.makedirs(out_dir, exist_ok=True)

    qemu_cmd = ["make", "run-fresh"]
    start = time.time()

    # Local state
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
    )

    sel = selectors.DefaultSelector()
    sel.register(jproc.stdout, selectors.EVENT_READ)

    err = None
    try:
        while True:
            if time.time() - start > TIMEOUT_SEC:
                raise TimeoutError("Test timed out")

            events = sel.select(timeout=0.1)
            for key, _ in events:
                line = key.fileobj.readline()
                if not line:
                    break

                log_lines.append(line)

                # Metrics capture
                if "METRICS_BEGIN" in line:
                    in_metrics = True
                    metrics_lines = []
                    continue

                if "METRICS_END" in line:
                    in_metrics = False
                    with open(os.path.join(out_dir, "metrics.json"), "w") as f:
                        f.write("".join(metrics_lines))
                    continue

                if in_metrics:
                    metrics_lines.append(line)
                    continue

                # Echo normal lines
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
        with open(os.path.join(out_dir, "runner_error.txt"), "w") as f:
            f.write(err + "\n")

    finally:
        with open(os.path.join(out_dir, "kernel.log"), "w") as f:
            f.writelines(log_lines)

        meta = {
            "run_id": run_id,
            "cmd": qemu_cmd,
            "saw_ready": saw_ready,
            "saw_done": saw_done,
            "done_code": done_code,
            "timeout_sec": TIMEOUT_SEC,
            "wall_ms": int((time.time() - start) * 1000),
            "error": err,
        }
        with open(os.path.join(out_dir, "run_meta.json"), "w") as f:
            json.dump(meta, f, indent=2)

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

    print(f"\nSaved artifacts to {out_dir}\n")

    if not saw_done:
        sys.exit(1)
    sys.exit(0 if done_code == 0 else 1)

if __name__ == "__main__":
    main()
