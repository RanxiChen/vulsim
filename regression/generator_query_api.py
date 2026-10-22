#!/usr/bin/env python3
"""QUERY interface declarations and wildcard array snapshots on both backends."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

HEADER = '''CONFIG(WIDTH, 9);
ENUM(Kind) { Zero=0, Active=3 };
ALIAS(Pair, uint32_t, dims=[2]);
STRUCT(Packet) { Int<80> wide; Kind kind; Pair values; };
ALIAS(ScalarGrid, Int<WIDTH>, dims=[2,3]);
ALIAS(SingleScalar, Int<WIDTH>, dims=[1]);
ALIAS(PacketGrid, Packet, dims=[2,3]);
ALIAS(PacketRow, Packet, dims=[3]);
ALIAS(PacketColumn, Packet, dims=[2]);
ALIAS(PairGrid, Pair, dims=[2,3]);
'''
LEAF = '''INTERFACE() {
    PARAMETER(ROW, 0);
    PARAMETER(COL, 0);
    QUERY(scalar, Int<WIDTH>);
    QUERY(packet, Packet);
    QUERY(pair, Pair);
}
REGISTER(cycle, Int<WIDTH>);
TICK_IMPL() { cycle.setnext(cycle.get()+Int<WIDTH>(1)); }
QUERY(scalar, Int<WIDTH>) { return Int<WIDTH>(ROW*10+COL)+cycle.get(); }
QUERY(pair, Pair) { return Pair{ROW, COL}; }
QUERY(packet, Packet) {
    Packet p{};
    p.wide=ROW*10+COL+cycle.get();
    p.wide.at<79,79>()=1;
    p.kind=Kind::Active;
    p.values=Pair{ROW, COL};
    return p;
}
'''
TOP = '''INTERFACE() {
    QUERY(grid, ScalarGrid);
    QUERY(packets, PacketGrid);
    QUERY(row, PacketRow);
    QUERY(column, PacketColumn);
    QUERY(pairs, PairGrid);
    QUERY(cell, Packet);
    QUERY(single, SingleScalar);
}
USE_VERSION(selected);
VERSION(unused) { }
VERSION(selected) {
    CHILD_INSTANCE(Leaf, mesh, dims=[2,3], PARAM(ROW)=COORD(0), PARAM(COL)=COORD(1));
    CHILD_INSTANCE(Leaf, one, dims=[1]);
    USE_CHILD_QUERY(one[*], scalar, one_snapshot, Int<WIDTH>);
    USE_CHILD_QUERY(mesh[*][*], scalar, scalar_snapshot, Int<WIDTH>, dims=[2,3]);
    USE_CHILD_QUERY(mesh[*][*], packet, packet_snapshot, Packet);
    USE_CHILD_QUERY(mesh[1][*], packet, row_snapshot, Packet, dims=[3]);
    USE_CHILD_QUERY(mesh[*][2], packet, column_snapshot, Packet, dims=[2]);
    USE_CHILD_QUERY(mesh[*][*], pair, pair_snapshot, Pair);
    USE_CHILD_QUERY(mesh[1][2], packet, cell_snapshot, Packet, dims=[]);
    QUERY(grid, ScalarGrid) { return scalar_snapshot(); }
    QUERY(packets, PacketGrid) { return packet_snapshot(); }
    QUERY(row, PacketRow) { return row_snapshot(); }
    QUERY(column, PacketColumn) { return column_snapshot(); }
    QUERY(pairs, PairGrid) { return pair_snapshot(); }
    QUERY(cell, Packet) { return cell_snapshot(); }
    QUERY(single, SingleScalar) { return one_snapshot(); }
}
'''
MAIN = '''#include <cstdlib>
#include <run.hpp>
TOP("Top.vul"); PROJECT(".");
QUERY(grid, ScalarGrid); QUERY(packets, PacketGrid);
QUERY(row, PacketRow); QUERY(column, PacketColumn);
QUERY(pairs, PairGrid); QUERY(cell, Packet); QUERY(single, SingleScalar);
SIMULATION() {
    auto check=[&](uint32_t cycle) {
        auto g=grid(); auto p=packets(); auto r=row(); auto c=column(); auto pair=pairs();
        for (uint32_t i=0; i<2; ++i) {
            for (uint32_t j=0; j<3; ++j) {
                if (g[i][j].to<uint32_t>()!=i*10+j+cycle) std::exit(1);
                if (static_cast<uint32_t>(p[i][j].wide.at<79,79>())!=1) std::exit(2);
                if (static_cast<uint32_t>(p[i][j].wide.at<31,0>())!=i*10+j+cycle) std::exit(3);
                if (p[i][j].kind!=Kind::Active || p[i][j].values!=Pair{i,j}) std::exit(4);
                if (pair[i][j]!=Pair{i,j}) std::exit(5);
            }
            if (c[i].values!=Pair{i,2}) std::exit(6);
        }
        for (uint32_t j=0; j<3; ++j) if (r[j].values!=Pair{1,j}) std::exit(7);
        if (cell().values!=Pair{1,2}) std::exit(8);
        if (single()[0].to<uint32_t>()!=cycle) std::exit(10);
    };
    auto snapshot=grid();
    check(0); sim_nextcycle(); check(1);
    if (snapshot[1][2].to<uint32_t>()!=12) std::exit(9);
    sim_nextcycle(); check(2); sim_reset(); check(0);
}
'''

def invoke(tool, path, output):
    return subprocess.run([str(ROOT / 'build' / tool), '-m', str(path / 'Main.utb'), '-f', '-o', str(output)],
                          text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=ROOT)

with tempfile.TemporaryDirectory(prefix='query-api-') as directory:
    path=Path(directory)
    for name, text in [('header.uh',HEADER), ('Leaf.vul',LEAF), ('Top.vul',TOP), ('Main.utb',MAIN)]:
        (path / name).write_text(text)
    for tool in ['vulsimgen','vulrtlgen']:
        out=path / tool
        result=invoke(tool,path,out)
        if result.returncode: raise RuntimeError(result.stdout)
        result=subprocess.run([str(out / 'run.sh')], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode: raise RuntimeError(result.stdout)
        print(f'QUERY check PASS: {tool}, multiaxis snapshots, partial wildcards, nested returns, enum, Int<80>, reset')
    (path / 'Main.utb').write_text('''#include <run.hpp>
TOP("Top.vul"); PROJECT("."); QUERY(result, uint32_t);
SIMULATION() { sim_nextcycle(); }
''')
    cases=[
        ('INTERFACE() {} QUERY(result, uint32_t) { return 0; }', 'has no declaration in INTERFACE'),
        ('INTERFACE() { QUERY(result, uint32_t); }', 'has no matching implementation'),
        ('INTERFACE() { QUERY(result, uint32_t) { return 0; } }', 'must be a declaration'),
        ('INTERFACE() { QUERY(result, uint32_t); } QUERY(result, uint16_t) { return 0; }', 'return type differs'),
        ('QUERY(result, uint32_t); QUERY(result, uint32_t); QUERY(result, uint32_t) { return 0; }', 'Duplicate QUERY declaration'),
        ('QUERY(result, uint32_t) { return 0; } QUERY(result, uint32_t) { return 1; }', 'Duplicate QUERY implementation'),
        ('QUERY(result, uint32_t) { return 0; } QUERY(result, uint32_t);', 'must appear before its implementation'),
        ('INTERFACE() { QUERY(result, uint32_t); } QUERY(result, uint32_t);', 'must provide an implementation block'),
        ('QUERY(result, uint32_t, dims=[2]) { return 0; }', "unknown attribute 'dims'"),
        ('CHILD_INSTANCE(Leaf, mesh, dims=[2,3]); USE_CHILD_QUERY(mesh[*][*], scalar, q, Int<WIDTH>, dims=[3,2]); QUERY(result, uint32_t) { return 0; }', 'do not match wildcard shape'),
        ('CHILD_INSTANCE(Leaf, mesh, dims=[2,3]); USE_CHILD_QUERY(mesh[*][9], scalar, q, Int<WIDTH>); QUERY(result, uint32_t) { return 0; }', 'out of range'),
        ('CHILD_INSTANCE(Leaf, mesh, dims=[2,3]); USE_CHILD_QUERY(mesh[*][*], scalar, q, uint32_t); QUERY(result, uint32_t) { return 0; }', 'return type mismatch'),
    ]
    for source,message in cases:
        (path / 'Top.vul').write_text(source)
        result=invoke('vulsimgen',path,path / 'error')
        if result.returncode==0 or message not in result.stdout:
            raise RuntimeError(f'expected {message!r}\n{result.stdout}')
    print('QUERY check PASS: interface implementation matching, duplicate declarations and wildcard diagnostics')
    # A non-versioned module remains valid, and a selected version must implement its interface.
    for source in [
        'QUERY(result, uint32_t); QUERY(result, uint32_t) { return 7; }',
        'INTERFACE() { QUERY(result, uint32_t); } USE_VERSION(v); VERSION(other) {} VERSION(v) { QUERY(result, uint32_t) { return 7; } }',
    ]:
        (path / 'Top.vul').write_text(source)
        result=invoke('vulsimgen',path,path / 'valid')
        if result.returncode: raise RuntimeError(result.stdout)
    print('QUERY check PASS: plain forward declarations and explicit version selection')
