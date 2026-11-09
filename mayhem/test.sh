#!/usr/bin/env bash
#
# mayhem/test.sh — RUN flecs' OWN functional test suites (already built by mayhem/build.sh).
# exit 0 = pass. EDIT per repo. PATCH-grade oracle: after an agent patches the source, the grader
# rebuilds (build.sh) then runs this. DELETE this file if the repo has no meaningful tests.
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

# Upstream's bake-driven suites (core, query, meta, addons, collections), built unsanitized by build.sh.
# Each runner ends with `PASS:<n>, FAIL:<n>, EMPTY:<n> (<suite>.all)`; the counts are taken from there,
# so a runner that exits early or prints nothing leaves its suite without a line and counts as failed.
T="$SRC/out/test"
passed=0; failed=0; skipped=0
for suite in core query meta addons collections; do
  bin="$T/test_$suite"
  if [ ! -x "$bin" ]; then
    echo "missing test runner $bin (mayhem/build.sh should have built it)" >&2
    failed=$((failed + 1)); continue
  fi
  log="$(mktemp)"
  timeout 1200 "$bin" > "$log" 2>&1; rc=$?
  line="$(sed 's/\x1b\[[0-9;]*m//g' "$log" | grep -E "^PASS: *[0-9]+, FAIL: *[0-9]+, EMPTY: *[0-9]+ \($suite\.all\)" | tail -1)"
  if [ -z "$line" ]; then
    echo "[$suite] no summary line (exit $rc)" >&2; tail -5 "$log" >&2
    failed=$((failed + 1)); rm -f "$log"; continue
  fi
  p="$(echo "$line" | sed -E 's/^PASS: *([0-9]+),.*/\1/')"
  f="$(echo "$line" | sed -E 's/.*FAIL: *([0-9]+),.*/\1/')"
  e="$(echo "$line" | sed -E 's/.*EMPTY: *([0-9]+) .*/\1/')"
  echo "[$suite] passed=$p failed=$f empty=$e (exit $rc)"
  if [ "$rc" -ne 0 ] && [ "$f" -eq 0 ]; then f=1; fi
  if [ "$p" -eq 0 ]; then f=$((f + 1)); fi
  passed=$((passed + p)); failed=$((failed + f)); skipped=$((skipped + e))
  rm -f "$log"
done

emit_ctrf "flecs-bake-tests" "$passed" "$failed" "$skipped"
