#!/usr/bin/env python3
import argparse
import os
import sys
import time
import json
from datetime import datetime

import boto3

TERMINAL = {"succeeded", "failed", "canceled"}

def fmt_ts(unix):
    if unix is None:
        return None
    try:
        return datetime.fromtimestamp(int(unix)).isoformat()
    except Exception:
        return str(unix)

def main():
    ap = argparse.ArgumentParser(description="Wait for a TinyOS job to finish by polling DynamoDB.")
    ap.add_argument("--table", default=os.environ.get("TINYOS_DDB_TABLE"), help="DynamoDB table name (or env TINYOS_DDB_TABLE)")
    ap.add_argument("--region", default=os.environ.get("AWS_REGION", os.environ.get("AWS_DEFAULT_REGION", "us-west-2")),
                    help="AWS region (or env AWS_REGION/AWS_DEFAULT_REGION)")
    ap.add_argument("--run-id", required=True)
    ap.add_argument("--created-at-unix", required=True, type=int)
    ap.add_argument("--poll-sec", type=float, default=2.0)
    ap.add_argument("--timeout-sec", type=int, default=0, help="0 = no timeout")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    if not args.table:
        print("ERROR: --table or env TINYOS_DDB_TABLE is required", file=sys.stderr)
        return 2

    ddb = boto3.resource("dynamodb", region_name=args.region)
    table = ddb.Table(args.table)

    key = {"run_id": args.run_id, "created_at_unix": args.created_at_unix}

    last_status = None
    start = time.time()

    while True:
        if args.timeout_sec and (time.time() - start) > args.timeout_sec:
            print("ERROR: wait timed out", file=sys.stderr)
            return 2

        resp = table.get_item(Key=key, ConsistentRead=True)
        item = resp.get("Item")

        if not item:
            if not args.quiet:
                print(f"[wait_job] not found yet: run_id={args.run_id} created_at_unix={args.created_at_unix}")
            time.sleep(args.poll_sec)
            continue

        status = item.get("status", "unknown")

        if status != last_status and not args.quiet:
            started = item.get("started_at_unix")
            finished = item.get("finished_at_unix")
            print(f"[wait_job] status: {last_status} -> {status}")
            if started is not None:
                print(f"  started_at:  {fmt_ts(started)}")
            if finished is not None:
                print(f"  finished_at: {fmt_ts(finished)}")
            err = item.get("error")
            if err:
                print(f"  error: {err}")
            s3p = item.get("s3_prefix")
            if s3p:
                print(f"  s3_prefix: {s3p}")

        last_status = status

        if status in TERMINAL:
            # Final summary (always)
            out = {
                "run_id": item.get("run_id"),
                "created_at_unix": item.get("created_at_unix"),
                "status": item.get("status"),
                "ok": item.get("ok"),
                "s3_prefix": item.get("s3_prefix"),
                "outbase": item.get("outbase"),
                "error": item.get("error"),
            }
            print(json.dumps(out, indent=2, sort_keys=True))

            if status == "succeeded" and item.get("ok") is True:
                return 0
            return 1

        time.sleep(args.poll_sec)

if __name__ == "__main__":
    raise SystemExit(main())
