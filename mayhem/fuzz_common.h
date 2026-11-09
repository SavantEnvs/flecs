/* Shared setup for the flecs harnesses: a quiet, file-system-free OS API and a
 * world with a few reflected components so scripts, JSON and queries have
 * something to resolve. Every input gets a fresh world. */
#ifndef MAYHEM_FUZZ_COMMON_H
#define MAYHEM_FUZZ_COMMON_H

#include <flecs.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct Position { float x, y; } Position;
typedef struct Velocity { float x, y; } Velocity;
typedef struct Named { ecs_string_t label; int32_t values[3]; } Named;

static ECS_COMPONENT_DECLARE(Position);
static ECS_COMPONENT_DECLARE(Velocity);
static ECS_COMPONENT_DECLARE(Named);

static void mayhem_log(int32_t level, const char *file, int32_t line, const char *msg) {
    (void)level; (void)file; (void)line; (void)msg;
}

/* Embedders decide whether script/JSON loading may touch the file system; the
 * fuzz inputs must not depend on it. */
static FILE* mayhem_fopen(const char *file, const char *mode) {
    (void)file; (void)mode;
    return NULL;
}

static void mayhem_init_os(void) {
    ecs_os_set_api_defaults();
    ecs_os_api_t api = ecs_os_get_api();
    api.log_ = mayhem_log;
    api.fopen_ = mayhem_fopen;
    ecs_os_set_api(&api);
}

/* Flecs only formats parse-error messages while the log level is >= -2 (flecs_parser_errorv in
 * src/addons/log.c); the library itself drops to -3 around its own silent probes (src/id.c).
 * The harnesses discard every message, so they run at -3. Formatting a message for an expression
 * that starts with a newline reads one byte before the buffer, which every input of the form
 * <selector>"\n..." reaches: see known-findings/ in fuzz_expr and fuzz_json. */
static void mayhem_skip_error_formatting(void) {
    ecs_log_set_level(-3);
}

/* reflect=false: a bare world (ecs_mini + pipeline) for the core-API target, which needs
 * no reflection or script; the parsers get the full ecs_init() world. */
static ecs_world_t* mayhem_new_world_ex(bool reflect) {
    ecs_world_t *world;
    if (reflect) {
        world = ecs_init();
    } else {
        world = ecs_mini();
        ECS_IMPORT(world, FlecsPipeline);
    }

    ecs_id(Position) = ecs_component(world, {
        .entity = ecs_entity(world, {.name = "Position"}),
        .type.size = ECS_SIZEOF(Position), .type.alignment = ECS_ALIGNOF(Position)
    });
    if (reflect) ecs_struct(world, {
        .entity = ecs_id(Position),
        .members = {{.name = "x", .type = ecs_id(ecs_f32_t)}, {.name = "y", .type = ecs_id(ecs_f32_t)}}
    });

    ecs_id(Velocity) = ecs_component(world, {
        .entity = ecs_entity(world, {.name = "Velocity"}),
        .type.size = ECS_SIZEOF(Velocity), .type.alignment = ECS_ALIGNOF(Velocity)
    });
    if (reflect) ecs_struct(world, {
        .entity = ecs_id(Velocity),
        .members = {{.name = "x", .type = ecs_id(ecs_f32_t)}, {.name = "y", .type = ecs_id(ecs_f32_t)}}
    });

    ecs_id(Named) = ecs_component(world, {
        .entity = ecs_entity(world, {.name = "Named"}),
        .type.size = ECS_SIZEOF(Named), .type.alignment = ECS_ALIGNOF(Named)
    });
    if (reflect) ecs_struct(world, {
        .entity = ecs_id(Named),
        .members = {
            {.name = "label", .type = ecs_id(ecs_string_t)},
            {.name = "values", .type = ecs_id(ecs_i32_t), .count = 3}
        }
    });

    ecs_entity(world, {.name = "Tag"});
    ecs_entity(world, {.name = "Rel"});
    ecs_entity(world, {.name = "Parent"});
    ecs_entity(world, {.name = "Parent.Child"});
    return world;
}

static ecs_world_t* mayhem_new_world(void) {
    return mayhem_new_world_ex(true);
}

/* NUL-terminated copy of the input, freed by the caller. */
static char* mayhem_cstr(const uint8_t *data, size_t size) {
    char *s = (char*)malloc(size + 1);
    memcpy(s, data, size);
    s[size] = 0;
    return s;
}

#endif
