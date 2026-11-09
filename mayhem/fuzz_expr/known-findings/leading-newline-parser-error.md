# heap-buffer-overflow (read, 1 byte before the buffer) in flecs_parser_errorv

Reproducer: `leading-newline-parser-error.bin` (selector byte `\n`, then the expression `\n`).
Run: `/mayhem/fuzz_expr-standalone mayhem/fuzz_expr/known-findings/leading-newline-parser-error.bin`

`flecs_parser_errorv` (src/addons/log.c:583) computes `ptr = &expr[column]` to find the start of the
offending line. When the error is reported at column 0 and `expr[0] == '\n'`, it does `ptr--` before the
`ptr > expr` bounds check, so the next `ptr[0]` read is `expr[-1]`.

Any parse error at column 0 of an expression that starts with a newline reaches it (here the JSON
deserializer's "invalid json" / the expression parser's lhs error). That made nearly every short input
`<selector>\n...` crash the target, including Mayhem's own `-runs=5` start-up probe, so the target never
started fuzzing (0 edges).

Not fixed upstream. The harness keeps every input as-is and instead runs at `ecs_log_set_level(-3)`
(`mayhem_skip_error_formatting` in `mayhem/fuzz_common.h`): flecs only formats a parse-error message at
level >= -2, and the library itself drops to -3 around its own silent probes (src/id.c). The harness
discards every message anyway. As a result the `.bin` above no longer crashes `fuzz_expr-standalone`;
to reproduce, call `ecs_parser_error_("x", "\n", 0, "e")` on a world with the default log level.

Caveat: `fuzz_query` and `fuzz_script` still run at the default log level and call the same function, so they
can still hit this bug. Upstream `master` still has the code; no upstream issue is filed yet (suggested report text:
`fuzz_json/known-findings/invalid-reflection-components-from-json.md`, last section).
