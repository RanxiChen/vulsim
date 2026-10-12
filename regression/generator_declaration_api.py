#!/usr/bin/env python3
"""Unified declarations through global headers, versions, bindings and generators."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def run(command):
    result = subprocess.run(command, cwd=ROOT, text=True, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    return result

def require(command):
    result = run(command)
    if result.returncode:
        raise RuntimeError(f"command failed: {command}\n{result.stdout}")
    return result.stdout

with tempfile.TemporaryDirectory(prefix="generator-declarations-") as directory:
    path = Path(directory)
    (path / "header.hpp").write_text('''#include <defhelper.hpp>
CONFIG(value=8, name=DEPTH);
ALIAS(type=uint32_t, name=Word);
ALIAS(dims=[2,3], name=Matrix, type=Word);
STRUCT(name=Packet) { Word data; bool valid; };
ENUM(name=State) { Idle=0, Active=1 };
''')
    modules = {
        "Top.hpp": '''INTERFACE() {
    PARAMETER(value=4, name=WIDTH);
    REQUEST(name=report, ARG(Word) value);
    SERVICE(name=load, ARG(Word) value);
    SERVICE(name=capture, ARG(Word) value);
    QUERY(result, Word);
}
USE_VERSION(name=selected);
VERSION(name=unused) {
    SERVICE(name=load, ARG(Word) value) { }
}
VERSION(name=selected) {
    CHILD_INSTANCE(name=node, module=Node, PARAM(BASE)=WIDTH);
    CHILD_INSTANCE(name=producer, module=Producer);
    CHILD_INSTANCE(name=sink, module=Sink);
    CONNECT_S_CS(dstserv=load, srcserv=load, dstchild=node);
    CONNECT_CR_R(dstreq=report, srcchild=node, srcreq=send);
    CONNECT_CR_CS(dstserv=receive, srcchild=producer, dstchild=sink, srcreq=send);
    CONNECT_CR_S(dstserv=capture, srcchild=sink, srcreq=send);
    REGISTER(type=Word, name=seen);
    SERVICE(name=capture, ARG(Word) value) { seen.setnext(value); }
    USE_CHILD_QUERY(alias=node_value, query=status, instance=node, type=Word);
    QUERY(type=Word, name=result) { return node_value() + seen.get(); }
}
''',
        "Node.hpp": '''PARAMETER(name=BASE, value=1);
REGISTER(name=saved, type=Word) { saved=BASE; }
REQUEST(name=send, ARG(Word) value);
SERVICE(name=load, ARG(Word) value) { saved.setnext(value+BASE); send(value); }
QUERY(name=status, type=Word) { return saved.get(); }
''',
        "Producer.hpp": '''REQUEST(name=send, ARG(Word) value);
TICK_IMPL() { send(3); }
''',
        "Sink.hpp": '''REQUEST(name=send, ARG(Word) value);
SERVICE(name=receive, ARG(Word) value) { send(value+1); }
''',
    }
    for filename, source in modules.items():
        (path / filename).write_text('#include "header.hpp"\n' + source)
    (path / "Main.cpp").write_text('''#include <cstdlib>
#include <run.hpp>
TOP(path="./Top.hpp"); PROJECT(path="."); PARAMETER(value=6, name=WIDTH);
QUERY(type=Word, name=result);
REQUEST(name=load, ARG(Word) value);
GLOBAL() { Word reported=0; }
SERVICE(name=report, ARG(Word) value) { reported=value; }
SIMULATION() {
    if (result()!=6) std::exit(1);
    load(10);
    sim_nextcycle();
    if (result()!=20 || reported!=10) std::exit(2);
    sim_reset();
    if (result()!=6) std::exit(3);
}
''')
    for tool in ("vulsimgen", "vulrtlgen"):
        out = path / tool
        require([str(ROOT / "build" / tool), "-m", str(path / "Main.cpp"), "-f", "-o", str(out)])
        require([str(out / "run.sh")])
        print(f"generator declaration check PASS: {tool}, named headers/versions/bindings/all connections")

    shapes = path / "shapes"
    shapes.mkdir()
    (shapes / "header.hpp").write_text("#pragma once\n")
    (shapes / "Leaf.hpp").write_text('''REGISTER(name=saved, type=uint32_t);
SERVICE(name=load, ARG(uint32_t) value) { saved.setnext(value); }
QUERY(name=status, type=uint32_t) { return saved.get(); }
''')
    (shapes / "ShapeTop.hpp").write_text('''CHILD_INSTANCE(name=n, module=Leaf, dims=[2,3]);
USE_CHILD_SERVICE(alias=write, service=load, instance=n[*][*], dims=[2,3], ARG(uint32_t) value);
USE_CHILD_QUERY(alias=cell, query=status, instance=n[1][2], type=uint32_t, dims=[]);
QUERY(name=result, type=uint32_t) { return cell(); }
TICK_IMPL() { write<1,2>(9); }
''')
    (shapes / "Main.cpp").write_text('''#include <cstdlib>
#include <run.hpp>
TOP(path="ShapeTop.hpp"); PROJECT(path="."); QUERY(name=result, type=uint32_t);
SIMULATION() {
    if (result()!=0) std::exit(1);
    sim_nextcycle();
    if (result()!=9) std::exit(2);
}
''')
    for tool in ("vulsimgen", "vulrtlgen"):
        out = path / (tool + '-shapes')
        require([str(ROOT / "build" / tool), "-m", str(shapes / "Main.cpp"), "-f", "-o", str(out)])
        require([str(out / "run.sh")])
        print(f"generator declaration check PASS: {tool}, explicit child alias shape")

    (path / "Main.cpp").write_text('''#include <run.hpp>
TOP(path="./Top.hpp"); PROJECT(path="."); QUERY(name=result, type=Word);
SIMULATION() { sim_nextcycle(); }
''')
    errors = [
        ('REGISTER(name=r, type=Word, ports=2, ports=3);', 'specified more than once'),
        ('REGISTER(name=r, type=Word, ports=);', 'cannot be empty'),
        ('REGISTER(r, Word, dim=4);', 'use dims'),
        ('SERVICE(name=s, handshake=false, ready=true) {}', 'conflicts with handshake=0'),
        ('SERVICE(name=s, handshake=true) {}', 'requires ready'),
        ('BRAM(name=r, type=Word, size=DEPTH, mode=1rw, read_ports=2);', 'requires read_ports=1'),
        ('BRAM(name=r, type=Word, size=DEPTH, mode=bad);', 'mode must be'),
        ('REQUEST(name=r, dims=[0]);', 'must be positive'),
        ('CHILD_INSTANCE(module=Node, name=n, BASE=3);', 'PARAM(name)=value'),
        ('CHILD_INSTANCE(module=Node, name=n); USE_CHILD_SERVICE(instance=n, service=load, alias=v, dims=[2], ARG(Word) value);', 'do not match wildcard shape'),
        ('CHILD_INSTANCE(module=Node, name=n); USE_CHILD_QUERY(instance=n, query=status, alias=v, type=Word, dims=[2]);', 'do not match wildcard shape'),
        ('REGISTER_MUL(r, Word, 2);', 'has been removed; use'),
    ]
    for source, message in errors:
        (path / "Top.hpp").write_text('#include "header.hpp"\n// skipped comment\n\n' + source + '\nQUERY(result, Word) { return 0; }\n')
        result = run([str(ROOT / "build/vulsimgen"), "-m", str(path / "Main.cpp"), "-f", "-o", str(path / "errors")])
        if result.returncode == 0 or message not in result.stdout:
            raise RuntimeError(f"expected {message!r} for {source}\n{result.stdout}")
        if 'ports=2, ports=3' in source and 'Line 4:1' not in result.stdout:
            raise RuntimeError('lost original declaration location: ' + result.stdout)
    print("generator declaration check PASS: semantic errors and original source positions")

    register_main = path / "RegisterMain.cpp"
    register_main.write_text('''#include <cstdlib>
#include <run.hpp>
TOP("''' + str(ROOT / "example/apiinline_register/Top.hpp") + '''");
PROJECT("''' + str(ROOT / "example/apiinline_register") + '''");
QUERY(summary, RegisterSummary);
SIMULATION() {
    auto before=summary();
    if (before.scalar!=1 || before.multi!=10) std::exit(1);
    sim_nextcycle();
    auto after=summary();
    if (after.scalar!=1 || after.multi!=2 || after.payload_data!=1) std::exit(2);
    sim_reset();
    if (summary().multi!=10) std::exit(3);
}
''')
    resources = path / "resources"
    resources.mkdir()
    (resources / "values.hex").write_text("05\n06\n07\n08\n")
    (resources / "header.hpp").write_text("ALIAS(name=Pair, type=uint32_t, dims=[2]);\n")
    (resources / "Top.hpp").write_text('''REGISTER(name=phase, type=uint32_t);
QUEUE(name=singleq, type=uint32_t, depth=4);
BRAM(name=bank, type=uint32_t, size=4, read_ports=2, write_ports=2);
BRAM(name=single, type=uint32_t, size=4, mode=1rw);
ROM(name=rom, data_width=8, size=4, init="values.hex");
QUERY(name=result, type=uint32_t) {
    return bank.readdata<0>() + single.readdata() + rom.readdata<0>().to<uint32_t>() + (singleq.deqvalid() ? 1000 : 0);
}
TICK_IMPL() {
    if (phase.get()==0) {
        singleq.enqnext(7);
        bank.write<0>(0, 9);
        single.req(0, 11, true);
    } else {
        if (phase.get()==1) {
            singleq.clrnext();
            singleq.enqnext(9);
        }
        bank.readreq<0>(0);
        single.req(0, 0, false);
    }
    rom.readreq<0>(0);
    phase.setnext(phase.get()+1);
}
''')
    (resources / "Main.cpp").write_text('''#include <cstdlib>
#include <run.hpp>
TOP(path="Top.hpp"); PROJECT(path="."); QUERY(name=result, type=uint32_t);
SIMULATION() {
    sim_nextcycle(); sim_nextcycle();
    if (result()!=25) std::exit(1);
    sim_nextcycle();
    if (result()!=25) std::exit(2);
}
''')
    for tool in ("vulsimgen", "vulrtlgen"):
        for label, main in [("register", register_main),
                            ("queue", ROOT / "example/queue_mp_demo/test/Main.cpp"),
                            ("memory", resources / "Main.cpp")]:
            out = path / (tool + '-' + label)
            require([str(ROOT / "build" / tool), "-m", str(main), "-f", "-o", str(out)])
            require([str(out / "run.sh")])
            print(f"generator declaration check PASS: {tool}, {label}")
