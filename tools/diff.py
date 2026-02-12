#!/usr/bin/env python3
import json, sys, argparse

IGNORE = {"ticks", "version", "schema_version"}

DEFAULT_ALLOW_PCT = 0.0
ALLOW_PCT_BY_PREFIX = {"disk_": 5.0}
ALLOW_PCT_BY_KEY = {
    "ctx_switches": 5.0,
    "syscall_enter": 0.0,
    "syscall_exit": 0.0,
    "page_faults": 0.0,
}

def load(p):
    with open(p) as f:
        return json.load(f)

def parse_allow(s: str):
    """
    Parses: "disk_=10,ctx_switches=5,default=0"
    - token ending with '_' is treated as a prefix rule (disk_)
    - otherwise treated as exact key
    - "default" sets DEFAULT_ALLOW_PCT
    """
    global DEFAULT_ALLOW_PCT, ALLOW_PCT_BY_PREFIX, ALLOW_PCT_BY_KEY
    if not s:
        return

    for tok in s.split(","):
        tok = tok.strip()
        if not tok:
            continue
        if "=" not in tok:
            raise ValueError(f"bad --allow token (missing '='): {tok}")
        name, val = tok.split("=", 1)
        name = name.strip()
        val = float(val.strip())

        if name == "default":
            DEFAULT_ALLOW_PCT = val
        elif name.endswith("_"):
            ALLOW_PCT_BY_PREFIX[name] = val
        else:
            ALLOW_PCT_BY_KEY[name] = val

def allowed_pct_for(key: str) -> float:
    if key in ALLOW_PCT_BY_KEY:
        return ALLOW_PCT_BY_KEY[key]
    for pref, pct in ALLOW_PCT_BY_PREFIX.items():
        if key.startswith(pref):
            return pct
    return DEFAULT_ALLOW_PCT

def fmt_delta(a, b):
    d = b - a
    if a == 0:
        return f"{d:+} (n/a)"
    pct = (d / a) * 100.0
    return f"{d:+} ({pct:+.1f}%)"

def pct_increase(a, b):
    if a == 0:
        return None
    return ((b - a) / a) * 100.0

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a", help="path to A metrics.json")
    ap.add_argument("b", help="path to B metrics.json")
    ap.add_argument("--allow", default="", help="thresholds like: disk_=10,ctx_switches=5,default=0")
    args = ap.parse_args()

    if args.allow:
        parse_allow(args.allow)

    A = load(args.a)
    B = load(args.b)

    keys = sorted(set(A.keys()) | set(B.keys()))
    keys = [k for k in keys if k not in IGNORE]
    keys = [k for k in keys
            if isinstance(A.get(k, 0), (int, float)) and isinstance(B.get(k, 0), (int, float))]

    print(f"{'metric':24} {'A':>12} {'B':>12}  delta     allow%   status")
    print("-"*78)

    failed = False

    for k in keys:
        a = A.get(k, 0)
        b = B.get(k, 0)
        allow = allowed_pct_for(k)
        status = "OK"

        if b > a:
            inc = pct_increase(a, b)
            if a == 0:
                if allow == 0.0:
                    status = "REGRESSION"
                    failed = True
            else:
                if inc is not None and inc > allow + 1e-9:
                    status = "REGRESSION"
                    failed = True

        print(f"{k:24} {a:12} {b:12}  {fmt_delta(a,b):9}  {allow:6.1f}%  {status}")

    sys.exit(1 if failed else 0)

if __name__ == "__main__":
    main()
