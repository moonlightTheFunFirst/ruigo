#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
: "${QT_PREFIX:?Set QT_PREFIX to the Qt 6 macOS installation (for example ~/Qt/6.8.3/macos)}"
BUILD="$ROOT/mac/build/release"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_PREFIX"
cmake --build "$BUILD" --parallel
ctest --test-dir "$BUILD" --output-on-failure
case "${1:-}" in
    --deploy)
        test -f "$ROOT/data/dictionary.sqlite" || { echo 'Generate dictionary.sqlite first.' >&2; exit 1; }
        mkdir -p "$ROOT/mac/output"
        ditto "$BUILD/ruigo.app" "$ROOT/mac/output/ruigo.app"
        "$QT_PREFIX/bin/macdeployqt" "$ROOT/mac/output/ruigo.app" -always-overwrite
        test -f "$ROOT/mac/output/ruigo.app/Contents/PlugIns/sqldrivers/libqsqlite.dylib"
        ;;
    --run) open "$BUILD/ruigo.app" ;;
    '') ;;
    *) echo 'Usage: sh mac/scripts/build.sh [--run|--deploy]' >&2; exit 1 ;;
esac
