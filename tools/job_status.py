#!/usr/bin/env python3
import os, argparse, time, json, sys
import boto3

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--table", default=os.getenv("TINYOS_DDB_TABLE"), required=False)
    ap.add_argument("--region", default=os.getenv("AWS_REGION", "us-west-1"))
    ap.add_argument("--run-id", required=True)
    ap.add_argument("--created-at-unix", type=int, required=True)
    ap.add_argument("--status", required=True)  # queued|running|succeeded|failed
    ap.add_argument("--message-id", default=None)
    ap.add_argument("--started-at-unix", type=int, default=None)
    ap.add_argument("--finished-at-unix", type=int, default=None)
    ap.add_argument("--error", default=None)
    args = ap.parse_args()

    if not args.table:
        print("error: missing --table (or env TINYOS_DDB_TABLE)", file=sys.stderr)
        return 2

    ddb = boto3.resource("dynamodb", region_name=args.region).Table(args.table)

    updates = {"status": args.status}
    if args.message_id is not None: updates["message_id"] = args.message_id
    if args.started_at_unix is not None: updates["started_at_unix"] = args.started_at_unix
    if args.finished_at_unix is not None: updates["finished_at_unix"] = args.finished_at_unix
    if args.error is not None: updates["error"] = args.error

    set_parts = []
    names = {}
    vals = {}
    for k, v in updates.items():
        names[f"#{k}"] = k
        vals[f":{k}"] = v
        set_parts.append(f"#{k} = :{k}")

    ddb.update_item(
        Key={"run_id": args.run_id, "created_at_unix": args.created_at_unix},
        UpdateExpression="SET " + ", ".join(set_parts),
        ExpressionAttributeNames=names,
        ExpressionAttributeValues=vals,
    )
    print(json.dumps({"ok": True, "updated": updates}))

if __name__ == "__main__":
    raise SystemExit(main())
