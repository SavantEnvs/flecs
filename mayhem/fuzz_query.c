/* Flecs query DSL and id/path string parsers: ecs_query_init(.expr), then
 * iterate it; ecs_id_from_str; ecs_lookup/ecs_entity_from_path. */
#include "fuzz_common.h"

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; (void)argv;
    mayhem_init_os();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return -1;
    uint8_t sel = data[0];
    char *expr = mayhem_cstr(data + 1, size - 1);

    ecs_world_t *world = mayhem_new_world();
    ecs_entity_t pos = ecs_lookup(world, "Position");
    ecs_entity_t vel = ecs_lookup(world, "Velocity");
    ecs_entity_t tag = ecs_lookup(world, "Tag");
    ecs_entity_t rel = ecs_lookup(world, "Rel");
    ecs_entity_t parent = ecs_lookup(world, "Parent");

    ecs_entity_t a = ecs_entity(world, {.name = "a"});
    ecs_set(world, a, Position, {1, 2});
    ecs_add_id(world, a, tag);
    ecs_entity_t b = ecs_entity(world, {.name = "b"});
    ecs_set(world, b, Position, {3, 4});
    ecs_set(world, b, Velocity, {5, 6});
    ecs_add_pair(world, b, rel, a);
    ecs_add_pair(world, b, EcsChildOf, parent);
    ecs_entity_t c = ecs_entity(world, {.name = "c"});
    ecs_add_pair(world, c, EcsIsA, b);
    (void)vel;

    switch (sel % 3) {
    case 0: {
        ecs_query_t *q = ecs_query(world, {.expr = expr});
        if (q) {
            char *str = ecs_query_str(q);
            ecs_os_free(str);
            ecs_iter_t it = ecs_query_iter(world, q);
            while (ecs_query_next(&it)) {
                (void)it.count;
            }
            ecs_query_fini(q);
        }
        break;
    }
    case 1:
        ecs_id_from_str(world, expr);
        break;
    default:
        ecs_lookup(world, expr);
        ecs_lookup_path_w_sep(world, 0, expr, ".", NULL, true);
        ecs_entity(world, {.name = expr});
        break;
    }

    ecs_fini(world);
    free(expr);
    return 0;
}
