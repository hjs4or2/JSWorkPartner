"""Isolated integration harness: only creates/opens databases beneath artifacts/<uuid>.

Run after publish: python JSWorkPartner.Mcp/verification/verify.py
No external AI, user database, settings or global MCP configuration is accessed.
"""
import concurrent.futures
import hashlib
import json
import pathlib
import queue
import sqlite3
import subprocess
import threading
import time
import uuid

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXE = ROOT / "bin/Release/net8.0/win-x64/publish/JSWorkPartner.Mcp.exe"
ARTIFACTS = ROOT / "verification/artifacts" / str(uuid.uuid4())
ARTIFACTS.mkdir(parents=True)
DB = ARTIFACTS / "tasks.db"
SCHEMA = """
CREATE TABLE tasks (
 id INTEGER PRIMARY KEY AUTOINCREMENT, sequence INTEGER NOT NULL UNIQUE,
 title TEXT NOT NULL, description TEXT NOT NULL DEFAULT '', external_url TEXT NULL,
 importance INTEGER NOT NULL DEFAULT 1, deadline TEXT NULL,
 status TEXT NOT NULL DEFAULT 'pending' CHECK(status IN ('pending', 'completed')),
 created_at TEXT NOT NULL, completed_at TEXT NULL);
CREATE INDEX idx_tasks_status_sequence ON tasks(status, sequence);
"""
RESULTS = []


def check(value, label):
    assert value, label
    RESULTS.append(label)


class Client:
    def __init__(self, db=DB, initialize=True):
        self.process = subprocess.Popen([str(EXE), "--database", str(db)], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8")
        self.messages = queue.Queue()
        self.lines = []
        self.errors = []
        self.stderr = []
        self.sequence = 0

        def read():
            for line in self.process.stdout:
                self.lines.append(line)
                try:
                    message = json.loads(line)
                    assert message.get("jsonrpc") == "2.0"
                    self.messages.put(message)
                except Exception as error:
                    self.errors.append(str(error))
        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()
        self.err_reader = threading.Thread(target=lambda: self.stderr.extend(self.process.stderr.readlines()), daemon=True)
        self.err_reader.start()
        if initialize:
            reply = self.rpc("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                        "clientInfo": {"name": "isolated-fixture", "version": "1.0"}})
            assert reply["result"]["serverInfo"]["name"] == "jsworkpartner", reply
            self.notify("notifications/initialized", {})

    def send(self, message):
        self.process.stdin.write(json.dumps(message, ensure_ascii=True) + "\n")
        self.process.stdin.flush()

    def notify(self, method, params):
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def rpc(self, method, params):
        self.sequence += 1
        self.send({"jsonrpc": "2.0", "id": self.sequence, "method": method, "params": params})
        while True:
            response = self.messages.get(timeout=20)
            if response.get("id") == self.sequence:
                return response

    def tool(self, name, **arguments):
        response = self.rpc("tools/call", {"name": name, "arguments": arguments})
        assert "result" in response, response
        result = response["result"]
        content = json.loads(result["content"][0]["text"])
        assert content == result["structuredContent"], result
        return content

    def close(self):
        if self.process.poll() is None:
            self.process.stdin.close()
            try:
                self.process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                self.process.kill()  # Only the child fixture process owned by this harness.
                raise AssertionError("MCP did not exit on EOF")
        self.reader.join(timeout=2)
        self.err_reader.join(timeout=2)
        assert self.process.returncode == 0, self.stderr
        assert not self.errors, self.errors
        assert not self.stderr, self.stderr


def count(table="tasks"):
    with sqlite3.connect(DB) as db:
        return db.execute("SELECT count(*) FROM " + table).fetchone()[0]


def request():
    return str(uuid.uuid4())


def expect_error(client, code, name="create_task", **arguments):
    result = client.tool(name, **arguments)
    check(result.get("error", {}).get("code") == code, code + ":" + name)


def main():
    assert EXE.is_file(), "Publish first"
    with sqlite3.connect(DB) as db:
        db.executescript(SCHEMA)
    original = hashlib.sha256(DB.read_bytes()).hexdigest()
    client = Client()
    tools = client.rpc("tools/list", {})["result"]["tools"]
    check({t["name"] for t in tools} == {"create_task", "list_tasks", "get_task", "complete_task"}, "initialize + four tool discovery")
    check(all(t["inputSchema"]["additionalProperties"] is False for t in tools), "strict advertised schemas")
    check(client.tool("list_tasks") == {"tasks": [], "nextCursor": None}, "initial empty list")
    check(hashlib.sha256(DB.read_bytes()).hexdigest() == original, "startup and reads do not write database")

    title = "[Updater] 테스트"
    key = request()
    created = client.tool("create_task", requestId=key, title=title, details="", importance=3,
                          deadline="2026-10-08", externalUrl="https://example.com/issue/1")
    task = created["task"]
    check(task["title"] == title and isinstance(task["id"], str), "exact Korean/bracket title + decimal ID")
    retry = client.tool("create_task", requestId=key, title=title, details="", importance=3,
                        deadline="2026-10-08", externalUrl="https://example.com/issue/1")
    check(created == retry and count() == 1, "same request returns identical first snapshot")
    expect_error(client, "request_conflict", requestId=key, title="changed")
    expect_error(client, "request_conflict", name="complete_task", requestId=key, id=task["id"])
    expect_error(client, "duplicate_title", requestId=request(), title=title)
    check(client.tool("get_task", id=task["id"])["task"] == task, "get exact round trip")
    check(client.tool("list_tasks", search="[Updater]")["tasks"] == [task], "literal title search")
    complete_key = request()
    completed = client.tool("complete_task", requestId=complete_key, id=task["id"])
    check(completed["task"]["status"] == "completed" and completed["task"]["completedAt"], "complete stores timestamp")
    time.sleep(1.05)
    again = client.tool("complete_task", requestId=request(), id=task["id"])
    check(completed == again, "already complete preserves completion timestamp")
    check(client.tool("complete_task", requestId=complete_key, id=task["id"]) == completed, "same completion request returns original snapshot")
    check(client.tool("create_task", requestId=key, title=title, details="", importance=3,
                      deadline="2026-10-08", externalUrl="https://example.com/issue/1") == created, "create retry after completion returns original pending snapshot")
    client.close()
    client = Client()
    check(client.tool("get_task", id=task["id"]) == completed, "restart persists completed task")
    check(client.tool("create_task", requestId=key, title=title, details="", importance=3,
                      deadline="2026-10-08", externalUrl="https://example.com/issue/1") == created, "restart persists retry snapshot")

    raw = "  [Updater] 테스트 업무 추가해줘  "
    check(client.tool("create_task", requestId=request(), title=raw)["task"]["title"] == raw, "raw title including boundary spaces preserved")
    injection = "'; DROP TABLE tasks; -- %_"
    client.tool("create_task", requestId=request(), title=injection)
    check(len(client.tool("list_tasks", status="all", search=injection)["tasks"]) == 1, "bound SQL injection and literal wildcard search")
    for number in range(4):
        client.tool("create_task", requestId=request(), title="page " + str(number))
    page = client.tool("list_tasks", status="all", limit=2)
    seen = [t["id"] for t in page["tasks"]]
    while page["nextCursor"]:
        page = client.tool("list_tasks", status="all", limit=2, cursor=page["nextCursor"])
        seen.extend(t["id"] for t in page["tasks"])
    check(len(seen) == len(set(seen)) == count(), "bounded cursor pages without duplicates")

    bad_inputs = [{"title": ""}, {"title": " \t\n"}, {"title": "x" * 501}, {"title": "x", "importance": 0},
                  {"title": "x", "importance": 6}, {"title": "x", "importance": 1.5}, {"title": "x", "importance": "1"},
                  {"title": "x", "deadline": "2026-02-30"}, {"title": "x", "deadline": "10/07/2026"},
                  {"title": "x", "externalUrl": "file:///secret"}, {"title": "x", "externalUrl": "https://a.com/a b"},
                  {"title": "x", "externalUrl": "https://a:b@example.com"}, {"title": "x", "details": "x" * 20001},
                  {"title": "x", "details": None}, {"title": "x", "database": str(DB)}, {"title": "x\u0000"},
                  {"title": "x", "requestId": "no"}]
    before = count()
    for bad in bad_inputs:
        args = {"requestId": request(), **bad}
        expect_error(client, "invalid_input", **args)
    for args in [{"limit": 0}, {"limit": 101}, {"status": "garbage"}, {"cursor": "01"}, {"cursor": "9223372036854775808"}]:
        expect_error(client, "invalid_input", name="list_tasks", **args)
    for invalid in ["0", "-1", "1 OR 1=1", 1, "9223372036854775808"]:
        expect_error(client, "invalid_input", name="get_task", id=invalid)
    expect_error(client, "not_found", name="get_task", id="9223372036854775807")
    check(count() == before, "input errors do not insert tasks")

    clients = [Client() for _ in range(4)]
    parallel_key = request()
    with concurrent.futures.ThreadPoolExecutor(4) as pool:
        replies = list(pool.map(lambda c: c.tool("create_task", requestId=parallel_key, title="parallel shared request"), clients))
    check(all(x == replies[0] for x in replies) and count() == before + 1, "four processes same requestId create exactly one task")
    for other in clients:
        other.close()

    conflict_clients = [Client(), Client()]
    conflict_key = request()
    before = count()
    with concurrent.futures.ThreadPoolExecutor(2) as pool:
        replies = list(pool.map(lambda item: item[1].tool("create_task", requestId=conflict_key,
                        title="parallel conflict " + str(item[0])), enumerate(conflict_clients)))
    check(sum("task" in item for item in replies) == 1 and
          sum(item.get("error", {}).get("code") == "request_conflict" for item in replies) == 1 and count() == before + 1,
          "concurrent same key different input: exactly one winner and one conflict")
    for other in conflict_clients:
        other.close()

    locked_key = request()
    with sqlite3.connect(DB) as db:
        db.execute("BEGIN IMMEDIATE")
        started = time.monotonic()
        expect_error(client, "database_error", requestId=locked_key, title="locked task")
        elapsed = time.monotonic() - started
        check(4 <= elapsed <= 9, "finite five-second SQLite busy timeout")
        db.rollback()
    check(client.tool("create_task", requestId=locked_key, title="locked task")["task"]["title"] == "locked task", "retry after lock release succeeds")

    cancel_key = request()
    before = count()
    with sqlite3.connect(DB) as db:
        db.execute("BEGIN IMMEDIATE")
        client.sequence += 1
        cancelled_id = client.sequence
        client.send({"jsonrpc": "2.0", "id": cancelled_id, "method": "tools/call", "params": {
            "name": "create_task", "arguments": {"requestId": cancel_key, "title": "cancelled while locked"}}})
        time.sleep(0.3)
        client.notify("notifications/cancelled", {"requestId": cancelled_id, "reason": "fixture cancellation"})
        time.sleep(5.2)
        db.rollback()
    check(count() == before, "cancellation during busy wait never inserts a task")
    client.tool("create_task", requestId=cancel_key, title="cancelled while locked")
    check(count() == before + 1, "cancelled mutation safely retries with the same requestId")

    with sqlite3.connect(DB) as db:
        db.execute("CREATE TRIGGER fixture_reject_request BEFORE INSERT ON mcp_requests BEGIN SELECT RAISE(ABORT, 'fixture failure'); END")
    before = count()
    rollback_key = request()
    expect_error(client, "database_error", requestId=rollback_key, title="must rollback")
    check(count() == before, "request-log failure rolls back task insert")
    pending_id = client.tool("list_tasks", search="locked task")["tasks"][0]["id"]
    expect_error(client, "database_error", name="complete_task", requestId=request(), id=pending_id)
    check(client.tool("get_task", id=pending_id)["task"]["status"] == "pending", "request-log failure rolls back completion update")
    with sqlite3.connect(DB) as db:
        db.execute("DROP TRIGGER fixture_reject_request")
    client.tool("create_task", requestId=rollback_key, title="must rollback")
    check(count() == before + 1, "rolled back requestId can be retried")
    client.close()
    check(True, "EOF exits zero; all stdout lines are JSON-RPC; diagnostics contain no task data")

    # New SDK lifecycle discovery without any handwritten server protocol handling.
    discovery = Client(initialize=False)
    meta = {"io.modelcontextprotocol/protocolVersion": "2026-07-28",
            "io.modelcontextprotocol/clientInfo": {"name": "isolated-modern-fixture", "version": "1.0"},
            "io.modelcontextprotocol/clientCapabilities": {}}
    reply = discovery.rpc("server/discover", {"_meta": meta})
    (ARTIFACTS / "discovery-probe.json").write_text(json.dumps(reply, indent=2), encoding="utf-8")
    check("result" in reply, "SDK server/discover 2026-07-28 lifecycle")
    modern_tools = discovery.rpc("tools/list", {"_meta": meta})
    check(len(modern_tools["result"]["tools"]) == 4, "modern tools/list with per-request metadata")
    modern_task = discovery.rpc("tools/call", {"name": "get_task", "arguments": {"id": task["id"]}, "_meta": meta})
    check(json.loads(modern_task["result"]["content"][0]["text"]) == completed, "modern tools/call with per-request metadata")
    discovery.close()

    missing = ARTIFACTS / "missing-parent/absent.db"
    invalid = ARTIFACTS / "invalid.db"
    invalid.write_bytes(b"not sqlite")
    old = ARTIFACTS / "old.db"
    foreign = ARTIFACTS / "foreign.db"
    with sqlite3.connect(old) as db:
        db.executescript("CREATE TABLE tasks(id INTEGER PRIMARY KEY, description TEXT);")
    with sqlite3.connect(foreign) as db:
        db.executescript("CREATE TABLE unrelated(id INTEGER PRIMARY KEY);")
    for bad in [missing, invalid, old, foreign]:
        digest = hashlib.sha256(bad.read_bytes()).hexdigest() if bad.exists() else None
        result = subprocess.run([str(EXE), "--database", str(bad)], input="", text=True, capture_output=True, timeout=15)
        check(result.returncode != 0 and not result.stdout, "reject DB without stdout: " + bad.name)
        if digest:
            check(hashlib.sha256(bad.read_bytes()).hexdigest() == digest, "rejected DB unchanged: " + bad.name)
    check(not missing.parent.exists(), "nonexistent DB and parent never created")
    for args in [[], ["--database", "relative.db"], ["--database", str(DB), "--other"]]:
        result = subprocess.run([str(EXE), *args], input="", text=True, capture_output=True, timeout=15)
        check(result.returncode == 2 and not result.stdout, "invalid CLI arguments rejected")


if __name__ == "__main__":
    try:
        main()
    finally:
        report = {"passed": RESULTS, "count": len(RESULTS), "fixtureDirectory": str(ARTIFACTS)}
        (ARTIFACTS / "results.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"passed": len(RESULTS), "artifacts": str(ARTIFACTS)}))
