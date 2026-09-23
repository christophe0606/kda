"""Launch the installed Humanize runtime with native Windows Git Bash, never WSL."""

import os
from pathlib import Path
import shutil
import subprocess
import sys


def find_git_bash():
    override = os.environ.get("HUMANIZE_GIT_BASH")
    if override:
        candidates = [Path(override)]
    else:
        candidates = [
            Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "Git/bin/bash.exe",
            Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Git/bin/bash.exe",
        ]
        if os.environ.get("LOCALAPPDATA"):
            candidates.append(Path(os.environ["LOCALAPPDATA"]) / "Programs/Git/bin/bash.exe")
    for candidate in candidates:
        candidate = candidate.resolve()
        git_roots = (candidate.parent.parent, candidate.parent.parent.parent)
        if (candidate.name.lower() == "bash.exe" and candidate.is_file()
                and any((root / "git-bash.exe").is_file() for root in git_roots)):
            return candidate
    raise RuntimeError("Git for Windows was not found. Set HUMANIZE_GIT_BASH to its bash.exe. WSL is not supported.")


def windows_hook_command(runtime_root):
    if shutil.which("uv"):
        command = ["uv", "run"]
    elif shutil.which("python3"):
        command = ["python3"]
    elif shutil.which("python"):
        command = ["python"]
    else:
        raise RuntimeError("Humanize requires uv or Python on PATH.")
    command.extend([str(Path(runtime_root) / "run-humanize.py"), "hooks/loop-codex-stop-hook.sh"])
    return subprocess.list2cmdline(command)


def main(args=None):
    args = sys.argv[1:] if args is None else args
    if os.name != "nt":
        print("This launcher requires native Windows Python; WSL is not supported.", file=sys.stderr)
        return 1
    if not args:
        print("Usage: run-humanize.py scripts/<script>.sh [arguments...]", file=sys.stderr)
        return 2
    try:
        bash = find_git_bash()
        launcher = Path(__file__).resolve().with_suffix(".sh")
        env = os.environ.copy()
        env.pop("BASH_ENV", None)
        # Inherit stdin/stdout/stderr so hooks receive their JSON and preserve
        # the runtime's output and exit code. No shell or PATH lookup for Bash.
        return subprocess.run(
            [str(bash), "--noprofile", "--norc", str(launcher), *args],
            env=env, shell=False,
        ).returncode
    except (OSError, RuntimeError) as exc:
        print(f"Humanize launcher: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
