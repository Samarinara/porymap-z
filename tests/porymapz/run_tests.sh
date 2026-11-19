#!/usr/bin/env bash
# Builds and runs every Porymap-Z test suite out of tree, the same way CI does.
#
#   tests/porymapz/run_tests.sh [build-dir] [extra qmake arguments...]
#
# QMAKE picks the qmake binary (default: qmake on PATH), JOBS the make job count.
# Checks against real decomp checkouts run only when these are set; each one
# that writes files must point at a disposable copy:
#   PORYMAP_Z_TEST_PROJECT        pokeemerald (dialogue round trip)
#   PORYMAP_SCRIPT_TEST_PROJECT   pokeemerald (map script parsing)
#   PORYMAP_Z_TRAINER_PROJECT     pokeemerald copy (creates a trainer through the main window)
#   PORYMAP_Z_BATTLE_CORPUS       folder holding emerald/ and firered/ checkouts
#   PORYMAP_Z_EXPANSION_PROJECT   pokeemerald-expansion
#   PORYMAP_Z_EXPANSION_CORPUS    copy of expansion's src/data/trainers.party
#   PORYMAP_Z_TRAINERPROC         expansion's built tools/trainerproc/trainerproc
set -uo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
build=${1:-$repo/build/tests}
shift $(( $# > 0 ? 1 : 0 ))
qmake=${QMAKE:-qmake}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}
export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen}
export PYTHONDONTWRITEBYTECODE=1
mkdir -p "$build"
build=$(cd "$build" && pwd)

failed=()

# run_suite <name> <build subfolder> <project file> <binary, or - to only build> [qmake arguments...]
run_suite() {
    local name=$1 dir=$build/$2 pro=$3 binary=$4
    shift 4
    echo "::group::$name"
    mkdir -p "$dir"
    if (cd "$dir" && "$qmake" "$pro" "$@" && make -j"$jobs" && { [ "$binary" = - ] || "./$binary"; }); then
        echo "::endgroup::"
    else
        echo "::endgroup::"
        echo "::error::$name failed"
        failed+=("$name")
    fi
}

for suite in dialogue:tst_dialogue editor:tst_dialogueeditor battle:tst_battle \
             scripts:tst_scripts trainers:tst_trainers; do
    run_suite "${suite##*:}" "${suite%%:*}" "$repo/tests/porymapz/${suite%%:*}.pro" "${suite##*:}" "$@"
done

# The main window test reuses the application's objects, so build the app first.
run_suite porymap-z app "$repo/porymap-z.pro" - "$@"
run_suite tst_mainwindow app "$repo/porymap-z.pro" tst_mainwindow CONFIG+=porymap_z_tests "$@"

echo "::group::playtest helper"
if python3 -m unittest discover -s "$repo/tests/porymapz" -p 'test_*.py'; then
    echo "::endgroup::"
else
    echo "::endgroup::"
    echo "::error::playtest helper tests failed"
    failed+=(playtest)
fi

if (( ${#failed[@]} )); then
    echo "Failed: ${failed[*]}"
    exit 1
fi
echo "All Porymap-Z tests passed."
