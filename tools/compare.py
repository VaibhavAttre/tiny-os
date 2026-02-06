#!/usr/bin/env python3
import argparse, subprocess, sys

def run(cmd):
    print("\n$ " + " ".join(cmd))
    return subprocess.call(cmd)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--label", required=True, help="base label for artifacts (produces <label>_a and <label>_b)")
    ap.add_argument("--allow", default="", help="pass-through to diff.py, e.g. disk_=10,ctx_switches=5,default=0")
    args = ap.parse_args()

    a = f"{args.label}_a"
    b = f"{args.label}_b"

    rc = run(["python3", "runner.py", "--label", a])
    if rc != 0:
        print("runner A failed")
        sys.exit(rc)

    rc = run(["python3", "runner.py", "--label", b])
    if rc != 0:
        print("runner B failed")
        sys.exit(rc)

    diff_cmd = ["python3", "tools/diff.py",
                f"artifacts/{a}/metrics.json",
                f"artifacts/{b}/metrics.json"]
    if args.allow:
        diff_cmd += ["--allow", args.allow]

    rc = run(diff_cmd)
    sys.exit(rc)

if __name__ == "__main__":
    main()
