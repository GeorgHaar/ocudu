"""gdb call tracer for generate_uml.py. Breaks on every function defined in the given source paths and records the
dynamic calls of a test run as JSON lines: callee, its file, caller, call-site file:line and stack depth.

Usage:
  gdb -q -batch -ex "source gdb_trace.py" -ex "trace-run REPO OUT.jsonl PREFIX,PREFIX,..." --args BINARY ARGS
"""
import json
import re

import gdb

REPO = ""


def rel(path):
    return path[len(REPO):] if path and path.startswith(REPO) else (path or "")


class Recorder(gdb.Breakpoint):
    records = []

    def __init__(self, spec):
        super().__init__(spec, internal=True)
        self.silent = True

    def stop(self):
        frame = gdb.selected_frame()
        caller = frame.older()
        depth = 0
        f = frame
        while f is not None:
            depth += 1
            f = f.older()
        site = ""
        if caller is not None:
            sal = caller.find_sal()
            if sal is not None and sal.symtab is not None:
                site = f"{rel(sal.symtab.filename)}:{sal.line}"
        here = frame.find_sal()
        function = frame.function()
        Recorder.records.append({
            "callee": frame.name() or "",
            "signature": function.print_name if function is not None else "",
            "file": rel(here.symtab.filename) if here is not None and here.symtab is not None else "",
            "caller": caller.name() if caller is not None else "",
            "site": site,
            "depth": depth,
        })
        return False


def function_spec(signature):
    """Returns "qualified::name(params)" of an "info functions" signature, or None."""
    text = signature.strip()
    if not text.endswith(")"):
        return None
    depth, i = 0, len(text) - 1
    while i >= 0:
        if text[i] == ")":
            depth += 1
        elif text[i] == "(":
            depth -= 1
            if depth == 0:
                break
        i -= 1
    if i <= 0:
        return None
    params, head = text[i:], text[:i]
    # The name is the last top-level token of the head; "(anonymous namespace)" contains a space.
    depth, j = 0, len(head) - 1
    while j >= 0:
        c = head[j]
        if c in ")>":
            depth += 1
        elif c in "(<":
            depth -= 1
        elif c == " " and depth == 0:
            break
        j -= 1
    name = head[j + 1:]
    if name.startswith("*") or name.startswith("&"):
        name = name.lstrip("*&")
    return name + params if name else None


class TraceRun(gdb.Command):
    def __init__(self):
        super().__init__("trace-run", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        global REPO
        repo, out_path, prefixes = arg.split(" ", 2)
        REPO = repo.rstrip("/") + "/"
        prefixes = [p.strip() for p in prefixes.split(",")]
        gdb.execute("set pagination off")
        gdb.execute("set print frame-arguments none")
        gdb.execute("set confirm off")
        # "info functions -n" lists non-debugging symbols too; parse only the "File ...:" sections.
        text = gdb.execute("info functions", to_string=True)
        current, specs = None, []
        for line in text.split("\n"):
            if m := re.match(r"^File (.+):$", line):
                current = rel(m.group(1))
                continue
            if current is None or not any(current.startswith(p) for p in prefixes):
                continue
            if m := re.match(r"^(\d+):\s+(.*);$", line):
                specs.append((f"{current}:{m.group(1)}", function_spec(m.group(2))))
        specs = sorted(set(specs))
        n = 0
        for by_line, by_name in specs:
            for spec in ([f"'{by_name}'"] if by_name else []) + [f"'{REPO}{by_line}'"]:
                try:
                    Recorder(spec)
                    n += 1
                    break
                except gdb.error:
                    pass
        gdb.write(f"[trace] {n} breakpoints\n")
        gdb.execute("run")
        with open(out_path, "w", encoding="utf-8") as fh:
            for r in Recorder.records:
                fh.write(json.dumps(r) + "\n")
        gdb.write(f"[trace] {len(Recorder.records)} calls written to {out_path}\n")


TraceRun()
