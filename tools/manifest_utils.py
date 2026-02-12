import subprocess

def get_git_info():
    try:
        commit = subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip()
        dirty = subprocess.check_output(["git", "status", "--porcelain"], text=True).strip() != ""
        return {"commit": commit, "dirty": dirty}
    except Exception:
        return {"commit": "unknown", "dirty": None}

def get_tool_versions():
    out = {}
    try:
        qemu_ver = subprocess.check_output(
            ["qemu-system-riscv64", "--version"],
            text=True
        ).splitlines()[0].strip()
        out["qemu_system_riscv64"] = qemu_ver
    except Exception:
        out["qemu_system_riscv64"] = "unknown"
    return out
