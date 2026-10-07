#!/usr/bin/env python3
"""Controlled subprocess fixtures; never invoked by the production program."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import time

base = Path(os.environ["WORK_FAKE_STATE"])
base.mkdir(parents=True, exist_ok=True)
tool = Path(sys.argv[0]).name
args = sys.argv[1:]
if tool not in ("tmux", "tmuxinator"):
    tool, args = args[0], args[1:]
with (base / "calls.jsonl").open("a") as log:
    log.write(json.dumps({"tool": tool, "args": args, "TMUX": os.environ.get("TMUX")}) + "\n")

def load(socket):
    path = base / (socket + ".json")
    return json.loads(path.read_text()) if path.exists() else None

def store(socket, state):
    (base / (socket + ".json")).write_text(json.dumps(state))

def scalar(text):
    text = text.strip()
    assert text.startswith("'") and text.endswith("'"), text
    return text[1:-1].replace("''", "'").replace("<%%", "<%")

def pane_record(pane):
    session, window, index, directory, command = pane
    return "\t".join(["pane", session, str(window), "1", "*", str(index),
                      ":title", ":" + directory.replace(" ", "\\ ", 1),
                      "1" if index == 0 else "0", "bash", ":" + command])

if tool == "tmuxinator":
    assert args[0] == "start"
    config = Path(args[args.index("-p") + 1])
    lines = config.read_text().splitlines()
    project = scalar(next(x[6:] for x in lines if x.startswith("name: ")))
    root = scalar(next(x[6:] for x in lines if x.startswith("root: ")))
    socket = scalar(next(x[13:] for x in lines if x.startswith("socket_name: ")))
    assert socket == "work-" + project
    assert "attach: false" in lines
    options = shlex.split(scalar(next(x[14:] for x in lines if x.startswith("tmux_options: "))))
    assert options[0] == "-f" and Path(options[1]).is_file()
    panes = []
    window = -1
    index = 0
    for line in lines:
        if line.startswith("  - "):
            window += 1
            index = 0
        elif line.startswith("        - "):
            panes.append([project, window, index, root, scalar(line[10:])])
            index += 1
    store(socket, {"panes": panes, "options": {}, "hooks": {}})
    if os.environ.get("FAKE_FAIL_START"):
        print("Deliberate tmuxinator failure", file=sys.stderr)
        sys.exit(17)
    print("Launched fixture workspace")
    sys.exit(0)

if tool == "tmux":
    assert args[0] == "-L", args
    socket = args[1]
    args = args[2:]
    if args[0] == "-f":
        assert Path(args[1]).is_file()
        args = args[2:]
    command = args[0]
    state = load(socket)
    if command == "has-session":
        sys.exit(0 if state and state["panes"] else 1)
    if command == "kill-server":
        (base / (socket + ".json")).unlink(missing_ok=True)
        sys.exit(0)
    if command == "new-session":
        name = args[args.index("-s") + 1]
        store(socket, {"panes": [[name, 0, 0, str(base), ""]], "options": {}, "hooks": {}})
        sys.exit(0)
    assert state is not None, (socket, args)
    if command == "set-option":
        key = args[2]
        if args[1] == "-gu":
            state["options"].pop(key, None)
        else:
            state["options"][key] = args[3]
        store(socket, state)
    elif command == "set-hook":
        state["hooks"][args[2]] = args[3]
        store(socket, state)
    elif command in ("run-shell", "bind-key", "select-pane", "select-window"):
        pass
    elif command == "display-message":
        fmt = args[-1]
        print("/tmp/tmux-fixture/" + socket + ",1000,0" if "socket_path" in fmt else "%0")
    elif command == "show-option":
        print(state["options"].get(args[-1], ""))
    elif command == "list-panes":
        for pane in state["panes"]:
            count = 4 if "pane_current_path" in args[-1] else 3
            print("\t".join(map(str, pane[:count])))
    elif command == "attach-session":
        tokens = shlex.split(state["hooks"]["client-detached"])
        assert tokens[:2] == ["run-shell", "-b"], tokens
        env = dict(os.environ, TMUX="/tmp/tmux-fixture/" + socket + ",1000,0")
        subprocess.run(["bash", "-c", tokens[2]], env=env, check=True, timeout=10)
    else:
        raise AssertionError(args)
    sys.exit(0)

assert tool in ("save", "restore"), tool
context = os.environ.get("TMUX", "")
socket = Path(context.split(",")[0]).name
assert socket.startswith("work-"), context
state = load(socket)
assert state
directory = Path(state["options"]["@resurrect-dir"])
last = directory / "last"
if tool == "save":
    if os.environ.get("FAKE_FAIL_SAVE"):
        print("Deliberate save failure", file=sys.stderr)
        sys.exit(23)
    if os.environ.get("FAKE_NO_SAVE"):
        sys.exit(0)
    if os.environ.get("FAKE_NO_CHANGE"):
        subprocess.run(["bash", "-c", state["options"]["@resurrect-hook-post-save-all"]], check=True)
        sys.exit(0)
    directory.mkdir(parents=True, exist_ok=True)
    snapshot = directory / ("tmux_resurrect_" + str(time.time_ns()) + ".txt")
    content = "\n".join(pane_record(x) for x in state["panes"]) + "\n"
    for window in sorted({p[1] for p in state["panes"]}):
        content += "\t".join(["window", state["panes"][0][0], str(window), ":window",
                             "1" if window == 0 else "0", "*" if window == 0 else "-", "layout", ":"]) + "\n"
    if os.environ.get("FAKE_BAD_SAVE"):
        content = "pane\tbroken\n"
    snapshot.write_text(content)
    last.unlink(missing_ok=True)
    last.symlink_to(snapshot.name)
    subprocess.run(["bash", "-c", state["options"]["@resurrect-hook-post-save-all"]], check=True)
else:
    if os.environ.get("FAKE_FAIL_RESTORE"):
        sys.exit(29)
    panes = []
    for line in last.read_text().splitlines():
        fields = line.split("\t")
        if fields[0] == "pane":
            panes.append([fields[1], int(fields[2]), int(fields[5]), fields[7][1:], fields[10][1:]])
    if os.environ.get("FAKE_INCOMPLETE_RESTORE"):
        panes = panes[:1]
    state["panes"] = panes
    store(socket, state)
