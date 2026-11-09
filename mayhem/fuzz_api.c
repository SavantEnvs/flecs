/* Flecs core API: a byte-driven sequence of valid entity/component/relationship
 * operations (create, delete, add, remove, pairs, ChildOf/IsA, names, clone,
 * deferred sections, progress). Entities and ids are chosen by index, so every
 * call uses live handles. flecs does not support cycles in IsA/ChildOf, so the harness
 * keeps its own edge model of those two relationships and never adds a cycle to it. */
#include "fuzz_common.h"

#define MAX_ENTITIES 24

typedef struct Cursor {
    const uint8_t *p, *end;
} Cursor;

static uint8_t next(Cursor *c) {
    return c->p < c->end ? *c->p++ : 0;
}

/* Edge model for ChildOf (0) and IsA (1): adj[r][a] has bit b set when ents[a] has the pair
 * (r, ents[b]). IsA is not exclusive; the bitmask holds every target. Outside a readonly
 * section it follows each operation exactly. Inside one, operations are merged later and
 * flecs batches the commands of an entity, so a removal may be applied after a later add:
 * there the model only gains edges (a superset of every intermediate state), and what the
 * section removed is dropped by later removals/deletes, never by the section itself. */
typedef struct Model {
    uint32_t adj[2][MAX_ENTITIES];
} Model;

static int model_rel(ecs_entity_t r) {
    return r == EcsChildOf ? 0 : r == EcsIsA ? 1 : -1;
}

/* Whether ents[to] is reachable from ents[from] along relationship r. Entities that are dead
 * have lost their pairs, so the walk does not go through them. */
static int reaches(const Model *m, ecs_world_t *world, const ecs_entity_t *ents,
    int r, int from, int to)
{
    uint32_t seen = 0, todo = 1u << from;
    while (todo) {
        int n = __builtin_ctz(todo);
        todo &= todo - 1;
        if (n == to) return 1;
        if (seen & (1u << n)) continue;
        seen |= 1u << n;
        if (!ecs_is_alive(world, ents[n])) continue;
        todo |= m->adj[r][n] & ~seen;
    }
    return 0;
}

static void model_forget(Model *m, int slot) {
    int r, i;
    for (r = 0; r < 2; r++) {
        m->adj[r][slot] = 0;
        for (i = 0; i < MAX_ENTITIES; i++) m->adj[r][i] &= ~(1u << slot);
    }
}

int LLVMFuzzerInitialize(int *argc, char ***argv) {
    (void)argc; (void)argv;
    mayhem_init_os();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 2) return -1;
    Cursor cur = {data, data + size};
    uint8_t setup = next(&cur);

    ecs_world_t *world = mayhem_new_world_ex(false);
    ecs_entity_t tag = ecs_lookup(world, "Tag");
    ecs_entity_t rel = ecs_lookup(world, "Rel");
    ecs_entity_t ids[4] = {tag, ecs_id(Position), ecs_id(Velocity), ecs_lookup(world, "Parent")};
    ecs_entity_t rels[3] = {rel, EcsChildOf, EcsIsA};

    if (setup & 1) ecs_add_id(world, tag, EcsDontFragment);
    if (setup & 2) ecs_add_id(world, ecs_id(Velocity), EcsDontFragment);
    if (setup & 4) ecs_add_pair(world, rel, EcsOnInstantiate, EcsInherit);
    if (setup & 8) ecs_add_id(world, ids[3], EcsOrderedChildren);

    ecs_world_t *w = world;
    ecs_entity_t ents[MAX_ENTITIES];
    int count = 0, names = 0, ro = 0, steps = 0;
    Model model;
    memset(&model, 0, sizeof(model));

    while (cur.p < cur.end && steps++ < 512) {
        uint8_t op = next(&cur) % 14;
        int es = count ? next(&cur) % count : 0;
        ecs_entity_t e = count ? ents[es] : 0;
        uint8_t arg = next(&cur);

        if (op == 0 || !count) {
            if (count < MAX_ENTITIES) {
                ents[count++] = (arg & 1) ? ecs_new(w) : ecs_new_w_id(w, ids[arg % 4]);
            }
            continue;
        }
        if (!ecs_is_alive(world, e)) continue;

        switch (op) {
        case 1:
            ecs_delete(w, e);
            if (!ro) model_forget(&model, es);
            break;
        case 2:
            ecs_add_id(w, e, ids[arg % 4]);
            break;
        case 3:
            ecs_remove_id(w, e, ids[arg % 4]);
            break;
        case 4: {
            int ts = next(&cur) % count;
            ecs_entity_t t = ents[ts];
            ecs_entity_t r = rels[arg % 3];
            int mr = model_rel(r);
            if (!ecs_is_alive(world, t) || ts == es) break;
            if (mr >= 0) {
                if (reaches(&model, world, ents, mr, ts, es)) break;
                if (mr == 0 && !ro) model.adj[0][es] = 0; /* ChildOf is exclusive */
                model.adj[mr][es] |= 1u << ts;
            }
            ecs_add_pair(w, e, r, t);
            break;
        }
        case 5: {
            int ts = next(&cur) % count;
            ecs_entity_t r = rels[arg % 3];
            if (ecs_is_alive(world, ents[ts])) {
                int mr = model_rel(r);
                if (mr >= 0 && !ro) model.adj[mr][es] &= ~(1u << ts);
                ecs_remove_pair(w, e, r, ents[ts]);
            }
            break;
        }
        case 6: {
            Position p = {(float)arg, (float)next(&cur)};
            ecs_set(w, e, Position, {p.x, p.y});
            break;
        }
        case 7: {
            char name[16];
            snprintf(name, sizeof(name), "n%d", names++);
            ecs_set_name(w, e, name);
            break;
        }
        case 8:
            ecs_enable(w, e, (arg & 1) != 0);
            break;
        case 9:
        case 10:
            /* Readonly section: operations go through stage 0 and are merged on end. */
            if (!ro) { ecs_readonly_begin(world, false); w = ecs_get_stage(world, 0); ro = 1; }
            else { ecs_readonly_end(world); w = world; ro = 0; }
            break;
        case 11:
            /* A deferred clone copies its source as of the merge, after later commands on the
             * source have been applied; the model cannot know those edges, so clone outside
             * readonly sections only. */
            if (!ro && count < MAX_ENTITIES) {
                model.adj[0][count] = model.adj[0][es];
                model.adj[1][count] = model.adj[1][es];
                ents[count++] = ecs_clone(w, 0, e, (arg & 1) != 0);
            }
            break;
        case 12:
            if (!ro) ecs_progress(world, 0);
            break;
        default:
            if (arg & 1) ecs_remove_all(w, ids[(arg >> 1) % 4]);
            else ecs_delete_with(w, ids[(arg >> 1) % 4]);
            break;
        }
    }

    if (ro) ecs_readonly_end(world);
    ecs_fini(world);
    return 0;
}
