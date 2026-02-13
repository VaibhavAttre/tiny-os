#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys
from typing import List, Dict, Any


def sh(cmd):

    try:
        return subprocess.check_output(cmd, text=True).strip()
    
    except subprocess.CalledProcessError as e:
        if e.output:
            print(e.output, file = sys.stderr)
        raise


def resolve_commit(commit):
    if commit != "HEAD":
        return commit

    return sh(["git", "rev-parse", "HEAD"])

def get_queue_url(queuename, region):

    return sh([
        "aws", "sqs", "get-queue-url", 
        "--queue-name", queuename,
        "--region", region,
        "--query", "QueueUrl",
        "--output", "text"
    ])

def parse_workload(workloads):
    
    parsed = [p.strip() for p in workloads.split(",")]
    return [p for p in parsed if p]

def merge_extra(payload, extra):
    try:
        extra = json.loads(extra)

    except Exception as e:
        raise SystemExit(f"ERROR: extra json not valid : {e}")
    
    if not isinstance(extra, dict):
        raise SystemExit("ERROR: extra json must be json")
    
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


def main():
    p = argparse.ArgumentParser(description="Submit a TinyOS run job to SQS.")
    p.add_argument("--commit", required=True, help="Git SHA or HEAD")

    # Workloads: either CSV or repeated flags
    p.add_argument("--workloads", default=None, help='Comma-separated workloads, e.g. "smoke,sleep50"')
    p.add_argument("--workload", action="append", default=[], help="Repeatable workload flag")

    p.add_argument("--repeat", type=int, default=1)
    p.add_argument("--timeout", type=int, default=30)

    p.add_argument("--region", default=os.getenv("AWS_REGION", "us-west-1"))
    p.add_argument("--queue-name", default=os.getenv("TINYOS_SQS_QUEUE_NAME", "tinyos-run-queue"))

    p.add_argument("--dry-run", action="store_true")
    p.add_argument(
        "--extra-json",
        default=None,
        help='Optional JSON object merged into payload (advanced), e.g. \'{"run_tag":"abc"}\'',
    )
    p.add_argument("--queue-url", default=os.getenv("TINYOS_SQS_QUEUE_URL"),
               help="Full SQS QueueUrl (bypass GetQueueUrl)")


    args = p.parse_args()

    commit = resolve_commit(args.commit)

    workloads: List[str] = []
    if args.workloads:
        workloads.extend(parse_workload(args.workloads))
    workloads.extend(args.workload)
    if not workloads:
        workloads = ["smoke"]

    payload: Dict[str, Any] = {
        "commit": commit,
        "workloads": workloads,
        "repeat": args.repeat,
        "timeout": args.timeout,
    }

    if args.extra_json:
        merge_extra(payload, args.extra_json)

    if args.queue_url:
        queue_url = args.queue_url
    else:
        queue_url = get_queue_url(args.queue_name, args.region)


    print(f"Region:    {args.region}")
    print(f"QueueName: {args.queue_name}")
    print(f"QueueUrl:  {queue_url}")
    print("Payload:")
    print(json.dumps(payload, indent=2))

    if args.dry_run:
        print("Dry run: not sending.")
        return

    resp = send_message(queue_url, payload, args.region)
    print("\nSent:")
    print(json.dumps(resp, indent=2))


if __name__ == "__main__":
    main()
