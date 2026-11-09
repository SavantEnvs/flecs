/* Flecs JSON deserializer: ecs_world_from_json / ecs_entity_from_json /
 * ecs_ptr_from_json. First byte selects the entry point and strict mode. */
#include "fuzz_common.h"

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; (void)argv;
    mayhem_init_os();
    mayhem_skip_error_formatting();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return -1;
    uint8_t sel = data[0];
    char *json = mayhem_cstr(data + 1, size - 1);

    ecs_world_t *world = mayhem_new_world();
    ecs_from_json_desc_t desc = {.name = "fuzz", .expr = json, .strict = (sel & 1) != 0};

    switch ((sel >> 1) % 4) {
    case 0:
        ecs_world_from_json(world, json, &desc);
        break;
    case 1: {
        ecs_entity_t e = ecs_new(world);
        ecs_entity_from_json(world, e, json, &desc);
        break;
    }
    case 2: {
        Position p = {0};
        ecs_ptr_from_json(world, ecs_id(ecs_f32_t) , &p, json, &desc);
        ecs_ptr_from_json(world, ecs_lookup(world, "Position"), &p, json, &desc);
        break;
    }
    default: {
        ecs_entity_t type = ecs_lookup(world, "Named");
        ecs_value_t v = ecs_value_new(world, type);
        ecs_ptr_from_json(world, type, v.ptr, json, &desc);
        ecs_value_fini(world, &v);
        break;
    }
    }

    ecs_fini(world);
    free(json);
    return 0;
}
