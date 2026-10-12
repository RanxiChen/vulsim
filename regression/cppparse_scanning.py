#!/usr/bin/env python3
"""Check scanner boundaries and source locations across the split translation units."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "cppparse.hpp"
#include "errormsg.hpp"
#include <stdexcept>

void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    using namespace cppparse;
    const std::vector<std::string> source = {
        "#include <ignored.hpp>",
        "CONFIG(size, choose(2, 3));",
        "ALIAS(pair, std::array<Int<8>, 2>);",
        "HELPER() {",
        R"(  const char *s = "}, ) // /*";)",
        "  auto number = 1'024 + 0xAB'CD;",
        "}",
        "SERVICE(run, ARG(std::array<Int<8>, 2>) value) {",
        "  if (value[0]) { call(value[1]); }",
        "}"
    };
    const auto entries = findAllMacroEntries(source);
    check(entries.size() == 4, "preprocessor lines must not become macro entries");
    check(entries[0].args == std::vector<std::string>{"size", "choose(2, 3)"},
          "nested call commas must stay within one argument");
    check(entries[1].args == std::vector<std::string>{"pair", "std::array<Int<8>, 2>"},
          "template commas must stay within one argument");
    check(entries[2].body.size() == 2 && entries[2].body[0] == source[4],
          "literal punctuation must not terminate a body");
    check(entries[2].body[1] == source[5], "digit separators must not start a character literal");
    check(entries[3].body.size() == 1 && entries[3].body[0] == source[8],
          "nested braces must stay within a body");
    check(entries[0].pos.line == 1 && entries[0].pos.column == 0,
          "skipping preprocessor lines must retain source positions");
    check(entries[2].body_pos[0].line == 4 && entries[2].body_pos[0].column == 0,
          "body positions must point into the original source");

    std::vector<LinePosition> positions;
    for (int i = 0; i < int(source.size()); ++i) positions.push_back({100 + i * 2, 7});
    const auto remapped = findAllMacroEntriesPreservingLinePositions(source, positions);
    check(remapped[0].pos.line == 102 && remapped[0].pos.column == 7,
          "caller-supplied macro positions must survive flattening");
    check(remapped[2].body_pos[0].line == 108 && remapped[2].body_pos[0].column == 7,
          "caller-supplied body positions must survive flattening");

    const auto stripped = stripComments({
        "// omitted", "CONFIG(size, /* block", " ends */ 1 + \\", "2); // tail",
        R"(HELPER() { const char *s = "// keep /* keep */"; })"
    });
    check(stripped.mapping == std::vector<uint32_t>{1, 2, 4},
          "comment removal and continuation must retain original line mapping");
    check(stripped.lines[1] == " 1 +2);", "continuation must merge physical lines");
    check(stripped.lines[2].find("// keep /* keep */") != std::string::npos,
          "comment markers inside literals must survive");

    check(codeblockContainsFunctionCall({"if (ready) { call(1); }"}, "call"),
          "call detection must find a nested call");
    check(!codeblockContainsFunctionCall({"callback(1);"}, "call"),
          "call detection must respect identifier boundaries");
    const auto macros = matchMacros({"ITEM(1, 2);"}, "ITEM($,$)");
    check(macros.size() == 1 && macros[0].args.size() == 2,
          "legacy macro matching must still resolve positional arguments");
    bool rejected = false;
    try { findAllMacroEntries({"SERVICE(broken) { call();"}); }
    catch (const VulException &) { rejected = true; }
    check(rejected, "unterminated macro bodies must still be rejected");
}
'''

with tempfile.TemporaryDirectory(prefix="cppparse-scanning-") as directory:
    source = Path(directory) / "scanner.cpp"
    executable = Path(directory) / "scanner"
    source.write_text(SOURCE)
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    subprocess.run(compiler + ["-std=c++20", "-I", str(ROOT / "src"), str(source)]
                   + [str(ROOT / "src/cppparse" / name) for name in
                      ("source.cpp", "scanning.cpp", "macro_entries.cpp")]
                   + [str(ROOT / "src/errormsg.cpp"), "-o", str(executable)],
                   cwd=ROOT, check=True)
    subprocess.run([str(executable)], cwd=ROOT, check=True)
print("cppparse scanning check PASS: nested syntax, literals, comments, positions and errors")
