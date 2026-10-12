#!/usr/bin/env python3
"""Compile trace-enabled simulations and check VCD widths, values and instance paths."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SIMGEN = ROOT / "build/vulsimgen"
WIDTHS = (64, 65, 80, 128, 129)
RESET_LOW = 0x0123456789ABCDEF
UPDATE_LOW = 0x2468ACE013579BDF


def run(command, cwd=ROOT):
    result = subprocess.run(command, cwd=cwd, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=180)
    if result.returncode:
        raise RuntimeError(f"command failed ({result.returncode}): {command}\n{result.stdout}")
    return result.stdout


def read_vcd(path):
    """Return declarations and known value changes; ignore the initial unknown values."""
    scopes, declarations, identifiers, changes = [], {}, {}, {}
    for line in path.read_text().splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "$scope":
            scopes.append(fields[2])
        elif fields[0] == "$upscope":
            scopes.pop()
        elif fields[0] == "$var":
            width, identifier, leaf = int(fields[2]), fields[3], fields[4]
            name = ".".join(scopes + [leaf])
            if name in declarations:
                raise RuntimeError(f"duplicate VCD declaration: {name}")
            declarations[name] = width
            identifiers[identifier] = name
            changes[name] = []
        elif fields[0].startswith("b") and len(fields) == 2:
            bits, identifier = fields[0][1:], fields[1]
            if set(bits) <= {"0", "1"}:
                name = identifiers[identifier]
                if len(bits) != declarations[name]:
                    raise RuntimeError(f"incorrect VCD bit count for {name}: {bits}")
                changes[name].append(int(bits, 2))
    return declarations, changes


def check_vcd(path, expected):
    declarations, changes = read_vcd(path)
    # The writer places all API signal paths under its synthetic logic scope.
    expected = {f"logic.{name}": value for name, value in expected.items()}
    if declarations != {name: width for name, (width, _) in expected.items()}:
        raise RuntimeError(f"incorrect VCD paths/widths: {declarations}")
    for name, (_, values) in expected.items():
        if changes[name] != values:
            raise RuntimeError(f"incorrect VCD values for {name}: {changes[name]} != {values}")


def assignments(target, width, low, middle=False):
    code = f"{target}.template pick<64>(0) = {low}ULL;\n"
    code += f"{target}.template pick<1>({width - 1}) = 1;\n"
    if middle and width > 64:
        code += f"{target}.template pick<1>(64) = 1;\n"
    return code


def expected_values(width, coordinate=0):
    reset = (RESET_LOW + coordinate) | (1 << (width - 1))
    update = (UPDATE_LOW + coordinate) | (1 << (width - 1))
    if width > 64:
        update |= 1 << 64
    return [reset, update, 0, reset]


def generated_case(directory, name, matcher):
    case = directory / name
    case.mkdir()
    (case / "header.hpp").write_text("#pragma once\n#include <defhelper.hpp>\n")
    top = '#include "header.hpp"\n'
    for width in WIDTHS:
        top += f"REGISTER(value{width}, Int<{width}>) {{\n"
        top += assignments(f"value{width}", width, RESET_LOW) + "}\n"
    top += "CHILD_INSTANCE(Node, lanes, dims=[2], COORD(0)=ROLE);\n"
    top += "USE_CHILD_SERVICE(lanes[*], write, write_lane);\n"
    top += "SERVICE(write, ARG(uint32_t) mode) {\n"
    for width in WIDTHS:
        top += f"Int<{width}> next{width}{{}};\nif (mode == 1) {{\n"
        top += assignments(f"next{width}", width, UPDATE_LOW, middle=True) + "}\n"
        top += f"value{width}.setnext(next{width});\n"
    top += "write_lane<0>(mode); write_lane<1>(mode);\n}\n"
    (case / "Top.hpp").write_text(top)
    node = '#include "header.hpp"\nINTERFACE() { PARAMETER(ROLE, 0); SERVICE(write, ARG(uint32_t) mode); }\n'
    node += "REGISTER(value, Int<80>) {\n"
    node += assignments("value", 80, RESET_LOW).replace(f"{RESET_LOW}ULL", f"({RESET_LOW}ULL + ROLE)") + "}\n"
    node += "SERVICE(write, ARG(uint32_t) mode) { Int<80> next{}; if (mode == 1) {\n"
    node += assignments("next", 80, UPDATE_LOW, middle=True).replace(f"{UPDATE_LOW}ULL", f"({UPDATE_LOW}ULL + ROLE)")
    node += "} value.setnext(next); }\n"
    (case / "Node.hpp").write_text(node)
    (case / "Main.cpp").write_text(
        '#include <defhelper.hpp>\n#include <run.hpp>\n'
        'TOP("./Top.hpp");\nPROJECT(".");\n'
        'REQUEST(write, ARG(uint32_t) mode);\n'
        'SIMULATION() { sim_nextcycle(); write(1); sim_nextcycle(); '
        'write(2); sim_nextcycle(); sim_reset(); sim_nextcycle(); }\n'
    )
    out = case / "out"
    command = [str(SIMGEN), "-m", str(case / "Main.cpp"), "-f", "-o", str(out)]
    if matcher:
        command += ["--trace", matcher]
    run(command)
    # Compile separately so a run.sh compilation failure cannot be hidden by its last command.
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    run(compiler + ["-std=c++20", "-O0", "main.cpp", "-I.", "-o", "simulation"], cwd=out)
    run([str(out / "simulation")], cwd=out)
    if matcher:
        expected = {f"top.value{width}": (width, expected_values(width)) for width in WIDTHS}
        coordinates = (1,) if name == "filtered" else (0, 1)
        expected.update({f"top.lanes[{i}].value": (80, expected_values(80, i)) for i in coordinates})
        check_vcd(out / "trace.vcd", expected)
    elif (out / "trace.vcd").exists():
        raise RuntimeError("tracing disabled but trace.vcd was produced")
    print(f"generator trace check PASS: {name}", flush=True)


RUNTIME_TEST = r'''
#include "VulTestMain.hpp"
#include <array>
#include <stdexcept>

void check(bool condition) {
    if (!condition) throw std::runtime_error("trace runtime validation failed");
}

template <typename Fn> void rejected(Fn fn, const char *message) {
    try { fn(); }
    catch (const std::runtime_error &error) {
        check(std::string(error.what()).find(message) != std::string::npos);
        return;
    }
    throw std::runtime_error("invalid trace value was accepted");
}

void VulTestMain::simulation() {
    const auto vector_id = trace_registe_signal("runtime.vector", 65);
    const auto array_id = trace_registe_signal("runtime.array", 129);
    const auto narrow_id = trace_registe_signal("runtime.narrow", 8);
    trace_init("runtime.vcd", 1, 1);
    trace_record(vector_id, std::vector<uint64_t>{5, 1});
    trace_record(array_id, std::array<uint64_t, 3>{7, 9, 1});
    trace_record(narrow_id, std::vector<uint64_t>{17});
    trace_commit();
    // A temporary array must be consumed synchronously, with no retained span.
    trace_record(array_id, std::array<uint64_t, 3>{11, 13, 1});
    rejected([&] { trace_record(vector_id, std::array<uint64_t, 1>{5}); }, "Insufficient");
    rejected([&] { trace_record(vector_id, std::vector<uint64_t>{5, 2}); }, "exceeds declared width");
    rejected([&] { trace_record(vector_id, std::array<uint64_t, 3>{5, 1, 1}); }, "High words");
    rejected([&] { trace_record(narrow_id, uint64_t(256)); }, "exceeds declared width");
    trace_commit();
}
'''


def runtime_case(directory):
    case = directory / "runtime"
    case.mkdir()
    # Compile the actual runtime entrypoint using a minimal harness instead of generated modules.
    (case / "VulTestMain.hpp").write_text(
        '#pragma once\n#include "vullib.h"\n'
        'struct VulTestMain { void simulation(); };\n'
    )
    source = case / "test.cpp"
    source.write_text(RUNTIME_TEST)
    executable = case / "simulation"
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    run(compiler + ["-std=c++20", "-I", str(case), "-I", str(ROOT / "vullib"),
                    str(ROOT / "vullib/main.cpp"), str(source), "-o", str(executable)])
    run([str(executable)], cwd=case)
    check_vcd(case / "runtime.vcd", {
        "runtime.vector": (65, [(1 << 64) | 5]),
        "runtime.array": (129, [(1 << 128) | (9 << 64) | 7, (1 << 128) | (13 << 64) | 11]),
        "runtime.narrow": (8, [17]),
    })
    print("generator trace check PASS: runtime compatibility and invalid values", flush=True)


def main():
    if not SIMGEN.is_file():
        raise RuntimeError(f"build vulsimgen first: {SIMGEN}")
    with tempfile.TemporaryDirectory(prefix="generator-trace-") as directory:
        directory = Path(directory)
        generated_case(directory, "filtered", "top.*,top::lanes[1].value")
        generated_case(directory, "wildcard", "top.*,top::lanes[*].value")
        generated_case(directory, "disabled", None)
        runtime_case(directory)


if __name__ == "__main__":
    main()
