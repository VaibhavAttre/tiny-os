#!/usr/bin/env python3
import os
import sys
import json
import time
import argparse
from pathlib import Path

import boto3
from boto3.s3.transfer import TransferConfig

def die(msg: str, code: int = 2):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)

def load_json(path: Path):
    if not path.exists():
        return None
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)

def infer_created_at_unix(run_dir: Path, manifest: dict | None, index: dict | None) -> int:
    # Prefer explicit field if present
    for src in (manifest, index):
        if isinstance(src, dict):
            for k in ("created_at_unix", "created_at", "timestamp_unix"):
                v = src.get(k)
                if isinstance(v, int):
                    return v
                if isinstance(v, float):
                    return int(v)
    # Fallback: directory mtime
    return int(run_dir.stat().st_mtime)

def collect_fields(run_id: str, run_dir: Path, manifest: dict | None, index: dict | None, s3_prefix: str):
    item = {
        "run_id": run_id,
        "created_at_unix": infer_created_at_unix(run_dir, manifest, index),
        "s3_prefix": s3_prefix,
    }

    # Common fields (best-effort)
    if isinstance(manifest, dict):
        git = manifest.get("git")
        if isinstance(git, dict):
            if "commit" in git and isinstance(git["commit"], str):
                item["commit"] = git["commit"]
            if "dirty" in git:
                item["dirty"] = bool(git["dirty"])

        # Fallback older schema
        if "commit" in manifest and isinstance(manifest["commit"], str):
            item["commit"] = manifest["commit"]

        # run settings live under manifest["run"]
        run = manifest.get("run")
        if isinstance(run, dict):
            if "repeat" in run and isinstance(run["repeat"], int):
                item["repeat"] = run["repeat"]
            if "workloads" in run and isinstance(run["workloads"], list) and all(isinstance(x, str) for x in run["workloads"]):
                item["workloads"] = run["workloads"]

        # created_at_unix exists at top-level in your manifest (already handled earlier)


    if isinstance(index, dict):
        if "commit" in index and "commit" not in item: item["commit"] = index["commit"]
        if "dirty" in index and "dirty" not in item: item["dirty"] = bool(index["dirty"])
        if "ok" in index: item["ok"] = bool(index["ok"])
        if "repeat" in index and isinstance(index["repeat"], int) and "repeat" not in item: item["repeat"] = index["repeat"]

        # workloads could be list of names or dict; handle list-of-strings
        w = index.get("workloads")
        if isinstance(w, list) and all(isinstance(x, str) for x in w):
            item["workloads"] = w

    # Last-resort defaults
    item.setdefault("dirty", False)
    item.setdefault("ok", False)
    item.setdefault("repeat", 1)

    return item

def upload_directory(s3, bucket: str, prefix: str, run_dir: Path, dry_run: bool = False):
    # Use multipart for large files automatically
    config = TransferConfig(
        multipart_threshold=8 * 1024 * 1024,
        multipart_chunksize=8 * 1024 * 1024,
        max_concurrency=8,
        use_threads=True,
    )

    uploaded = 0
    for p in run_dir.rglob("*"):
        if p.is_dir():
            continue
        rel = p.relative_to(run_dir).as_posix()
        key = f"{prefix}{rel}"
        if dry_run:
            print(f"[dry-run] s3://{bucket}/{key}  <=  {p}")
        else:
            s3.upload_file(str(p), bucket, key, Config=config)
        uploaded += 1
    return uploaded

def put_ddb_item(ddb, table: str, item: dict, dry_run: bool = False):
    # DynamoDB expects types; boto3 resource handles marshaling if we pass plain Python types.
    if dry_run:
        print("[dry-run] dynamodb put_item:", json.dumps(item, indent=2))
        return
    ddb.Table(table).put_item(Item=item)

def main():
    ap = argparse.ArgumentParser(description="Upload a TinyOS run bundle to S3 and index it in DynamoDB.")
    ap.add_argument("run_dir", help="Path to run directory, e.g. artifacts/20260211_180211")
    ap.add_argument("--bucket", default=os.getenv("TINYOS_S3_BUCKET"), help="S3 bucket (or env TINYOS_S3_BUCKET)")
    ap.add_argument("--table", default=os.getenv("TINYOS_DDB_TABLE"), help="DynamoDB table (or env TINYOS_DDB_TABLE)")
    ap.add_argument("--region", default=os.getenv("AWS_REGION", "us-west-1"), help="AWS region (default: env AWS_REGION or us-west-1)")
    ap.add_argument("--prefix", default="runs/", help="S3 prefix root (default: runs/)")
    ap.add_argument("--dry-run", action="store_true", help="Print actions without uploading/writing")
    args = ap.parse_args()

    run_dir = Path(args.run_dir).resolve()
    if not run_dir.exists() or not run_dir.is_dir():
        die(f"run_dir not found or not a directory: {run_dir}")

    if not args.bucket:
        die("missing --bucket and env TINYOS_S3_BUCKET not set")
    if not args.table:
        die("missing --table and env TINYOS_DDB_TABLE not set")

    run_id = run_dir.name
    s3_prefix = f"{args.prefix.rstrip('/')}/{run_id}/"

    manifest = load_json(run_dir / "manifest.json")
    index = load_json(run_dir / "index.json")

    item = collect_fields(run_id, run_dir, manifest, index, s3_prefix)

    # Validate required DDB keys exist
    if not isinstance(item.get("run_id"), str) or not item["run_id"]:
        die("DynamoDB item missing run_id")
    if not isinstance(item.get("created_at_unix"), int):
        die("DynamoDB item missing created_at_unix int")

    session = boto3.session.Session(region_name=args.region)
    s3 = session.client("s3")
    ddb = session.resource("dynamodb")

    print(f"Uploading: {run_dir}")
    print(f"  S3:  s3://{args.bucket}/{s3_prefix}...")
    print(f"  DDB: {args.table}  (PK run_id, SK created_at_unix) in {args.region}")
    print(f"  Index item keys: run_id={item['run_id']} created_at_unix={item['created_at_unix']}")

    n = upload_directory(s3, args.bucket, s3_prefix, run_dir, dry_run=args.dry_run)
    print(f"Uploaded {n} files to S3.")

    put_ddb_item(ddb, args.table, item, dry_run=args.dry_run)
    print("Wrote DynamoDB index item.")

if __name__ == "__main__":
    main()
