#!/usr/bin/env python3
import argparse
import json
import os
import sys
import time
from pathlib import Path

import boto3


def die(msg: str, code: int = 2):
    print(f"error: {msg}", file=sys.stderr)
    raise SystemExit(code)


def load_json(p: Path) -> dict:
    try:
        with open(p, "r") as f:
            return json.load(f)
    except Exception as e:
        die(f"failed to read json {p}: {e}")


def collect_fields(run_id: str, run_dir: Path, manifest: dict, index: dict, s3_prefix: str) -> dict:
    """
    Builds the "final" fields for the run record based on manifest/index.
    These fields are safe to SET onto an existing queued/running job item.
    """
    created_at_unix = None
    # Prefer manifest.created_at_unix if present; otherwise use index.timestamp_unix
    if isinstance(manifest, dict):
        created_at_unix = manifest.get("created_at_unix")
    if created_at_unix is None and isinstance(index, dict):
        created_at_unix = index.get("timestamp_unix")
    if created_at_unix is None:
        created_at_unix = int(time.time())

    git = (manifest.get("git") or {}) if isinstance(manifest, dict) else {}
    run = (manifest.get("run") or {}) if isinstance(manifest, dict) else {}
    artifacts = (manifest.get("artifacts") or {}) if isinstance(manifest, dict) else {}

    item = {
        # Keys (do not change schema)
        "run_id": run_id,
        "created_at_unix": int(created_at_unix),

        # Where artifacts live
        "s3_prefix": s3_prefix,

        # Convenience metadata
        "outbase": artifacts.get("outbase", str(run_dir)),
        "commit": git.get("commit"),
        "dirty": git.get("dirty"),

        # Run parameters
        "workloads": run.get("workloads", index.get("workloads")),
        "repeat": run.get("repeat", index.get("repeat")),
        "timeout_sec": run.get("timeout_sec", index.get("timeout_sec")),
        "fail_fast": run.get("fail_fast", index.get("fail_fast")),

        # Result
        "ok": bool(index.get("ok")),
        "schema_version": int(index.get("schema_version", 1)),
    }

    # Optional: store a compact summary string or counts if you want (safe to omit)
    # Example: number of results
    if isinstance(index.get("results"), list):
        item["results_count"] = len(index["results"])

    return item


def upload_directory(s3, bucket: str, prefix: str, run_dir: Path, dry_run: bool = False) -> int:
    """
    Upload all files under run_dir to s3://bucket/prefix/<relative path>
    """
    uploaded = 0
    for p in sorted(run_dir.rglob("*")):
        if not p.is_file():
            continue
        rel = p.relative_to(run_dir).as_posix()
        key = f"{prefix.rstrip('/')}/{rel}"

        if dry_run:
            print(f"[dry-run] s3 upload {p} -> s3://{bucket}/{key}")
            uploaded += 1
            continue

        s3.upload_file(str(p), bucket, key)
        uploaded += 1
    return uploaded


def finalize_ddb_item(ddb_table, key: dict, updates: dict, dry_run: bool = False):
    """
    Update an existing DynamoDB item with SET updates.
    Does not overwrite the whole row (so lifecycle fields survive).
    """
    if dry_run:
        print("[dry-run] dynamodb update_item key:", json.dumps(key, indent=2))
        print("[dry-run] dynamodb update_item updates:", json.dumps(updates, indent=2))
        return

    set_parts = []
    expr_vals = {}
    expr_names = {}

    for k, v in updates.items():
        nk = f"#{k}"
        vk = f":{k}"
        expr_names[nk] = k
        expr_vals[vk] = v
        set_parts.append(f"{nk} = {vk}")

    if not set_parts:
        return

    ddb_table.update_item(
        Key=key,
        UpdateExpression="SET " + ", ".join(set_parts),
        ExpressionAttributeNames=expr_names,
        ExpressionAttributeValues=expr_vals,
    )


def main():
    ap = argparse.ArgumentParser(description="Upload TinyOS run bundle to S3 and finalize DynamoDB index item.")
    ap.add_argument("run_dir", help="Path to run directory, e.g. artifacts/20260211_180211")
    ap.add_argument("--bucket", default=os.getenv("TINYOS_S3_BUCKET"), help="S3 bucket (or env TINYOS_S3_BUCKET)")
    ap.add_argument("--table", default=os.getenv("TINYOS_DDB_TABLE"), help="Dynamo table (or env TINYOS_DDB_TABLE)")
    ap.add_argument("--region", default=os.getenv("AWS_REGION", "us-west-1"), help="AWS region")
    ap.add_argument("--prefix", default="runs/", help="S3 prefix root (default: runs/)")
    ap.add_argument("--dry-run", action="store_true", help="Print actions without uploading/writing")

    # Lifecycle extras (worker can pass these)
    ap.add_argument("--run-id", default=None, help="Override run_id (default: run_dir name)")
    ap.add_argument("--created-at-unix", type=int, default=None, help="Override created_at_unix key")
    ap.add_argument("--status", default=None, help='Set status (e.g. "succeeded" or "failed")')
    ap.add_argument("--message-id", default=None, help="SQS MessageId (optional)")
    ap.add_argument("--started-at-unix", type=int, default=None, help="Optional started_at_unix")
    ap.add_argument("--finished-at-unix", type=int, default=None, help="Optional finished_at_unix")
    ap.add_argument("--error", default=None, help="Error string if failed")

    args = ap.parse_args()

    run_dir = Path(args.run_dir).resolve()
    if not run_dir.exists() or not run_dir.is_dir():
        die(f"run_dir not found or not a directory: {run_dir}")

    if not args.bucket:
        die("missing --bucket and env TINYOS_S3_BUCKET not set")
    if not args.table:
        die("missing --table and env TINYOS_DDB_TABLE not set")

    # Required run files
    manifest_path = run_dir / "manifest.json"
    index_path = run_dir / "index.json"
    if not manifest_path.exists():
        die(f"missing {manifest_path}")
    if not index_path.exists():
        die(f"missing {index_path}")

    run_id = args.run_id or run_dir.name
    s3_prefix = f"{args.prefix.rstrip('/')}/{run_id}/"

    manifest = load_json(manifest_path)
    index = load_json(index_path)

    # Build "final fields"
    item = collect_fields(run_id, run_dir, manifest, index, s3_prefix)

    # Allow overriding key timestamp (important if submitter pre-created row)
    if args.created_at_unix is not None:
        item["created_at_unix"] = int(args.created_at_unix)

    # 1) Upload artifacts to S3
    s3 = boto3.client("s3", region_name=args.region)
    n = upload_directory(s3, args.bucket, s3_prefix, run_dir, dry_run=args.dry_run)
    print(f"Uploaded {n} files to S3.")

    # 2) Finalize Dynamo item via update_item (no clobber)
    ddb = boto3.resource("dynamodb", region_name=args.region)
    table = ddb.Table(args.table)

    key = {"run_id": item["run_id"], "created_at_unix": item["created_at_unix"]}

    updates = dict(item)
    # Don't SET the key attributes
    updates.pop("run_id", None)
    updates.pop("created_at_unix", None)

    # Lifecycle extras if provided
    if args.status is not None:
        updates["status"] = args.status
    if args.message_id is not None:
        updates["message_id"] = args.message_id
    if args.started_at_unix is not None:
        updates["started_at_unix"] = int(args.started_at_unix)
    if args.finished_at_unix is not None:
        updates["finished_at_unix"] = int(args.finished_at_unix)
    if args.error is not None:
        updates["error"] = args.error

    finalize_ddb_item(table, key, updates, dry_run=args.dry_run)
    print("Finalized DynamoDB index item (update_item).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
