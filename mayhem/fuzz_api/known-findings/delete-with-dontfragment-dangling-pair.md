# ecs_delete_with() on a DontFragment id leaves children with a dangling ChildOf/IsA pair

Reproducer: `delete-with-dontfragment-dangling-pair.bin`.
Run: `/mayhem/fuzz_api-standalone mayhem/fuzz_api/known-findings/delete-with-dontfragment-dangling-pair.bin`
(aborts with `assert(tgt != 0)` at src/storage/table.c:346, `flecs_table_init_flags`).

Sequence: `Tag` is made `DontFragment`; entity `A` is created with `Tag`; entity `B` is added as
`(ChildOf, A)`; `ecs_delete_with(world, Tag)` deletes `A`. `B` is still alive, but the cascade for
`(ChildOf, A)` did not run, so `B` keeps a pair whose target is dead. The next table that contains the pair
(here `ecs_set_name(B)`) asserts that the target is alive. The same input with `Tag` not `DontFragment`
(first byte `61` -> `60`) does not crash. Other crashes of this family reach `flecs_table_traversable_add`
(`traversable_count >= 0`) and `flecs_table_new_edge` (`target of IsA pair is not alive`) through the same
dangling pair, and the IsA variant also runs inside a readonly section.

This is a flecs bug reached by valid API use (no hierarchy cycle, live handles only), so the harness keeps it.
