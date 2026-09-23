"""Native launcher regression checks; no LLM calls or WSL execution."""

import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch, Mock

spec = importlib.util.spec_from_file_location(
    "humanize_launcher", Path(__file__).resolve().parents[1] / "run-humanize.py"
)
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)


class WindowsLauncherTests(unittest.TestCase):
    def test_git_bash_is_selected_from_installation_with_spaces(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "Program Files" / "Git"
            (root / "bin").mkdir(parents=True)
            (root / "bin/bash.exe").touch()
            (root / "git-bash.exe").touch()
            with patch.dict(os.environ, {"HUMANIZE_GIT_BASH": str(root / "bin/bash.exe")}, clear=True):
                self.assertEqual(launcher.find_git_bash(), (root / "bin/bash.exe").resolve())

    def test_non_git_bash_override_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            fake = Path(directory) / "System32/bash.exe"
            fake.parent.mkdir()
            fake.touch()
            with patch.dict(os.environ, {"HUMANIZE_GIT_BASH": str(fake)}, clear=True):
                with self.assertRaisesRegex(RuntimeError, "WSL is not supported"):
                    launcher.find_git_bash()

    def test_missing_explicit_git_installation_does_not_fall_back_to_path(self):
        with patch.dict(os.environ, {"HUMANIZE_GIT_BASH": "Z:/missing/Git/bin/bash.exe"}):
            with patch.object(launcher.shutil, "which") as which:
                with self.assertRaises(RuntimeError):
                    launcher.find_git_bash()
                which.assert_not_called()

    def test_launch_preserves_arguments_streams_and_exit_status(self):
        bash = Path("C:/Program Files/Git/bin/bash.exe")
        args = ["scripts/ask-codex.sh", "--question-file", "a path/$(literal).md"]
        with patch.object(launcher, "find_git_bash", return_value=bash), \
                patch.object(launcher.subprocess, "run", return_value=Mock(returncode=23)) as run, \
                patch.dict(os.environ, {"BASH_ENV": "must-not-load.sh"}):
            self.assertEqual(launcher.main(args), 23)
        call = run.call_args
        self.assertEqual(call.args[0][0:3], [str(bash), "--noprofile", "--norc"])
        self.assertEqual(call.args[0][4:], args)
        self.assertFalse(call.kwargs["shell"])
        self.assertNotIn("BASH_ENV", call.kwargs["env"])
        for stream in ("stdin", "stdout", "stderr"):
            self.assertNotIn(stream, call.kwargs)

    def test_hook_command_prefers_uv_and_quotes_paths(self):
        with patch.object(launcher.shutil, "which", return_value="found"):
            result = launcher.windows_hook_command("C:/a path/humanize")
        self.assertEqual(result, subprocess.list2cmdline([
            "uv", "run", str(Path("C:/a path/humanize/run-humanize.py")),
            "hooks/loop-codex-stop-hook.sh",
        ]))

    def test_hook_command_falls_back_to_python(self):
        for interpreter in ("python3", "python"):
            with self.subTest(interpreter=interpreter), \
                    patch.object(launcher.shutil, "which", side_effect=lambda name: name if name == interpreter else None):
                self.assertTrue(launcher.windows_hook_command("runtime").startswith(interpreter + " "))

    def test_hook_command_reports_missing_python(self):
        with patch.object(launcher.shutil, "which", return_value=None):
            with self.assertRaises(RuntimeError):
                launcher.windows_hook_command("runtime")


if __name__ == "__main__":
    unittest.main()
