#!/usr/bin/env python3
"""Generates navigable UML diagrams and a source viewer for the precoding unit tests.

Covers every test program built from tests/unittests/ran/precoding and produces:
- a start page with all test programs and their source files,
- per test program, the call chain from main() through gtest to every test case,
- per test case, a sequence diagram with the static call chain from the fixture set-up and TestBody() into the
  libraries,
- class diagrams of the precoding types, the beamforming and PHY types, and the test fixtures,
- Doxygen documentation of the involved code, if Doxygen is installed,
- a web page that shows the diagrams next to the original source code and the Doxygen documentation.

Requires clang-uml, Java and a PlantUML JAR. Usage, from any directory:

    python3 docs/uml/precoding_tests/generate_uml.py

Open <build dir>/clang_uml/precoding_tests/viewer/index.html in a web browser afterwards.

By default, trivial getters are hidden and repeated calls are expanded once. Pass --all-calls to show every call.
"""

import argparse
import fnmatch
import glob
import json
import os
import re
import resource
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO = SCRIPT_DIR.parents[2]
OUTPUT_NAME = "precoding_tests"
TITLE = "Precoding-Unittests"

TEST_DIR = "tests/unittests/ran/precoding"
CMAKE_FILE = "tests/unittests/ran/CMakeLists.txt"
GTEST_MAIN = "tests/test_doubles/utils/ocudu_gtest_main.cpp"
GTEST_HEADER = "/usr/include/gtest/gtest.h"

# Library translation units that implement a header directory. A test program parses the ones whose headers it
# includes.
LIBRARY_SOURCES = [
    ("include/ocudu/ran/precoding/", ["lib/ran/precoding/*.cpp", "lib/ran/precoding/*/*.cpp"]),
    ("lib/ran/precoding/", ["lib/ran/precoding/*.cpp", "lib/ran/precoding/*/*.cpp"]),
    ("include/ocudu/ran/beamforming/", ["lib/ran/beamforming/*.cpp"]),
    ("include/ocudu/phy/support/", ["lib/phy/support/support_factories.cpp", "lib/phy/support/resource_grid_impl.cpp",
                                    "lib/phy/support/resource_grid_mapper_impl.cpp",
                                    "lib/phy/support/resource_grid_reader_impl.cpp",
                                    "lib/phy/support/resource_grid_writer_impl.cpp"]),
    ("include/ocudu/phy/generic_functions/precoding/",
     ["lib/phy/generic_functions/precoding/precoding_factories.cpp",
      "lib/phy/generic_functions/precoding/channel_precoder_impl.cpp",
      "lib/phy/generic_functions/precoding/channel_precoder_generic.cpp"]),
]

# Only functions and classes declared under these paths appear in the sequence diagrams. Calls are followed from the
# test, so a broad path adds only what the test reaches.
SEQUENCE_PATHS = [
    TEST_DIR,
    "include/ocudu/ran",
    "lib/ran/precoding",
    "lib/ran/beamforming",
    "include/ocudu/phy/support",
    "lib/phy/support",
    "include/ocudu/phy/generic_functions/precoding",
    "lib/phy/generic_functions/precoding",
]

# Inputs of the Doxygen run and of the definition lookup.
DOC_PATHS = [
    TEST_DIR,
    "include/ocudu/ran/precoding",
    "lib/ran/precoding",
    "include/ocudu/ran/beamforming",
    "lib/ran/beamforming",
    "include/ocudu/ran/precoding_beamforming_configuration.h",
    "include/ocudu/ran/antenna_topology.h",
    "include/ocudu/phy/support",
    "lib/phy/support",
    "include/ocudu/phy/generic_functions/precoding",
    "lib/phy/generic_functions/precoding",
    GTEST_MAIN,
]

# Class diagrams: name, title, translation units, shown paths and excluded element patterns.
CLASS_DIAGRAMS = [
    ("classes_precoding", "Precoding-Typen",
     ["lib/ran/precoding/*.cpp", "lib/ran/precoding/*/*.cpp", f"{TEST_DIR}/precoding_beamforming_configuration_test.cpp"],
     ["include/ocudu/ran/precoding", "lib/ran/precoding", "include/ocudu/ran/precoding_beamforming_configuration.h"],
     []),
    ("classes_beamforming_phy", "Beamforming- und PHY-Typen",
     ["lib/ran/beamforming/*.cpp", "lib/phy/support/support_factories.cpp",
      "lib/phy/generic_functions/precoding/precoding_factories.cpp",
      f"{TEST_DIR}/precoding_matrix_resource_grid_test.cpp"],
     ["include/ocudu/ran/beamforming", "include/ocudu/ran/antenna_topology.h",
      "include/ocudu/phy/support/precoding_configuration.h", "include/ocudu/phy/support/resource_grid.h",
      "include/ocudu/phy/support/resource_grid_mapper.h", "include/ocudu/phy/support/resource_grid_reader.h",
      "include/ocudu/phy/support/resource_grid_writer.h", "include/ocudu/phy/generic_functions/precoding"],
     []),
    ("classes_test_fixtures", "Test-Fixtures und Testdaten",
     [f"{TEST_DIR}/*.cpp"],
     [TEST_DIR],
     [r".*_Test$"]),
]

# Overview groups of precoding_codebooks_test, matched against the test name in order.
GROUPS = {
    "precoding_codebooks_test": [
        ("Grundmatrizen", r"^(OneLayerOnePort|OneLayerAllPorts|Identity)$"),
        ("Zwei Ports", r"TwoPorts$"),
        ("Type I Single-Panel, 4 Ports, Panel 2x1", r"^Type1SinglePanelMode1_\w+FourPorts$"),
        ("Type I Single-Panel, 8 Ports, Panel 4x1", r"^Type1SinglePanelMode1_\w+_4x1$"),
        ("Type I Single-Panel, 8 Ports, Panel 2x2", r"^Type1SinglePanelMode1_\w+_2x2$"),
        ("Type I Single-Panel, Fehlerfälle", r"^TypeISinglePanelMode1_"),
        ("Type II", r"^Type2_"),
    ],
}

# Trivial getters hidden by default. The assertion macros evaluate them several times per call, which buries the
# relevant calls.
NOISE_ELEMENTS = [r".*::get_nof_(layers|ports)\(\) const"]

TEST_RE = re.compile(r"^(?P<macro>TEST|TEST_F|TEST_P)\(\s*(?P<suite>\w+)\s*,\s*(?P<name>\w+)\s*\)")
INSTANTIATE_RE = re.compile(r"^INSTANTIATE_TEST_SUITE_P\(\s*(?P<prefix>\w+)\s*,\s*(?P<fixture>\w+)\s*,", re.S)
CLASS_RE = re.compile(r"^(?:class|struct)\s+(?P<name>\w+)\s*(?:final\s*)?:\s*public\s+(?P<base>[\w:]+)")
FIXTURE_METHODS = {
    "SetUpTestSuite": r"\bstatic\s+void\s+SetUpTestSuite\s*\(\s*\)",
    "SetUp": r"\bvoid\s+SetUp\s*\(\s*\)",
    "TearDown": r"\bvoid\s+TearDown\s*\(\s*\)",
    "TearDownTestSuite": r"\bstatic\s+void\s+TearDownTestSuite\s*\(\s*\)",
}

LINK = r"(?: \[\[src://(?P<path>[^#\]]+)#(?P<line>\d+)(?:\{[^}]*\})?\]\])?"
PARTICIPANT_RE = re.compile(r'^participant "(?P<title>.*)" as (?P<id>C_\d+)' + LINK + "$")
CALL_RE = re.compile(r"^(?P<a>C_\d+) -> (?P<b>C_\d+)" + LINK + r"(?: : (?P<label>.*))?$")
RETURN_RE = re.compile(r"^(?P<a>C_\d+) --> (?P<b>C_\d+)(?: : (?P<label>.*))?$")
ENTRY_RE = re.compile(r"^\[-> (?P<b>C_\d+)(?: : (?P<label>.*))?$")
GROUP_RE = re.compile(r"^(?P<kw>loop|alt|opt|else|group)(?: (?P<text>.*))?$")


class Sources:
    """Cache of source files, addressed by repository-relative or absolute path."""

    def __init__(self):
        self._lines = {}
        self._normalized = {}

    def lines(self, path):
        if path not in self._lines:
            p = Path(path)
            try:
                text = (p if p.is_absolute() else REPO / p).read_text(encoding="utf-8", errors="replace")
                self._lines[path] = text.split("\n")
            except OSError:
                self._lines[path] = None
        return self._lines[path]

    def text(self, path):
        lines = self.lines(path)
        return None if lines is None else "\n".join(lines)

    def line_of(self, path, pattern, start=1):
        """Returns the first line at or after start that matches the regular expression, or None."""
        lines = self.lines(path) or []
        regex = re.compile(pattern)
        for no in range(max(start, 1), len(lines) + 1):
            if regex.search(lines[no - 1]):
                return no
        return None

    def find_code(self, path, needle, start=1):
        """Returns the line where needle starts, ignoring whitespace, searching from start first."""
        if path not in self._normalized:
            chars, owners = [], []
            for no, line in enumerate(self.lines(path) or [], start=1):
                for c in line:
                    if not c.isspace():
                        chars.append(c)
                        owners.append(no)
            self._normalized[path] = ("".join(chars), owners)
        text, owners = self._normalized[path]
        wanted = re.sub(r"\s+", "", needle)
        if not wanted:
            return None
        first = next((i for i, no in enumerate(owners) if no >= start), len(owners))
        pos = text.find(wanted, first)
        if pos < 0:
            pos = text.find(wanted)
        return owners[pos] if pos >= 0 else None


def files_under(paths, patterns=("*.h", "*.cpp")):
    """Returns the repository-relative files of the given files and directories."""
    out = []
    for p in paths:
        full = Path(p) if Path(p).is_absolute() else REPO / p
        if full.is_file():
            out.append(p)
            continue
        for pattern in patterns:
            out += [os.path.relpath(f, REPO) for f in glob.glob(str(full / "**" / pattern), recursive=True)]
    return sorted(set(out))


def yaml_str(value):
    return '"' + value.replace("\\", "\\\\").replace('"', '\\"') + '"'


def loc(path, line, span="stmt"):
    """Source location of a viewer element. span is "block" for code with a body, "stmt" for one statement."""
    if not path or not line:
        return None
    return {"path": path, "line": int(line), "span": span}


# Test discovery.


class TestCase:
    def __init__(self, macro, suite, name, source, line, program):
        self.macro, self.suite, self.name, self.source, self.line = macro, suite, name, source, line
        self.program = program
        self.key = f"{suite}__{name}"
        self.instances = 1

    @property
    def test_class(self):
        return f"{self.suite}_{self.name}_Test"

    @property
    def location(self):
        return loc(self.source, self.line, "block")

    def group(self):
        for label, pattern in GROUPS.get(self.program.name, []):
            if re.search(pattern, self.name):
                return label
        return f"{self.suite} ({self.macro})"


class TestProgram:
    def __init__(self, name, sources):
        self.name = name
        self.sources = sources
        self.headers = []
        self.tests = []
        self.instantiations = []
        self.classes = {}

    @property
    def key(self):
        return f"exe__{self.name}"


def find_programs(sources, build_dir):
    """Returns the test programs built from the precoding test directory, with their test cases."""
    text = (REPO / CMAKE_FILE).read_text(encoding="utf-8")
    cmake_dir = os.path.dirname(CMAKE_FILE)
    programs = []
    for m in re.finditer(r"add_executable\(\s*(\w+)\s+([^)]*)\)", text):
        files = [os.path.normpath(f"{cmake_dir}/{s}") for s in m.group(2).split()]
        files = [f for f in files if f.startswith(TEST_DIR + "/")]
        if files:
            programs.append(TestProgram(m.group(1), files))
    for program in programs:
        for source in program.sources:
            for inc in re.findall(r'^#include "(\w+\.h)"', sources.text(source) or "", re.M):
                header = f"{TEST_DIR}/{inc}"
                if (REPO / header).is_file() and header not in program.headers:
                    program.headers.append(header)
        for path in program.sources + program.headers:
            lines = sources.lines(path) or []
            # Named namespaces qualify the fixture names clang-uml expects; anonymous ones do not.
            namespaces = []
            for no, line in enumerate(lines, start=1):
                if ns := re.match(r"^namespace\s*(\w*)\s*\{", line):
                    namespaces.append(ns.group(1))
                elif re.match(r"^\}\s*//\s*namespace", line) and namespaces:
                    namespaces.pop()
                if m := TEST_RE.match(line):
                    program.tests.append(TestCase(m["macro"], m["suite"], m["name"], path, no, program))
                elif line.startswith("INSTANTIATE_TEST_SUITE_P(") and (
                        m := INSTANTIATE_RE.match(tail := " ".join(lines[no - 1:no + 3]))):
                    generator = re.search(r"(?:ValuesIn|Values)\(\s*(\w+)\s*\(", tail)
                    program.instantiations.append({"prefix": m["prefix"], "fixture": m["fixture"], "path": path,
                                                   "line": no, "generator": generator.group(1) if generator else None})
                elif m := CLASS_RE.match(line):
                    qualified = "::".join([n for n in namespaces if n] + [m["name"]])
                    program.classes[m["name"]] = {"base": m["base"], "path": path, "line": no, "qualified": qualified,
                                                  "methods": class_methods(lines, no)}
        count_instances(program, build_dir)
    return programs


def class_methods(lines, start):
    """Returns the gtest fixture methods defined in the class body that starts at the given line."""
    depth, opened, methods = 0, False, {}
    for no in range(start, min(len(lines), start + 2000) + 1):
        text = lines[no - 1]
        for method, pattern in FIXTURE_METHODS.items():
            if method not in methods and re.search(pattern, text):
                methods[method] = no
        depth += text.count("{") - text.count("}")
        opened = opened or "{" in text
        if opened and depth <= 0:
            break
    return methods


def fixture_method(program, fixture, method):
    """Returns (class, path, line) of the fixture method gtest calls, following base classes in the tests."""
    seen = set()
    while fixture in program.classes and fixture not in seen:
        seen.add(fixture)
        info = program.classes[fixture]
        if method in info["methods"]:
            return info["qualified"], info["path"], info["methods"][method]
        fixture = info["base"]
    return None


def count_instances(program, build_dir):
    """Counts the parameter instances of each TEST_P from the built test program, if available."""
    binary = build_dir / os.path.dirname(CMAKE_FILE) / program.name
    if not binary.is_file():
        return
    try:
        listing = subprocess.run([str(binary), "--gtest_list_tests"], capture_output=True, text=True, timeout=60).stdout
    except (OSError, subprocess.TimeoutExpired):
        return
    counts, suite = {}, None
    for line in listing.split("\n"):
        if line and not line.startswith(" ") and not line.startswith("["):
            suite = line.strip().rstrip(".").split("/")[-1]
        elif line.startswith("  ") and suite:
            name = line.strip().split()[0].split("/")[0]
            counts[(suite, name)] = counts.get((suite, name), 0) + 1
    for test in program.tests:
        test.instances = counts.get((test.suite, test.name), 1)


def library_tus(program, sources, compile_db):
    """Returns the library translation units that implement the headers the test program includes, directly or
    through other repository headers."""
    includes, pending = set(), list(program.sources + program.headers)
    while pending:
        path = pending.pop()
        for inc in re.findall(r'^#include "([^"]+)"', sources.text(path) or "", re.M):
            header = "include/" + inc if inc.startswith("ocudu/") else inc
            if header not in includes and (REPO / header).is_file():
                includes.add(header)
                pending.append(header)
    patterns = [p for prefix, globs in LIBRARY_SOURCES if any(i.startswith(prefix) for i in includes) for p in globs]
    return sorted(f for f in compile_db if any(fnmatch.fnmatch(f, p) for p in patterns))


def read_compile_db(build_dir):
    entries = json.loads((build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    return sorted({os.path.relpath(e["file"], REPO) for e in entries})


# clang-uml configuration.


def config_header(build_dir, out_dir, output="raw"):
    return [
        "# Generated by generate_uml.py. Edit the script instead of this file.",
        f"relative_to: {yaml_str(str(REPO))}",
        f"compilation_database_dir: {yaml_str(str(build_dir))}",
        f"output_directory: {yaml_str(str(out_dir / output))}",
        # Clang rejects some GCC warning flags; -Werror would turn that into a parse failure. The GCC system include
        # paths are not queried, because Clang cannot parse the GCC intrinsics headers.
        "remove_compile_flags:",
        "  - -Werror",
        "  - -Wno-error=stringop-overflow",
        "  - -Wno-maybe-uninitialized",
        "add_compile_flags:",
        "  - -Wno-unknown-warning-option",
        "diagrams:",
    ]


LINK_TEMPLATE = [
    "    generate_links:",
    '      link: "src://{{ element.source.path }}#{{ element.source.line }}"',
    '      tooltip: "{{ element.source.path }}:{{ element.source.line }}"',
]


def start_functions(test):
    """Returns the functions gtest runs for a test case, in call order."""
    starts = []
    if test.macro != "TEST":
        for method in ("SetUpTestSuite", "SetUp"):
            if found := fixture_method(test.program, test.suite, method):
                starts.append(f"{found[0]}::{method}()")
    starts.append(f"{test.test_class}::TestBody()")
    if test.macro != "TEST" and (found := fixture_method(test.program, test.suite, "TearDown")):
        starts.append(f"{found[0]}::TearDown()")
    return starts


def start_entries(test):
    """Returns the YAML lines of the "from" list: the gtest entry points, then the functions that the gdb trace
    reached only through code outside the diagram, such as std::visit, gtest assertions or implicit members."""
    lines = [f"      - function: {yaml_str(f)}" for f in start_functions(test)]
    for bridge in getattr(test, "bridges", []):
        lines += ["      - function:", f"          r: {yaml_str(bridge['regex'])}"]
    return lines


def write_sequence_config(path, build_dir, out_dir, program, tus, all_calls, tests=None, output="raw"):
    lines = config_header(build_dir, out_dir, output)
    for test in tests if tests is not None else program.tests:
        lines += [
            f"  {test.key}:",
            "    type: sequence",
            "    glob:",
            f"      - {yaml_str(str(REPO / test.source))}",
            *[f"      - {yaml_str(str(REPO / t))}" for t in tus],
            "    include:",
            "      paths:",
            *[f"        - {yaml_str(str(REPO / p))}" for p in SEQUENCE_PATHS],
            "    exclude:",
            *([] if all_calls else ["      elements:", *[f"        - r: {yaml_str(e)}" for e in NOISE_ELEMENTS]]),
            "      namespaces:",
            "        - std",
            "    generate_method_arguments: abbreviated",
            "    generate_condition_statements: true",
            "    generate_return_values: true",
            "    inline_lambda_messages: true",
            *([] if all_calls else ["    fold_repeated_activities: true"]),
            *LINK_TEMPLATE,
            "    from:",
            *start_entries(test),
            "    plantuml:",
            "      before:",
            f"        - {yaml_str(f'title {program.name}: {test.suite}.{test.name}')}",
            f"        - {yaml_str(f'header [[../executables/{program.name}.svg Zurück zu {program.name}]]')}",
            f"        - {yaml_str(f'footer Quelle: {test.source}:{test.line}')}",
        ]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_class_config(path, build_dir, out_dir, compile_db):
    lines = config_header(build_dir, out_dir)
    for name, title, tu_globs, shown, excluded in CLASS_DIAGRAMS:
        tus = [f for f in compile_db if any(fnmatch.fnmatch(f, g) for g in tu_globs)]
        lines += [
            f"  {name}:",
            "    type: class",
            "    using_namespace: ocudu",
            "    glob:",
            *[f"      - {yaml_str(str(REPO / t))}" for t in tus],
            "    include:",
            "      paths:",
            *[f"        - {yaml_str(str(REPO / p))}" for p in shown],
            "    exclude:",
            *(["      elements:", *[f"        - r: {yaml_str(e)}" for e in excluded]] if excluded else []),
            "      namespaces:",
            "        - std",
            "    generate_method_arguments: abbreviated",
            *LINK_TEMPLATE,
            "    plantuml:",
            "      before:",
            f"        - {yaml_str(f'title Klassendiagramm: {title}')}",
            f"        - {yaml_str('header [[../index.svg Zurück zur Übersicht]]')}",
        ]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


MEMORY_LIMIT_GB = 6


def limit_memory():
    """Caps the address space of a clang-uml process. A diagram that needs more fails instead of exhausting the
    machine; the generator then keeps the diagram of the first pass."""
    limit = MEMORY_LIMIT_GB * 1024 ** 3
    try:
        resource.setrlimit(resource.RLIMIT_AS, (limit, limit))
    except (ValueError, OSError):
        pass


def run_clang_uml(config, threads, log, diagram=None):
    """Runs clang-uml on a configuration, or on one of its diagrams. Returns the output text."""
    cmd = ["clang-uml", "-c", str(config), "-g", "plantuml", "-g", "json", "-t", str(threads)]
    if diagram:
        cmd += ["-n", diagram]
    result = subprocess.run(cmd, capture_output=True, text=True, cwd=REPO, preexec_fn=limit_memory)
    with open(log, "a", encoding="utf-8") as f:
        f.write(f"\n===== {config}{' -n ' + diagram if diagram else ''} =====\n{result.stdout}{result.stderr}")
    return result.stdout + result.stderr


UNKNOWN_START_RE = re.compile(r"Failed to find participant matching '([^']+)' for 'from' condition")


def run_bridged(program, test, config_dir, build_dir, out_dir, tus, all_calls, log):
    """Regenerates one diagram with its bridges. Bridges clang-uml does not know are dropped and the run is
    repeated. Returns True when the bridged diagram was written."""
    for _ in range(len(test.bridges) + 1):
        if not test.bridges:
            return False
        config = config_dir / f"clang-uml_{test.key}_bridges.yml"
        write_sequence_config(config, build_dir, out_dir, program, tus, all_calls, [test], output="bridged")
        output = run_clang_uml(config, 1, log, test.key)
        if (out_dir / "bridged" / f"{test.key}.json").is_file():
            return True
        unknown = UNKNOWN_START_RE.findall(output)
        if not unknown:
            return False
        test.bridges = [b for b in test.bridges if b["regex"] not in unknown]
        test.dropped = getattr(test, "dropped", []) + [b["name"] for b in test.bridges if b["regex"] in unknown]
    return False


# gdb validation. The dynamic call chain of one test run is compared with the static diagram.


def gdb_filter(test):
    return f"*/{test.suite}.{test.name}/0" if test.macro == "TEST_P" else f"{test.suite}.{test.name}"


def run_gdb_trace(test, binary, out_path):
    """Records the dynamic calls of one test run. Returns the records, or None if gdb could not run the test."""
    prefixes = ",".join(SEQUENCE_PATHS)
    cmd = ["gdb", "-q", "-batch", "-ex", f"source {SCRIPT_DIR / 'gdb_trace.py'}",
           "-ex", f"trace-run {REPO} {out_path} {prefixes}", "--args", str(binary), f"--gtest_filter={gdb_filter(test)}"]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=600, cwd=REPO)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if "[trace]" not in result.stdout or not Path(out_path).is_file():
        return None
    return [json.loads(line) for line in Path(out_path).read_text(encoding="utf-8").split("\n") if line]


def gdb_name(name):
    """Qualified name of a gdb frame without arguments, template arguments, namespaces and const."""
    name = (name or "").replace("(anonymous namespace)::", "").replace("ocudu::", "")
    if "operator()" in name:
        name = name[:name.index("operator()") + len("operator()")]
    else:
        name = re.sub(r"\(.*$", "", name)
    for _ in range(3):
        name = re.sub(r"<[^<>]*>", "", name)
    return re.sub(r"\s+const$", "", name).strip()


def model_names(json_path):
    """Returns the normalized names of all activities in a clang-uml sequence model."""
    data = json.loads(Path(json_path).read_text(encoding="utf-8"))
    names = set()
    for p in data["participants"]:
        for a in [p] + p.get("activities", []):
            names.add(gdb_name(a.get("full_name") or a.get("name", "")))
    return names


def trace_windows(records, test):
    """Returns the records that run inside the gtest entry points of the test."""
    starts = [gdb_name(s) for s in start_functions(test)]
    inside = []
    for i, r in enumerate(records):
        if gdb_name(r["callee"]) in starts:
            depth = r["depth"]
            j = i + 1
            while j < len(records) and records[j]["depth"] > depth:
                j += 1
            inside += records[i + 1:j]
    return inside


def is_user_defined(sources, callee):
    """True if the function has a definition in the source tree. Compiler-generated members have none, and
    clang-uml cannot start a sequence from them."""
    parts = callee.split("::")
    short, owner = parts[-1], parts[-2] if len(parts) >= 2 else ""
    if owner and short == owner:
        pattern = re.compile(r"(^|[\s:])(explicit\s+)?" + re.escape(short) + r"\s*\(")
    elif owner and short == "operator=":
        pattern = re.compile(re.escape(owner) + r"\s*&\s*operator=\s*\(|operator=\s*\(\s*(const\s+)?" +
                             re.escape(owner))
    else:
        return True
    return any(pattern.search(line) for path in files_under(SEQUENCE_PATHS)
               for line in (sources.lines(path) or []) if not line.lstrip().startswith("//"))


def bridge_regex(callee, signature=""):
    """Regular expression for the clang-uml "from" condition. The parameter type names of the gdb signature
    select the overload, so that a bridge adds one sequence instead of one per overload."""
    segments = [re.escape(seg) for seg in callee.split("::")]
    if segments[-1] == re.escape("operator()"):
        segments[-1] = r"operator\(\)"
    params = ""
    if signature and signature.endswith((")", "const")):
        i = signature.rfind("(") if "operator()" not in signature else signature.rfind("(", 0, signature.rfind(")"))
        inner = signature[i + 1:signature.rfind(")")] if i >= 0 else ""
        types = []
        for part in split_top_level(inner):
            words = [w for w in re.findall(r"[A-Za-z_]\w*", re.sub(r"<[^<>]*>", "", part))
                     if w not in ("const", "unsigned", "int", "ocudu", "std", "volatile")]
            types.append(re.escape(words[-1]) if words else "")
        params = "".join(f"[^,]*{t}[^,]*," for t in types)
        params = params[:-1] if params else ""
    return ".*" + r"(<[^>]*>)?::".join(segments) + r"\(" + params + r"\).*"


def find_bridges(records, test, known, sources, limit=6):
    """Returns the functions the trace reached only through callers that the diagram cannot show. Everything a
    bridge reaches itself is covered by its sequence and needs no bridge of its own."""
    inside = trace_windows(records, test)
    known_short = {name.split("::")[-1] for name in known}
    bridges, seen, i = [], set(), 0
    while i < len(inside) and len(bridges) < limit:
        r = inside[i]
        callee, caller = gdb_name(r["callee"]), gdb_name(r["caller"])
        short = callee.split("::")[-1]
        i += 1
        if not callee or callee in known or callee in seen or short.startswith("~"):
            continue
        if short in ("get_nof_layers", "get_nof_ports") or "lambda" in callee or "lambda" in caller:
            continue
        if caller in known and not (short in known_short and len(callee.split("::")) >= 2):
            # The caller is in the diagram; a missing callee there is a modelling gap, not a bridge. An unknown
            # method whose name the diagram knows from an interface is reached by virtual dispatch.
            continue
        if not is_user_defined(sources, callee):
            continue
        seen.add(callee)
        bridges.append({"name": callee, "regex": bridge_regex(callee, r.get("signature", "")), "caller": caller,
                        "site": r["site"]})
        # Skip the calls inside this bridge.
        depth = r["depth"]
        while i < len(inside) and inside[i]["depth"] > depth:
            i += 1
    return bridges


def validate_with_gdb(programs, build_dir, out_dir, raw_dir, sources, reuse=False):
    """Traces every test case with gdb, attaches the bridges to the tests and returns the validation report."""
    report = {}
    trace_dir = out_dir / "gdb"
    trace_dir.mkdir(parents=True, exist_ok=True)
    for program in programs:
        binary = build_dir / os.path.dirname(CMAKE_FILE) / program.name
        if not binary.is_file():
            print(f"gdb: {program.name} is not built, skipped.")
            continue
        for test in program.tests:
            model = raw_dir / f"{test.key}.json"
            trace = trace_dir / f"{test.key}.jsonl"
            if reuse and trace.is_file():
                records = [json.loads(line) for line in trace.read_text(encoding="utf-8").split("\n") if line]
            else:
                records = run_gdb_trace(test, binary, trace)
            if records is None or not model.is_file():
                report[test.key] = {"status": "no trace"}
                continue
            known = model_names(model)
            inside = trace_windows(records, test)
            test.bridges = find_bridges(records, test, known, sources)
            unmodelled = sorted({gdb_name(r["callee"]) for r in inside
                                 if gdb_name(r["callee"]) not in known and gdb_name(r["caller"]) in known
                                 and not gdb_name(r["callee"]).split("::")[-1].startswith("~")
                                 and gdb_name(r["callee"]).split("::")[-1] not in ("get_nof_layers", "get_nof_ports")})
            implicit = [c for c in unmodelled if not is_user_defined(sources, c)]
            report[test.key] = {"status": "ok", "calls": len(inside), "bridges": test.bridges,
                                "missing": [c for c in unmodelled if c not in implicit], "implicit": implicit}
    return report


# Definition lookup. clang-uml reports the first declaration it sees, which is often a header declaration.


def split_top_level(text):
    """Splits text at commas that are not nested in brackets."""
    parts, depth, cur = [], 0, ""
    for c in text:
        if c in "<([{":
            depth += 1
        elif c in ">)]}":
            depth -= 1
        if c == "," and depth == 0:
            parts.append(cur)
            cur = ""
        else:
            cur += c
    if cur.strip():
        parts.append(cur)
    return parts


def normalize_type(text):
    text = text.replace("ocudu::", "")
    text = re.sub(r"\bunsigned int\b", "unsigned", text)
    return re.sub(r"\s+", "", text)


def signature_params(signature):
    i, j = signature.find("("), signature.rfind(")")
    if i < 0 or j < i:
        return []
    return [normalize_type(p) for p in split_top_level(signature[i + 1:j])]


def definition_head(sources, path, line):
    """Returns (is_definition, parameter text) of the function declarator that starts at path:line."""
    lines = sources.lines(path)
    if not lines or not 1 <= line <= len(lines):
        return False, ""
    text = "\n".join(lines[line - 1:line + 40])
    i = text.find("(")
    if i < 0:
        return False, ""
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "(":
            depth += 1
        elif text[j] == ")":
            depth -= 1
            if depth == 0:
                break
    else:
        return False, ""
    m = re.search(r"[{;]", text[j + 1:])
    return bool(m and m.group() == "{"), text[i + 1:j]


class DefinitionIndex:
    """Function definitions that start in the first column, indexed by function name."""

    NAME_RE = re.compile(r"(operator\s*[^\s(]+|[A-Za-z_~]\w*)\s*\(")

    def __init__(self, sources, paths):
        self.sources = sources
        self.by_name = {}
        for path in files_under(paths):
            for no, text in enumerate(sources.lines(path) or [], start=1):
                if not text or text[0].isspace() or text.startswith(("/", "#", "return", "}")):
                    continue
                if m := self.NAME_RE.search(text):
                    self.by_name.setdefault(m.group(1), []).append((path, no))

    def find(self, name, owner, signature, decl):
        """Returns the location of the function body for a callable, or None."""
        if decl and definition_head(self.sources, decl["path"], decl["line"])[0]:
            return loc(decl["path"], decl["line"], "block")
        qualified = re.compile((re.escape(owner) + r"(?:<[^>]*>)?::" if owner else r"(?:^|[\s*&:])") +
                               re.escape(name) + r"\s*\(")
        wanted = signature_params(signature)
        best, best_score = None, -1
        for path, no in self.by_name.get(name, []):
            text = self.sources.lines(path)[no - 1]
            if not qualified.search(text):
                continue
            is_def, params = definition_head(self.sources, path, no)
            if not is_def:
                continue
            have = [normalize_type(p) for p in split_top_level(params)]
            score = sum(1 for k, w in enumerate(wanted) if k < len(have) and have[k].startswith(w))
            score += 1 if len(have) == len(wanted) else 0
            score += 1 if decl and path == decl["path"] else 0
            if score > best_score:
                best, best_score = loc(path, no, "block"), score
        return best


# Post-processing of the clang-uml output.


def pid(token):
    return str(int(token[2:]))


def safe_label(label):
    return not re.search(r"[\[\]{}]", label)


def tooltip(text):
    return re.sub(r"[{}\[\]]", "", text)


def link(idx, location, tip, label):
    return f"[[src://{location['path']}?e={idx}#{location['line']}{{{tooltip(tip)}}} {label}]]"


def dedupe(entries):
    """Returns the labelled locations, dropping empty and repeated ones."""
    out, seen = [], set()
    for label, location in entries:
        if location and (location["path"], location["line"]) not in seen:
            seen.add((location["path"], location["line"]))
            out.append({"label": label, **location})
    return out


class Model:
    """clang-uml JSON model of one sequence diagram, with definition lookup."""

    def __init__(self, json_path, definitions, test):
        data = json.loads(Path(json_path).read_text(encoding="utf-8"))
        self.definitions = definitions
        self.test = test
        self.participants = {}
        self.activities = {}
        for p in data["participants"]:
            self.participants[p["id"]] = p
            if "activities" not in p:
                self.activities[p["id"]] = {**p, "owner": None}
            for a in p.get("activities", []):
                self.activities[a["id"]] = {**a, "owner": p}
        self.calls, self.returns, self.blocks, self.switches = [], [], [], []
        self.starts = [s.get("from", {}).get("activity_id") for s in data["sequences"]]
        for s in data["sequences"]:
            self._walk(s.get("messages", []))
        self.call_index = {}
        for m in self.calls:
            sl = m.get("source_location") or {}
            key = (m["from"]["participant_id"], m["to"]["participant_id"], sl.get("file"), sl.get("line"))
            self.call_index.setdefault(key, []).append(m)
        self.return_pos = 0
        self.block_pos = 0
        self.switch_pos = 0
        self._locations = {}

    def _walk(self, messages):
        for m in messages:
            t = m.get("type")
            if t == "message":
                self.calls.append(m)
            elif t == "return":
                self.returns.append(m)
            elif "condition_text" in m:
                self.blocks.append(m)
            elif m.get("name") == "switch":
                self.switches.append(m)
            self._walk(m.get("messages", []))
            for b in m.get("branches", []):
                self._walk(b.get("messages", []))

    def is_test(self, participant):
        return bool(participant) and participant.get("name", "") == self.test.test_class

    def display(self, act):
        if not act:
            return ""
        return act.get("full_name") or act.get("display_name") or act.get("name", "")

    def short(self, act):
        return act.get("name", "") if act else ""

    def locations(self, act_id):
        """Returns (definition, declaration) of an activity or participant."""
        if act_id in self._locations:
            return self._locations[act_id]
        act = self.activities.get(act_id) or self.participants.get(act_id)
        result = (None, None)
        if act:
            sl = act.get("source_location") or {}
            decl = loc(sl.get("file"), sl.get("line"))
            if self.is_test(act) or self.is_test(act.get("owner")):
                result = (self.test.location, None)
            elif act.get("type") == "class":
                result = (loc(sl.get("file"), sl.get("line"), "block"), None)
            else:
                owner = act.get("owner")
                owner_name = re.sub(r"<.*", "", owner.get("name", "")) if owner else None
                definition = self.definitions.find(act.get("name", ""), owner_name, act.get("full_name", ""), decl)
                result = (definition or decl, decl)
        self._locations[act_id] = result
        return result

    def match_call(self, from_pid, to_pid, path, line, label):
        """Returns the JSON call for a diagram message. Several calls between the same participants can share a
        line, for example set_coefficient(get_coefficient()), so the callee name in the label decides."""

        def named(m):
            act = self.activities.get(m["to"]["activity_id"])
            name = act.get("name", "") if act else ""
            return bool(name) and re.search(r"(^|[:\s*])" + re.escape(name) + r"\(", label or "")

        candidates = self.call_index.get((from_pid, to_pid, path, line), [])
        for m in candidates:
            if named(m):
                return m
        if label:
            same = [m for m in self.calls
                    if (m["from"]["participant_id"], m["to"]["participant_id"]) == (from_pid, to_pid)
                    and (m.get("source_location") or {}).get("file") == path and named(m)]
            if same:
                return min(same, key=lambda m: abs((m.get("source_location") or {}).get("line", 0) - (line or 0)))
        return candidates[0] if candidates else None

    def match_switch(self):
        if self.switch_pos < len(self.switches):
            self.switch_pos += 1
            return self.switches[self.switch_pos - 1]
        return None

    def match_return(self, from_pid, to_pid, label):
        for i in range(self.return_pos, len(self.returns)):
            r = self.returns[i]
            if (r["from"]["participant_id"], r["to"]["participant_id"], r.get("name", "")) == (from_pid, to_pid, label):
                self.return_pos = i + 1
                return r
        return None

    def match_block(self, kind, text):
        types = {"loop": ("loop",), "alt": ("alt", "opt"), "opt": ("alt", "opt")}[kind]
        for i in range(self.block_pos, len(self.blocks)):
            b = self.blocks[i]
            if b.get("type") in types and b.get("condition_text", "").strip() == text.strip():
                self.block_pos = i + 1
                return b
        return None


GROUP_OPEN_RE = re.compile(r"^(loop|alt|opt|group|critical|break|par)\b")
MESSAGE_RE = re.compile(r"^(\[->|C_\d+ --?> )")
GTEST_HANDLERS = {"else const ::std::exception &", "else ..."}


def is_gtest_scaffolding(head, branches):
    """Returns true for blocks that gtest assertion macros expand to. They have no counterpart in the source."""
    if "src://" in head:
        return False
    if "::testing::" in head or "gtest_" in head:
        return True
    if head == "group switch":
        return all(b == "else 0" for b in branches)
    if head == "group try":
        return bool(branches) and set(branches) <= GTEST_HANDLERS
    return False


def simplify_groups(lines):
    """Removes gtest macro blocks and unlinked blocks without messages. Their content stays in place."""
    root = {"items": []}
    stack = [root]
    for line in lines:
        if GROUP_OPEN_RE.match(line):
            node = {"head": line, "items": [], "end": "end"}
            stack[-1]["items"].append(node)
            stack.append(node)
        elif re.match(r"^else\b", line) and len(stack) > 1:
            stack[-1]["items"].append({"else": line})
        elif line.strip() == "end" and len(stack) > 1:
            stack.pop()["end"] = line
        else:
            stack[-1]["items"].append(line)
    if len(stack) != 1:
        return lines

    def has_message(node):
        for i in node["items"]:
            if isinstance(i, str):
                if MESSAGE_RE.match(i):
                    return True
            elif "head" in i and has_message(i):
                return True
        return False

    def emit(node, out, keep_else):
        for i in node["items"]:
            if isinstance(i, str):
                out.append(i)
            elif "else" in i:
                if keep_else:
                    out.append(i["else"])
            else:
                branches = [x["else"] for x in i["items"] if isinstance(x, dict) and "else" in x]
                keep = ("src://" in i["head"] or has_message(i)) and not is_gtest_scaffolding(i["head"], branches)
                if keep:
                    out.append(i["head"])
                    emit(i, out, True)
                    out.append(i["end"])
                else:
                    emit(i, out, False)

    out = []
    emit(root, out, True)
    return out


def process_test_puml(puml_path, json_path, sources, definitions, test):
    """Adds viewer links to a clang-uml sequence diagram. Returns the element table of the viewer."""
    model = Model(json_path, definitions, test)
    elements = []
    # Search start per activity, so that repeated conditions map to successive source lines.
    cursor = {}
    stack = []
    out = []
    entry = 0

    def add(element):
        elements.append(element)
        return len(elements) - 1

    for raw in puml_path.read_text(encoding="utf-8").split("\n"):
        line = raw
        if m := PARTICIPANT_RE.match(raw):
            definition, declaration = model.locations(pid(m["id"]))
            locs = dedupe([("Definition", definition), ("Deklaration", declaration)])
            if locs:
                idx = add({"kind": "participant", "title": m["title"], "locs": locs})
                target = locs[0]
                line = (f'participant "{m["title"]}" as {m["id"]} '
                        f'[[src://{target["path"]}?e={idx}#{target["line"]}{{{target["path"]}:{target["line"]}}}]]')
        elif m := CALL_RE.match(raw):
            a, b = pid(m["a"]), pid(m["b"])
            label = m["label"] or ""
            # clang-uml marks calls inside a condition as **[label]**.
            in_condition = re.fullmatch(r"\*\*\[(.*)\]\*\*", label)
            if in_condition:
                label = in_condition.group(1).strip("*")
            msg = model.match_call(a, b, m["path"], int(m["line"]) if m["line"] else None, label)
            callee_id = msg["to"]["activity_id"] if msg else b
            caller_id = msg["from"]["activity_id"] if msg else a
            callee = model.activities.get(callee_id)
            caller = model.activities.get(caller_id)
            cursor.pop(callee_id, None)
            definition, declaration = model.locations(callee_id)
            call_site = loc(m["path"], m["line"])
            if not call_site and msg:
                sl = msg.get("source_location") or {}
                call_site = loc(sl.get("file"), sl.get("line"))
            label = label or f"{model.short(callee)}()"
            if in_condition:
                label += " (in Bedingung)"
            locs = dedupe([("Aufrufstelle", call_site), ("Aufgerufene Funktion", definition),
                           ("Deklaration", declaration)])
            if locs:
                idx = add({"kind": "call", "title": label, "from": model.display(caller),
                           "to": model.display(callee), "locs": locs})
                text = link(idx, locs[0], f"Aufruf, {locs[0]['path']}:{locs[0]['line']}",
                            label if safe_label(label) else "call") + ("" if safe_label(label) else f" {label}")
                line = f"{m['a']} -> {m['b']} : {text}"
        elif m := RETURN_RE.match(raw):
            label = m["label"] or ""
            expr = label[2:-2] if label.startswith("//") and label.endswith("//") else label
            ret = model.match_return(pid(m["a"]), pid(m["b"]), expr)
            if ret:
                sl = ret.get("source_location") or {}
                location = loc(sl.get("file"), sl.get("line"))
                if location:
                    idx = add({"kind": "return", "title": expr or "(kein Wert)",
                               "from": model.display(model.activities.get(ret["from"]["activity_id"])),
                               "to": model.display(model.activities.get(ret["to"]["activity_id"])),
                               "locs": dedupe([("return-Anweisung", location)])})
                    tip = f"Rückgabe, {location['path']}:{location['line']}"
                    line = f"{m['a']} --> {m['b']} : {link(idx, location, tip, 'return')}" + (f" {label}" if label else "")
        elif m := ENTRY_RE.match(raw):
            # Each start function of the diagram begins with an entry message, in the order of the starts.
            act_id = model.starts[entry] if entry < len(model.starts) else None
            entry += 1
            act = model.activities.get(act_id)
            label = m["label"] or model.short(act) + "()"
            if act is None or model.is_test(act.get("owner")):
                element = {"kind": "test", "title": label, "locs": dedupe([("TEST-Makro", test.location)])}
            else:
                definition, declaration = model.locations(act_id)
                bridge = next((b for b in getattr(test, "bridges", [])
                               if gdb_name(model.display(act)) == b["name"]), None)
                call_site = None
                if bridge and ":" in bridge["site"]:
                    site_path, site_line = bridge["site"].rsplit(":", 1)
                    call_site = loc(site_path, site_line)
                origin = (f"{bridge['caller']} (laut gdb; dieser Aufrufer liegt außerhalb des Diagramms)"
                          if bridge else "gtest")
                element = {"kind": "call", "title": label, "from": origin, "to": model.display(act),
                           "locs": dedupe([("Aufrufstelle", call_site), ("Aufgerufene Funktion", definition),
                                           ("Deklaration", declaration)])}
            if element["locs"] and safe_label(label):
                idx = add(element)
                line = f"[-> {m['b']} : {link(idx, element['locs'][0], element['locs'][0]['label'], label)}"
        elif m := GROUP_RE.match(raw):
            kw, text = m["kw"], (m["text"] or "").strip()
            location, act_id = None, None
            needle, switch_line = text, None
            if kw in ("loop", "alt", "opt") and text:
                block = model.match_block(kw, text)
                act_id = block.get("activity_id") if block else None
            elif kw == "group" and text == "switch":
                block = model.match_switch()
                act_id = block.get("activity_id") if block else None
                needle = "switch("
            elif kw == "else" and text and stack and stack[-1]["kw"] in ("alt", "opt"):
                act_id = stack[-1]["act"]
            elif kw == "else" and text and stack and stack[-1]["kw"] == "switch" and stack[-1]["line"]:
                act_id = stack[-1]["act"]
                needle = "default:" if text == "default" else f"case {text}:"
            if act_id:
                definition, _ = model.locations(act_id)
                if definition:
                    start = cursor.get(act_id, definition["line"])
                    if needle.startswith(("case ", "default:")):
                        start = stack[-1]["line"]
                    found = sources.find_code(definition["path"], needle, start)
                    if found:
                        if not needle.startswith(("case ", "default:")):
                            cursor[act_id] = found + 1
                        location = loc(definition["path"], found, "block")
                        switch_line = found
            if kw == "group":
                stack.append({"kw": "switch" if text == "switch" else kw, "act": act_id, "line": switch_line})
            elif kw != "else":
                stack.append({"kw": kw, "act": act_id, "line": None})
            if location:
                kind = "loop" if kw == "loop" else "condition"
                idx = add({"kind": kind, "title": text, "from": model.display(model.activities.get(act_id)),
                           "locs": dedupe([("Stelle im Code", location)])})
                tip = f"{'Schleife' if kind == 'loop' else 'Bedingung'}, {location['path']}:{location['line']}"
                if safe_label(text):
                    line = f"{kw} {link(idx, location, tip, text)}"
                else:
                    line = f"{kw} {link(idx, location, tip, 'for' if kind == 'loop' else 'if')} {text}"
        elif raw.strip() == "end" and stack:
            stack.pop()
        out.append(line)

    puml_path.write_text("\n".join(simplify_groups(out)), encoding="utf-8")
    return elements


def write_placeholder(puml_path, test):
    """Writes a diagram for a test case that clang-uml could not process. Returns its element table."""
    location = test.location
    puml_path.write_text("\n".join([
        "@startuml",
        f"title {test.program.name}: {test.suite}.{test.name}",
        f"header [[../executables/{test.program.name}.svg Zurück zu {test.program.name}]]",
        f'participant "{test.test_class}" as t [[src://{location["path"]}?e=0#{location["line"]}{{TEST-Makro}}]]',
        "note over t : clang-uml konnte für diesen Test kein Sequenzdiagramm erzeugen.\\n"
        "Die Ursache steht in clang-uml.log.",
        "@enduml",
    ]) + "\n", encoding="utf-8")
    return [{"kind": "test", "title": f"{test.suite}.{test.name}", "locs": dedupe([("TEST-Makro", location)])}]


CLASS_DECL_RE = re.compile(r'^(?P<kw>class|enum|abstract|struct|interface) "(?P<name>.*)" as (?P<id>C_\d+)')
CLASS_BODY_RE = re.compile(r"^(?P<kw>class|enum|abstract|struct|interface) (?P<id>C_\d+)\b")
RELATION_RE = re.compile(r"^(?P<a>C_\d+) (?P<rel>\S+) (?P<b>C_\d+)(?:.*? : (?P<label>.*))?$")
SRC_RE = re.compile(r"src://(?P<path>[^#?\]{}]+)#(?P<line>\d+)")


def process_class_puml(puml_path):
    """Adds viewer links to a clang-uml class diagram. Returns the element table of the viewer."""
    lines = puml_path.read_text(encoding="utf-8").split("\n")
    names, kinds = {}, {}
    for raw in lines:
        if m := CLASS_DECL_RE.match(raw):
            names[m["id"]] = m["name"]
            kinds[m["id"]] = m["kw"]
    elements, out, current = [], [], None
    for raw in lines:
        line = raw
        src = SRC_RE.search(raw)
        body = CLASS_BODY_RE.match(raw)
        if body and raw.rstrip().endswith("{"):
            current = body["id"]
        if src:
            if body:
                element = {"kind": "class", "type": kinds.get(body["id"], "class"),
                           "title": names.get(body["id"], body["id"]),
                           "locs": dedupe([("Definition", loc(src["path"], src["line"], "block"))])}
            elif rel := RELATION_RE.match(raw):
                title = f"{names.get(rel['a'], rel['a'])} {rel['rel']} {names.get(rel['b'], rel['b'])}"
                element = {"kind": "relation", "title": title + (f" : {rel['label']}" if rel["label"] else ""),
                           "locs": dedupe([("Deklaration", loc(src["path"], src["line"]))])}
            else:
                text = re.sub(r"\s*\[\[\[.*?\]\]\]", "", raw).strip()
                element = {"kind": "member", "title": text, "from": names.get(current, ""),
                           "locs": dedupe([("Deklaration", loc(src["path"], src["line"],
                                                               "block" if "(" in text else "stmt"))])}
            elements.append(element)
            idx = len(elements) - 1
            line = raw[:src.start()] + f"src://{src['path']}?e={idx}#{src['line']}" + raw[src.end():]
        if raw.strip() == "}":
            current = None
        out.append(line)
    puml_path.write_text("\n".join(out), encoding="utf-8")
    return elements


# Doxygen.


def run_doxygen(out_dir, build_dir):
    """Generates Doxygen HTML for the involved code next to the viewer. Returns the XML directory, or None."""
    if not shutil.which("doxygen"):
        print("Doxygen not found, the viewer shows no documentation.")
        return None
    html_root = out_dir / "viewer" / "doxygen"
    xml_dir = out_dir / "doxygen_xml"
    for d in (html_root, xml_dir):
        shutil.rmtree(d, ignore_errors=True)
    html_root.mkdir(parents=True)
    options = {
        "PROJECT_NAME": f'"{TITLE}"',
        "OUTPUT_DIRECTORY": f'"{html_root}"',
        "HTML_OUTPUT": "html",
        "XML_OUTPUT": f'"{xml_dir}"',
        "INPUT": " ".join(f'"{REPO / p}"' for p in DOC_PATHS),
        "RECURSIVE": "YES",
        "FILE_PATTERNS": "*.h *.cpp",
        "EXTRACT_ALL": "YES",
        "EXTRACT_PRIVATE": "YES",
        "EXTRACT_STATIC": "YES",
        "EXTRACT_ANON_NSPACES": "YES",
        "FULL_PATH_NAMES": "YES",
        "STRIP_FROM_PATH": f'"{REPO}"',
        "HAVE_DOT": "YES" if shutil.which("dot") else "NO",
        "DOT_IMAGE_FORMAT": "svg",
        "INTERACTIVE_SVG": "YES",
        "CALL_GRAPH": "YES",
        "CALLER_GRAPH": "YES",
        "REFERENCED_BY_RELATION": "YES",
        "REFERENCES_RELATION": "YES",
        "GENERATE_XML": "YES",
        "GENERATE_LATEX": "NO",
        "GENERATE_TREEVIEW": "NO",
        "HTML_COLORSTYLE": "LIGHT",
        "USE_MATHJAX": "YES",
        "PREDEFINED": "HAVE_AVX2=1",
        "QUIET": "YES",
        "WARN_LOGFILE": f'"{out_dir / "doxygen.log"}"',
        "NUM_PROC_THREADS": "4",
        "DOT_NUM_THREADS": "4",
    }
    # Same theme and TeX macros as the project documentation.
    css = REPO / "docs" / "doxygen" / "doxygen-awesome.css"
    if css.is_file():
        options["HTML_EXTRA_STYLESHEET"] = f'"{css}"'
    macros = build_dir / "docs" / "doxygen" / "math_macros.tex"
    if macros.is_file():
        options["FORMULA_MACROFILE"] = f'"{macros}"'
    doxyfile = out_dir / "Doxyfile"
    doxyfile.write_text("".join(f"{k} = {v}\n" for k, v in options.items()), encoding="utf-8")
    result = subprocess.run(["doxygen", str(doxyfile)], cwd=REPO, capture_output=True, text=True)
    if result.returncode != 0 or not (xml_dir / "index.xml").is_file():
        print(f"Doxygen failed: {result.stderr.strip()[:500]}", file=sys.stderr)
        return None
    return xml_dir


def doc_text(node, skip=("parameterlist", "simplesect")):
    """Returns the plain text of a Doxygen XML description, without the skipped child elements."""
    if node is None:
        return ""
    parts = []

    def walk(n):
        if n.tag in skip:
            return
        if n.text:
            parts.append(n.text)
        for child in n:
            walk(child)
            if child.tail:
                parts.append(child.tail)

    walk(node)
    return re.sub(r"\s+", " ", "".join(parts)).strip()


def describe(node):
    """Returns brief, details, parameters, return value and notes of a Doxygen compound or member."""
    detailed = node.find("detaileddescription")
    details = [t for t in (doc_text(p) for p in (detailed.findall("para") if detailed is not None else [])) if t]
    params = []
    for plist in node.iter("parameterlist"):
        for item in plist.findall("parameteritem"):
            for pname in item.iter("parametername"):
                params.append({"name": (pname.text or "").strip(), "dir": pname.get("direction", ""),
                               "kind": plist.get("kind"),
                               "text": doc_text(item.find("parameterdescription"))})
    returns, notes = "", []
    for sect in node.iter("simplesect"):
        kind = sect.get("kind")
        if kind == "return":
            returns = doc_text(sect, skip=())
        elif kind in ("remark", "note", "warning", "attention", "pre", "post"):
            notes.append(doc_text(sect, skip=()))
    return {"brief": doc_text(node.find("briefdescription")), "details": details, "params": params,
            "returns": returns, "notes": notes}


class DoxygenIndex:
    """Doxygen documentation, addressed by source location."""

    def __init__(self, xml_dir):
        self.docs = {}
        self._by_location = {}
        if not xml_dir:
            return
        for path in sorted(Path(xml_dir).glob("*.xml")):
            if path.name in ("index.xml", "Doxyfile.xml"):
                continue
            try:
                root = ET.parse(path).getroot()
            except ET.ParseError:
                continue
            for c in root.iter("compounddef"):
                cid, ckind = c.get("id"), c.get("kind")
                if ckind in ("class", "struct", "union"):
                    self._add(c.find("location"), cid, 0, {"name": c.findtext("compoundname"), "kind": ckind,
                                                           "page": f"{cid}.html", "anchor": "", **describe(c)})
                for m in c.iter("memberdef"):
                    mid = m.get("id")
                    anchor = mid[len(cid) + 2:] if mid.startswith(cid + "_1") else ""
                    priority = 0 if ckind in ("class", "struct", "union", "file") else 1
                    signature = ((m.findtext("definition") or "") + (m.findtext("argsstring") or "")).strip()
                    self._add(m.find("location"), mid, priority,
                              {"name": signature or m.findtext("name"), "kind": m.get("kind"),
                               "page": f"{cid}.html", "anchor": anchor, **describe(m)})

    def _add(self, location, doc_id, priority, doc):
        if location is None:
            return
        self.docs.setdefault(doc_id, doc)
        for file_key, line_key in (("file", "line"), ("declfile", "declline"), ("bodyfile", "bodystart")):
            path, line = location.get(file_key), location.get(line_key)
            if path and line and line.lstrip("-").isdigit():
                key = (path, int(line))
                if key not in self._by_location or self._by_location[key][0] > priority:
                    self._by_location[key] = (priority, doc_id)

    def lookup(self, path, line):
        hit = self._by_location.get((path, line))
        return hit[1] if hit else None


# Locations of an element that point at the documented entity, by element kind.
DOC_LABELS = {
    "participant": ("Definition", "Deklaration"),
    "call": ("Aufgerufene Funktion", "Deklaration"),
    "test": ("TEST-Makro",),
    "class": ("Definition",),
    "member": ("Deklaration",),
    "relation": ("Deklaration",),
    "instantiation": ("Testfall-Generator",),
}


def attach_docs(diagrams, doxygen):
    """Adds Doxygen references to the elements. Returns the documentation entries in use."""
    used = set()

    def find(kind, locs):
        for location in locs:
            if location and location.get("label") in DOC_LABELS.get(kind, ()):
                doc_id = doxygen.lookup(location["path"], location["line"])
                if doc_id:
                    return doc_id
        return None

    for d in diagrams:
        for element in d["elements"]:
            if doc_id := find(element["kind"], element["locs"]):
                element["doc"] = doc_id
                used.add(doc_id)
        if d.get("test"):
            if doc_id := find("test", [{"label": "TEST-Makro", **d["test"]}]):
                d["test"] = {**d["test"], "doc": doc_id}
                used.add(doc_id)
    return {doc_id: doxygen.docs[doc_id] for doc_id in sorted(used)}


def base_name(name):
    return re.sub(r"<.*", "", name or "").split("::")[-1].strip()


def link_classes(diagrams, docs):
    """Cross-references sequence diagram elements with the class diagrams, and classes with the tests using them."""
    class_diagrams = [d for d in diagrams if d["kind"] == "class"]
    by_location, by_name = {}, {}
    for d in class_diagrams:
        for idx, element in enumerate(d["elements"]):
            target = {"d": d["name"], "e": idx}
            if element["kind"] in ("class", "member"):
                first = element["locs"][0]
                by_location.setdefault((first["path"], first["line"]), target)
            if element["kind"] == "class":
                by_name.setdefault(base_name(element["title"]), target)
    words_per_test = {}
    for d in diagrams:
        if d["kind"] != "test":
            continue
        words = set()
        for element in d["elements"]:
            # The Doxygen signature adds the return type, which the diagram labels omit.
            texts = [element.get(key, "") for key in ("title", "from", "to")]
            texts.append(docs.get(element.get("doc"), {}).get("name", ""))
            for text in texts:
                words.update(re.findall(r"[A-Za-z_]\w*", text))
            target = None
            if element["kind"] in ("participant", "call"):
                for location in element["locs"]:
                    if location["label"] in ("Definition", "Aufgerufene Funktion", "Deklaration"):
                        target = by_location.get((location["path"], location["line"]))
                        if target is not None:
                            break
                if target is None and element["kind"] == "participant" and "(" not in element["title"]:
                    target = by_name.get(base_name(element["title"]))
                if target is None and element["kind"] == "call" and "::" in element.get("to", ""):
                    owner = re.sub(r"\(.*", "", element["to"]).split("::")
                    target = by_name.get(base_name(owner[-2])) if len(owner) > 1 else None
            if target is not None:
                element["cls"] = target
        words_per_test[d["name"]] = (d["title"], words)
    for d in class_diagrams:
        for element in d["elements"]:
            if element["kind"] == "class":
                name = base_name(element["title"])
                element["usedIn"] = [{"name": key, "title": title}
                                     for key, (title, words) in words_per_test.items() if name in words]


# Overview diagrams.


class OverviewWriter:
    """Builds a hand-written PlantUML diagram whose elements link to the source code."""

    def __init__(self, sources):
        self.sources = sources
        self.elements = []

    def at(self, file, pattern, start_pattern=None, span="stmt"):
        start = self.sources.line_of(file, start_pattern) if start_pattern else 1
        if start is None:
            return None
        return loc(file, self.sources.line_of(file, pattern, start), span)

    def add(self, element):
        self.elements.append(element)
        return len(self.elements) - 1

    def url(self, element):
        idx = self.add(element)
        first = element["locs"][0]
        return f"src://{first['path']}?e={idx}#{first['line']}"

    def participant(self, title, alias, location, kind="participant"):
        if not location:
            return f'participant "{title}" as {alias}'
        url = self.url({"kind": kind, "title": title.replace("\\n", " "), "locs": dedupe([("Definition", location)])})
        return f'participant "{title}" as {alias} [[{url}{{{location["path"]}:{location["line"]}}}]]'

    def call(self, a, b, label, call_site=None, callee=None, callee_label="Aufgerufene Funktion"):
        locs = dedupe([("Aufrufstelle", call_site), (callee_label, callee)])
        if not locs:
            return f"{a} -> {b} : {label}"
        return f"{a} -> {b} : {link(self.add({'kind': 'call', 'title': label, 'locs': locs}), locs[0], 'Aufruf', label)}"


def class_header_links(prefix):
    return " · ".join(f"[[{prefix}tests/{name}.svg {title}]]" for name, title, *_ in CLASS_DIAGRAMS)


def write_start_page(path, programs, sources):
    """Writes the start diagram with all test programs and their files. Returns its element table."""
    w = OverviewWriter(sources)
    out = [
        "@startuml",
        f"title {TITLE}: Test-Programme aus {TEST_DIR}",
        f"header Klick auf ein Test-Programm öffnet seine Aufrufkette. Klassendiagramme: {class_header_links('')}",
        "footer Die Test-Programme stehen in " + CMAKE_FILE + " und laufen über ctest.",
        "left to right direction",
        "skinparam componentStyle rectangle",
        'node "ctest" as ctest',
    ]
    main_loc = loc(GTEST_MAIN, sources.line_of(GTEST_MAIN, r"^int main\("), "block")
    main_url = w.url({"kind": "file", "title": GTEST_MAIN, "locs": dedupe([("main()", main_loc)])})
    out.append(f'artifact "ocudu_gtest_main.cpp\\nmain() für alle Programme" as gmain [[{main_url}{{{GTEST_MAIN}}}]]')
    files = {}
    for i, program in enumerate(programs):
        nof_runs = sum(t.instances for t in program.tests)
        runs = f", {nof_runs} Läufe" if nof_runs != len(program.tests) else ""
        out.append(f'component "{program.name}\\n{len(program.tests)} Testfälle{runs}" as p{i} '
                   f'[[executables/{program.name}.svg{{Aufrufkette von {program.name}}}]]')
        out.append(f"ctest --> p{i}")
        out.append(f"p{i} ..> gmain : main()")
        for file in program.sources + program.headers:
            if file not in files:
                files[file] = f"f{len(files)}"
                first_test = next((t.line for t in program.tests if t.source == file), 1)
                url = w.url({"kind": "file", "title": file, "locs": dedupe([("Datei", loc(file, first_test))])})
                out.append(f'artifact "{os.path.basename(file)}" as {files[file]} [[{url}{{{file}}}]]')
            relation = "Quelle" if file in program.sources else "#include"
            out.append(f"p{i} --> {files[file]} : {relation}")
    out.append("@enduml")
    path.write_text("\n".join(out) + "\n", encoding="utf-8")
    return w.elements


def write_program_overview(path, program, sources):
    """Writes the call chain from main() through gtest to the test cases of one program. Returns its elements."""
    w = OverviewWriter(sources)
    main_file, gtest = GTEST_MAIN, GTEST_HEADER
    fixtures = sorted({t.suite for t in program.tests if t.macro != "TEST" and t.suite in program.classes})
    fixture_loc = (loc(program.classes[fixtures[0]]["path"], program.classes[fixtures[0]]["line"], "block")
                   if fixtures else None)
    out = [
        "@startuml",
        f"title {program.name}: Aufrufkette von main() bis zu den Testfällen",
        f"header [[../index.svg Zurück zur Übersicht]] · Klick auf einen Testfall öffnet sein Sequenzdiagramm. "
        f"Klassendiagramme: {class_header_links('../')}",
        f"footer Quellen: {', '.join(program.sources)}. Startkette aus {GTEST_MAIN} und einem gdb-Backtrace.",
        "skinparam maxMessageSize 260",
        w.participant("main()", "main", loc(main_file, sources.line_of(main_file, r"^int main\("), "block")),
        w.participant("testing::UnitTest", "ut", w.at(gtest, r"^class GTEST_API_ UnitTest\b", span="block")),
        w.participant("testing::internal::UnitTestImpl", "uti", None),
        w.participant("OCUDUTestEnvironment", "env", w.at(main_file, r"^class OCUDUTestEnvironment\b", span="block")),
        w.participant("RandomGeneratorResetListener", "lis",
                      w.at(main_file, r"^class RandomGeneratorResetListener\b", span="block")),
        w.participant("testing::TestSuite", "suite", w.at(gtest, r"^class GTEST_API_ TestSuite\b", span="block")),
        w.participant("testing::TestInfo", "info", w.at(gtest, r"^class GTEST_API_ TestInfo\b", span="block")),
        w.participant("testing::Test", "test", w.at(gtest, r"^class GTEST_API_ Test\b", span="block")),
    ]
    if fixtures:
        out.append(w.participant("Fixture\\n" + "\\n".join(fixtures), "fix", fixture_loc))
    out += [
        w.participant("TEST(...)::TestBody()", "body",
                      w.at(gtest, r"virtual void TestBody\(\) = 0;", r"^class GTEST_API_ Test\b")),
        "",
        "[-> main : Programmstart",
        "activate main",
        w.call("main", "main", "setup_default_log_level(argc, argv)",
               w.at(main_file, r"^\s+setup_default_log_level\(argc, argv\);"),
               w.at(main_file, r"^void setup_default_log_level\(", span="block")),
        "note right : Setzt den Log-Level aus --log_level und initialisiert ocudulog.",
        w.call("main", "ut", "testing::InitGoogleTest(&argc, argv)", w.at(main_file, r"InitGoogleTest\(&argc, argv\)"),
               w.at(gtest, r"void InitGoogleTest\(int\* argc, char\*\* argv\);"), "Deklaration"),
        "create env",
        w.call("main", "env", "new OCUDUTestEnvironment()", w.at(main_file, r"new OCUDUTestEnvironment"),
               w.at(main_file, r"^\s+OCUDUTestEnvironment\(\)$", span="block")),
        "note right : Legt den Basis-Seed fest, aus --gtest_random_seed oder zufällig.",
        w.call("main", "ut", "AddGlobalTestEnvironment(env)", w.at(main_file, r"AddGlobalTestEnvironment\("),
               w.at(gtest, r"^inline Environment\* AddGlobalTestEnvironment\(", span="block")),
        "create lis",
        w.call("main", "lis", "new RandomGeneratorResetListener()", w.at(main_file, r"new RandomGeneratorResetListener"),
               w.at(main_file, r"^class RandomGeneratorResetListener\b", span="block")),
        w.call("main", "ut", "listeners().Append(listener)", w.at(main_file, r"listeners\.Append\("),
               w.at(gtest, r"void Append\(TestEventListener\* listener\);"), "Deklaration"),
        w.call("main", "ut", "RUN_ALL_TESTS() ruft UnitTest::Run()", w.at(main_file, r"return RUN_ALL_TESTS\(\);"),
               w.at(gtest, r"^inline int RUN_ALL_TESTS\(\)")),
        "activate ut",
        "ut -> uti : RunAllTests()",
        "note right : Aufruf über HandleExceptionsInMethodIfSupported",
        "activate uti",
        w.call("uti", "lis", "OnTestIterationStart()", None,
               w.at(main_file, r"void OnTestIterationStart\(", span="block")),
        "note right : Leitet den Seed dieser Iteration ab.",
        w.call("uti", "env", "SetUp()", None, w.at(gtest, r"virtual void SetUp\(\) \{\}", r"^class Environment\b")),
    ]

    # gtest runs the suites in registration order; each suite runs its tests in order.
    suites = []
    for test in program.tests:
        if not suites or suites[-1][0] != test.suite:
            suites.append((test.suite, []))
        suites[-1][1].append(test)
    for suite, tests in suites:
        fixture = suite if tests[0].macro != "TEST" else None
        out += [f"uti -> suite : Run() für {suite}", "activate suite"]
        for inst in (i for i in program.instantiations if i["fixture"] == suite):
            generator_loc = None
            if inst["generator"]:
                for header in program.sources + program.headers:
                    if no := sources.line_of(header, rf"^\S.*\b{inst['generator']}\s*\("):
                        generator_loc = loc(header, no, "block")
                        break
            locs = dedupe([("INSTANTIATE_TEST_SUITE_P", loc(inst["path"], inst["line"])),
                           ("Testfall-Generator", generator_loc)])
            url = w.url({"kind": "instantiation", "title": f"INSTANTIATE_TEST_SUITE_P({inst['prefix']}, {suite}, ...)",
                         "locs": locs})
            runs = max(t.instances for t in tests)
            out.append(f"note over suite, info : [[{url}{{Parametrisierung}} INSTANTIATE_TEST_SUITE_P({inst['prefix']})]] "
                       f"erzeugt die Parameter mit {inst['generator'] or 'einem Generator'}()."
                       + (f"\\nJeder TEST_P läuft {runs}-mal." if runs > 1 else ""))
        if fixture and (found := fixture_method(program, fixture, "SetUpTestSuite")):
            out.append(w.call("suite", "fix", f"{found[0]}::SetUpTestSuite()", None, loc(found[1], found[2], "block")))
            out.append("note right : Einmal pro Test-Suite, vor dem ersten Test.")
        nof_runs = sum(t.instances for t in tests)
        out += [f"loop für jeden der {nof_runs} Testläufe, in dieser Reihenfolge",
                "suite -> info : Run()",
                "activate info",
                w.call("info", "lis", "OnTestStart()", None, w.at(main_file, r"void OnTestStart\(")),
                "note right : Setzt den Zufallsgenerator zurück.",
                "info -> test : Run()",
                "activate test"]
        if fixture and (found := fixture_method(program, fixture, "SetUp")):
            out.append(w.call("test", "fix", f"{found[0]}::SetUp()", None, loc(found[1], found[2], "block")))
        out.append("note right of test : Ruft TestBody() über HandleExceptionsInMethodIfSupported auf.\\n"
                   "Pro Durchlauf läuft genau einer der folgenden Testfälle.")
        current = None
        for test in tests:
            label = test.group()
            if label != current:
                if current is not None:
                    out.append("end")
                out.append(f"group {label}")
                current = label
            runs = f" ({test.instances}x)" if test.instances > 1 else ""
            out.append(f"test -> body : [[../tests/{test.key}.svg{{{test.macro}, {test.source}:{test.line}}} "
                       f"{test.name}]]{runs}")
        out.append("end")
        if fixture and (found := fixture_method(program, fixture, "TearDown")):
            out.append(w.call("test", "fix", f"{found[0]}::TearDown()", None, loc(found[1], found[2], "block")))
        out += ["deactivate test",
                w.call("info", "lis", "OnTestEnd()", None, w.at(main_file, r"void OnTestEnd\(")),
                "deactivate info",
                "end",
                "deactivate suite"]
    out += [
        w.call("uti", "env", "TearDown()", None, w.at(main_file, r"void TearDown\(\) override", span="block")),
        "note right : Leert die Log-Puffer.",
        "deactivate uti",
        "ut --> main : Exit-Code",
        "deactivate ut",
        "@enduml",
    ]
    path.write_text("\n".join(out) + "\n", encoding="utf-8")
    return w.elements


# Viewer.


def write_viewer(out_dir, diagrams, sources, docs, build_dir):
    """Writes the viewer page and its data scripts."""
    viewer = out_dir / "viewer"
    data = viewer / "data"
    data.mkdir(parents=True, exist_ok=True)
    for stale in data.glob("d_*.js"):
        stale.unlink()
    shutil.copyfile(SCRIPT_DIR / "viewer.html", viewer / "index.html")

    used = set()
    manifest = []
    for d in diagrams:
        svg = (out_dir / d["svg"]).read_text(encoding="utf-8")
        svg = re.sub(r"^<\?xml[^>]*\?>", "", svg)
        script = f"d_{d['name']}.js"
        payload = json.dumps({"svg": svg, "elements": d["elements"]}, ensure_ascii=False)
        (data / script).write_text(
            "window.UML_DIAGRAMS = window.UML_DIAGRAMS || {};\n"
            f"window.UML_DIAGRAMS[{json.dumps(d['name'])}] = {payload};\n", encoding="utf-8")
        for element in d["elements"]:
            used.update(location["path"] for location in element["locs"])
        if d.get("test"):
            used.add(d["test"]["path"])
        manifest.append({k: d[k] for k in ("name", "title", "group", "kind", "svg", "test", "program", "gdb")
                         if k in d} | {"data": script})

    embedded = {path: sources.text(path) for path in sorted(used) if sources.text(path) is not None}
    (data / "sources.js").write_text(
        f"window.UML_SOURCES = {json.dumps(embedded, ensure_ascii=False)};\n", encoding="utf-8")
    (data / "docs.js").write_text(f"window.UML_DOCS = {json.dumps(docs, ensure_ascii=False)};\n", encoding="utf-8")
    full_doxygen = build_dir / "docs" / "doxygen" / "html" / "index.html"
    info = {
        "title": TITLE,
        "index": "index",
        "classes": CLASS_DIAGRAMS[0][0],
        "doxygen": "doxygen/html/index.html" if (viewer / "doxygen" / "html" / "index.html").is_file() else None,
        "fullDoxygen": os.path.relpath(full_doxygen, viewer) if full_doxygen.is_file() else None,
        "diagrams": manifest,
    }
    (data / "manifest.js").write_text(
        "window.UML_MANIFEST = " + json.dumps(info, ensure_ascii=False) + ";\n", encoding="utf-8")
    return viewer / "index.html", len(embedded)


def find_plantuml(explicit):
    """Returns the command prefix that runs PlantUML."""
    java = ["java", "-Djava.awt.headless=true", "-DPLANTUML_LIMIT_SIZE=16384", "-Xmx2g", "-jar"]
    if explicit:
        return java + [explicit]
    if os.environ.get("PLANTUML_JAR"):
        return java + [os.environ["PLANTUML_JAR"]]
    if shutil.which("plantuml"):
        return ["plantuml"]
    # The CLion PlantUML Integration plugin ships a PlantUML JAR.
    candidates = sorted(glob.glob(os.path.expanduser("~/.local/share/JetBrains/*/plantuml4idea/lib/plantuml-*.jar")))
    if candidates:
        return java + [candidates[-1]]
    sys.exit("PlantUML not found. Install it with apt, or pass --plantuml-jar or set PLANTUML_JAR.")


def main():
    global MEMORY_LIMIT_GB
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", default=str(REPO / "cmake-build-debug"),
                        help="CMake build directory with compile_commands.json")
    parser.add_argument("--plantuml-jar", help="path to a PlantUML JAR")
    parser.add_argument("--threads", type=int, default=min(4, os.cpu_count() or 4),
                        help="clang-uml threads; each thread can take several hundred MB")
    parser.add_argument("--all-calls", action="store_true",
                        help="keep trivial getters and expand every repeated call")
    parser.add_argument("--reuse", action="store_true",
                        help="skip clang-uml and Doxygen and reuse their previous output")
    parser.add_argument("--no-gdb", action="store_true",
                        help="skip the gdb validation of the call chains")
    parser.add_argument("--memory-limit", type=int, default=MEMORY_LIMIT_GB,
                        help="address space limit per clang-uml process in GB")
    args = parser.parse_args()
    MEMORY_LIMIT_GB = args.memory_limit

    build_dir = Path(args.build_dir).resolve()
    if not (build_dir / "compile_commands.json").is_file():
        sys.exit(f"{build_dir}/compile_commands.json not found. Configure the build first.")
    if not shutil.which("clang-uml"):
        sys.exit("clang-uml not found. See https://github.com/bkryza/clang-uml for installation.")
    plantuml = find_plantuml(args.plantuml_jar)

    out_dir = build_dir / "clang_uml" / OUTPUT_NAME
    raw_dir, bridged_dir = out_dir / "raw", out_dir / "bridged"
    tests_dir, programs_dir = out_dir / "tests", out_dir / "executables"
    for d in (tests_dir, programs_dir, bridged_dir) + (() if args.reuse else (raw_dir,)):
        shutil.rmtree(d, ignore_errors=True)
        d.mkdir(parents=True)

    sources = Sources()
    compile_db = read_compile_db(build_dir)
    programs = find_programs(sources, build_dir)
    print(f"{len(programs)} test programs with {sum(len(p.tests) for p in programs)} test cases found.")

    log = out_dir / "clang-uml.log"
    log.write_text("", encoding="utf-8")
    if not args.reuse:
        for program in programs:
            tus = library_tus(program, sources, compile_db)
            print(f"clang-uml: {program.name}, {len(program.tests)} test cases, {len(tus)} library files.")
            config = out_dir / f"clang-uml_{program.name}.yml"
            write_sequence_config(config, build_dir, out_dir, program, tus, args.all_calls)
            run_clang_uml(config, args.threads, log)
        print("clang-uml: class diagrams.")
        class_config = out_dir / "clang-uml_classes.yml"
        write_class_config(class_config, build_dir, out_dir, compile_db)
        run_clang_uml(class_config, min(args.threads, len(CLASS_DIAGRAMS)), log)
    report = {}
    if not args.no_gdb and shutil.which("gdb"):
        print("gdb: tracing every test case once.")
        report = validate_with_gdb(programs, build_dir, out_dir, raw_dir, sources, args.reuse)
        (out_dir / "gdb_validation.json").write_text(json.dumps(report, indent=1, ensure_ascii=False),
                                                      encoding="utf-8")
        config_dir = out_dir / "configs"
        config_dir.mkdir(exist_ok=True)
        for program in programs:
            bridged = [t for t in program.tests if getattr(t, "bridges", [])]
            if not bridged:
                continue
            print(f"clang-uml: {program.name}, {len(bridged)} test cases with extra entry points from gdb.")
            tus = library_tus(program, sources, compile_db)
            # One process per diagram: the extra entry points multiply the memory, and a failure must not take
            # the other diagrams with it.
            with ThreadPoolExecutor(max_workers=max(1, min(args.threads, 2))) as pool:
                list(pool.map(lambda t: run_bridged(program, t, config_dir, build_dir, out_dir, tus,
                                                    args.all_calls, log), bridged))
    elif not args.no_gdb:
        print("gdb not found, the call chains are not validated.")
    for source_dir in (raw_dir, bridged_dir):
        for puml in source_dir.glob("*.puml"):
            if not puml.with_suffix(".json").is_file():
                continue
            shutil.copyfile(puml, tests_dir / puml.name)
            shutil.copyfile(puml.with_suffix(".json"), tests_dir / f"{puml.stem}.json")

    if args.reuse and (out_dir / "doxygen_xml" / "index.xml").is_file():
        doxygen = DoxygenIndex(out_dir / "doxygen_xml")
    else:
        print("Running Doxygen.")
        doxygen = DoxygenIndex(run_doxygen(out_dir, build_dir))
    definitions = DefinitionIndex(sources, SEQUENCE_PATHS)

    print("Linking diagram elements to the source code.")
    diagrams = [{"name": "index", "title": "Übersicht", "group": "", "kind": "index", "svg": "index.svg",
                 "elements": write_start_page(out_dir / "index.puml", programs, sources)}]
    for name, title, *_ in CLASS_DIAGRAMS:
        puml = tests_dir / f"{name}.puml"
        if puml.is_file():
            diagrams.append({"name": name, "title": title, "group": "Klassendiagramme", "kind": "class",
                             "svg": f"tests/{name}.svg", "elements": process_class_puml(puml)})
    failed = []
    for program in programs:
        diagrams.append({"name": program.key, "title": f"{program.name}: Aufrufkette", "group": program.name,
                         "kind": "program", "svg": f"executables/{program.name}.svg",
                         "elements": write_program_overview(programs_dir / f"{program.name}.puml", program, sources)})
        for test in program.tests:
            puml, model = tests_dir / f"{test.key}.puml", tests_dir / f"{test.key}.json"
            if puml.is_file() and model.is_file():
                elements = process_test_puml(puml, model, sources, definitions, test)
            else:
                failed.append(test.key)
                elements = write_placeholder(puml, test)
            title = test.name + (f" ({test.instances}x)" if test.instances > 1 else "")
            diagram = {"name": test.key, "title": title, "group": program.name, "kind": "test",
                       "program": program.key, "svg": f"tests/{test.key}.svg", "test": test.location,
                       "elements": elements}
            if (gdb := report.get(test.key)) and gdb.get("status") == "ok":
                applied = (bridged_dir / f"{test.key}.json").is_file()
                diagram["gdb"] = {"calls": gdb["calls"], "bridges": [b["name"] for b in test.bridges],
                                  "dropped": getattr(test, "dropped", []), "missing": gdb["missing"],
                                  "implicit": gdb["implicit"], "bridgesApplied": applied or not test.bridges}
            diagrams.append(diagram)
    if failed:
        print(f"clang-uml failed for: {', '.join(failed)}. See {log}.", file=sys.stderr)

    print("Rendering SVG files with PlantUML.")
    pumls = ([str(out_dir / "index.puml")] + sorted(str(p) for p in programs_dir.glob("*.puml"))
             + sorted(str(p) for p in tests_dir.glob("*.puml")))
    subprocess.run(plantuml + ["-charset", "UTF-8", "-tsvg"] + pumls, check=True)

    docs = attach_docs(diagrams, doxygen)
    link_classes(diagrams, docs)
    page, nof_sources = write_viewer(out_dir, diagrams, sources, docs, build_dir)
    nof_elements = sum(len(d["elements"]) for d in diagrams)
    print(f"Viewer: {len(diagrams)} diagrams, {nof_elements} clickable elements, {nof_sources} source files, "
          f"{len(docs)} documented entities.")
    if report:
        traced = [r for r in report.values() if r.get("status") == "ok"]
        gaps = sum(len(r["missing"]) for r in traced)
        implicit = sum(len(r["implicit"]) for r in traced)
        bridges = sum(len(r["bridges"]) for r in traced)
        print(f"gdb: {len(traced)} of {len(report)} test cases traced, {bridges} entry points added, "
              f"{gaps} dynamic calls without static counterpart, {implicit} compiler-generated members. "
              f"Details in {out_dir / 'gdb_validation.json'}.")
    print(f"Done. Open {page} in a web browser.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
