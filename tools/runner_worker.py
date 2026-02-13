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
        print(p.stderr.strip(), file = sys.stderr, flush=True)
        time.sleep(2)
        return None
    
    if not p.stdout.strip():
        return None
    
    obj = json.loads(p.stdout)
    msgs = obj.get("Messages", [])
    return msgs[0] if msgs else None

def delete_msg(region, queue_url, recepit):

    run(["aws", "sqs", "delete-message",
        "--region", region,
        "--queue-url", queue_url,
        "--receipt-handle", recepit])
    

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

        try:
            job = json.loads(body)
            commit = job["commit"]
            workloads = job.get("workloads", ["smoke"])
            repeat = int(job.get("repeat", 1))
            timeout = int(job.get("timeout", 30))
            fail_fast = bool(job.get("fail_fast", False))

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

            delete_msg(args.region, args.queue_url, receipt)
            print("OK: job complete; message deleted", flush=True)

        except Exception as e:
            print(f"ERROR: job failed ({e}); leaving message for retry", file=sys.stderr, flush=True)
            time.sleep(3)

if __name__ == "__main__":
    main()