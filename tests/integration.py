#!/usr/bin/env python3
"""Subprocess integration tests without installing or touching real tmux."""
import json
import os
from pathlib import Path
import pty
import shlex
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1) if len(sys.argv) > 1 else "build/work").resolve()
FIXTURE = Path(__file__).with_name("fake_tools.py").resolve()

class Integration(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="work-test-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / "project with spaces and 'quotes'"
        self.root.mkdir()
        self.config = self.base / "config 'quoted'"
        self.state = self.base / "state"
        self.fake_state = self.base / "fake-state"
        self.bin = self.base / "bin"
        self.bin.mkdir()
        for name in ("tmux", "tmuxinator"):
            target = self.bin / name
            target.write_text("#!" + sys.executable + "\nexec(compile(open(" + repr(str(FIXTURE)) + ").read(), " + repr(str(FIXTURE)) + ", 'exec'))\n")
            target.chmod(0o755)
        plugin = self.base / "plugin 'quoted'"
        (plugin / "scripts").mkdir(parents=True)
        (plugin / "resurrect.tmux").write_text("#!/bin/bash\nexit 0\n")
        for name in ("save", "restore"):
            (plugin / "scripts" / (name + ".sh")).write_text(
                "#!/bin/bash\nexec " + shlex.quote(sys.executable) + " " + shlex.quote(str(FIXTURE)) + " " + name + "\n")
        self.env = dict(os.environ, WORK_CONFIG_HOME=str(self.config), WORK_STATE_HOME=str(self.state),
                        WORK_RESURRECT_DIR=str(plugin), WORK_FAKE_STATE=str(self.fake_state),
                        PATH=str(self.bin) + os.pathsep + os.environ.get("PATH", ""))
        for key in ("TMUX", "TMUX_PANE", "WORK_PROJECT", "FAKE_FAIL_START", "FAKE_FAIL_SAVE",
                    "FAKE_FAIL_RESTORE", "FAKE_INCOMPLETE_RESTORE", "FAKE_NO_SAVE", "FAKE_BAD_SAVE", "FAKE_NO_CHANGE"):
            self.env.pop(key, None)

    def call(self, *args, code=0, **extra):
        result = subprocess.run([str(BINARY), *args], env=dict(self.env, **extra), capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, code, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def init(self, name="demo"):
        return self.call("init", name, "--root", str(self.root))

    def start(self, name="demo", **extra):
        return self.call("open", name, "--no-attach", **extra)

    def snapshot(self, name="demo"):
        return self.state / "projects" / name / "resurrect" / "last"

    def kill(self, name="demo"):
        subprocess.run([str(self.bin / "tmux"), "-L", "work-" + name, "kill-server"],
                       env=self.env, check=True, capture_output=True, timeout=10)

    def calls(self):
        file = self.fake_state / "calls.jsonl"
        return [json.loads(line) for line in file.read_text().splitlines()] if file.exists() else []

    def test_init_and_config(self):
        self.init()
        path = Path(self.call("config", "demo").strip())
        self.assertIn(str(self.root), path.read_text())
        self.assertEqual(path.stat().st_mode & 0o777, 0o600)

    def test_duplicate_init_preserves_edits(self):
        self.init()
        path = self.config / "projects/demo.work"
        path.write_text(path.read_text() + "# custom\n")
        before = path.read_text()
        self.call("init", "demo", code=1)
        self.assertEqual(path.read_text(), before)

    def test_invalid_project_names(self):
        for name in ("../oops", "-flag", "a.b", "a:b", "two words", "a" * 49):
            with self.subTest(name=name):
                self.call("init", name, code=1)
        self.assertFalse((self.config / "projects").exists())

    def test_invalid_config_prevents_process_launch(self):
        self.init()
        (self.config / "projects/demo.work").write_text("root = /tmp\n[window code]\nunknown = nope\n")
        self.start(code=1)
        self.assertEqual(self.calls(), [])

    def test_new_workspace_is_generated_and_detached(self):
        self.init()
        self.assertIn("Created workspace", self.start())
        state = json.loads((self.fake_state / "work-demo.json").read_text())
        self.assertEqual(len(state["panes"]), 4)
        self.assertEqual(state["panes"][0][3], str(self.root))
        self.assertIn("save 'demo' --quiet", state["hooks"]["client-detached"])
        self.assertNotIn("attach-session", str(self.calls()))

    def test_existing_workspace_is_reused(self):
        self.init()
        self.start()
        self.assertIn("Reusing", self.start())
        self.assertEqual(sum(x["tool"] == "tmuxinator" for x in self.calls()), 1)

    def test_save_then_restore_and_last_project(self):
        self.init()
        self.start()
        self.call("save", "demo")
        before = self.snapshot().read_text()
        self.kill()
        self.assertIn("Restored", self.call("open", "--no-attach"))
        self.assertEqual(self.snapshot().read_text(), before)
        self.assertEqual(sum(x["tool"] == "tmuxinator" for x in self.calls()), 1)
        self.assertEqual((self.state / "last-project").read_text(), "demo\n")

    def test_project_servers_and_snapshots_are_isolated(self):
        for name in ("alpha", "beta"):
            self.init(name)
            self.start(name)
            self.call("save", name)
        beta = (self.fake_state / "work-beta.json").read_text()
        self.kill("alpha")
        self.start("alpha")
        self.assertEqual((self.fake_state / "work-beta.json").read_text(), beta)
        self.assertNotEqual(self.snapshot("alpha").read_text(), self.snapshot("beta").read_text())
        plugins = [x for x in self.calls() if x["tool"] in ("save", "restore")]
        self.assertTrue(all("/work-" in x["TMUX"] for x in plugins))

    def test_headless_restore_explicitly_selects_window_and_panes(self):
        self.init()
        self.start()
        self.call("save", "demo")
        self.kill()
        self.start()
        commands = [x["args"][2:] for x in self.calls() if x["tool"] == "tmux"]
        self.assertIn(["select-pane", "-t", "=demo:0.0"], commands)
        self.assertIn(["select-pane", "-t", "=demo:1.0"], commands)
        self.assertIn(["select-window", "-t", "=demo:0"], commands)

    def test_corrupt_snapshot_does_not_start_server(self):
        self.init()
        directory = self.snapshot().parent
        directory.mkdir(parents=True)
        self.snapshot().write_text("pane\tbroken\n")
        self.start(code=1)
        self.assertFalse((self.fake_state / "work-demo.json").exists())
        self.assertEqual(self.snapshot().read_text(), "pane\tbroken\n")

    def test_missing_saved_directory_requires_fresh(self):
        self.init()
        self.start()
        self.call("save", "demo")
        self.kill()
        self.snapshot().write_text(self.snapshot().read_text().replace(str(self.root), "/does/not/exist"))
        self.assertIn("--fresh", self.start(code=1))
        self.assertIn("Created workspace", self.call("open", "demo", "--no-attach", "--fresh"))

    def test_incomplete_restore_cleans_only_its_server(self):
        for name in ("demo", "other"):
            self.init(name)
            self.start(name)
        self.call("save", "demo")
        self.kill()
        self.start(code=1, FAKE_INCOMPLETE_RESTORE="1")
        self.assertFalse((self.fake_state / "work-demo.json").exists())
        self.assertTrue((self.fake_state / "work-other.json").exists())

    def test_failed_start_cleans_only_its_server(self):
        self.init("other")
        self.start("other")
        self.init()
        self.start(code=1, FAKE_FAIL_START="1")
        self.assertFalse((self.fake_state / "work-demo.json").exists())
        self.assertTrue((self.fake_state / "work-other.json").exists())

    def test_save_failure_preserves_previous_snapshot(self):
        self.init()
        self.start()
        self.call("save", "demo")
        before = self.snapshot().read_text()
        for extra in ({"FAKE_FAIL_SAVE": "1"}, {"FAKE_BAD_SAVE": "1"}, {"FAKE_NO_SAVE": "1"}):
            with self.subTest(extra=extra):
                self.call("save", "demo", code=1, **extra)
                self.assertEqual(self.snapshot().read_text(), before)

    def test_stopped_project_save_preserves_snapshot(self):
        self.init()
        self.start()
        self.call("save", "demo")
        before = self.snapshot().read_text()
        self.kill()
        self.call("save", "demo", code=1)
        self.assertEqual(self.snapshot().read_text(), before)

    def test_successful_unchanged_save_is_accepted(self):
        self.init()
        self.start()
        self.call("save", "demo")
        before = self.snapshot().read_text()
        self.call("save", "demo", FAKE_NO_CHANGE="1")
        self.assertEqual(self.snapshot().read_text(), before)

    def test_missing_plugin_before_start(self):
        self.init()
        self.start(code=1, WORK_RESURRECT_DIR="/missing/plugin")
        self.assertEqual(self.calls(), [])

    def test_headless_requires_flag_before_start(self):
        self.init()
        self.call("open", "demo", code=1)
        self.assertEqual(self.calls(), [])

    def test_current_project_environment_takes_priority(self):
        for name in ("alpha", "beta"):
            self.init(name)
            self.start(name)
        self.call("save", WORK_PROJECT="alpha")
        self.assertTrue(self.snapshot("alpha").exists())
        self.assertFalse(self.snapshot("beta").exists())

    def test_list_state_transitions(self):
        self.init()
        self.assertIn("configured", self.call("list"))
        self.start()
        self.assertIn("running", self.call("list"))
        self.call("save", "demo")
        self.kill()
        self.assertIn("saved", self.call("list"))

    def test_doctor_and_no_previous_project(self):
        self.assertIn("OK", self.call("doctor"))
        self.assertIn("No previous project", self.call(code=1))

    def test_state_whitespace_fails_before_start(self):
        self.init()
        self.start(code=1, WORK_STATE_HOME=str(self.base / "state with spaces"))
        self.assertEqual(self.calls(), [])

    def test_concurrent_opens_create_once(self):
        self.init()
        commands = [[str(BINARY), "open", "demo", "--no-attach"]] * 2
        processes = [subprocess.Popen(cmd, env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) for cmd in commands]
        for child in processes:
            stdout, stderr = child.communicate(timeout=15)
            self.assertEqual(child.returncode, 0, stdout + stderr)
        self.assertEqual(sum(x["tool"] == "tmuxinator" for x in self.calls()), 1)

    def test_detach_hook_saves_without_deadlock(self):
        self.init()
        master, slave = pty.openpty()
        try:
            child = subprocess.Popen([str(BINARY), "open", "demo"], env=self.env,
                                     stdin=slave, stdout=slave, stderr=slave)
            self.assertEqual(child.wait(timeout=15), 0)
            self.assertTrue(self.snapshot().exists())
        finally:
            os.close(master)
            os.close(slave)

if __name__ == "__main__":
    unittest.main(verbosity=2)
