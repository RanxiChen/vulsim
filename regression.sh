#!/usr/bin/env bash
set -uo pipefail

run_circt=1
for option in "$@"; do
    case "$option" in
        --no-circt) run_circt=0 ;;
        -h|--help)
            echo "Usage: bash regression.sh [--no-circt]"
            echo "Build and run Python checks and each project's sim, rtl and circt simulations."
            echo "  --no-circt  Skip all CIRCT checks, including Python regression cases."
            exit 0
            ;;
        *) echo "Unknown option: $option (use --help)" >&2; exit 2 ;;
    esac
done

if (( run_circt )) && ! command -v circt-opt >/dev/null 2>&1; then
    echo "[regression] circt-opt not found in PATH. Configure CIRCT as described in README.md or use --no-circt." >&2
    exit 1
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
cd "$ROOT_DIR"

echo "[regression] Configuring CMake build in $BUILD_DIR"
if cmake -S "$ROOT_DIR" -B "$BUILD_DIR"; then
    :
else
    status=$?
    echo "[regression] CMake configuration failed (exit $status)." >&2
    exit "$status"
fi

echo "[regression] Building project"
if cmake --build "$BUILD_DIR" --parallel; then
    :
else
    status=$?
    echo "[regression] Project build failed (exit $status)." >&2
    exit "$status"
fi

shopt -s nullglob
main_files=("$ROOT_DIR"/regression/*/Main.utb)
python_files=("$ROOT_DIR"/regression/*.py)
if (( ${#main_files[@]} == 0 && ${#python_files[@]} == 0 )); then
    echo "[regression] No regression/*/Main.utb or regression/*.py files found." >&2
    exit 1
fi

declare -a summaries=()
passed=0
failed=0

for python_file in "${python_files[@]}"; do
    name="$(basename "$python_file")"
    echo
    echo "[regression] === $name ==="
    if VUL_REGRESSION_CIRCT="$run_circt" python3 "$python_file"; then
        summaries+=("PASS $name (exit=0)")
        ((passed += 1))
    else
        status=$?
        summaries+=("FAIL $name (exit=$status)")
        ((failed += 1))
    fi
done

for main_file in "${main_files[@]}"; do
    case_dir="$(dirname "$main_file")"
    name="$(basename "$case_dir")"
    relative_main="${main_file#"$ROOT_DIR"/}"
    sim_out="build/${name}-sim"
    rtl_out="build/${name}-rtl"
    circt_out="build/${name}-circt"
    simgen_status="not run"
    sim_status="not run"
    rtlgen_status="not run"
    rtl_status="not run"
    circtgen_status="skipped"
    circt_status="skipped"

    echo
    echo "[regression] === $name ==="

    echo "[regression] Generating C++ simulation"
    if "$BUILD_DIR/vulsimgen" -m "$relative_main" -f -o "$sim_out"; then
        simgen_status=0
        echo "[regression] Running $sim_out/run.sh"
        if "$BUILD_DIR/$name-sim/run.sh"; then
            sim_status=0
        else
            sim_status=$?
        fi
    else
        simgen_status=$?
    fi

    echo "[regression] Generating Verilator simulation"
    if "$BUILD_DIR/vulrtlgen" -m "$relative_main" -f -o "$rtl_out"; then
        rtlgen_status=0
        echo "[regression] Running $rtl_out/run.sh"
        if "$BUILD_DIR/$name-rtl/run.sh"; then
            rtl_status=0
        else
            rtl_status=$?
        fi
    else
        rtlgen_status=$?
    fi

    if (( run_circt )); then
        circtgen_status="not run"
        circt_status="not run"
        echo "[regression] Generating CIRCT Verilator simulation"
        if "$BUILD_DIR/vulrtlgen" --circt -m "$relative_main" -f -o "$circt_out"; then
            circtgen_status=0
            echo "[regression] Running $circt_out/run.sh"
            if "$BUILD_DIR/$name-circt/run.sh"; then
                circt_status=0
            else
                circt_status=$?
            fi
        else
            circtgen_status=$?
        fi
    fi

    if [[ "$simgen_status" == 0 && "$sim_status" == 0 &&
          "$rtlgen_status" == 0 && "$rtl_status" == 0 ]] &&
       { (( ! run_circt )) || [[ "$circtgen_status" == 0 && "$circt_status" == 0 ]]; }; then
        summaries+=("PASS $name (sim=$sim_status, rtl=$rtl_status, circt=$circt_status)")
        ((passed += 1))
    else
        summaries+=("FAIL $name (simgen=$simgen_status, sim=$sim_status, rtlgen=$rtlgen_status, rtl=$rtl_status, circtgen=$circtgen_status, circt=$circt_status)")
        ((failed += 1))
    fi
done

echo
echo "[regression] Results: $passed passed, $failed failed"
for summary in "${summaries[@]}"; do
    echo "[regression] $summary"
done

(( failed == 0 ))
