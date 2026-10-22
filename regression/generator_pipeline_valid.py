#!/usr/bin/env python3
"""Simgen skips invalid computation/data copies while valid advances normally."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(args):
    result = subprocess.run(list(map(str, args)), cwd=ROOT, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    assert result.returncode == 0, result.stdout[-8000:]
    return result.stdout


with tempfile.TemporaryDirectory(prefix='vul-pipeline-valid-') as directory:
    base = Path(directory)
    for latency in (1, 2, 8):
        case = base / f'case{latency}'
        case.mkdir()
        (case / 'header.uh').write_text('''
CONFIG(LATENCY, %d);
HELPER() {
    inline uint32_t computation_calls = 0;
    inline uint32_t counted(uint32_t x) { ++computation_calls; return x + 17; }
}
''' % latency)
        # Put OVALID before data outputs to exercise commit ordering.
        (case / 'Top.vul').write_text('''
INTERFACE() {
    SERVICE(push, ARG(uint32_t) x, ARG(bool) v);
    QUERY(data, uint32_t); QUERY(other, uint32_t);
    QUERY(valid, bool); QUERY(calls, uint32_t);
}
PIPELINE(p, LATENCY, OVALID(ov), IREG(input, uint32_t),
    IVALID(iv), OREG(output, uint32_t), OREG(output2, uint32_t)) {
    output = counted(input);
    output2 = output + 3;
}
SERVICE(push, ARG(uint32_t) x, ARG(bool) v) {
    input.setnext(x); iv.setnext(v);
}
QUERY(data, uint32_t) { return output; }
QUERY(other, uint32_t) { return output2; }
QUERY(valid, bool) { return ov; }
QUERY(calls, uint32_t) { return computation_calls; }
''')
        (case / 'Main.utb').write_text('''
#include <cstdlib>
#include <cstdio>
#include <run.hpp>
TOP("Top.vul"); PROJECT(".");
REQUEST(push, ARG(uint32_t) x, ARG(bool) v);
QUERY(data, uint32_t); QUERY(other, uint32_t);
QUERY(valid, bool); QUERY(calls, uint32_t);
SIMULATION() {
    const bool sequence[] = {false, true, true, false, false, true, false,
                             true, false, false, true, true, false};
    for (int pass=0; pass<2; ++pass) {
        uint32_t before = calls();
        sim_reset();
        // Reset evaluates even with valid=false and flushes in-flight values.
        if (calls()!=before+1 || data()!=17 || other()!=20 || valid()) std::exit(1);
        uint32_t expected_calls=calls(), last=17;
        const int count=sizeof(sequence)/sizeof(sequence[0]);
        for (int i=0; i<count+LATENCY+2; ++i) {
            bool v=i<count && sequence[i];
            push(uint32_t(100+i), v);
            sim_nextcycle();
            if (i>0 && i-1<count && sequence[i-1]) ++expected_calls;
            int source=i-LATENCY;
            bool expected_valid=source>=0 && source<count && sequence[source];
            if (expected_valid) last=uint32_t(100+source+17);
            if (calls()!=expected_calls || valid()!=expected_valid ||
                data()!=last || other()!=last+3) std::exit(2);
        }
        // Valid holds when unwritten, so every subsequent cycle computes.
        push(999, true);
        for (int i=0; i<LATENCY+3; ++i) sim_nextcycle();
        if (!valid() || data()!=1016 || other()!=1019) std::exit(3);
        // Reset while this continuous valid stream is still in flight.
    }
    printf("PASS simgen valid skipping latency=%d\\n", int(LATENCY));
}
''')
        output = base / f'output{latency}'
        run([ROOT / 'build/vulsimgen', '-m', case / 'Main.utb', '-f', '-o', output])
        print(run([output / 'run.sh']).strip())
