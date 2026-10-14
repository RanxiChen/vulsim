#!/usr/bin/env python3
"""Parameterized pipeline-only child modules, coordinates, and release cleanup."""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]

def run(args):
    p=subprocess.run(list(map(str,args)),cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if p.returncode: raise AssertionError(f'{args}\n{p.stdout[-6000:]}')
    return p.stdout

with tempfile.TemporaryDirectory(prefix='pipeline-instances-') as directory:
    case=Path(directory)
    (case/'header.hpp').write_text('ALIAS(Row, Int<16>, dims=[3]);')
    (case/'Leaf.hpp').write_text('''INTERFACE() {
        PARAMETER(OFFSET, 0);
        SERVICE(push, ARG(Int<16>) value);
        QUERY(result, Int<16>);
    }
    PIPELINE(calc, OFFSET+1, IREG(a, Int<16>) { a=OFFSET; }, OREG(b, Int<16>)) { b=a+Int<16>(OFFSET); }
    SERVICE(push, ARG(Int<16>) value) { a.setnext(value); }
    QUERY(result, Int<16>) { return b; }
    ''')
    (case/'Top.hpp').write_text('''INTERFACE() { SERVICE(push, dims=[3], ARG(Int<16>) value); QUERY(results, Row); }
    CHILD_INSTANCE(Leaf, mesh, dims=[3], PARAM(OFFSET)=COORD(0));
    CONNECT_S_CS(push, mesh[*], push);
    USE_CHILD_QUERY(mesh[*], result, snapshot, Int<16>);
    QUERY(results, Row) { return snapshot(); }
    ''')
    (case/'Main.cpp').write_text('''#include <cstdlib>
    #include <run.hpp>
    TOP("Top.hpp"); PROJECT(".");
    REQUEST(push, dims=[3], ARG(Int<16>) value); QUERY(results, Row);
    SIMULATION() {
        for (uint32_t i=0; i<20; ++i) {
            push<0>(Int<16>(100+i)); push<1>(Int<16>(100+i)); push<2>(Int<16>(100+i));
            sim_nextcycle();
            auto r=results();
            for (uint32_t j=0; j<3; ++j) if (i>=j+1 && r[j].to<uint32_t>()!=100+i-(j+1)+j) std::exit(1);
        }
    }
    ''')
    for tool in ('vulsimgen','vulrtlgen'):
        out=case/tool
        run([ROOT/'build'/tool,'-m',case/'Main.cpp','-f','-o',out])
        run([out/'run.sh'])
    release=case/'release'
    run([ROOT/'build/vulrtlgen','-m',case/'Main.cpp','-f','--release','-o',release])
    assert not list(release.rglob('*.pipeline_*.cpp'))
    assert not list(release.rglob('*.dbgmap'))
    print('PASS parameterized pipeline child instances and release cleanup')
