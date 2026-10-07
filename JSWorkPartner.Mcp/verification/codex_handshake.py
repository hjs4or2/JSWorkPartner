"""Optional Codex app-server fixture probe. No global config/auth or inference.

python codex_handshake.py --codex <installed native codex.exe>
Starts only this fixture MCP using an isolated child CODEX_HOME and cwd.
"""
import argparse
import json
import os
import pathlib
import queue
import sqlite3
import subprocess
import threading
import uuid

from verify import EXE, SCHEMA

parser = argparse.ArgumentParser()
parser.add_argument("--codex", required=True, type=pathlib.Path)
args = parser.parse_args()
assert args.codex.is_file() and args.codex.suffix == ".exe", "Use the installed native Codex executable"
root = pathlib.Path(__file__).resolve().parent / "artifacts" / ("codex-" + str(uuid.uuid4()))
root.mkdir(parents=True)
home = root / "home"
home.mkdir()
database = root / "fixture.db"
with sqlite3.connect(database) as connection:
    connection.executescript(SCHEMA)
config = ("[mcp_servers.jsworkpartner_fixture]\ncommand = " + json.dumps(str(EXE)) +
          "\nargs = " + json.dumps(["--database", str(database)]) + "\n")
(home / "config.toml").write_text(config, encoding="utf-8")
environment = os.environ.copy()
environment["CODEX_HOME"] = str(home)
environment.pop("OPENAI_API_KEY", None)
environment.pop("CODEX_API_KEY", None)
process = subprocess.Popen([str(args.codex), "app-server", "--listen", "stdio://"],
                           cwd=root, env=environment, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, text=True, encoding="utf-8")
messages = queue.Queue()
transcript = []
errors = []


def read():
    for line in process.stdout:
        value = json.loads(line)
        transcript.append(value)
        messages.put(value)


reader = threading.Thread(target=read, daemon=True)
reader.start()
err_reader = threading.Thread(target=lambda: errors.extend(process.stderr.readlines()), daemon=True)
err_reader.start()
sequence = 0


def rpc(method, params):
    global sequence
    sequence += 1
    process.stdin.write(json.dumps({"id": sequence, "method": method, "params": params}) + "\n")
    process.stdin.flush()
    while True:
        response = messages.get(timeout=35)
        if response.get("id") == sequence:
            if "error" in response:
                raise AssertionError(response)
            return response["result"]


try:
    initialization = rpc("initialize", {"clientInfo": {"name": "jsworkpartner-isolated-probe", "version": "1.0"},
                                        "capabilities": {"experimentalApi": True}})
    process.stdin.write(json.dumps({"method": "initialized"}) + "\n")
    process.stdin.flush()
    effective = rpc("config/read", {"includeLayers": False})
    configured = effective["config"].get("mcp_servers", {})
    assert set(configured) == {"jsworkpartner_fixture"}, "Unexpected MCP config; refusing to start discovery"
    inventory = rpc("mcpServerStatus/list", {"detail": "toolsAndAuthOnly"})
    entries = inventory["data"]
    assert len(entries) == 1 and entries[0]["name"] == "jsworkpartner_fixture", inventory
    tools = entries[0]["tools"]
    assert len(tools) == 4, inventory
    report = {"success": True, "initialize": initialization, "inventory": inventory,
              "isolatedHome": str(home), "database": str(database), "inference": False}
    (root / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"success": True, "tools": len(tools), "artifacts": str(root)}))
finally:
    process.stdin.close()
    try:
        process.wait(timeout=15)
    except subprocess.TimeoutExpired:
        process.terminate()  # Only this harness's app-server process.
        process.wait(timeout=10)
    reader.join(timeout=2)
    err_reader.join(timeout=2)
    (root / "transcript.json").write_text(json.dumps(transcript, indent=2), encoding="utf-8")
    (root / "stderr.txt").write_text("".join(errors), encoding="utf-8")
