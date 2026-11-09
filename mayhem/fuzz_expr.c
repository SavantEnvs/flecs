/* Flecs script expressions: ecs_expr_run into a typed value. First byte
 * selects the destination type and whether constant folding is disabled. */
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
    char *expr = mayhem_cstr(data + 1, size - 1);

    ecs_world_t *world = mayhem_new_world();
    ecs_entity_t types[] = {
        0, ecs_id(ecs_i64_t), ecs_id(ecs_f64_t), ecs_id(ecs_string_t), ecs_id(ecs_bool_t),
        ecs_lookup(world, "Position"), ecs_lookup(world, "Named")
    };
    ecs_entity_t type = types[(sel >> 1) % (sizeof(types) / sizeof(types[0]))];

    ecs_script_vars_t *vars = ecs_script_vars_init(world);
    ecs_script_var_t *v = ecs_script_vars_declare(vars, "x");
    int64_t xval = 42;
    v->value.type = ecs_id(ecs_i64_t);
    v->value.ptr = &xval;

    ecs_expr_eval_desc_t desc = {
        .name = "fuzz", .expr = expr, .vars = vars, .type = type,
        .disable_folding = (sel & 1) != 0
    };

    ecs_value_t value = {0};
    if (type) {
        value = ecs_value_new(world, type);
    }
    ecs_expr_run(world, expr, &value, &desc);
    if (value.ptr) {
        /* ecs_value_new() memory is released by ecs_value_fini(); with type 0, ecs_expr_run()
         * allocates the result itself and documents ecs_ptr_free() for it (script.h). */
        if (type) {
            ecs_value_fini(world, &value);
        } else {
            ecs_ptr_free(world, value.type, value.ptr);
        }
    }

    ecs_script_vars_fini(vars);
    ecs_fini(world);
    free(expr);
    return 0;
}
