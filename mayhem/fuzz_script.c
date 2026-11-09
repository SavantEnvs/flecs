/* Flecs script: parse + evaluate a script (entities, components, templates,
 * expressions, for/if, variables). First byte selects the evaluator. */
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
    ecs_script_eval_desc_t desc = {.ir = (flags & 1) != 0, .skip_unknown = (flags & 2) != 0};
    ecs_script_eval_result_t result = {0};
    ecs_script_run_w_desc(world, "fuzz.flecs", code, &desc, &result);
    ecs_os_free(result.error);
    ecs_fini(world);

    free(code);
    return 0;
}
