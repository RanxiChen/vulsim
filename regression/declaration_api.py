#!/usr/bin/env python3
"""Canonical declaration grammar and editor-only C++ declarations."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''
#include "vulcpp/declaration_args.hpp"
#include "vulcpp/common.hpp"
#include <stdexcept>

void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
cppparse::MacroEntry parse(const std::string &source) {
    return normalizeDeclaration(cppparse::findAllMacroEntries({source}).at(0));
}
void equivalent(const std::string &left, const std::string &right) {
    check(parse(left).args == parse(right).args, "positional/named declarations differ");
}
void rejected(const std::string &source, const std::string &message) {
    try { parse(source); }
    catch (const VulException &e) {
        check(std::string(e.what()).find(message) != std::string::npos, e.what());
        check(std::string(e.what()).find("line 1") != std::string::npos, "missing source position");
        return;
    }
    throw std::runtime_error("expected rejection: " + source);
}
int main() {
    equivalent("CONFIG(width, (4 < 5 ? 8 : 16));", "CONFIG(value=(4 < 5 ? 8 : 16), name=width);");
    equivalent("CONFIG(width, 2<3);", "CONFIG(name=width, value=2<3);");
    equivalent("ALIAS(M, uint32_t, [2,clog2(9)]);", "ALIAS(type=uint32_t, dims=[2,clog2(9)], name=M);");
    equivalent("REGISTER(r, uint32_t, 2, [4]);", "REGISTER(name=r, dims=[4], type=uint32_t, ports=2);");
    equivalent("REQUEST(send, true, [2,3], ARG(uint32_t) x, RESP(bool) y);",
               "REQUEST(name=send, dims=[2,3], handshake=true, ARG(uint32_t) x, RESP(bool) y);");
    equivalent("SERVICE(s, true, [], -1, (1 < 2 && 3 >= 2), ARG(uint32_t) x) {}",
               "SERVICE(name=s, priority=-1, ready=(1 < 2 && 3 >= 2), dims=[], handshake=true, ARG(uint32_t) x) {}");
    equivalent("ROM(r, 32, 8, 2, \"a=b.hex\");", "ROM(init=\"a=b.hex\", size=8, name=r, read_ports=2, data_width=32);");
    equivalent("QUEUE(q, uint8_t, 8, 2, 3);", "QUEUE(type=uint8_t, name=q, deq_width=3, depth=8, enq_width=2);");
    equivalent("BRAM(r, uint32_t, 8, 1, 1, 1rw);", "BRAM(name=r, type=uint32_t, size=8, mode=1rw, read_ports=1, write_ports=1);");
    equivalent("CHILD_INSTANCE(Node, n, [2], PARAM(W)=8, PARAM(ROW)=COORD(0));",
               "CHILD_INSTANCE(name=n, module=Node, dims=[2], PARAM(W)=8, PARAM(ROW)=COORD(0));");
    equivalent("CHILD_INSTANCE(Node, n, PARAM( ROW )=COORD(0));",
               "CHILD_INSTANCE(Node, n, PARAM(ROW)=COORD(0));");
    check(parseCoordinateBindingValue(" COORD ( 1 + clog2(2) ) ") == std::optional<std::string>{"1 + clog2(2)"}, "coordinate dimension expression");
    check(!parseCoordinateBindingValue("WIDTH+1") && !parseCoordinateBindingValue("COORD_WIDTH"), "constant parameter must remain an expression");
    equivalent("CONNECT_CR_CS(a, out, b, in);", "CONNECT_CR_CS(dstserv=in, dstchild=b, srcchild=a, srcreq=out);");
    equivalent("CONNECT_CR_S(a, out, in);", "CONNECT_CR_S(dstserv=in, srcchild=a, srcreq=out);");
    equivalent("CONNECT_CR_R(a, out, exposed);", "CONNECT_CR_R(dstreq=exposed, srcchild=a, srcreq=out);");
    equivalent("CONNECT_S_CS(in, b, in);", "CONNECT_S_CS(dstserv=in, srcserv=in, dstchild=b);");
    equivalent("USE_CHILD_SERVICE(n[*], in, alias, [2], ARG(uint32_t) x);",
               "USE_CHILD_SERVICE(service=in, instance=n[*], dims=[2], alias=alias, ARG(uint32_t) x);");
    equivalent("USE_CHILD_QUERY(n[*], status, alias, uint32_t, [2]);",
               "USE_CHILD_QUERY(query=status, instance=n[*], type=uint32_t, dims=[2], alias=alias);");
    equivalent("VERSION(v) {}", "VERSION(name=v) {}");
    equivalent("USE_VERSION(v);", "USE_VERSION(name=v);");
    equivalent("TOP(\"a=b.hpp\");", "TOP(path=\"a=b.hpp\");");
    equivalent("PROJECT(\".\");", "PROJECT(path=\".\");");
    check(parse("REQUEST(r, ARG(uint32_t) x);").args.size() == 2, "default handshake must stay implicit");
    check(parse("SERVICE(s, ready=true) {}").args == std::vector<std::string>{"s", "ready=true"}, "ready must retain implicit handshake");
    equivalent("ALIAS(A, uint32_t, [1'024, 2]);", "ALIAS(name=A, type=uint32_t, dims=[1'024, 2]);");
    check(parse("ALIAS(pair, std::array<Int<8>, 2>);").args.size() == 2, "template type split");
    check(parse("ALIAS(pair, std::array<Int<(4>2 ? clog2(8) : 1)>, 2>);").args.size() == 2, "nested template comparison split");
    check(parse("SERVICE(s, ready=1<2, priority=3) {}").args.size() == 3, "unparenthesized comparison split");
    check(!parseKeyValueArg("x == y") && !parseKeyValueArg("x <= y") &&
          !parseKeyValueArg("f(x = 1)") && !parseKeyValueArg("\"a=b\""), "assignment tokenizer");
    check(parseKeyValueArg("value=1'024 + 2")->second == "1'024 + 2", "digit separator");
    for (const auto &source : {"REGISTER_MUL(r, uint32_t, 2);", "REGISTER_ARRAY1(r, uint32_t, 4, 1);",
         "ALIAS_ARRAY1(A, uint32_t, 4);", "ALIAS_ARRAY2(A, uint32_t, 4, 2);",
         "REQUEST_READY(r);", "SERVICE_READY(s, true) {}", "SERVICE_PRIO(s, 2) {}",
         "SERVICE_PRIO_READY(s, 2, true) {}", "CHILD_INSTANCE_ARRAY1(N, n, 2);",
         "CHILD_INSTANCE_ARRAY2(N, n, 2, 3);", "BRAM_1RW(r, uint32_t, 8);",
         "QUEUE_MP(q, uint32_t, 8, 2, 2);", "USE_CHILD_SERVICE_PORT(n, s, a);"}) rejected(source, "has been removed; use");
    rejected("REGISTER(r, type=uint32_t, 2);", "positional attributes cannot follow");
    rejected("REGISTER(r, uint32_t, ports=1, ports=2);", "specified more than once");
    rejected("REGISTER(r, uint32_t, name=r);", "specified more than once");
    rejected("REGISTER(r, uint32_t, portnum=2);", "use ports");
    rejected("REQUEST(r, array=2);", "use dims");
    rejected("REQUEST(r, ARRAY(2));", "ARRAY(...) has been removed");
    rejected("REGISTER(r, uint32_t, dim=2);", "use dims");
    rejected("ALIAS(A, uint32_t, 4);", "bracketed dimension list");
    rejected("ALIAS(A, uint32_t, dims=[2,,3]);", "empty dimension expression");
    rejected("REGISTER(r, uint32_t, dims=[2,]);", "empty dimension expression");
    rejected("REGISTER(r, uint32_t, dims=);", "cannot be empty");
    rejected("REGISTER(r, uint32_t, 1, [2], 3);", "too many positional");
    rejected("REGISTER(name=r);", "requires parameter 'type'");
    rejected("ROM(r, 32, 8);", "requires parameter 'init'");
    rejected("CHILD_INSTANCE(N, n, WIDTH=32);", "PARAM(name)=value");
    rejected("CHILD_INSTANCE(N, n, PARAM(W)=2, PARAM(W)=3);", "specified more than once");
    rejected("CHILD_INSTANCE(N, n, COORD(0)=ROW);", "use PARAM(name)=COORD(dim)");
    rejected("CHILD_INSTANCE(N, n, PARAM(ROW)=COORD(0), PARAM(ROW)=3);", "specified more than once");
    rejected("CHILD_INSTANCE(N, n, PARAM(ROW)=3, PARAM(ROW)=COORD(0));", "specified more than once");
    rejected("CHILD_INSTANCE(N, n, PARAM( ROW )=COORD(0), PARAM(ROW)=3);", "specified more than once");
    rejected("CHILD_INSTANCE(N, n, PARAM(ROW)=COORD());", "dimension cannot be empty");
    rejected("CHILD_INSTANCE(N, n, PARAM(ROW)=COORD(0)+1);", "without trailing expressions");
    rejected("CHILD_INSTANCE(N, n, PARAM(ROW)=COORD(0,1));", "exactly one dimension expression");
    rejected("SERVICE(s, ARG(uint32_t) x, ready=true) {}", "attributes must precede");
    rejected("HELPER(1) {}", "too many positional");
    rejected("REGISTER(r,);", "empty argument");
}
'''

EDITOR_SOURCE = r'''
#include <defhelper.hpp>
#include <run.hpp>
#include <type_traits>
CONFIG(N, 8);
PARAMETER(W, 32);
STRUCT(Packet) { uint32_t data; bool valid; };
ENUM(State) { Idle, Busy };
ALIAS(Word, uint32_t);
REGISTER(count, Word);
REGISTER(packet, Packet, 2) { packet.valid = true; }
WIRE(enabled, bool) { enabled = false; }
REQUEST(send, ARG(Word) value);
REQUEST(fetch, true, ARG(uint32_t) address, RESP(Word) value);
REQUEST(ping);
REQUEST(plain, false, RESP(Word) value);
SERVICE(receive, ARG(Word) value) { count.setnext(value); }
QUERY(status, Word) { return count.get(); }
CHILD_INSTANCE(Node, node);
USE_CHILD_SERVICE(node, receive, child_receive, ARG(Word) value);
USE_CHILD_QUERY(node, status, child_status, Word);
CONNECT_CR_CS(a, out, b, in);
CONNECT_CR_S(a, out, in);
CONNECT_CR_R(a, out, exposed);
CONNECT_S_CS(in, b, in);
QUEUE(q, Word, N);
QUEUE(mp, Word, N, 2, 3);
BRAM(ram, Word, N);
BRAM(mpram, Word, N, 2, 2);
BRAM(single, Word, N, 1, 1, 1rw);
ROM(rom, W, N, 1, "values.hex");
static_assert(std::is_same_v<decltype(count), VulRegister<Word>>);
static_assert(std::is_same_v<decltype(packet), VulRegister<Packet, 2>>);
static_assert(std::is_same_v<decltype(send(Word{})), void>);
static_assert(std::is_same_v<decltype(fetch(0, std::declval<Word&>())), bool>);
static_assert(std::is_same_v<decltype(plain(std::declval<Word&>())), void>);
static_assert(std::is_same_v<decltype(ping()), void>);
static_assert(std::is_same_v<decltype(&child_status), Word(*)()>);
static_assert(std::is_same_v<decltype(q), VulQueue<Word, N>>);
static_assert(std::is_same_v<decltype(mp), VulQueueMP<Word, N, 2, 3>>);
static_assert(std::is_same_v<decltype(single), VulBRAM1RW<Word, N>>);
TICK_IMPL() { count.setnext(count.get() + 1); q.enqnext(count.get()); }
TOP("Top.hpp"); PROJECT(".");
GLOBAL() { int cycles = 0; }
SIMULATION() { ++cycles; sim_nextcycle(); sim_reset(); sim_exit(); }
'''

with tempfile.TemporaryDirectory(prefix="vul-declarations-") as directory:
    path = Path(directory)
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    source = path / "declarations.cpp"
    source.write_text(SOURCE)
    sources = ["vulcpp/declaration_args.cpp", "vulcpp/common.cpp", "errormsg.cpp",
               "cppparse/source.cpp", "cppparse/scanning.cpp", "cppparse/macro_entries.cpp"]
    executable = path / "declarations"
    subprocess.run(compiler + ["-std=c++20", "-I", str(ROOT / "src"), str(source)]
                   + [str(ROOT / "src" / name) for name in sources]
                   + ["-o", str(executable)], check=True, cwd=ROOT)
    subprocess.run([str(executable)], check=True, cwd=ROOT)
    editor = path / "editor.cpp"
    editor.write_text(EDITOR_SOURCE)
    subprocess.run(compiler + ["-std=c++20", "-fsyntax-only", "-I", str(ROOT / "vullib"), str(editor)],
                   check=True, cwd=ROOT)
print("declaration API check PASS: canonical grammar, diagnostics and editor types")
