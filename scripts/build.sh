#!/bin/sh
# Usable from bash, zsh, fish, dash, Git Bash and the matching MSYS2 shell.
set -eu
cd "$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
task_gui=ON task_tui=ON task_tests=ON task_jobs=2 task_package=0
case "$(uname -s)" in
    Darwin) task_platform=macos; task_preset=macos ;;
    MINGW*|MSYS*) task_platform=windows; task_preset=$(printf '%s' "${MSYSTEM:-MINGW64}" | tr '[:upper:]' '[:lower:]') ;;
    *) task_platform=linux; task_preset=linux ;;
esac
while [ "$#" -gt 0 ]; do
    case "$1" in
        --tui-only) task_gui=OFF ;;
        --gui-only) task_tui=OFF ;;
        --skip-tests) task_tests=OFF ;;
        --package) task_package=1 ;;
        --jobs) shift; task_jobs="$1" ;;
        --preset) shift; task_preset="$1" ;;
        --help) printf '%s\n' 'sh scripts/build.sh [--tui-only|--gui-only] [--jobs N] [--preset NAME] [--package] [--skip-tests]'; exit 0 ;;
        *) printf 'Unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done
[ "$task_gui$task_tui" != OFFOFF ] || { printf '%s\n' 'Choose one frontend or both.' >&2; exit 2; }
# MSYS2 uses the current shell's prefix, so custom MSYS2 locations also work.
if [ "$task_platform" = windows ]; then
    cmake -S . -B "build/$task_preset" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$MINGW_PREFIX" \
        -DNOVA_BUILD_GUI="$task_gui" -DNOVA_BUILD_TUI="$task_tui" -DNOVA_BUILD_TESTS="$task_tests"
else
    cmake --preset "$task_preset" -DNOVA_BUILD_GUI="$task_gui" -DNOVA_BUILD_TUI="$task_tui" -DNOVA_BUILD_TESTS="$task_tests"
fi
cmake --build "build/$task_preset" --parallel "$task_jobs"
if [ "$task_tests" = ON ]; then ctest --test-dir "build/$task_preset" --output-on-failure; fi
if [ "$task_package" = 1 ]; then
    python3 scripts/package.py --build-dir "build/$task_preset" --platform "$task_platform-$(uname -m)-$task_preset"
fi
