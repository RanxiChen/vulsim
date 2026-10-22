#!/usr/bin/env python3
"""Regression checks for generator Service scheduling and transaction connectivity."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SIMGEN = ROOT / "build/vulsimgen"


def run_case(name, modules, expected_error=None, main="", setup=""):
    with tempfile.TemporaryDirectory(prefix=f"generator-{name}-") as directory:
        case = Path(directory)
        (case / "header.uh").write_text(
            "#pragma once\n#include <defhelper.hpp>\n"
            "STRUCT(Values) { uint32_t before; uint32_t after; };\n"
        )
        for filename, source in modules.items():
            (case / filename).write_text('#include "header.uh"\n' + source)
        (case / "Main.utb").write_text(
            '#include <cstdlib>\n#include <defhelper.hpp>\n#include <run.hpp>\n'
            '#include "header.uh"\nTOP("./Top.vul");\nPROJECT(".");\n'
            + main + '\nSIMULATION() { ' + setup + ' sim_nextcycle(); '
            + ('Values s = result(); if (s.before != 5 || s.after != 7) std::exit(1);'
               if main else '') + ' }\n'
        )
        generated = subprocess.run(
            [str(SIMGEN), "-m", str(case / "Main.utb"), "-f", "-o", str(case / "out")],
            cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
        if expected_error:
            if generated.returncode == 0 or expected_error not in generated.stdout:
                raise RuntimeError(f"{name}: expected {expected_error!r}\n{generated.stdout}")
        else:
            if generated.returncode:
                raise RuntimeError(f"{name}: generation failed\n{generated.stdout}")
            simulated = subprocess.run(
                [str(case / "out/run.sh")], cwd=ROOT, text=True,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            )
            if simulated.returncode:
                raise RuntimeError(f"{name}: simulation failed\n{simulated.stdout}")
        print(f"generator scheduling/connectivity check PASS: {name}")


run_case("ancestor-tick", {
    "Top.vul": '''
WIRE(value, uint32_t) { value = 0; }
REGISTER(before, uint32_t);
SERVICE(mark, priority=20) { value = 5; }
SERVICE(sample, priority=-20, RESP(uint32_t) out) { out = value; }
CHILD_INSTANCE(HighWrapper, z_high);
CHILD_INSTANCE(LowWrapper, a_low);
CONNECT_CR_S(z_high, mark, mark);
CONNECT_CR_S(a_low, sample, sample);
USE_CHILD_QUERY(a_low, result, low_result, uint32_t);
TICK_IMPL() { before.setnext(value); value = 7; }
QUERY(result, Values) { return Values{before.get(), low_result()}; }
''',
    "HighWrapper.vul": '''
REQUEST(mark);
CHILD_INSTANCE(HighSource, leaf);
CONNECT_CR_R(leaf, mark, mark);
''',
    "HighSource.vul": "REQUEST(mark);\nTICK_IMPL() { mark(); }\n",
    "LowWrapper.vul": '''
REQUEST(sample, RESP(uint32_t) out);
CHILD_INSTANCE(LowSource, leaf);
CONNECT_CR_R(leaf, sample, sample);
USE_CHILD_QUERY(leaf, result, leaf_result, uint32_t);
QUERY(result, uint32_t) { return leaf_result(); }
''',
    "LowSource.vul": '''
REQUEST(sample, RESP(uint32_t) out);
REGISTER(saved, uint32_t);
TICK_IMPL() { uint32_t out = 0; sample(out); saved.setnext(out); }
QUERY(result, uint32_t) { return saved.get(); }
''',
}, main="QUERY(result, Values);")

run_case("array-forward", {
    "Top.vul": '''
SERVICE(input, dims=[2], ARG(uint32_t) value);
CHILD_INSTANCE(Lane, lane, dims=[2]);
CONNECT_S_CS(input, lane[*], input);
USE_CHILD_QUERY(lane[*], result, lane_result, uint32_t);
QUERY(result, Values) { auto values = lane_result(); return Values{values[0], values[1]}; }
''',
    "Lane.vul": '''
REGISTER(saved, uint32_t);
SERVICE(input, ARG(uint32_t) value) { saved.setnext(value); }
QUERY(result, uint32_t) { return saved.get(); }
''',
}, main="QUERY(result, Values);\nREQUEST(input, dims=[2], ARG(uint32_t) value);",
    setup="input<0>(5); input<1>(7);")

run_case("missing-body", {"Top.vul": "SERVICE(missing);\n"}, "no matching implementation")
run_case("forward-type-mismatch", {
    "Top.vul": "SERVICE(input, ARG(uint32_t) value);\nCHILD_INSTANCE(Sink, sink);\nCONNECT_S_CS(input, sink, input);\n",
    "Sink.vul": "SERVICE(input, ARG(bool) value) { }\n",
}, "signature mismatch")
run_case("forward-and-body", {
    "Top.vul": "SERVICE(input) { }\nCHILD_INSTANCE(Sink, sink);\nCONNECT_S_CS(input, sink, input);\n",
    "Sink.vul": "SERVICE(input) { }\n",
}, "both local implementation and child-service forwarding")
run_case("multiple-sources", {
    "Top.vul": "CHILD_INSTANCE(Source, a);\nCHILD_INSTANCE(Source, b);\nCHILD_INSTANCE(Sink, sink);\nCONNECT_CR_CS(a, send, sink, receive);\nCONNECT_CR_CS(b, send, sink, receive);\n",
    "Source.vul": "REQUEST(send);\nTICK_IMPL() { send(); }\n",
    "Sink.vul": "SERVICE(receive) { }\n",
}, "called by multiple instances")
run_case("call-cycle", {
    "Top.vul": "CHILD_INSTANCE(Source, source);\nCHILD_INSTANCE(Sink, sink);\nCONNECT_CR_CS(source, send, sink, receive);\nCONNECT_CR_CS(sink, back, source, again);\n",
    "Source.vul": "REQUEST(send);\nSERVICE(again) { send(); }\nTICK_IMPL() { send(); }\n",
    "Sink.vul": "REQUEST(back);\nSERVICE(receive) { back(); }\n",
}, "Cyclic call")
run_case("repeated-call", {
    "Top.vul": "CHILD_INSTANCE(Branch, a);\nCHILD_INSTANCE(Branch, b);\nCHILD_INSTANCE(Sink, sink);\nUSE_CHILD_SERVICE(a, enter, enter_a);\nUSE_CHILD_SERVICE(b, enter, enter_b);\nCONNECT_CR_CS(a, send, sink, receive);\nCONNECT_CR_CS(b, send, sink, receive);\nTICK_IMPL() { enter_a(); enter_b(); }\n",
    "Branch.vul": "REQUEST(send);\nSERVICE(enter) { send(); }\n",
    "Sink.vul": "SERVICE(receive) { }\n",
}, "Repeated call")
run_case("update-cycle", {
    "Top.vul": "CHILD_INSTANCE(Source, a);\nCHILD_INSTANCE(Source, b);\nCHILD_INSTANCE(Sink, x);\nCHILD_INSTANCE(Sink, y);\nCONNECT_CR_CS(a, first, x, low);\nCONNECT_CR_CS(a, second, y, high);\nCONNECT_CR_CS(b, first, x, high);\nCONNECT_CR_CS(b, second, y, low);\n",
    "Source.vul": "REQUEST(first);\nREQUEST(second);\nTICK_IMPL() { first(); second(); }\n",
    "Sink.vul": "SERVICE(low, priority=10) { }\nSERVICE(high, priority=20) { }\n",
}, "Cyclic update constraints")
