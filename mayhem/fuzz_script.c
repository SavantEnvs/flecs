/* Flecs script: parse + evaluate a script (entities, components, templates,
 * expressions, for/if, variables). First byte bit 1 selects lenient mode. */
#include "fuzz_common.h"

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; (void)argv;
    mayhem_init_os();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return -1;
    uint8_t flags = data[0];
    char *code = mayhem_cstr(data + 1, size - 1);

    ecs_world_t *world = mayhem_new_world();
    ecs_script_eval_desc_t desc = {.lenient = (flags & 2) != 0};
    ecs_script_eval_result_t result = {0};
    ecs_script_t *script = ecs_script_parse(world, "fuzz.flecs", code, &desc, &result);
    if (script) {
        ecs_script_eval(script, &desc, &result);
        ecs_script_free(script);
    }
    ecs_os_free(result.error);
    ecs_fini(world);

    free(code);
    return 0;
}
