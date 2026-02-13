#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys
import time
from typing import List, Dict, Any


def sh(cmd):
    try:
        return subprocess.check_output(cmd, text=True).strip()
    except subprocess.CalledProcessError as e:
        if e.output:
            print(e.output, file=sys.stderr)
        raise


def resolve_commit(commit: str) -> str:
    if commit == "HEAD":
        return sh(["git", "rev-parse", "HEAD"])
    return commit


def get_queue_url(queuename, region):
    return sh([
        "aws", "sqs", "get-queue-url",
        "--queue-name", queuename,
        "--region", region,
        "--query", "QueueUrl",
        "--output", "text"
    ])


def parse_workloads(csv: str) -> List[str]:
    parsed = [p.strip() for p in csv.split(",")]
    return [p for p in parsed if p]


def merge_extra(payload, extra):
    try:
        extra = json.loads(extra)
    except Exception as e:
        raise SystemExit(f"ERROR: extra json not valid: {e}")

    if not isinstance(extra, dict):
        raise SystemExit("ERROR: extra json must be an object")
    payload.update(extra)


def send_message(queue_url, payload, region):
    out = sh([
        "aws", "sqs", "send-message",
        "--queue-url", queue_url,
        "--message-body", json.dumps(payload, separators=(",", ":")),
        "--region", region,
        "--output", "json",
    ])
    return json.loads(out)


def write_queued_status(region, table, run_id, created_at_unix, message_id):
    cmd = [
        "python3", "tools/job_status.py",
        "--table", table,
        "--region", region,
        "--run-id", run_id,
        "--created-at-unix", str(created_at_unix),
        "--status", "queued",
        "--message-id", message_id,
    ]
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, text=True)


def main():
    p = argparse.ArgumentParser(description="Submit a TinyOS run job to SQS (+ mark queued in Dynamo).")

    p.add_argument("--commit", default="HEAD", help="Git SHA or HEAD (default: HEAD)")

    p.add_argument("--workloads", default=None, help='Comma-separated workloads, e.g. "smoke,sleep50"')
    p.add_argument("--workload", action="append", default=[], help="Repeatable workload flag")
    p.add_argument("--repeat", type=int, default=1)
    p.add_argument("--timeout", type=int, default=30)
    p.add_argument("--fail-fast", action="store_true")

    p.add_argument("--region", default=os.getenv("AWS_REGION", "us-west-1"))
    p.add_argument("--queue-name", default=os.getenv("TINYOS_SQS_QUEUE_NAME", "tinyos-run-queue"))
    p.add_argument("--queue-url", default=os.getenv("TINYOS_SQS_QUEUE_URL"),
                   help="Full SQS QueueUrl (bypass GetQueueUrl)")

    p.add_argument("--table", default=os.getenv("TINYOS_DDB_TABLE"),
                   help="Dynamo table (or env TINYOS_DDB_TABLE)")

    p.add_argument("--dry-run", action="store_true")
    p.add_argument("--extra-json", default=None,
                   help='Optional JSON object merged into payload, e.g. \'{"run_tag":"abc"}\'')

    args = p.parse_args()

    commit = resolve_commit(args.commit)

    workloads: List[str] = []
    if args.workloads:
        workloads.extend(parse_workloads(args.workloads))
    workloads.extend(args.workload)
    if not workloads:
        workloads = ["smoke"]

    run_id = time.strftime("%Y%m%d_%H%M%S")
    created_at_unix = int(time.time())

    payload: Dict[str, Any] = {
        "run_id": run_id,
        "created_at_unix": created_at_unix,
        "commit": commit,
        "workloads": workloads,
        "repeat": args.repeat,
        "timeout": args.timeout,
        "fail_fast": bool(args.fail_fast),
    }

    if args.extra_json:
        merge_extra(payload, args.extra_json)

    queue_url = args.queue_url or get_queue_url(args.queue_name, args.region)

    print(f"Region:    {args.region}")
    print(f"QueueUrl:  {queue_url}")
    print("Payload:")
    print(json.dumps(payload, indent=2))

    if args.dry_run:
        print("Dry run: not sending.")
        return

    resp = send_message(queue_url, payload, args.region)
    print("\nSent:")
    print(json.dumps(resp, indent=2))

    message_id = resp.get("MessageId")
    if args.table and message_id:
        write_queued_status(args.region, args.table, run_id, created_at_unix, message_id)
        print("OK: wrote Dynamo status=queued")
    else:
        print("WARN: queued status not written (missing --table or MessageId).", file=sys.stderr)

    print("\nRun keys:")
    print(f"  run_id={run_id}")
    print(f"  created_at_unix={created_at_unix}")


if __name__ == "__main__":
    main()
