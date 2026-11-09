#!/usr/bin/env bash
#
# mayhem/build.sh — build the flecs fuzz harnesses (sanitized flecs + libFuzzer / standalone
# binaries) and the upstream functional test suite (normal flags). Runs in /mayhem as `mayhem`.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"

BAKE=/opt/toolchains/deps/bake-2b5b0f5/src
[ -d "$BAKE/drivers/test" ] || { echo "missing cached bake at $BAKE" >&2; exit 1; }

TEST_SUITES="core query meta addons collections"

# wait_all: wait for every background compile, failing if any failed.
wait_all() {
    local rc=0 j
    for j in $(jobs -p); do wait "$j" || rc=1; done
    return "$rc"
}

# flecs sources, as upstream's CMakeLists compiles them (C99 + GNU extensions, static).
mapfile -t FLECS_SRCS < <(find src -name '*.c' | sort)

# build_flecs <outdir> <flags...>: compile every flecs source into <outdir>/libflecs.a.
build_flecs() {
    local out="$1"; shift
    rm -rf "$out"; mkdir -p "$out"
    printf '%s\n' "${FLECS_SRCS[@]}" | xargs -P "$MAYHEM_JOBS" -I{} sh -c '
        out=$1; src=$2; shift 2
        o="$out/$(echo "$src" | tr / _).o"
        '"$CC"' -std=gnu99 -Iinclude -Dflecs_STATIC -c "$@" "$src" -o "$o"
    ' _ "$out" {} "$@"
    rm -f "$out/libflecs.a"
    ar rcs "$out/libflecs.a" "$out"/*.o
}

# 0) Upstream's own test suites with the project's NORMAL flags (no sanitizers), for mayhem/test.sh.
#    They need only the bake test driver, built here as a static library from the cached source.
T=out/test
rm -rf "$T"; mkdir -p "$T"
BAKE_INC="-I$BAKE/include -I$BAKE/util/include -I$BAKE/drivers/test/include"
find "$BAKE/src" "$BAKE/util/src" "$BAKE/drivers/test/src" -name '*.c' ! -path '*/src/win/*' | sort |
    xargs -P "$MAYHEM_JOBS" -I{} sh -c '
        f=$1; shift
        '"$CC"' -std=gnu99 -O1 -w -D__BAKE__=1 -Dbake_test_EXPORTS=1 -DUT_IMPL=1 -D_XOPEN_SOURCE=600 "$@" \
            -c "$f" -o "'"$T"'/bake_$(echo "$f" | md5sum | cut -c1-12).o"
    ' _ {} $BAKE_INC
ar rcs "$T/libbake.a" "$T"/bake_*.o
# shellcheck disable=SC2086
build_flecs "$T/flecs" -O1 $COVERAGE_FLAGS
for s in $TEST_SUITES; do
    # shellcheck disable=SC2086
    $CC -std=gnu99 -O1 -w -D__BAKE__=1 $COVERAGE_FLAGS $BAKE_INC -Iinclude -Itest/$s/include -Dflecs_STATIC \
        test/$s/src/*.c "$T/flecs/libflecs.a" "$T/libbake.a" -lm -lpthread -o "$T/test_$s" &
done
wait_all

# 1) Sanitized + SanCov-instrumented flecs for the fuzz targets. fuzzer-no-link adds the
#    coverage instrumentation to the library itself (not only the harness TU).
# shellcheck disable=SC2086
build_flecs out/fuzz -O1 $SANITIZER_FLAGS -fsanitize=fuzzer-no-link $DEBUG_FLAGS

# 2) Harnesses: libFuzzer binary + standalone run-once reproducer, both with LSan off.
# shellcheck disable=SC2086
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o out/fuzz/lsan_off.o
# shellcheck disable=SC2086
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -x c -c "$STANDALONE_FUZZ_MAIN" -o out/fuzz/standalone_main.o

TARGETS="fuzz_script"
for t in $TARGETS; do
    # shellcheck disable=SC2086
    $CC -O1 -std=gnu99 $SANITIZER_FLAGS -fsanitize=fuzzer-no-link $DEBUG_FLAGS -Iinclude -Dflecs_STATIC \
        -c "mayhem/$t.c" -o "out/fuzz/$t.o"
    # shellcheck disable=SC2086
    $CC $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "out/fuzz/$t.o" out/fuzz/lsan_off.o \
        out/fuzz/libflecs.a -lm -lpthread -o "/mayhem/$t"
    # shellcheck disable=SC2086
    $CC $SANITIZER_FLAGS $DEBUG_FLAGS "out/fuzz/$t.o" out/fuzz/standalone_main.o out/fuzz/lsan_off.o \
        out/fuzz/libflecs.a -lm -lpthread -o "/mayhem/$t-standalone"
done
