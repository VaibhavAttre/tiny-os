#!/usr/bin/env python3
import argparse, os, subprocess, sys, tempfile
from pathlib import Path

def die(msg: str, code: int = 2):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)

def sh(cmd: list[str]):
    print("+", " ".join(cmd))
    subprocess.check_call(cmd)

def main():
    ap = argparse.ArgumentParser(description="Run TinyOS workloads using prebuilt build artifacts from S3, then upload the run bundle.")
    ap.add_argument("--commit", required=True, help="Git commit SHA used for S3 builds/<commit>/ artifacts")
    ap.add_argument("--bucket", default=os.getenv("TINYOS_S3_BUCKET"), help="S3 bucket (or env TINYOS_S3_BUCKET)")
    ap.add_argument("--region", default=os.getenv("AWS_REGION", "us-west-1"), help="AWS region (default: env AWS_REGION or us-west-1)")
    ap.add_argument("--table", default=os.getenv("TINYOS_DDB_TABLE"), help="DynamoDB table (or env TINYOS_DDB_TABLE)")
    ap.add_argument("--workloads", nargs="*", default=None, help="Workloads (default: smoke sleep50)")
    ap.add_argument("--repeat", type=int, default=1, help="Repeat each workload N times")
    ap.add_argument("--timeout", type=float, default=None, help="Timeout seconds passed through to run_many.py/run_one.py")
    ap.add_argument("--fail-fast", action="store_true", help="Stop early after first failure")
    ap.add_argument("--out", default=None, help="Output directory (default: artifacts/<run_id>)")
    ap.add_argument("--no-upload", action="store_true", help="Do not upload/index after run")
    ap.add_argument("--keep-build-dir", action="store_true", help="Do not delete downloaded build dir (debug)")
    args = ap.parse_args()

    if not args.bucket:
        die("missing --bucket and env TINYOS_S3_BUCKET not set")
    if not args.no_upload and not args.table:
        die("missing --table and env TINYOS_DDB_TABLE not set (needed for upload_run.py)")

    workloads = args.workloads if args.workloads else ["smoke", "sleep50"]

    # Temp dir for downloaded build artifacts
    tmp_root = Path(tempfile.mkdtemp(prefix="tinyos_build_"))
    build_dir = tmp_root / args.commit
    build_dir.mkdir(parents=True, exist_ok=True)

    kernel_path = build_dir / "kernel.elf"
    disk_path   = build_dir / "disk.img"

    prefix = f"builds/{args.commit}/"
    sh(["aws", "s3", "cp", f"s3://{args.bucket}/{prefix}kernel.elf", str(kernel_path), "--region", args.region])
    sh(["aws", "s3", "cp", f"s3://{args.bucket}/{prefix}disk.img",   str(disk_path),   "--region", args.region])

    # Run workloads using prebuilt mode
    cmd = ["./tools/run_many.py", *workloads, "--repeat", str(args.repeat)]
    if args.timeout is not None:
        cmd += ["--timeout", str(args.timeout)]
    if args.fail_fast:
        cmd += ["--fail-fast"]
    if args.out is not None:
        cmd += ["--out", args.out]

    cmd += ["--kernel", str(kernel_path), "--disk", str(disk_path), "--latest"]

    sh(cmd)

    latest_path = Path("artifacts") / "LATEST"
    if not latest_path.exists():
        die("artifacts/LATEST not found (did run_many.py run with --latest?)")

    outbase = latest_path.read_text().strip()
    print(f"Run bundle at: {outbase}")

    if not args.no_upload:
        env = os.environ.copy()
        env["TINYOS_S3_BUCKET"] = args.bucket
        env["AWS_REGION"] = args.region
        env["TINYOS_DDB_TABLE"] = args.table

        sh(["./tools/upload_run.py", outbase])

    if args.keep_build_dir:
        print(f"Keeping build dir: {build_dir}")
    else:
        # best-effort cleanup
        try:
            import shutil
            shutil.rmtree(tmp_root)
        except Exception:
            pass

if __name__ == "__main__":
    main()
