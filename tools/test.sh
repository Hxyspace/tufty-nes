#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
python_bin="${PYTHON:-$(command -v python3)}"
"$python_bin" -B "$project_dir/tools/deps.py" --profile host --radio
build_dir="${TUFTY_TEST_BUILD_DIR:-$project_dir/build/tests}"
cmake -S "$project_dir/tests" -B "$build_dir" -G Ninja "-DPython3_EXECUTABLE=$python_bin"
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"
ctest --test-dir "$build_dir" --output-on-failure
