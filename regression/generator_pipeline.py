#!/usr/bin/env python3
"""PIPELINE grammar, half APIs, standalone lowering, and cycle alignment."""
from pathlib import Path
import os
import shutil
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'regression/pipeline'
CIRCT_ENABLED = os.environ.get('VUL_REGRESSION_CIRCT', '1') != '0'
BACKENDS = [('sim', 'vulsimgen'), ('rtl', 'vulrtlgen')]
if CIRCT_ENABLED:
    BACKENDS.append(('circt', 'vulrtlgen'))


def run(args, cwd=ROOT, success=True):
    p = subprocess.run(list(map(str, args)), cwd=cwd, text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if (p.returncode == 0) != success:
        raise AssertionError(f'{args}: exit={p.returncode}\n{p.stdout[-8000:]}')
    return p.stdout


def generate(tool, case, output, success=True, circt=False):
    args = [ROOT/'build'/tool, '-m', case/'Main.utb', '-f', '-o', output]
    if circt:
        args.append('--circt')
    return run(args, success=success)


with tempfile.TemporaryDirectory(prefix='vul-pipeline-') as directory:
    base = Path(directory)
    for latency in (1, 2, 3, 4, 8):
        case = base/f'case{latency}'
        shutil.copytree(FIXTURE, case)
        header = case/'header.uh'
        header.write_text(header.read_text().replace('CONFIG(LATENCY, 3)', f'CONFIG(LATENCY, {latency})'))
        for backend, tool in BACKENDS:
            out = base/f'{backend}{latency}'
            generation_log = generate(tool, case, out, circt=(backend == 'circt'))
            log = run([out/'run.sh'])
            assert f'PIPELINE PASS latency={latency}' in log, log
            if backend != 'sim':
                sv = (out/'sim/top.sv').read_text()
                assert 'begin : __vul_pipeline_transform' in sv
                assert 'begin : __vul_pipeline_copy' in sv
                assert 'resetvalue_output' not in sv and 'resetvalue_ovalid' not in sv
                assert '__vul_pipeline_reset_' not in sv
                assert '__vul_pipeline_q_output <= 80\'h' in sv
                assert 'oreg_reset output vector<uint64_t>=' in (out/'sim/top.sv.dbg').read_text()
                assert "__vul_pipeline_q_ovalid = 1'b0" in sv
                assert 'wen_output' not in sv
                pipeline_sources = list((out/'sim').glob('*.pipeline_transform.cpp'))
                assert len(pipeline_sources) == 1, pipeline_sources
                pipeline_source = pipeline_sources[0].read_text()
                assert 'bool __vul_port_ovalid;' in pipeline_source
                assert '__vul_value_ovalid' not in pipeline_source
                assert not re.search(r'__vul_port_ovalid\s*=', pipeline_source)
                if backend == 'rtl':
                    assert len(re.findall(r'logic rtlzz_valid_s\d+;', sv)) == latency-1
                else:
                    assert ', CIRCT' in generation_log
                if latency > 1:
                    assert 'always_ff' in sv
                    assert 'internal_stages=' in (out/'sim/top.sv.dbg').read_text()
        print(f'PASS pipeline latency={latency}, sim+native' + ('+circt' if CIRCT_ENABLED else ' (CIRCT skipped)'))

    case = base/'negative'
    case.mkdir()
    (case/'header.uh').write_text('')
    (case/'Main.utb').write_text('TOP("Top.vul"); PROJECT("."); SIMULATION() {}')
    default = 'PIPELINE(p, 2, IREG(a, Int<16>), OREG(b, Int<16>)) { b=a; }'
    invalid = {
        'zero latency': default.replace('p, 2', 'p, 0'),
        'negative latency': default.replace('p, 2', 'p, -1'),
        'missing IREG': 'PIPELINE(p, 1, OREG(b, bool)) { b=true; }',
        'missing OREG': 'PIPELINE(p, 1, IREG(a, bool)) {}',
        'unpaired IVALID': default.replace('OREG(b, Int<16>)', 'IVALID(iv), OREG(b, Int<16>)'),
        'unpaired OVALID': default.replace('OREG(b, Int<16>)', 'OREG(b, Int<16>), OVALID(ov)'),
        'duplicate valid': default.replace('OREG(b, Int<16>)', 'IVALID(iv), IVALID(iv2), OREG(b, Int<16>), OVALID(ov), OVALID(ov2)'),
        'valid type': default.replace('OREG(b, Int<16>)', 'IVALID(iv, type=bool), OREG(b, Int<16>), OVALID(ov)'),
        'valid dimensions': default.replace('OREG(b, Int<16>)', 'IVALID(iv, dims=[]), OREG(b, Int<16>), OVALID(ov)'),
        'OVALID ports': default.replace('OREG(b, Int<16>)', 'IVALID(iv), OREG(b, Int<16>), OVALID(ov, ports=1)'),
        'OREG reset': default.replace('OREG(b, Int<16>)', 'OREG(b, Int<16>) {}'),
        'IVALID reset': default.replace('OREG(b, Int<16>)', 'IVALID(iv) {}, OREG(b, Int<16>), OVALID(ov)'),
        'OVALID reset': default.replace('OREG(b, Int<16>)', 'IVALID(iv), OREG(b, Int<16>), OVALID(ov) {}'),
        'IREG empty dims': default.replace('IREG(a, Int<16>)', 'IREG(a, Int<16>, dims=[])'),
        'IREG dims': default.replace('IREG(a, Int<16>)', 'IREG(a, Int<16>, dims=[2])'),
        'OREG empty dims': default.replace('OREG(b, Int<16>)', 'OREG(b, Int<16>, dims=[])'),
        'OREG dims': default.replace('OREG(b, Int<16>)', 'OREG(b, Int<16>, dims=[2])'),
        'OREG explicit one port': default.replace('OREG(b, Int<16>)', 'OREG(b, Int<16>, ports=1)'),
        'IREG array alias': 'ALIAS(A, Int<16>, dims=[2]); '+default.replace('IREG(a, Int<16>)', 'IREG(a, A)'),
        'OREG array alias': 'ALIAS(A, Int<16>, dims=[2]); '+default.replace('OREG(b, Int<16>)', 'OREG(b, A)'),
        'IREG chained array alias': 'ALIAS(A, Int<16>, dims=[2]); ALIAS(B, A); '+default.replace('IREG(a, Int<16>)', 'IREG(a, B)'),
        'OREG chained array alias': 'ALIAS(A, Int<16>, dims=[2]); ALIAS(B, A); '+default.replace('OREG(b, Int<16>)', 'OREG(b, B)'),
        'OREG ports': default.replace('OREG(b, Int<16>)', 'OREG(b, Int<16>, ports=2)'),
        'IVALID ports': default.replace('OREG(b, Int<16>)', 'IVALID(iv, ports=0), OREG(b, Int<16>), OVALID(ov)'),
        'duplicate name': default.replace('OREG(b, Int<16>)', 'OREG(a, Int<16>)'),
        'attribute after endpoints': default.replace('OREG(b, Int<16>)', 'OREG(b, Int<16>), latency=3'),
    }
    for name, source in invalid.items():
        (case/'Top.vul').write_text(source)
        for tool in ('vulsimgen', 'vulrtlgen'):
            generate(tool, case, base/'bad', success=False)
        if CIRCT_ENABLED:
            generate('vulrtlgen', case, base/'bad-circt', success=False, circt=True)
    print(f'PASS {len(invalid)} declaration diagnostics on all backends')

    # Compile sim output to verify isolation without imposing RTLZZ's subset.
    invalid_logic = {
        'input read outside': default+' TICK_IMPL() { auto x=a.get(); }',
        'output write outside': default+' TICK_IMPL() { b.setnext(Int<16>(1)); }',
        'output assignment outside': default+' TICK_IMPL() { b=Int<16>(1); }',
        'input write inside': default.replace('b=a;', 'a=Int<16>(1); b=a;'),
        'hidden OVALID': default.replace('OREG(b, Int<16>)', 'IVALID(iv), OREG(b, Int<16>), OVALID(ov)').replace('b=a;', 'b=a; ov=iv;'),
        'register access inside': 'REGISTER(other, Int<16>); '+default.replace('b=a;', 'b=other.get();'),
        'helper captures register': 'REGISTER(other, Int<16>); HELPER() { Int<16> bad() { return other.get(); } } '+default.replace('b=a;', 'b=bad();'),
        'internal storage access': default+' TICK_IMPL() { auto x=__vul_store_a.get(); }',
    }
    for name, source in invalid_logic.items():
        (case/'Top.vul').write_text(source)
        out = base/'bad-sim'
        p = subprocess.run([str(ROOT/'build/vulsimgen'), '-m', str(case/'Main.utb'), '-f', '-o', str(out)], cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if p.returncode == 0:
            run(['g++', '-std=c++20', '-fsyntax-only', '-I'+str(out), out/'main.cpp'], success=False)
        generate('vulrtlgen', case, base/'bad-rtl', success=False)
        if CIRCT_ENABLED:
            generate('vulrtlgen', case, base/'bad-circt', success=False, circt=True)
    print(f'PASS {len(invalid_logic)} access isolation cases')

    for body in ('if (a != Int<16>(0)) b=a;', 'b=b+Int<16>(1);', 'return;'):
        (case/'Top.vul').write_text(default.replace('b=a;', body))
        generate('vulsimgen', case, base/'incomplete-sim')
        error = generate('vulrtlgen', case, base/'incomplete-rtl', success=False)
        assert 'definition' in error or 'coverage' in error or 'final value' in error, error
    print('PASS RTL-only output definition checks')

    # Pure native C++ unsupported by the synthesizer remains usable in simgen.
    source = default.replace('b=a;', 'try { b=a; } catch (...) { b=0; }')
    (case/'Top.vul').write_text(source)
    generate('vulsimgen', case, base/'native-sim')
    run(['g++', '-std=c++20', '-fsyntax-only', '-I'+str(base/'native-sim'), base/'native-sim/main.cpp'])
    generate('vulrtlgen', case, base/'native-rtl', success=False)
    print('PASS sim-only native C++ and pipeline-only module')
