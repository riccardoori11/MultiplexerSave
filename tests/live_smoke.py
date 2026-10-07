#!/usr/bin/env python3
"""Opt-in round trip with real dependencies, isolated from the user's projects."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

binary = Path(sys.argv[1] if len(sys.argv) > 1 else "build/work").resolve()
name = "smoke-" + str(os.getpid())
socket = "work-" + name

def command(args, env, check=True):
    response = subprocess.run(args, env=env, capture_output=True, text=True, timeout=60)
    if check and response.returncode:
        raise RuntimeError("Command failed: " + repr(args) + "\n" + response.stdout + response.stderr)
    return response

with tempfile.TemporaryDirectory(prefix="work-live-") as directory:
    base = Path(directory)
    project_root = base / "project with spaces and 'quotes'"
    project_root.mkdir()
    sockets = base / "sockets"
    sockets.mkdir(mode=0o700)
    env = dict(os.environ, WORK_CONFIG_HOME=str(base / "config"), WORK_STATE_HOME=str(base / "state"),
               TMUX_TMPDIR=str(sockets))
    for key in ("TMUX", "TMUX_PANE", "WORK_PROJECT"):
        env.pop(key, None)
    def work(*args):
        response = command([str(binary), *args], env)
        print(response.stdout.strip())
        return response
    def tmux(*args, check=True):
        return command(["tmux", "-L", socket, *args], env, check=check)
    def inspect():
        panes = tmux("list-panes", "-a", "-F",
                     "#{session_name}\t#{window_index}\t#{pane_index}\t#{pane_current_path}\t#{pane_active}").stdout
        windows = tmux("list-windows", "-a", "-F",
                       "#{session_name}\t#{window_index}\t#{window_name}\t#{window_active}\t#{window_layout}").stdout
        # Layout strings contain pane IDs, which change on restore.
        normalized = []
        import re
        for line in windows.splitlines():
            fields = line.split("\t")
            layout = fields[-1]
            # Drop checksum and pane IDs; retain sizes and coordinates.
            layout = re.sub(r"(\d+x\d+,\d+,\d+),\d+", r"\1", layout[5:])
            fields[-1] = layout
            normalized.append("\t".join(fields))
        return sorted(panes.splitlines()), sorted(normalized)
    try:
        work("doctor")
        work("init", name, "--root", str(project_root))
        project = base / "config/projects" / (name + ".work")
        project.write_text(
            "root = " + str(project_root) + "\n"
            "[window code]\nlayout = even-horizontal\npane = \npane = \n"
            "[window tools]\npane = \n")
        work("open", name, "--no-attach")
        tmux("split-window", "-d", "-t", name + ":0", "-c", str(project_root))
        tmux("select-layout", "-t", name + ":0", "tiled")
        tmux("select-pane", "-t", name + ":0.1")
        tmux("select-window", "-t", name + ":1")
        time.sleep(0.2)
        before = inspect()
        work("save", name)
        tmux("kill-server")
        time.sleep(0.2)
        work("open", name, "--no-attach")
        time.sleep(0.2)
        after = inspect()
        if before != after:
            raise AssertionError("Round trip differs:\nBEFORE " + repr(before) + "\nAFTER " + repr(after))
        print("PASS: real upstream save/restore round trip.")
    finally:
        tmux("kill-server", check=False)
