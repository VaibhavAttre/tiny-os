#!/usr/bin/env python3
import argparse, json, subprocess, sys, time

def run(cmd, **kwargs):
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(cmd, check=True, text=True, **kwargs)

def receive_one(region, queue_url, wait=20):
    p = subprocess.run(
        ["aws", "sqs", "receive-message",
         "--region", region,
         "--queue-url", queue_url,
         "--wait-time-seconds", str(wait),
         "--max-number-of-messages", "1"],
        text=True, capture_output=True
    )

    if p.returncode != 0:
        print(p.stderr.strip(), file=sys.stderr, flush=True)
        time.sleep(2)
        return None

    if not p.stdout.strip():
        return None

    obj = json.loads(p.stdout)
    msgs = obj.get("Messages", [])
    return msgs[0] if msgs else None

def delete_msg(region, queue_url, receipt):
    run(["aws", "sqs", "delete-message",
        "--region", region,
        "--queue-url", queue_url,
        "--receipt-handle", receipt])

def update_status(args, run_id, created_at_unix, status, message_id=None, started=None, finished=None, error=None):
    cmd = [
        "python3", "tools/job_status.py",
        "--table", args.table,
        "--region", args.region,
        "--run-id", run_id,
        "--created-at-unix", str(created_at_unix),
        "--status", status,
    ]
    if message_id:
        cmd += ["--message-id", message_id]
    if started is not None:
        cmd += ["--started-at-unix", str(started)]
    if finished is not None:
        cmd += ["--finished-at-unix", str(finished)]
    if error:
        cmd += ["--error", str(error)[:900]]  # keep it short-ish for DDB
    run(cmd)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--queue-url", required=True)
    ap.add_argument("--region", required=True)
    ap.add_argument("--bucket", required=True)
    ap.add_argument("--table", required=True)
    ap.add_argument("--poll-wait", type=int, default=20)
    args = ap.parse_args()

    while True:
        msg = receive_one(args.region, args.queue_url, wait=args.poll_wait)
        if not msg:
            continue

        receipt = msg["ReceiptHandle"]
        body = msg["Body"]

        # helpful identifiers
        message_id = msg.get("MessageId")

        try:
            job = json.loads(body)

            # --- Milestone D: require these in the submitted job body ---
            run_id = job["run_id"]
            created_at_unix = int(job["created_at_unix"])

            commit = job["commit"]

            import re
            if not re.fullmatch(r"[0-9a-f]{7,40}", commit):
                print(f"BAD JOB (invalid commit): {commit!r} -> marking failed + deleting message", flush=True)
                # mark failed (if row exists)
                try:
                    update_status(args, run_id, created_at_unix, "failed",
                                  message_id=message_id, finished=int(time.time()),
                                  error=f"invalid commit {commit!r}")
                except Exception as ee:
                    print(f"warn: failed to update DDB status: {ee}", file=sys.stderr, flush=True)
                delete_msg(args.region, args.queue_url, receipt)
                continue

            workloads = job.get("workloads", ["smoke"])
            repeat = int(job.get("repeat", 1))
            timeout = int(job.get("timeout", 30))
            fail_fast = bool(job.get("fail_fast", False))

            # --- status = running ---
            started = int(time.time())
            update_status(args, run_id, created_at_unix, "running",
                          message_id=message_id, started=started)

            cmd = [
                "python3", "./tools/run_cloud_prebuilt.py",
                "--commit", commit,
                "--bucket", args.bucket,
                "--table", args.table,
                "--region", args.region,
                "--workloads", *workloads,
                "--repeat", str(repeat),
                "--timeout", str(timeout),
            ]
            if fail_fast:
                cmd.append("--fail-fast")

            run(cmd)

            # --- status = succeeded ---
            finished = int(time.time())
            update_status(args, run_id, created_at_unix, "succeeded",
                          message_id=message_id, finished=finished)

            delete_msg(args.region, args.queue_url, receipt)
            print("OK: job complete; message deleted", flush=True)

        except Exception as e:
            
            print(f"ERROR: job failed ({e}); leaving message for retry", file=sys.stderr, flush=True)
            try:
                
                now = int(time.time())
                if "job" in locals() and isinstance(job, dict) and "run_id" in job and "created_at_unix" in job:
                    update_status(args, job["run_id"], int(job["created_at_unix"]), "failed",
                                  message_id=message_id, finished=now, error=str(e))
            except Exception as ee:
                print(f"warn: failed to update DDB status: {ee}", file=sys.stderr, flush=True)

            time.sleep(3)

if __name__ == "__main__":
    main()
