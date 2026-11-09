# ecs_world_from_json aborts / dereferences NULL / stores misaligned on invalid component contents (9 Mayhem defects, 4 root causes)

The 300s Mayhem run of `fuzz-json` (run 4) listed 9 defects (run 3 listed 6; the extra 3 are covered below).
None is a harness bug: every first app frame is in `src/`, reached through `ecs_world_from_json`
-> `flecs_json_deser_components`, which writes a component (or adds an id) and calls `ecs_modified_id`, whose
OnSet hooks/observers then assume the value is valid. Groups 1-3 are one family: **the JSON gives an entity
one of flecs' own reflection components (`flecs.core.Component`, `flecs.meta.type`, `flecs.meta.struct`) with
contents that `ecs_component()` / `ecs_struct()` would never produce, and nothing validates them before the
hooks run.** Group 4 is a separate root cause (a trait constraint), also reachable from the JSON alone.

| Group | Mayhem defect (first app frame) | Reproducer |
|---|---|---|
| 1. `EcsComponent` with a size/alignment mismatch | `flecs_type_info_init_id` `type_info.c:753` | `component-size-alignment-mismatch.bin` |
| 2. `EcsType` without the components it describes | `flecs_rtt_init_default_hooks` `rtt_lifecycle.c:873` (assert) and `:879` (NULL dereference) | `meta-type-without-backing-components.bin` |
| 3. `EcsStruct` member that is not a valid member | `ecs_get_id` `entity.c:1578`, `flecs_name_index_ensure` `name_index.c:225`, `flecs_type_size` `type_info.c:854`, `ecs_get_type_info` `type_info.c:908`, `flecs_meta_set_number` `cursor.c:1698` | `invalid-struct-member-type.bin`, `struct-member-type-zero.bin`, `struct-member-unaligned-offset.bin` |
| 4. `Singleton` component added to another entity | `flecs_on_singleton_add_remove` `constraint_traits.c:101` | `singleton-trait-on-other-entity.bin` |

Each `.bin` is the selector byte `\0` (`ecs_world_from_json`, non-strict) followed by the JSON shown below.
Run: `/mayhem/fuzz_json-standalone mayhem/fuzz_json/known-findings/<name>.bin`

## Group 1: size XOR alignment is zero (`type_info.c:753`)

```json
{"results":[{"name":"Foo","components":{"flecs.core.Component":{"size":8}}}]}
```

`flecs_on_component` forwards `size`/`alignment` of the new `EcsComponent` to `flecs_type_info_init_id`, which
asserts `size == 0 && alignment == 0` whenever either one is 0. `{"alignment":4}` alone does the same.

## Group 2: `flecs.meta.type` set with no backing component (`rtt_lifecycle.c:873`, `:879`)

```json
{"results":[{"name":"Foo","components":{"flecs.meta.type":{"kind":"StructType"}}}]}
```

The `EcsType` OnSet observer `flecs_rtt_init_default_hooks` does `ecs_get_type_info(...)` and asserts it is
non-NULL (line 873): `Foo` has no `EcsComponent`. With a valid `EcsComponent` but no `EcsStruct`,

```json
{"results":[{"name":"Foo","components":{"flecs.core.Component":{"size":8,"alignment":4},"flecs.meta.type":{"kind":"StructType"}}}]}
```

it reads `st->members` from `ecs_get(world, component, EcsStruct)` == NULL (line 879, UBSan "member access
within null pointer"). The `ArrayType`/`VectorType`/`MapType` branches (lines 881-889) read `EcsArray`/`EcsVector`/`EcsMap` the same way
(by code reading, not run separately).

## Group 3: struct member with no name, type 0, a non-component type or a misaligned offset (`serializer.c`, `cursor.c`)

`invalid-struct-member-type.bin` (array member whose type `Tag` is not a component, `type_info.c:854`):

```json
{"results":[{"name":"Foo","components":{"flecs.core.Component":{"size":8,"alignment":4},"flecs.meta.struct":{"members":[{"name":"x","type":"Tag","count":2}]},"flecs.meta.type":{"kind":"StructType"}}}]}
```

The other two sites need only a different member (same wrapper):

- member without `"name"` -> `flecs_name_index_ensure` `name_index.c:225` (`assert(name != NULL)`):
  `{"type":"flecs.meta.f32"}`
- member whose type resolves to entity 0 (or names nothing that exists) on a fresh type -> `ecs_get_type_info`
  `type_info.c:908` (`abort`), reached from the rtt observer `flecs_rtt_init_default_hooks` ->
  `flecs_rtt_struct_hooks` -> `flecs_rtt_struct_members` (`rtt_lifecycle.c:196`). `struct-member-type-zero.bin`:
  `{"results":[{"name":"Foo","components":{"flecs.core.Component":{"size":8,"alignment":4},"flecs.meta.struct":{"members":[{"name":"x","type":0}]},"flecs.meta.type":{"kind":"StructType"}}}]}`
- the same member type 0 -> `ecs_get_id` `entity.c:1578` (`serializer.c:92`) once the rtt observer no longer
  aborts first. Define a valid `Foo`, instantiate it, then redefine it with `{"name":"x","type":0}` in the
  same document:
  `{"results":[<Foo with members x,y: f32>,{"name":"e","components":{"Foo":{"x":1,"y":2}}},{"name":"Foo","components":{"flecs.meta.struct":{"members":[{"name":"x","type":0}]},"flecs.meta.type":{"kind":"StructType"}}}]}`

- member whose `offset` is not a multiple of its type's alignment -> `flecs_meta_set_number` `cursor.c:1698`
  (UBSan "store to misaligned address ... for type 'ecs_f32_t'"), from `ecs_ptr_from_json`
  (`deserialize.c:385`) when a later entity sets that component. This is **not** group 1: the `EcsComponent`
  is valid (`size 8`, `alignment 4`); the misalignment comes from the `EcsStruct` member's own `offset`
  (`"offset":1` on an `f32`), so it belongs here. `struct-member-unaligned-offset.bin`:
  `{"results":[{"name":"Foo","components":{"flecs.core.Component":{"size":8,"alignment":4},"flecs.meta.struct":{"members":[{"name":"x","type":"flecs.meta.f32","offset":1}]},"flecs.meta.TypeSerializer":null,"flecs.meta.type":{"kind":"StructType"}}},{"name":"e","components":{"Foo":{"x":1}}}]}`

`flecs_meta_type_serializer_init` assumes every member has a name, a type that is itself a reflected
component and an `offset` that fits that type; `ecs_struct()` would never produce an invalid one.

## Group 4: `Singleton` component added to an entity other than itself (`constraint_traits.c:101`)

`flecs_on_singleton_add_remove` is the OnAdd observer for the `Singleton` trait; it does
`ecs_check(component == e, ...)` ("singleton component must be added to itself"). The JSON adds ids to
entities with `ecs_add_id` (`deserialize.c:407`) without checking trait constraints. Reproducer
`singleton-trait-on-other-entity.bin`: mark `S` as a singleton, then list `S` in the `tags` of another entity:

```json
{"results":[{"name":"S","tags":["flecs.core.Singleton"]},{"name":"a","tags":["S"]}]}
```

(`{"name":"S","tags":["flecs.core.Singleton"]}` alone is fine, because `S` is then added to itself.) This is
not a reflection-component problem. It needs no harness state: it reproduces on a plain `ecs_init()` world
at the default log level through `ecs_world_from_json` alone, so it stays. In a `FLECS_NDEBUG` release build
`ecs_check` is a no-op, so the invalid singleton is silently accepted instead of aborting.

## Reachable by a real `ecs_world_from_json` caller

No group depends on state the harness builds. All of them, including the `entity.c:1578` document and the
Singleton one, reproduce
on a plain `ecs_init()` world at the default log level with only `ecs_world_from_json(world, json, &desc)`
(the harness's `Position`/`Named` components, its OS API and its log level are not involved; the documents
define their own `Foo`).

The flecs docs and `include/flecs/addons/json.h` do not say the JSON must be trusted: `ecs_world_from_json`
is documented as reading the format `ecs_world_to_json` writes, with no statement about untrusted input. So
these are valid findings against `ecs_world_from_json` as an input parser, with one caveat on severity: the
checks are `ecs_assert`s, which flecs compiles out when `NDEBUG`/`FLECS_NDEBUG` is set. This build runs with
asserts on (like a default flecs debug build). In a release build the same documents do not abort but reach
undefined behavior: group 1 is silently treated as a tag, group 2 dereferences NULL
(`rtt_lifecycle.c:874`/`:879`), group 3 dereferences NULL (`type_info.c:855`, `name_index.c:95`,
`table_cache.c:70`) or does the misaligned store (`cursor.c:1698`, harmless on x86, a fault on strict-alignment
CPUs), and group 4 is accepted silently.

## Not fixed upstream

The harness keeps every input as-is. The 9 defects are expected to keep showing in `fuzz-json`'s run until
upstream validates `flecs.core.Component`, `flecs.meta.type` and `flecs.meta.struct` values (and trait
constraints such as `Singleton`) loaded from JSON.

## Also: parse-error formatting is not fuzzed here

`fuzz_json` and `fuzz_expr` run at `ecs_log_set_level(-3)` (`mayhem_skip_error_formatting`), which avoids the
1-byte-before-buffer read in `flecs_parser_errorv` (`src/addons/log.c`, see
`leading-newline-parser-error.md`) but also means these two targets cannot find bugs in error-message
formatting. `fuzz_query` and `fuzz_script` run at the default level and call the same function, so they can
still hit it. Upstream `master` still has the code and I found no existing issue for it.

Suggested upstream report (not yet filed): in `flecs_parser_errorv`, `ptr --` when `ptr[0] == '\n'` runs before
the `ptr > expr` check, so with `column == 0` and `expr[0] == '\n'` the next `ptr[0]` reads `expr[-1]`.
`ecs_parser_error_("x", "\n", 0, "e")` at the default log level triggers it under ASan. Fix: guard the first
decrement with `ptr > expr`.
