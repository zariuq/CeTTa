#include "gslt_compiled_runtime.h"

#include "gslt_dense_bitset_v1.h"
#include "match.h"
#include "native_sha256.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    GSLT_PLAN_SYMBOL = 1,
    GSLT_PLAN_VARIABLE = 2,
    GSLT_PLAN_STRING = 3,
    GSLT_PLAN_INTEGER = 4,
    GSLT_PLAN_APPLICATION = 5,
    GSLT_PLAN_DEPTH_LIMIT = 4096,
    GSLT_DISPATCH_OTHER = 255,
};

typedef struct {
    uint8_t kind;
    uint32_t child_offset;
    uint32_t child_count;
    int64_t integer;
    uint32_t variable;
    char *text;
    SymbolId symbol;
    Atom *ground_atom;
    uint32_t ground_node_count;
} GsltCompiledNode;

typedef struct {
    uint32_t head;
    uint32_t body_offset;
    uint32_t body_count;
    uint32_t variable_count;
    char *name;
    bool flat_variable_head;
} GsltCompiledRule;

typedef struct {
    uint8_t kind;
    SymbolId symbol;
    uint32_t arity;
    int64_t integer;
    const char *text;
} GsltCompiledDispatchKey;

typedef struct {
    GsltCompiledDispatchKey key;
    uint32_t *positions;
    uint32_t position_count;
    uint32_t position_cap;
} GsltCompiledDispatchGroup;

typedef struct {
    GsltCompiledDispatchKey key;
    uint32_t count;
    bool occupied;
} GsltCompiledDispatchCountSlot;

typedef struct {
    SymbolId head;
    uint32_t arity;
    uint32_t *rules;
    uint32_t rule_count;
    uint32_t rule_cap;
    uint32_t dispatch_child;
    uint32_t *dispatch_wildcards;
    uint32_t dispatch_wildcard_count;
    uint32_t dispatch_wildcard_cap;
    GsltCompiledDispatchGroup *dispatch_groups;
    uint32_t dispatch_group_count;
    uint32_t dispatch_group_cap;
} GsltCompiledBucket;

struct CettaGsltCompiledProgram {
    uint64_t owner_symbols_instance_id;
    Arena ground_arena;
    GsltCompiledNode *nodes;
    uint32_t node_count;
    uint32_t *children;
    uint32_t child_count;
    GsltCompiledRule *rules;
    uint32_t rule_count;
    uint32_t *bodies;
    uint32_t body_count;
    GsltCompiledBucket *buckets;
    uint32_t bucket_count;
    uint32_t bucket_cap;
    uint32_t *bucket_slots;
    size_t bucket_slot_cap;
    uint64_t bucket_insert_collisions;
    size_t bucket_max_probe;
    size_t dispatch_scratch_slot_capacity;
    size_t dispatch_scratch_allocation_count;
    uint32_t max_variable_count;
    uint32_t variable_support_slot_capacity;
    size_t variable_support_word_capacity;
    size_t variable_support_allocation_count;
    uint32_t flat_variable_head_rule_count;
    uint32_t ground_cached_plan_node_count;
    uint64_t ground_cached_value_node_count;
};

typedef struct {
    const uint8_t *cursor;
    const uint8_t *end;
} GsltPlanReader;

typedef struct {
    Arena arena;
    Atom *query;
    Atom **goals;
    uint32_t goal_count;
    uint32_t depth;
} GsltCompiledState;

typedef struct {
    GsltCompiledState **items;
    size_t head;
    size_t count;
    size_t cap;
} GsltCompiledQueue;

static bool compiled_error(char *buffer, size_t size,
                           const char *format, ...) {
    if (buffer && size > 0u) {
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(buffer, size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool plan_read_u8(GsltPlanReader *reader, uint8_t *value) {
    if (!reader || !value || reader->cursor == reader->end)
        return false;
    *value = *reader->cursor++;
    return true;
}

static bool plan_read_u32(GsltPlanReader *reader, uint32_t *value) {
    if (!reader || !value || (size_t)(reader->end - reader->cursor) < 4u)
        return false;
    const uint8_t *bytes = reader->cursor;
    *value = (uint32_t)bytes[0] |
             ((uint32_t)bytes[1] << 8u) |
             ((uint32_t)bytes[2] << 16u) |
             ((uint32_t)bytes[3] << 24u);
    reader->cursor += 4u;
    return true;
}

static bool plan_read_text(GsltPlanReader *reader, char **text) {
    uint32_t length;
    if (!plan_read_u32(reader, &length) ||
        (size_t)(reader->end - reader->cursor) < length ||
        memchr(reader->cursor, 0, length))
        return false;
    char *copy = cetta_malloc((size_t)length + 1u);
    memcpy(copy, reader->cursor, length);
    copy[length] = '\0';
    reader->cursor += length;
    *text = copy;
    return true;
}

static bool compiled_u32_list_add(
    uint32_t **items, uint32_t *count, uint32_t *capacity,
    uint32_t value) {
    if (*count == *capacity) {
        uint32_t next = *capacity ? *capacity * 2u : 8u;
        if (next < *capacity)
            return false;
        *items = cetta_realloc(*items, sizeof(**items) * next);
        *capacity = next;
    }
    (*items)[(*count)++] = value;
    return true;
}

static bool compiled_bucket_add(GsltCompiledBucket *bucket,
                                uint32_t rule) {
    return compiled_u32_list_add(
        &bucket->rules, &bucket->rule_count, &bucket->rule_cap, rule);
}

static GsltCompiledBucket *compiled_bucket(
    CettaGsltCompiledProgram *program, SymbolId head, uint32_t arity,
    bool create) {
    for (uint32_t index = 0u; index < program->bucket_count; index++) {
        GsltCompiledBucket *bucket = &program->buckets[index];
        if (bucket->arity == arity && bucket->head == head)
            return bucket;
    }
    if (!create)
        return NULL;
    if (program->bucket_count == program->bucket_cap) {
        uint32_t next = program->bucket_cap ? program->bucket_cap * 2u : 16u;
        if (next < program->bucket_cap)
            return NULL;
        program->buckets = cetta_realloc(
            program->buckets, sizeof(*program->buckets) * next);
        memset(program->buckets + program->bucket_cap, 0,
               sizeof(*program->buckets) * (next - program->bucket_cap));
        program->bucket_cap = next;
    }
    GsltCompiledBucket *bucket = &program->buckets[program->bucket_count++];
    bucket->head = head;
    bucket->arity = arity;
    bucket->dispatch_child = UINT32_MAX;
    return bucket;
}

static bool compiled_bind_symbols(
    CettaGsltCompiledProgram *program,
    char *error, size_t error_size) {
    if (!program || !g_symbols)
        return compiled_error(error, error_size,
                              "compiled GSLT symbol table is unavailable");
    program->owner_symbols_instance_id =
        symbol_table_instance_id(g_symbols);
    if (program->owner_symbols_instance_id == 0u)
        return compiled_error(error, error_size,
                              "compiled GSLT symbol table has no identity");
    for (uint32_t index = 0u; index < program->node_count; index++) {
        GsltCompiledNode *node = &program->nodes[index];
        if (node->kind != GSLT_PLAN_SYMBOL &&
            node->kind != GSLT_PLAN_APPLICATION)
            continue;
        node->symbol = symbol_intern_cstr(g_symbols, node->text);
        if (node->symbol == SYMBOL_ID_NONE)
            return compiled_error(error, error_size,
                                  "cannot intern compiled GSLT symbol");
    }
    return true;
}

static bool compiled_symbols_current(
    const CettaGsltCompiledProgram *program) {
    return program && g_symbols &&
        program->owner_symbols_instance_id != 0u &&
        program->owner_symbols_instance_id ==
            symbol_table_instance_id(g_symbols);
}

static bool compiled_validate_nodes(
    const CettaGsltCompiledProgram *program,
    char *error, size_t error_size) {
    uint32_t *depths = cetta_malloc(
        sizeof(*depths) * program->node_count);
    uint8_t *child_slots = program->child_count
        ? cetta_malloc(program->child_count) : NULL;
    if (child_slots)
        memset(child_slots, 0, program->child_count);
    bool valid = true;
    for (uint32_t node_index = 0u;
         valid && node_index < program->node_count; node_index++) {
        const GsltCompiledNode *node = &program->nodes[node_index];
        depths[node_index] = 0u;
        switch (node->kind) {
        case GSLT_PLAN_SYMBOL:
            valid = node->text && node->text[0] != '\0' &&
                node->child_offset == 0u && node->child_count == 0u &&
                node->integer == 0 && node->variable == 0u;
            break;
        case GSLT_PLAN_VARIABLE:
            valid = node->text && node->text[0] == '\0' &&
                node->child_offset == 0u && node->child_count == 0u &&
                node->integer == 0;
            break;
        case GSLT_PLAN_STRING:
            valid = node->text && node->child_offset == 0u &&
                node->child_count == 0u && node->integer == 0 &&
                node->variable == 0u;
            break;
        case GSLT_PLAN_INTEGER:
            valid = node->text && node->text[0] == '\0' &&
                node->child_offset == 0u && node->child_count == 0u &&
                node->variable == 0u;
            break;
        case GSLT_PLAN_APPLICATION:
            valid = node->text && node->text[0] != '\0' &&
                node->integer == 0 && node->variable == 0u &&
                node->child_offset <= program->child_count &&
                node->child_count <=
                    program->child_count - node->child_offset;
            if (!valid)
                break;
            depths[node_index] = 1u;
            for (uint32_t index = 0u;
                 valid && index < node->child_count; index++) {
                uint32_t slot = node->child_offset + index;
                uint32_t child = program->children[slot];
                valid = !child_slots[slot] && child < node_index;
                if (!valid)
                    break;
                child_slots[slot] = 1u;
                uint32_t candidate_depth = depths[child] + 1u;
                if (candidate_depth > depths[node_index])
                    depths[node_index] = candidate_depth;
                valid = depths[node_index] <= GSLT_PLAN_DEPTH_LIMIT;
            }
            break;
        default:
            valid = false;
            break;
        }
    }
    for (uint32_t slot = 0u;
         valid && slot < program->child_count; slot++)
        valid = child_slots[slot] != 0u;
    free(child_slots);
    free(depths);
    if (!valid)
        return compiled_error(error, error_size,
                              "compiled GSLT node table is malformed");
    return true;
}

static bool compiled_validate_rule_forest(
    CettaGsltCompiledProgram *program,
    char *error, size_t error_size) {
    uint8_t *claimed = cetta_malloc(program->node_count);
    memset(claimed, 0, program->node_count);
    uint8_t *body_slots = program->body_count
        ? cetta_malloc(program->body_count) : NULL;
    if (body_slots)
        memset(body_slots, 0, program->body_count);
    size_t stack_capacity = (size_t)program->child_count +
        (size_t)program->body_count + (size_t)program->rule_count;
    uint32_t *stack = cetta_malloc(sizeof(*stack) * stack_capacity);
    CettaGsltDenseBitsetV1 variable_support = {0};
    bool valid = true;
    uint32_t support_slot_capacity = 0u;
    for (uint32_t rule_index = 0u;
         valid && rule_index < program->rule_count; rule_index++) {
        uint32_t variable_count = program->rules[rule_index].variable_count;
        valid = variable_count <= program->node_count;
        if (valid && variable_count > support_slot_capacity)
            support_slot_capacity = variable_count;
    }
    valid = valid && cetta_gslt_dense_bitset_init_v1(
                         &variable_support, support_slot_capacity);
    program->variable_support_slot_capacity = support_slot_capacity;
    program->variable_support_word_capacity = variable_support.word_len;
    program->variable_support_allocation_count =
        variable_support.word_len != 0u ? 1u : 0u;
    for (uint32_t rule_index = 0u;
         valid && rule_index < program->rule_count; rule_index++) {
        const GsltCompiledRule *rule = &program->rules[rule_index];
        if (!rule->name || rule->name[0] == '\0' ||
            rule->head >= program->node_count ||
            rule->variable_count > program->node_count ||
            rule->body_offset > program->body_count ||
            rule->body_count > program->body_count - rule->body_offset ||
            program->nodes[rule->head].kind != GSLT_PLAN_APPLICATION) {
            valid = false;
            break;
        }
        for (uint32_t earlier = 0u; earlier < rule_index; earlier++)
            if (strcmp(rule->name, program->rules[earlier].name) == 0) {
                valid = false;
                break;
            }
        cetta_gslt_dense_bitset_clear_v1(&variable_support);
        size_t stack_count = 0u;
        if (valid)
            stack[stack_count++] = rule->head;
        for (uint32_t body = 0u;
             valid && body < rule->body_count; body++) {
            uint32_t slot = rule->body_offset + body;
            uint32_t node = program->bodies[slot];
            valid = !body_slots[slot] && node < program->node_count &&
                program->nodes[node].kind == GSLT_PLAN_APPLICATION;
            if (valid) {
                body_slots[slot] = 1u;
                stack[stack_count++] = node;
            }
        }
        while (valid && stack_count > 0u) {
            uint32_t node_index = stack[--stack_count];
            if (claimed[node_index]) {
                valid = false;
                break;
            }
            claimed[node_index] = 1u;
            const GsltCompiledNode *node = &program->nodes[node_index];
            if (node->kind == GSLT_PLAN_VARIABLE) {
                valid = node->variable < rule->variable_count;
                if (valid) {
                    bool changed;
                    valid = cetta_gslt_dense_bitset_set_v1(
                        &variable_support, node->variable, &changed);
                }
            } else if (node->kind == GSLT_PLAN_APPLICATION) {
                for (uint32_t child = 0u; child < node->child_count; child++)
                    stack[stack_count++] = program->children[
                        node->child_offset + child];
            }
        }
        if (valid) {
            bool full;
            valid = cetta_gslt_dense_bitset_prefix_full_v1(
                        &variable_support, rule->variable_count, &full) &&
                    full;
        }
    }
    for (uint32_t node = 0u; valid && node < program->node_count; node++)
        valid = claimed[node] != 0u;
    for (uint32_t slot = 0u; valid && slot < program->body_count; slot++)
        valid = body_slots[slot] != 0u;
    free(stack);
    free(body_slots);
    free(claimed);
    cetta_gslt_dense_bitset_free_v1(&variable_support);
    if (!valid)
        return compiled_error(error, error_size,
                              "compiled GSLT rule forest is malformed");
    return true;
}

static void compiled_analyze_rule_shapes(CettaGsltCompiledProgram *program) {
    for (uint32_t rule_index = 0u;
         rule_index < program->rule_count; rule_index++) {
        GsltCompiledRule *rule = &program->rules[rule_index];
        const GsltCompiledNode *head = &program->nodes[rule->head];
        bool flat = head->kind == GSLT_PLAN_APPLICATION;
        for (uint32_t index = 0u; flat && index < head->child_count; index++) {
            uint32_t child = program->children[head->child_offset + index];
            flat = program->nodes[child].kind == GSLT_PLAN_VARIABLE;
        }
        rule->flat_variable_head = flat;
        if (flat)
            program->flat_variable_head_rule_count++;
        if (rule->variable_count > program->max_variable_count)
            program->max_variable_count = rule->variable_count;
    }
}

static bool compiled_dispatch_key_equal(
    const GsltCompiledDispatchKey *left,
    const GsltCompiledDispatchKey *right) {
    if (!left || !right || left->kind != right->kind)
        return false;
    switch (left->kind) {
    case GSLT_PLAN_SYMBOL:
        return left->symbol == right->symbol;
    case GSLT_PLAN_STRING:
        return left->text && right->text &&
            strcmp(left->text, right->text) == 0;
    case GSLT_PLAN_INTEGER:
        return left->integer == right->integer;
    case GSLT_PLAN_APPLICATION:
        return left->symbol == right->symbol &&
            left->arity == right->arity;
    case GSLT_DISPATCH_OTHER:
        return true;
    default:
        return false;
    }
}

static uint64_t compiled_dispatch_key_hash(
    const GsltCompiledDispatchKey *key) {
    uint64_t value = key ? key->kind : 0u;
    if (!key)
        return value;
    switch (key->kind) {
    case GSLT_PLAN_SYMBOL:
        value ^= (uint64_t)key->symbol << 17u;
        break;
    case GSLT_PLAN_STRING:
        if (key->text)
            for (const unsigned char *cursor =
                     (const unsigned char *)key->text;
                 *cursor; cursor++) {
                value ^= *cursor;
                value *= UINT64_C(1099511628211);
            }
        break;
    case GSLT_PLAN_INTEGER:
        value ^= (uint64_t)key->integer;
        break;
    case GSLT_PLAN_APPLICATION:
        value ^= ((uint64_t)key->symbol << 32u) | key->arity;
        break;
    default:
        break;
    }
    value ^= value >> 30u;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27u;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31u);
}

static bool compiled_rule_dispatch_key(
    const CettaGsltCompiledProgram *program,
    const GsltCompiledRule *rule, uint32_t child,
    GsltCompiledDispatchKey *key) {
    if (!program || !rule || !key || rule->head >= program->node_count)
        return false;
    const GsltCompiledNode *head = &program->nodes[rule->head];
    if (head->kind != GSLT_PLAN_APPLICATION || child >= head->child_count)
        return false;
    uint32_t node_index = program->children[head->child_offset + child];
    if (node_index >= program->node_count)
        return false;
    const GsltCompiledNode *node = &program->nodes[node_index];
    memset(key, 0, sizeof(*key));
    key->kind = node->kind;
    switch (node->kind) {
    case GSLT_PLAN_VARIABLE:
        return false;
    case GSLT_PLAN_SYMBOL:
        key->symbol = node->symbol;
        return true;
    case GSLT_PLAN_STRING:
        key->text = node->text;
        return true;
    case GSLT_PLAN_INTEGER:
        key->integer = node->integer;
        return true;
    case GSLT_PLAN_APPLICATION:
        key->symbol = node->symbol;
        key->arity = node->child_count;
        return true;
    default:
        return false;
    }
}

static uint64_t compiled_u64_add_sat(uint64_t left, uint64_t right) {
    return UINT64_MAX - left < right ? UINT64_MAX : left + right;
}

static uint64_t compiled_u64_mul_sat(uint64_t left, uint64_t right) {
    return left != 0u && right > UINT64_MAX / left
        ? UINT64_MAX : left * right;
}

/* Construct every substitution-independent node once in the immutable
 * program region.  Child indices precede their parents in the validated plan,
 * so one forward pass is both the groundness analysis and the cache builder. */
static bool compiled_build_ground_cache(
    CettaGsltCompiledProgram *program,
    char *error, size_t error_size) {
    if (!program)
        return compiled_error(error, error_size,
                              "compiled GSLT ground cache has no program");
    for (uint32_t index = 0u; index < program->node_count; index++) {
        GsltCompiledNode *node = &program->nodes[index];
        Atom *cached = NULL;
        uint64_t node_count = 1u;
        switch (node->kind) {
        case GSLT_PLAN_SYMBOL:
            cached = atom_symbol_id(&program->ground_arena, node->symbol);
            break;
        case GSLT_PLAN_STRING:
            cached = atom_string(&program->ground_arena, node->text);
            break;
        case GSLT_PLAN_INTEGER:
            cached = atom_int(&program->ground_arena, node->integer);
            break;
        case GSLT_PLAN_VARIABLE:
            continue;
        case GSLT_PLAN_APPLICATION: {
            bool ground = true;
            for (uint32_t child = 0u;
                 ground && child < node->child_count; child++) {
                const GsltCompiledNode *compiled_child =
                    &program->nodes[program->children[
                        node->child_offset + child]];
                ground = compiled_child->ground_atom != NULL;
                if (ground) {
                    node_count = compiled_u64_add_sat(
                        node_count, compiled_child->ground_node_count);
                }
            }
            if (!ground)
                continue;
            Atom **elements = arena_alloc(
                &program->ground_arena,
                sizeof(*elements) * (size_t)(node->child_count + 1u));
            elements[0] = atom_symbol_id(
                &program->ground_arena, node->symbol);
            if (!elements[0])
                return compiled_error(
                    error, error_size,
                    "cannot construct compiled GSLT ground head");
            for (uint32_t child = 0u; child < node->child_count; child++)
                elements[child + 1u] =
                    program->nodes[program->children[
                        node->child_offset + child]].ground_atom;
            cached = atom_expr(
                &program->ground_arena, elements,
                (CettaExprLen)(node->child_count + 1u));
            break;
        }
        default:
            return compiled_error(error, error_size,
                                  "compiled GSLT ground cache saw an unknown node");
        }
        if (!cached)
            return compiled_error(error, error_size,
                                  "cannot construct compiled GSLT ground cache");
        node->ground_atom = cached;
        node->ground_node_count = node_count > UINT32_MAX
            ? UINT32_MAX : (uint32_t)node_count;
        program->ground_cached_plan_node_count++;
        program->ground_cached_value_node_count = compiled_u64_add_sat(
            program->ground_cached_value_node_count, node_count);
    }
    return true;
}

static uint32_t compiled_bucket_dispatch_child(
    const CettaGsltCompiledProgram *program,
    const GsltCompiledBucket *bucket,
    GsltCompiledDispatchCountSlot *counts, size_t capacity) {
    uint32_t best_child = UINT32_MAX;
    uint64_t best_score = 0u;
    if (!bucket || !counts || bucket->rule_count == 0u ||
        (size_t)bucket->rule_count > SIZE_MAX / 2u ||
        capacity < (size_t)bucket->rule_count * 2u)
        return best_child;
    for (uint32_t child = 0u; child < bucket->arity; child++) {
        memset(counts, 0, sizeof(*counts) * capacity);
        uint64_t rigid = 0u;
        uint64_t separated_pairs = 0u;
        for (uint32_t position = 0u;
             position < bucket->rule_count; position++) {
            GsltCompiledDispatchKey key;
            const GsltCompiledRule *rule =
                &program->rules[bucket->rules[position]];
            if (!compiled_rule_dispatch_key(
                    program, rule, child, &key))
                continue;
            size_t slot = (size_t)compiled_dispatch_key_hash(&key) &
                (capacity - 1u);
            while (counts[slot].occupied &&
                   !compiled_dispatch_key_equal(&counts[slot].key, &key))
                slot = (slot + 1u) & (capacity - 1u);
            uint64_t equal = counts[slot].occupied
                ? counts[slot].count : 0u;
            separated_pairs = compiled_u64_add_sat(
                separated_pairs, rigid - equal);
            if (!counts[slot].occupied) {
                counts[slot].key = key;
                counts[slot].occupied = true;
            }
            counts[slot].count++;
            rigid++;
        }
        uint64_t score = compiled_u64_add_sat(
            compiled_u64_mul_sat(
                separated_pairs, (uint64_t)bucket->rule_count + 1u),
            rigid);
        if (score > best_score) {
            best_score = score;
            best_child = child;
        }
    }
    return best_child;
}

static GsltCompiledDispatchGroup *compiled_dispatch_group(
    GsltCompiledBucket *bucket,
    const GsltCompiledDispatchKey *key, bool create) {
    for (uint32_t index = 0u;
         index < bucket->dispatch_group_count; index++) {
        GsltCompiledDispatchGroup *group =
            &bucket->dispatch_groups[index];
        if (compiled_dispatch_key_equal(&group->key, key))
            return group;
    }
    if (!create)
        return NULL;
    if (bucket->dispatch_group_count == bucket->dispatch_group_cap) {
        uint32_t next = bucket->dispatch_group_cap
            ? bucket->dispatch_group_cap * 2u : 4u;
        if (next < bucket->dispatch_group_cap)
            return NULL;
        bucket->dispatch_groups = cetta_realloc(
            bucket->dispatch_groups,
            sizeof(*bucket->dispatch_groups) * next);
        memset(bucket->dispatch_groups + bucket->dispatch_group_cap, 0,
               sizeof(*bucket->dispatch_groups) *
                   (next - bucket->dispatch_group_cap));
        bucket->dispatch_group_cap = next;
    }
    GsltCompiledDispatchGroup *group =
        &bucket->dispatch_groups[bucket->dispatch_group_count++];
    group->key = *key;
    return group;
}

static bool compiled_build_bucket_dispatch(
    const CettaGsltCompiledProgram *program,
    GsltCompiledBucket *bucket,
    GsltCompiledDispatchCountSlot *counts, size_t capacity) {
    bucket->dispatch_child = compiled_bucket_dispatch_child(
        program, bucket, counts, capacity);
    if (bucket->dispatch_child == UINT32_MAX)
        return true;
    for (uint32_t position = 0u;
         position < bucket->rule_count; position++) {
        const GsltCompiledRule *rule =
            &program->rules[bucket->rules[position]];
        GsltCompiledDispatchKey key;
        if (!compiled_rule_dispatch_key(
                program, rule, bucket->dispatch_child, &key)) {
            if (!compiled_u32_list_add(
                    &bucket->dispatch_wildcards,
                    &bucket->dispatch_wildcard_count,
                    &bucket->dispatch_wildcard_cap, position))
                return false;
            for (uint32_t group_index = 0u;
                 group_index < bucket->dispatch_group_count; group_index++) {
                GsltCompiledDispatchGroup *group =
                    &bucket->dispatch_groups[group_index];
                if (!compiled_u32_list_add(
                        &group->positions, &group->position_count,
                        &group->position_cap, position))
                    return false;
            }
            continue;
        }
        GsltCompiledDispatchGroup *group =
            compiled_dispatch_group(bucket, &key, false);
        if (!group) {
            group = compiled_dispatch_group(bucket, &key, true);
            if (!group)
                return false;
            for (uint32_t wildcard = 0u;
                 wildcard < bucket->dispatch_wildcard_count; wildcard++)
                if (!compiled_u32_list_add(
                        &group->positions, &group->position_count,
                        &group->position_cap,
                        bucket->dispatch_wildcards[wildcard]))
                    return false;
        }
        if (!compiled_u32_list_add(
                &group->positions, &group->position_count,
                &group->position_cap, position))
            return false;
    }
    for (uint32_t group_index = 0u;
         group_index < bucket->dispatch_group_count; group_index++) {
        const GsltCompiledDispatchGroup *group =
            &bucket->dispatch_groups[group_index];
        for (uint32_t index = 0u; index < group->position_count; index++)
            if (group->positions[index] >= bucket->rule_count ||
                (index > 0u && group->positions[index - 1u] >=
                    group->positions[index]))
                return false;
    }
    return true;
}

static uint64_t compiled_bucket_hash(SymbolId head, uint32_t arity) {
    uint64_t value = ((uint64_t)head << 32u) | (uint64_t)arity;
    value ^= value >> 30u;
    value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27u;
    value *= UINT64_C(0x94d049bb133111eb);
    return value ^ (value >> 31u);
}

static const GsltCompiledBucket *compiled_bucket_index_lookup(
    const CettaGsltCompiledProgram *program,
    SymbolId head, uint32_t arity) {
    if (!program || !program->bucket_slots ||
        program->bucket_slot_cap == 0u)
        return NULL;
    size_t slot = (size_t)compiled_bucket_hash(head, arity) &
        (program->bucket_slot_cap - 1u);
    for (size_t probe = 0u; probe < program->bucket_slot_cap; probe++) {
        uint32_t stored = program->bucket_slots[slot];
        if (stored == UINT32_MAX)
            return NULL;
        const GsltCompiledBucket *bucket = &program->buckets[stored];
        if (bucket->head == head && bucket->arity == arity)
            return bucket;
        slot = (slot + 1u) & (program->bucket_slot_cap - 1u);
    }
    return NULL;
}

static bool compiled_index_buckets(
    CettaGsltCompiledProgram *program,
    char *error, size_t error_size) {
    size_t required = (size_t)program->bucket_count * 2u;
    size_t capacity = 16u;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u)
            return compiled_error(error, error_size,
                                  "compiled GSLT bucket index is too large");
        capacity *= 2u;
    }
    if (capacity > SIZE_MAX / sizeof(*program->bucket_slots))
        return compiled_error(error, error_size,
                              "compiled GSLT bucket index exceeds host size");
    program->bucket_slots = cetta_malloc(
        sizeof(*program->bucket_slots) * capacity);
    program->bucket_slot_cap = capacity;
    for (size_t slot = 0u; slot < capacity; slot++)
        program->bucket_slots[slot] = UINT32_MAX;
    for (uint32_t index = 0u; index < program->bucket_count; index++) {
        const GsltCompiledBucket *bucket = &program->buckets[index];
        size_t slot = (size_t)compiled_bucket_hash(
            bucket->head, bucket->arity) & (capacity - 1u);
        size_t probe = 0u;
        while (program->bucket_slots[slot] != UINT32_MAX &&
               probe < capacity) {
            program->bucket_insert_collisions++;
            slot = (slot + 1u) & (capacity - 1u);
            probe++;
        }
        if (probe == capacity)
            return compiled_error(error, error_size,
                                  "compiled GSLT bucket index is full");
        if (probe > program->bucket_max_probe)
            program->bucket_max_probe = probe;
        program->bucket_slots[slot] = index;
    }
    for (uint32_t index = 0u; index < program->bucket_count; index++) {
        const GsltCompiledBucket *bucket = &program->buckets[index];
        if (compiled_bucket_index_lookup(
                program, bucket->head, bucket->arity) != bucket)
            return compiled_error(error, error_size,
                                  "compiled GSLT bucket index is inconsistent");
    }
    return true;
}

static bool compiled_build_buckets(CettaGsltCompiledProgram *program,
                                   char *error, size_t error_size) {
    for (uint32_t index = 0u; index < program->rule_count; index++) {
        GsltCompiledRule *rule = &program->rules[index];
        const GsltCompiledNode *head = &program->nodes[rule->head];
        GsltCompiledBucket *bucket = compiled_bucket(
            program, head->symbol, head->child_count, true);
        if (!bucket || !compiled_bucket_add(bucket, index))
            return compiled_error(error, error_size,
                                  "cannot index compiled GSLT rules");
    }
    size_t maximum_rules = 0u;
    for (uint32_t index = 0u; index < program->bucket_count; index++)
        if (program->buckets[index].rule_count > maximum_rules)
            maximum_rules = program->buckets[index].rule_count;
    if (maximum_rules > SIZE_MAX / 2u ||
        maximum_rules * 2u >
            SIZE_MAX / sizeof(GsltCompiledDispatchCountSlot))
        return compiled_error(
            error, error_size,
            "compiled GSLT dispatch scratch exceeds host size");
    size_t required = maximum_rules * 2u;
    size_t capacity = 8u;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u)
            return compiled_error(
                error, error_size,
                "compiled GSLT dispatch scratch exceeds host size");
        capacity *= 2u;
    }
    GsltCompiledDispatchCountSlot *counts =
        cetta_malloc(sizeof(*counts) * capacity);
    program->dispatch_scratch_slot_capacity = capacity;
    program->dispatch_scratch_allocation_count = 1u;
    bool dispatched = true;
    for (uint32_t index = 0u;
         dispatched && index < program->bucket_count; index++)
        dispatched = compiled_build_bucket_dispatch(
            program, &program->buckets[index], counts, capacity);
    free(counts);
    if (!dispatched)
        return compiled_error(
            error, error_size,
            "cannot build compiled GSLT discrimination index");
    return compiled_index_buckets(program, error, error_size);
}

bool cetta_gslt_compiled_program_load_v1(
    const CettaGsltCompiledInputV1 *input,
    CettaGsltCompiledProgram **out,
    char *error, size_t error_size) {
    static const uint8_t magic[] = {'C', 'G', 'P', '1'};
    if (!input || !out || !input->bytes || input->length < sizeof(magic) ||
        !input->sha256)
        return compiled_error(error, error_size,
                              "invalid compiled GSLT plan input");
    *out = NULL;
    char digest[65];
    cetta_native_sha256_hex(input->bytes, input->length, digest);
    if (strcmp(digest, input->sha256) != 0)
        return compiled_error(error, error_size,
                              "compiled GSLT plan digest changed");
    GsltPlanReader reader = {
        .cursor = input->bytes,
        .end = input->bytes + input->length,
    };
    if ((size_t)(reader.end - reader.cursor) < sizeof(magic) ||
        memcmp(reader.cursor, magic, sizeof(magic)) != 0)
        return compiled_error(error, error_size,
                              "compiled GSLT plan has an invalid magic");
    reader.cursor += sizeof(magic);
    uint32_t node_count, child_count, rule_count, body_count;
    if (!plan_read_u32(&reader, &node_count) ||
        !plan_read_u32(&reader, &child_count) ||
        !plan_read_u32(&reader, &rule_count) ||
        !plan_read_u32(&reader, &body_count) ||
        node_count == 0u || rule_count == 0u)
        return compiled_error(error, error_size,
                              "compiled GSLT plan has an invalid header");
    size_t remaining = (size_t)(reader.end - reader.cursor);
    if ((size_t)node_count > remaining / 25u ||
        (size_t)rule_count > remaining / 20u ||
        (size_t)child_count > remaining / 4u ||
        (size_t)body_count > remaining / 4u)
        return compiled_error(error, error_size,
                              "compiled GSLT plan counts exceed its payload");
    CettaGsltCompiledProgram *program = cetta_malloc(sizeof(*program));
    memset(program, 0, sizeof(*program));
    arena_init(&program->ground_arena);
    arena_set_runtime_kind(
        &program->ground_arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    program->node_count = node_count;
    program->child_count = child_count;
    program->rule_count = rule_count;
    program->body_count = body_count;
    program->nodes = cetta_malloc(sizeof(*program->nodes) * node_count);
    memset(program->nodes, 0, sizeof(*program->nodes) * node_count);
    program->children = child_count
        ? cetta_malloc(sizeof(*program->children) * child_count) : NULL;
    program->rules = cetta_malloc(sizeof(*program->rules) * rule_count);
    memset(program->rules, 0, sizeof(*program->rules) * rule_count);
    program->bodies = body_count
        ? cetta_malloc(sizeof(*program->bodies) * body_count) : NULL;
    bool ok = true;
    for (uint32_t index = 0u; ok && index < node_count; index++) {
        GsltCompiledNode *node = &program->nodes[index];
        uint32_t integer_low = 0u, integer_high = 0u;
        ok = plan_read_u8(&reader, &node->kind) &&
             plan_read_u32(&reader, &node->child_offset) &&
             plan_read_u32(&reader, &node->child_count) &&
             plan_read_u32(&reader, &integer_low) &&
             plan_read_u32(&reader, &integer_high) &&
             plan_read_u32(&reader, &node->variable) &&
             plan_read_text(&reader, &node->text);
        uint64_t bits = (uint64_t)integer_low |
                        ((uint64_t)integer_high << 32u);
        memcpy(&node->integer, &bits, sizeof(node->integer));
    }
    for (uint32_t index = 0u; ok && index < child_count; index++)
        ok = plan_read_u32(&reader, &program->children[index]);
    for (uint32_t index = 0u; ok && index < rule_count; index++) {
        GsltCompiledRule *rule = &program->rules[index];
        ok = plan_read_u32(&reader, &rule->head) &&
             plan_read_u32(&reader, &rule->body_offset) &&
             plan_read_u32(&reader, &rule->body_count) &&
             plan_read_u32(&reader, &rule->variable_count) &&
             plan_read_text(&reader, &rule->name);
    }
    for (uint32_t index = 0u; ok && index < body_count; index++)
        ok = plan_read_u32(&reader, &program->bodies[index]);
    bool indexed = ok && reader.cursor == reader.end &&
        compiled_validate_nodes(program, error, error_size) &&
        compiled_validate_rule_forest(program, error, error_size);
    if (indexed)
        compiled_analyze_rule_shapes(program);
    indexed = indexed &&
        compiled_bind_symbols(program, error, error_size) &&
        compiled_build_ground_cache(program, error, error_size) &&
        compiled_build_buckets(program, error, error_size);
    if (!indexed) {
        if (error && error_size > 0u && error[0] == '\0')
            compiled_error(error, error_size,
                           "compiled GSLT plan has trailing or malformed data");
        cetta_gslt_compiled_program_free(program);
        return false;
    }
    *out = program;
    return true;
}

void cetta_gslt_compiled_program_free(CettaGsltCompiledProgram *program) {
    if (!program)
        return;
    for (uint32_t index = 0u; index < program->node_count; index++)
        free(program->nodes[index].text);
    for (uint32_t index = 0u; index < program->rule_count; index++)
        free(program->rules[index].name);
    for (uint32_t index = 0u; index < program->bucket_count; index++) {
        for (uint32_t group = 0u;
             group < program->buckets[index].dispatch_group_count; group++)
            free(program->buckets[index].dispatch_groups[group].positions);
        free(program->buckets[index].dispatch_groups);
        free(program->buckets[index].dispatch_wildcards);
        free(program->buckets[index].rules);
    }
    free(program->bucket_slots);
    free(program->buckets);
    free(program->bodies);
    free(program->rules);
    free(program->children);
    free(program->nodes);
    arena_free(&program->ground_arena);
    free(program);
}

size_t cetta_gslt_compiled_program_rule_count(
    const CettaGsltCompiledProgram *program) {
    return program ? program->rule_count : 0u;
}

bool cetta_gslt_compiled_program_index_stats_v1(
    const CettaGsltCompiledProgram *program,
    CettaGsltCompiledIndexStatsV1 *stats) {
    if (!program || !stats)
        return false;
    *stats = (CettaGsltCompiledIndexStatsV1){
        .bucket_count = program->bucket_count,
        .slot_count = program->bucket_slot_cap,
        .insertion_collisions = program->bucket_insert_collisions,
        .maximum_probe = program->bucket_max_probe,
        .dispatch_scratch_slot_capacity =
            program->dispatch_scratch_slot_capacity,
        .dispatch_scratch_allocation_count =
            program->dispatch_scratch_allocation_count,
        .variable_support_slot_capacity =
            program->variable_support_slot_capacity,
        .variable_support_word_capacity =
            program->variable_support_word_capacity,
        .variable_support_allocation_count =
            program->variable_support_allocation_count,
        .plan_node_count = program->node_count,
        .flat_variable_head_rule_count =
            program->flat_variable_head_rule_count,
        .ground_cached_plan_node_count =
            program->ground_cached_plan_node_count,
        .ground_cached_value_node_count =
            program->ground_cached_value_node_count,
    };
    for (uint32_t index = 0u; index < program->bucket_count; index++) {
        const GsltCompiledBucket *bucket = &program->buckets[index];
        stats->head_indexed_rule_count =
            SIZE_MAX - stats->head_indexed_rule_count < bucket->rule_count
                ? SIZE_MAX
                : stats->head_indexed_rule_count + bucket->rule_count;
        if (bucket->dispatch_child != UINT32_MAX)
            stats->dispatch_bucket_count++;
        stats->dispatch_group_count =
            SIZE_MAX - stats->dispatch_group_count <
                bucket->dispatch_group_count
                ? SIZE_MAX
                : stats->dispatch_group_count +
                    bucket->dispatch_group_count;
        stats->dispatch_wildcard_count =
            SIZE_MAX - stats->dispatch_wildcard_count <
                bucket->dispatch_wildcard_count
                ? SIZE_MAX
                : stats->dispatch_wildcard_count +
                    bucket->dispatch_wildcard_count;
    }
    return true;
}

static bool compiled_node_matches_source(
    const CettaGsltCompiledProgram *program, uint32_t node_index,
    const Atom *source, VarId *variables, bool *assigned,
    uint32_t variable_count, uint32_t depth) {
    if (!program || node_index >= program->node_count || !source ||
        depth > GSLT_PLAN_DEPTH_LIMIT)
        return false;
    const GsltCompiledNode *node = &program->nodes[node_index];
    switch (node->kind) {
    case GSLT_PLAN_SYMBOL:
        return source->kind == ATOM_SYMBOL &&
            node->symbol == source->sym_id;
    case GSLT_PLAN_STRING:
        return source->kind == ATOM_GROUNDED &&
            source->ground.gkind == GV_STRING &&
            strcmp(node->text, source->ground.sval) == 0;
    case GSLT_PLAN_INTEGER:
        return source->kind == ATOM_GROUNDED &&
            source->ground.gkind == GV_INT &&
            node->integer == source->ground.ival;
    case GSLT_PLAN_VARIABLE:
        if (source->kind != ATOM_VAR || node->variable >= variable_count)
            return false;
        if (assigned[node->variable])
            return variables[node->variable] == source->var_id;
        for (uint32_t index = 0u; index < variable_count; index++)
            if (assigned[index] && variables[index] == source->var_id)
                return false;
        variables[node->variable] = source->var_id;
        assigned[node->variable] = true;
        return true;
    case GSLT_PLAN_APPLICATION:
        if (source->kind != ATOM_EXPR ||
            source->expr.len != (CettaExprLen)(node->child_count + 1u) ||
            source->expr.elems[0]->kind != ATOM_SYMBOL ||
            node->symbol != source->expr.elems[0]->sym_id)
            return false;
        for (uint32_t index = 0u; index < node->child_count; index++)
            if (!compiled_node_matches_source(
                    program, program->children[node->child_offset + index],
                    source->expr.elems[index + 1u], variables, assigned,
                    variable_count, depth + 1u))
                return false;
        return true;
    default:
        return false;
    }
}

static const GsltCompiledRule *compiled_rule_named(
    const CettaGsltCompiledProgram *program, const char *name) {
    for (uint32_t index = 0u; index < program->rule_count; index++)
        if (strcmp(program->rules[index].name, name) == 0)
            return &program->rules[index];
    return NULL;
}

bool cetta_gslt_compiled_program_matches_source_v1(
    const CettaGsltCompiledProgram *program,
    const CettaGsltHornProgram *source,
    char *error, size_t error_size) {
    if (!program || !source)
        return compiled_error(error, error_size,
                              "invalid GSLT source-plan comparison");
    if (!compiled_symbols_current(program))
        return compiled_error(error, error_size,
                              "compiled GSLT symbol identity is stale");
    size_t source_count = cetta_gslt_horn_program_rule_count(source);
    if (source_count != program->rule_count)
        return compiled_error(error, error_size,
                              "compiled GSLT rule inventory differs from admitted source");
    for (size_t source_index = 0u;
         source_index < source_count; source_index++) {
        CettaGsltHornRuleViewV1 view;
        if (!cetta_gslt_horn_program_rule_view_v1(
                source, source_index, &view))
            return compiled_error(error, error_size,
                                  "cannot inspect admitted GSLT source rule");
        const GsltCompiledRule *rule = compiled_rule_named(
            program, view.name);
        if (!rule || rule->body_count != view.body_count)
            return compiled_error(
                error, error_size,
                "compiled GSLT rule '%s' differs from admitted source",
                view.name);
        VarId *variables = rule->variable_count
            ? cetta_malloc(sizeof(*variables) * rule->variable_count)
            : NULL;
        bool *assigned = rule->variable_count
            ? cetta_malloc(sizeof(*assigned) * rule->variable_count)
            : NULL;
        if (assigned)
            memset(assigned, 0, sizeof(*assigned) * rule->variable_count);
        bool matches = compiled_node_matches_source(
            program, rule->head, view.head, variables, assigned,
            rule->variable_count, 0u);
        for (uint32_t body = 0u;
             matches && body < rule->body_count; body++)
            matches = compiled_node_matches_source(
                program, program->bodies[rule->body_offset + body],
                view.body[body], variables, assigned,
                rule->variable_count, 0u);
        for (uint32_t variable = 0u;
             matches && variable < rule->variable_count; variable++)
            matches = assigned[variable];
        free(assigned);
        free(variables);
        if (!matches)
            return compiled_error(
                error, error_size,
                "compiled GSLT rule '%s' differs from admitted source",
                view.name);
    }
    return true;
}

static Atom *compiled_materialize(
    const CettaGsltCompiledProgram *program, uint32_t node_index,
    Atom **variables, uint64_t *variable_epochs, uint64_t variable_epoch,
    uint32_t variable_count,
    Arena *arena, CettaGsltHornResult *result, uint32_t depth) {
    if (!program || node_index >= program->node_count || !arena ||
        depth > GSLT_PLAN_DEPTH_LIMIT)
        return NULL;
    const GsltCompiledNode *node = &program->nodes[node_index];
    if (node->ground_atom) {
        if (result) {
            result->rule_ground_subterm_cache_hits =
                compiled_u64_add_sat(
                    result->rule_ground_subterm_cache_hits, 1u);
            result->rule_ground_subterm_nodes_elided =
                compiled_u64_add_sat(
                    result->rule_ground_subterm_nodes_elided,
                    node->ground_node_count);
        }
        return node->ground_atom;
    }
    switch (node->kind) {
    case GSLT_PLAN_SYMBOL:
        return atom_symbol_id(arena, node->symbol);
    case GSLT_PLAN_STRING:
        return atom_string(arena, node->text);
    case GSLT_PLAN_INTEGER:
        return atom_int(arena, node->integer);
    case GSLT_PLAN_VARIABLE:
        if (node->variable >= variable_count || !variable_epochs)
            return NULL;
        if (variable_epochs[node->variable] != variable_epoch) {
            variables[node->variable] = atom_var_with_id(
                arena, "gslt-plan", fresh_var_id());
            variable_epochs[node->variable] = variable_epoch;
        }
        return variables[node->variable];
    case GSLT_PLAN_APPLICATION: {
        Atom **elements = arena_alloc(
            arena, sizeof(*elements) * (size_t)(node->child_count + 1u));
        elements[0] = atom_symbol_id(arena, node->symbol);
        for (uint32_t index = 0u; index < node->child_count; index++) {
            elements[index + 1u] = compiled_materialize(
                program, program->children[node->child_offset + index],
                variables, variable_epochs, variable_epoch, variable_count,
                arena, result, depth + 1u);
            if (!elements[index + 1u])
                return NULL;
        }
        return atom_expr(
            arena, elements, (CettaExprLen)(node->child_count + 1u));
    }
    default:
        return NULL;
    }
}

static const GsltCompiledBucket *compiled_find_bucket(
    const CettaGsltCompiledProgram *program, const Atom *goal) {
    if (!program || !goal || goal->kind != ATOM_EXPR ||
        goal->expr.len == 0u || goal->expr.elems[0]->kind != ATOM_SYMBOL)
        return NULL;
    SymbolId head = goal->expr.elems[0]->sym_id;
    uint32_t arity = (uint32_t)(goal->expr.len - 1u);
    return compiled_bucket_index_lookup(program, head, arity);
}

static bool compiled_query_dispatch_key(
    const Atom *goal, uint32_t child,
    GsltCompiledDispatchKey *key) {
    if (!goal || !key || goal->kind != ATOM_EXPR || !goal->expr.elems ||
        goal->expr.len == 0u || child >= goal->expr.len - 1u)
        return false;
    const Atom *candidate = goal->expr.elems[child + 1u];
    if (!candidate || candidate->kind == ATOM_VAR)
        return false;
    memset(key, 0, sizeof(*key));
    switch (candidate->kind) {
    case ATOM_SYMBOL:
        key->kind = GSLT_PLAN_SYMBOL;
        key->symbol = candidate->sym_id;
        return true;
    case ATOM_GROUNDED:
        if (candidate->ground.gkind == GV_STRING &&
            candidate->ground.sval) {
            key->kind = GSLT_PLAN_STRING;
            key->text = candidate->ground.sval;
        } else if (candidate->ground.gkind == GV_INT) {
            key->kind = GSLT_PLAN_INTEGER;
            key->integer = candidate->ground.ival;
        } else {
            key->kind = GSLT_DISPATCH_OTHER;
        }
        return true;
    case ATOM_EXPR:
        if (!candidate->expr.elems || candidate->expr.len == 0u ||
            !candidate->expr.elems[0])
            return false;
        if (candidate->expr.elems[0]->kind == ATOM_VAR)
            return false;
        if (candidate->expr.elems[0]->kind != ATOM_SYMBOL) {
            key->kind = GSLT_DISPATCH_OTHER;
            return true;
        }
        key->kind = GSLT_PLAN_APPLICATION;
        key->symbol = candidate->expr.elems[0]->sym_id;
        key->arity = (uint32_t)(candidate->expr.len - 1u);
        return true;
    default:
        key->kind = GSLT_DISPATCH_OTHER;
        return true;
    }
}

static bool compiled_dispatch_positions(
    const GsltCompiledBucket *bucket, const Atom *goal,
    const uint32_t **positions, uint32_t *position_count) {
    if (!bucket || !goal || !positions || !position_count ||
        bucket->dispatch_child == UINT32_MAX)
        return false;
    GsltCompiledDispatchKey key;
    if (!compiled_query_dispatch_key(
            goal, bucket->dispatch_child, &key))
        return false;
    for (uint32_t index = 0u;
         index < bucket->dispatch_group_count; index++) {
        const GsltCompiledDispatchGroup *group =
            &bucket->dispatch_groups[index];
        if (!compiled_dispatch_key_equal(&group->key, &key))
            continue;
        *positions = group->positions;
        *position_count = group->position_count;
        return true;
    }
    *positions = bucket->dispatch_wildcards;
    *position_count = bucket->dispatch_wildcard_count;
    return true;
}

static void compiled_consume_dispatch_rejects(
    CettaGsltHornResult *result, CettaGsltHornLimits limits,
    uint32_t rejected, bool candidate_follows, bool *stopped) {
    if (!result || !stopped || *stopped || rejected == 0u)
        return;
    uint64_t available = limits.max_rule_attempts - result->rule_attempts;
    uint64_t consumed = rejected < available ? rejected : available;
    result->rule_attempts += consumed;
    result->rule_dispatch_rejects += consumed;
    if ((uint64_t)rejected > available ||
        (candidate_follows && result->rule_attempts >=
            limits.max_rule_attempts)) {
        result->outcome = CETTA_GSLT_HORN_RULE_LIMIT;
        *stopped = true;
    }
}

static bool compiled_rigid_compatible(
    const CettaGsltCompiledProgram *program, uint32_t node_index,
    const Atom *candidate, uint32_t depth) {
    if (!program || node_index >= program->node_count || !candidate ||
        depth > GSLT_PLAN_DEPTH_LIMIT)
        return true;
    if (candidate->kind == ATOM_VAR)
        return true;
    const GsltCompiledNode *node = &program->nodes[node_index];
    switch (node->kind) {
    case GSLT_PLAN_VARIABLE:
        return true;
    case GSLT_PLAN_SYMBOL:
        return candidate->kind == ATOM_SYMBOL &&
            candidate->sym_id == node->symbol;
    case GSLT_PLAN_STRING:
        return candidate->kind == ATOM_GROUNDED &&
            candidate->ground.gkind == GV_STRING &&
            candidate->ground.sval != NULL && node->text != NULL &&
            strcmp(candidate->ground.sval, node->text) == 0;
    case GSLT_PLAN_INTEGER:
        return candidate->kind == ATOM_GROUNDED &&
            candidate->ground.gkind == GV_INT &&
            candidate->ground.ival == node->integer;
    case GSLT_PLAN_APPLICATION:
        if (candidate->kind != ATOM_EXPR ||
            candidate->expr.len != (CettaExprLen)node->child_count + 1u)
            return false;
        if (!candidate->expr.elems || !candidate->expr.elems[0])
            return true;
        if (candidate->expr.elems[0]->kind != ATOM_VAR &&
            (candidate->expr.elems[0]->kind != ATOM_SYMBOL ||
             candidate->expr.elems[0]->sym_id != node->symbol))
            return false;
        for (uint32_t index = 0u; index < node->child_count; index++) {
            if (!candidate->expr.elems[index + 1u])
                return true;
            if (!compiled_rigid_compatible(
                    program,
                    program->children[node->child_offset + index],
                    candidate->expr.elems[index + 1u], depth + 1u))
                return false;
        }
        return true;
    default:
        return true;
    }
}

static bool compiled_rigid_head_arguments_compatible(
    const CettaGsltCompiledProgram *program,
    const GsltCompiledRule *rule, const Atom *goal) {
    if (!program || !rule || rule->head >= program->node_count || !goal)
        return true;
    const GsltCompiledNode *head = &program->nodes[rule->head];
    if (head->kind != GSLT_PLAN_APPLICATION)
        return true;
    if (goal->kind != ATOM_EXPR ||
        goal->expr.len != (CettaExprLen)head->child_count + 1u)
        return false;
    if (!goal->expr.elems)
        return true;
    for (uint32_t index = 0u; index < head->child_count; index++) {
        if (!goal->expr.elems[index + 1u])
            return true;
        if (!compiled_rigid_compatible(
                program, program->children[head->child_offset + index],
                goal->expr.elems[index + 1u], 1u))
            return false;
    }
    return true;
}

static bool compiled_match_ground_dense_node(
    const CettaGsltCompiledProgram *program, uint32_t node_index,
    Atom *candidate, Atom **variables, uint64_t *variable_epochs,
    uint64_t variable_epoch, uint32_t variable_count,
    uint32_t depth) {
    if (!program || node_index >= program->node_count || !candidate ||
        depth > GSLT_PLAN_DEPTH_LIMIT)
        return false;
    const GsltCompiledNode *node = &program->nodes[node_index];
    switch (node->kind) {
    case GSLT_PLAN_VARIABLE:
        if (node->variable >= variable_count || !variable_epochs)
            return false;
        if (variable_epochs[node->variable] != variable_epoch) {
            variables[node->variable] = candidate;
            variable_epochs[node->variable] = variable_epoch;
            return true;
        }
        return atom_eq_fast(variables[node->variable], candidate);
    case GSLT_PLAN_SYMBOL:
        return candidate->kind == ATOM_SYMBOL &&
            candidate->sym_id == node->symbol;
    case GSLT_PLAN_STRING:
        return candidate->kind == ATOM_GROUNDED &&
            candidate->ground.gkind == GV_STRING &&
            candidate->ground.sval != NULL && node->text != NULL &&
            strcmp(candidate->ground.sval, node->text) == 0;
    case GSLT_PLAN_INTEGER:
        return candidate->kind == ATOM_GROUNDED &&
            candidate->ground.gkind == GV_INT &&
            candidate->ground.ival == node->integer;
    case GSLT_PLAN_APPLICATION:
        if (candidate->kind != ATOM_EXPR || !candidate->expr.elems ||
            candidate->expr.len != (CettaExprLen)node->child_count + 1u ||
            !candidate->expr.elems[0] ||
            candidate->expr.elems[0]->kind != ATOM_SYMBOL ||
            candidate->expr.elems[0]->sym_id != node->symbol)
            return false;
        for (uint32_t index = 0u; index < node->child_count; index++)
            if (!compiled_match_ground_dense_node(
                program,
                program->children[node->child_offset + index],
                candidate->expr.elems[index + 1u], variables,
                variable_epochs, variable_epoch, variable_count,
                depth + 1u))
                return false;
        return true;
    default:
        return false;
    }
}

static bool compiled_match_ground_dense_head(
    const CettaGsltCompiledProgram *program, const GsltCompiledRule *rule,
    Atom *goal, Atom **variables, uint64_t *variable_epochs,
    uint64_t variable_epoch) {
    if (!program || !rule || !goal ||
        rule->head >= program->node_count)
        return false;
    const GsltCompiledNode *head = &program->nodes[rule->head];
    if (head->kind != GSLT_PLAN_APPLICATION || goal->kind != ATOM_EXPR ||
        !goal->expr.elems ||
        goal->expr.len != (CettaExprLen)head->child_count + 1u)
        return false;
    for (uint32_t index = 0u; index < head->child_count; index++)
        if (!compiled_match_ground_dense_node(
                program, program->children[head->child_offset + index],
                goal->expr.elems[index + 1u], variables, variable_epochs,
                variable_epoch, rule->variable_count, 1u))
            return false;
    return true;
}

/* Consume the constructor equations exposed by the generated plan directly.
 * A variable on either side remains an ordinary unifier boundary.  Equal
 * rigid applications are decomposed in source order, exactly as the general
 * matcher would do after materializing the rule-side application. */
static bool compiled_match_constructor_guided_node(
    const CettaGsltCompiledProgram *program, uint32_t node_index,
    Atom *candidate, Atom **variables, uint64_t *variable_epochs,
    uint64_t variable_epoch, uint32_t variable_count,
    Bindings *bindings, Arena *arena, CettaGsltHornResult *result,
    uint32_t depth) {
    if (!program || node_index >= program->node_count || !candidate ||
        !bindings || !arena || depth > GSLT_PLAN_DEPTH_LIMIT)
        return false;
    const GsltCompiledNode *node = &program->nodes[node_index];
    if (candidate->kind == ATOM_VAR || node->kind == GSLT_PLAN_VARIABLE) {
        Atom *materialized = compiled_materialize(
            program, node_index, variables, variable_epochs, variable_epoch,
            variable_count, arena, result, depth);
        return materialized && match_atoms(candidate, materialized, bindings);
    }
    switch (node->kind) {
    case GSLT_PLAN_SYMBOL:
        return candidate->kind == ATOM_SYMBOL &&
            candidate->sym_id == node->symbol;
    case GSLT_PLAN_STRING:
        return candidate->kind == ATOM_GROUNDED &&
            candidate->ground.gkind == GV_STRING &&
            candidate->ground.sval != NULL && node->text != NULL &&
            strcmp(candidate->ground.sval, node->text) == 0;
    case GSLT_PLAN_INTEGER:
        return candidate->kind == ATOM_GROUNDED &&
            candidate->ground.gkind == GV_INT &&
            candidate->ground.ival == node->integer;
    case GSLT_PLAN_APPLICATION: {
        if (candidate->kind != ATOM_EXPR || !candidate->expr.elems ||
            candidate->expr.len != (CettaExprLen)node->child_count + 1u ||
            !candidate->expr.elems[0])
            return false;
        if (!node->ground_atom && result)
            result->rule_constructor_nodes_elided = compiled_u64_add_sat(
                result->rule_constructor_nodes_elided, 1u);
        Atom *candidate_head = candidate->expr.elems[0];
        if (candidate_head->kind == ATOM_VAR) {
            Atom *expected_head = atom_symbol_id(arena, node->symbol);
            if (!expected_head ||
                !match_atoms(candidate_head, expected_head, bindings))
                return false;
        } else if (candidate_head->kind != ATOM_SYMBOL ||
                   candidate_head->sym_id != node->symbol) {
            return false;
        }
        for (uint32_t index = 0u; index < node->child_count; index++)
            if (!compiled_match_constructor_guided_node(
                    program,
                    program->children[node->child_offset + index],
                    candidate->expr.elems[index + 1u], variables,
                    variable_epochs, variable_epoch, variable_count,
                    bindings, arena, result, depth + 1u))
                return false;
        return true;
    }
    default:
        return false;
    }
}

static bool compiled_match_constructor_guided_head(
    const CettaGsltCompiledProgram *program, const GsltCompiledRule *rule,
    Atom *goal, Atom **variables, uint64_t *variable_epochs,
    uint64_t variable_epoch, Bindings *bindings, Arena *arena,
    CettaGsltHornResult *result) {
    if (!program || !rule || !goal || !bindings || !arena ||
        rule->head >= program->node_count)
        return false;
    const GsltCompiledNode *head = &program->nodes[rule->head];
    if (head->kind != GSLT_PLAN_APPLICATION || goal->kind != ATOM_EXPR ||
        !goal->expr.elems ||
        goal->expr.len != (CettaExprLen)head->child_count + 1u)
        return false;
    for (uint32_t index = 0u; index < head->child_count; index++)
        if (!compiled_match_constructor_guided_node(
                program, program->children[head->child_offset + index],
                goal->expr.elems[index + 1u], variables,
                variable_epochs, variable_epoch, rule->variable_count,
                bindings, arena, result, 1u))
            return false;
    return true;
}

static bool compiled_match_flat_variable_head(
    const CettaGsltCompiledProgram *program, const GsltCompiledRule *rule,
    Atom *goal, Atom **variables, uint64_t *variable_epochs,
    uint64_t variable_epoch, Bindings *bindings, Arena *arena,
    CettaGsltHornResult *result) {
    if (!program || !rule || !rule->flat_variable_head || !goal ||
        goal->kind != ATOM_EXPR || !goal->expr.elems || !bindings || !arena ||
        rule->head >= program->node_count)
        return false;
    const GsltCompiledNode *head = &program->nodes[rule->head];
    if (goal->expr.len != (CettaExprLen)head->child_count + 1u)
        return false;
    for (uint32_t index = 0u; index < head->child_count; index++) {
        Atom *argument = goal->expr.elems[index + 1u];
        uint32_t child = program->children[head->child_offset + index];
        Atom *variable = compiled_materialize(
            program, child, variables, variable_epochs, variable_epoch,
            rule->variable_count, arena, result, 0u);
        if (!argument || !variable ||
            !match_atoms(argument, variable, bindings))
            return false;
    }
    return true;
}

static GsltCompiledState *compiled_state_new(
    Atom *query, Atom *const *goals, uint32_t goal_count, uint32_t depth) {
    if (!query || (goal_count != 0u && !goals))
        return NULL;
    GsltCompiledState *state = cetta_malloc(sizeof(*state));
    memset(state, 0, sizeof(*state));
    arena_init(&state->arena);
    arena_set_runtime_kind(
        &state->arena, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    AtomDeepCopySession *copy = atom_deep_copy_session_new(&state->arena);
    state->query = atom_deep_copy_session_copy(copy, query);
    state->goals = goal_count
        ? arena_alloc(&state->arena, sizeof(*state->goals) * goal_count)
        : NULL;
    bool healthy = copy && state->query;
    for (uint32_t index = 0u; healthy && index < goal_count; index++) {
        state->goals[index] = atom_deep_copy_session_copy(copy, goals[index]);
        healthy = state->goals[index] != NULL;
    }
    atom_deep_copy_session_free(copy);
    if (!healthy) {
        arena_free(&state->arena);
        free(state);
        return NULL;
    }
    state->goal_count = goal_count;
    state->depth = depth;
    return state;
}

static void compiled_state_destroy(GsltCompiledState *state) {
    if (!state)
        return;
    arena_free(&state->arena);
    free(state);
}

static bool compiled_queue_push(GsltCompiledQueue *queue,
                                GsltCompiledState *state) {
    if (queue->count == queue->cap) {
        size_t next = queue->cap ? queue->cap * 2u : 64u;
        if (next < queue->cap ||
            next > SIZE_MAX / sizeof(*queue->items))
            return false;
        queue->items = cetta_realloc(
            queue->items, sizeof(*queue->items) * next);
        queue->cap = next;
    }
    queue->items[queue->count++] = state;
    return true;
}

static void compiled_note_worklist_state(
    CettaGsltHornResult *result, const GsltCompiledQueue *queue,
    const GsltCompiledState *state, uint64_t *live_bytes) {
    if (!result || !queue || !state || !live_bytes)
        return;
    result->worklist_states_created = compiled_u64_add_sat(
        result->worklist_states_created, 1u);
    uint64_t pending = queue->count >= queue->head
        ? (uint64_t)(queue->count - queue->head) : 0u;
    if (pending > result->worklist_pending_peak)
        result->worklist_pending_peak = pending;
    *live_bytes = compiled_u64_add_sat(
        *live_bytes, (uint64_t)arena_accounted_live_bytes(&state->arena));
    if (*live_bytes > result->worklist_state_bytes_peak)
        result->worklist_state_bytes_peak = *live_bytes;
}

static void compiled_reclaim_worklist_state(
    CettaGsltHornResult *result, GsltCompiledState *state,
    uint64_t *live_bytes) {
    if (!state)
        return;
    uint64_t bytes = (uint64_t)arena_accounted_live_bytes(&state->arena);
    if (live_bytes)
        *live_bytes = *live_bytes >= bytes ? *live_bytes - bytes : 0u;
    if (result)
        result->worklist_states_reclaimed = compiled_u64_add_sat(
            result->worklist_states_reclaimed, 1u);
    compiled_state_destroy(state);
}

static bool compiled_result_push(
    CettaGsltHornResult *result, Arena *output_arena,
    const Atom *query, CettaGsltHornLimits limits) {
    if (result->answer_count >= limits.max_answers) {
        result->outcome = CETTA_GSLT_HORN_ANSWER_LIMIT;
        return false;
    }
    Atom *answer = atom_deep_copy(output_arena, (Atom *)query);
    if (!answer) {
        result->outcome = CETTA_GSLT_HORN_FAULT;
        return false;
    }
    result->answers = cetta_realloc(
        result->answers,
        sizeof(*result->answers) * (result->answer_count + 1u));
    result->answers[result->answer_count++] = answer;
    return true;
}

bool cetta_gslt_compiled_query_v1(
    const CettaGsltCompiledProgram *program,
    Arena *output_arena, Atom *query,
    CettaGsltHornLimits limits,
    CettaGsltHornResult *result,
    char *error, size_t error_size) {
    return cetta_gslt_compiled_query_with_providers_v1(
        program, NULL, output_arena, query, limits,
        result, error, error_size);
}

bool cetta_gslt_compiled_query_with_providers_v1(
    const CettaGsltCompiledProgram *program,
    const CettaGsltProviderRegistryV1 *providers,
    Arena *output_arena, Atom *query,
    CettaGsltHornLimits limits,
    CettaGsltHornResult *result,
    char *error, size_t error_size) {
    if (!program || !output_arena || !query || !result)
        return compiled_error(error, error_size,
                              "invalid compiled GSLT query request");
    if (!compiled_symbols_current(program))
        return compiled_error(error, error_size,
                              "compiled GSLT symbol identity is stale");
    memset(result, 0, sizeof(*result));
    if (limits.max_rule_attempts == 0u || limits.max_answers == 0u ||
        limits.max_depth == 0u)
        return compiled_error(error, error_size,
                              "compiled GSLT limits must be positive");
    if (!cetta_gslt_provider_registry_validate_v1(
            providers, error, error_size))
        return false;
    Arena scratch;
    arena_init(&scratch);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    GsltCompiledQueue queue = {0};
    Atom **variable_slots = program->max_variable_count
        ? cetta_malloc(sizeof(*variable_slots) *
            (size_t)program->max_variable_count)
        : NULL;
    uint64_t *variable_epochs = program->max_variable_count
        ? cetta_malloc(sizeof(*variable_epochs) *
            (size_t)program->max_variable_count)
        : NULL;
    if (variable_epochs)
        memset(variable_epochs, 0,
               sizeof(*variable_epochs) * program->max_variable_count);
    result->rule_variable_slot_capacity = program->max_variable_count;
    result->rule_variable_slot_allocation_count =
        program->max_variable_count > 0u ? 2u : 0u;
    uint64_t variable_epoch = 0u;
    uint64_t worklist_live_bytes = 0u;
    Atom *initial_goals[] = {query};
    GsltCompiledState *initial_state = compiled_state_new(
        query, initial_goals, 1u, 0u);
    bool healthy = initial_state &&
        compiled_queue_push(&queue, initial_state);
    if (healthy) {
        compiled_note_worklist_state(
            result, &queue, initial_state, &worklist_live_bytes);
    } else {
        compiled_state_destroy(initial_state);
    }
    bool stopped = false;
    while (healthy && !stopped && queue.head < queue.count) {
        GsltCompiledState *state = queue.items[queue.head++];
        if (state->depth > result->max_depth_observed)
            result->max_depth_observed = state->depth;
        if (state->depth > limits.max_depth) {
            result->outcome = CETTA_GSLT_HORN_DEPTH_LIMIT;
            stopped = true;
            compiled_reclaim_worklist_state(
                result, state, &worklist_live_bytes);
            break;
        }
        if (state->goal_count == 0u) {
            if (!compiled_result_push(
                    result, output_arena, state->query, limits))
                stopped = true;
            compiled_reclaim_worklist_state(
                result, state, &worklist_live_bytes);
            continue;
        }
        const GsltCompiledBucket *bucket = compiled_find_bucket(
            program, state->goals[0]);
        if (!bucket) {
            const CettaGsltProviderV1 *provider =
                cetta_gslt_provider_find_v1(providers, state->goals[0]);
            if (!provider) {
                compiled_reclaim_worklist_state(
                    result, state, &worklist_live_bytes);
                continue;
            }
            ArenaMark provider_mark = arena_mark(&scratch);
            CettaGsltProviderAnswersV1 answers = {0};
            CettaGsltProviderOutcomeV1 provider_outcome = provider->query(
                provider->context, &scratch, state->goals[0],
                limits.max_answers, &answers,
                error, error_size);
            if (provider_outcome == CETTA_GSLT_PROVIDER_COMPLETED &&
                answers.answer_count > limits.max_answers)
                provider_outcome = CETTA_GSLT_PROVIDER_ANSWER_LIMIT;
            if (provider_outcome == CETTA_GSLT_PROVIDER_ANSWER_LIMIT) {
                result->outcome = CETTA_GSLT_HORN_ANSWER_LIMIT;
                stopped = true;
            } else if (provider_outcome != CETTA_GSLT_PROVIDER_COMPLETED) {
                healthy = false;
            }
            for (size_t index = 0u;
                 healthy && !stopped && index < answers.answer_count;
                 index++) {
                Atom *answer = answers.answers[index];
                Atom *goal = state->goals[0];
                if (!answer || answer->kind != ATOM_EXPR ||
                    answer->expr.len != goal->expr.len ||
                    answer->expr.elems[0]->kind != ATOM_SYMBOL ||
                    goal->expr.elems[0]->kind != ATOM_SYMBOL ||
                    answer->expr.elems[0]->sym_id !=
                        goal->expr.elems[0]->sym_id) {
                    healthy = false;
                    compiled_error(
                        error, error_size,
                        "semantic provider %s returned a malformed %s/%u answer",
                        provider->semantic_id, provider->relation,
                        provider->arity);
                    break;
                }
                Bindings bindings;
                bindings_init(&bindings);
                bool matched = match_atoms(goal, answer, &bindings);
                if (matched && bindings.eq_len != 0u) {
                    healthy = false;
                    compiled_error(
                        error, error_size,
                        "semantic provider produced unsupported residual constraints");
                    matched = false;
                }
                if (matched) {
                    if (state->depth == UINT32_MAX) {
                        healthy = false;
                        compiled_error(
                            error, error_size,
                            "compiled provider continuation exceeds its ABI");
                    } else {
                        uint32_t next_count = state->goal_count - 1u;
                        Atom **next_goals = next_count
                            ? arena_alloc(
                                &scratch,
                                sizeof(*next_goals) * next_count)
                            : NULL;
                        Atom *next_query_scratch = bindings_apply_if_vars(
                            &bindings, &scratch, state->query);
                        healthy = next_query_scratch != NULL;
                        for (uint32_t goal_index = 1u;
                             healthy && goal_index < state->goal_count;
                             goal_index++) {
                            Atom *resolved = bindings_apply_if_vars(
                                &bindings, &scratch,
                                state->goals[goal_index]);
                            next_goals[goal_index - 1u] =
                                resolved;
                            healthy = resolved != NULL;
                        }
                        if (healthy) {
                            GsltCompiledState *next_state =
                                compiled_state_new(
                                    next_query_scratch, next_goals,
                                    next_count, state->depth + 1u);
                            healthy = next_state &&
                                compiled_queue_push(&queue, next_state);
                            if (healthy) {
                                compiled_note_worklist_state(
                                    result, &queue, next_state,
                                    &worklist_live_bytes);
                            } else {
                                compiled_state_destroy(next_state);
                            }
                        }
                    }
                }
                bindings_free(&bindings);
            }
            cetta_gslt_provider_answers_free_v1(&answers);
            arena_reset(&scratch, provider_mark);
            compiled_reclaim_worklist_state(
                result, state, &worklist_live_bytes);
            continue;
        }
        const uint32_t *dispatch_positions = NULL;
        uint32_t dispatch_count = 0u;
        bool dispatched = compiled_dispatch_positions(
            bucket, state->goals[0], &dispatch_positions, &dispatch_count);
        uint32_t source_position = 0u;
        uint32_t candidate_count = dispatched
            ? dispatch_count : bucket->rule_count;
        /* Groundness is an immutable property of this dequeued goal.  Once
         * checked here, the intrinsically ground linear matcher may consume
         * every descendant without repeating the certificate test. */
        bool goal_ground = !atom_has_vars(state->goals[0]);
        for (uint32_t candidate = 0u;
             healthy && !stopped && candidate < candidate_count;
             candidate++) {
            uint32_t position = dispatched
                ? dispatch_positions[candidate] : candidate;
            if (dispatched) {
                compiled_consume_dispatch_rejects(
                    result, limits, position - source_position, true,
                    &stopped);
                if (stopped)
                    break;
                source_position = position + 1u;
            }
            if (result->rule_attempts >= limits.max_rule_attempts) {
                result->outcome = CETTA_GSLT_HORN_RULE_LIMIT;
                stopped = true;
                break;
            }
            result->rule_attempts++;
            const GsltCompiledRule *rule =
                &program->rules[bucket->rules[position]];
            result->rule_outer_head_elisions++;
            if (!compiled_rigid_head_arguments_compatible(
                    program, rule, state->goals[0])) {
                result->rule_prefilter_rejects++;
                continue;
            }
            ArenaMark mark = arena_mark(&scratch);
            Atom **variables = variable_slots;
            if (rule->variable_count > 0u) {
                if (variable_epoch == UINT64_MAX) {
                    memset(variable_epochs, 0,
                           sizeof(*variable_epochs) *
                               program->max_variable_count);
                    variable_epoch = 1u;
                } else {
                    variable_epoch++;
                }
                result->rule_variable_slot_buffer_uses =
                    compiled_u64_add_sat(
                        result->rule_variable_slot_buffer_uses, 1u);
                result->rule_variable_slot_bytes_elided =
                    compiled_u64_add_sat(
                        result->rule_variable_slot_bytes_elided,
                        compiled_u64_mul_sat(
                            (uint64_t)rule->variable_count,
                            (uint64_t)sizeof(*variables)));
                result->rule_variable_slot_clear_bytes_elided =
                    compiled_u64_add_sat(
                        result->rule_variable_slot_clear_bytes_elided,
                        compiled_u64_mul_sat(
                            (uint64_t)rule->variable_count,
                            (uint64_t)sizeof(*variable_epochs)));
            }
            Bindings bindings;
            bindings_init(&bindings);
            bool matched;
            if (goal_ground) {
                result->rule_ground_dense_attempts++;
                matched = compiled_match_ground_dense_head(
                    program, rule, state->goals[0], variables,
                    variable_epochs, variable_epoch);
                if (matched)
                    result->rule_ground_dense_matches++;
            } else if (rule->flat_variable_head) {
                result->rule_flat_head_attempts++;
                matched = compiled_match_flat_variable_head(
                    program, rule, state->goals[0], variables,
                    variable_epochs, variable_epoch,
                    &bindings, &scratch, result);
                if (matched)
                    result->rule_flat_head_matches++;
            } else {
                result->rule_constructor_guided_attempts++;
                matched = compiled_match_constructor_guided_head(
                    program, rule, state->goals[0], variables,
                    variable_epochs, variable_epoch,
                    &bindings, &scratch, result);
                if (matched)
                    result->rule_constructor_guided_matches++;
            }
            if (matched && bindings.eq_len != 0u) {
                healthy = false;
                compiled_error(
                    error, error_size,
                    "compiled GSLT plan produced unsupported residual constraints");
                matched = false;
            }
            if (matched) {
                Atom **body = rule->body_count
                    ? arena_alloc(
                        &scratch, sizeof(*body) * rule->body_count)
                    : NULL;
                bool materialized = true;
                for (uint32_t index = 0u;
                     materialized && index < rule->body_count; index++) {
                    body[index] = compiled_materialize(
                        program,
                        program->bodies[rule->body_offset + index],
                        variables, variable_epochs, variable_epoch,
                        rule->variable_count, &scratch, result, 0u);
                    materialized = body[index] != NULL;
                }
                if (!materialized) {
                    healthy = false;
                    compiled_error(
                        error, error_size,
                        "validated compiled GSLT body failed to materialize");
                    bindings_free(&bindings);
                    arena_reset(&scratch, mark);
                    break;
                }
                result->rule_matches++;
                uint64_t next_count_wide = (uint64_t)rule->body_count +
                    (uint64_t)state->goal_count - 1u;
                if (next_count_wide > UINT32_MAX ||
                    state->depth == UINT32_MAX) {
                    healthy = false;
                    compiled_error(
                        error, error_size,
                        "compiled GSLT continuation exceeds its ABI");
                    bindings_free(&bindings);
                    arena_reset(&scratch, mark);
                    break;
                }
                uint32_t next_count = (uint32_t)next_count_wide;
                Atom **next_goals = next_count
                    ? arena_alloc(
                        &scratch, sizeof(*next_goals) * next_count)
                    : NULL;
                Atom *next_query_scratch = bindings_apply_if_vars(
                    &bindings, &scratch, state->query);
                healthy = next_query_scratch != NULL;
                for (uint32_t index = 0u;
                     healthy && index < rule->body_count; index++) {
                    Atom *resolved = bindings_apply_if_vars(
                        &bindings, &scratch, body[index]);
                    next_goals[index] = resolved;
                    healthy = resolved != NULL;
                }
                for (uint32_t index = 1u;
                     healthy && index < state->goal_count; index++) {
                    Atom *resolved = bindings_apply_if_vars(
                        &bindings, &scratch, state->goals[index]);
                    next_goals[rule->body_count + index - 1u] =
                        resolved;
                    healthy = resolved != NULL;
                }
                if (healthy) {
                    GsltCompiledState *next_state = compiled_state_new(
                        next_query_scratch, next_goals, next_count,
                        state->depth + 1u);
                    healthy = next_state &&
                        compiled_queue_push(&queue, next_state);
                    if (healthy) {
                        compiled_note_worklist_state(
                            result, &queue, next_state,
                            &worklist_live_bytes);
                    } else {
                        compiled_state_destroy(next_state);
                    }
                }
            }
            bindings_free(&bindings);
            arena_reset(&scratch, mark);
        }
        if (healthy && !stopped && dispatched)
            compiled_consume_dispatch_rejects(
                result, limits, bucket->rule_count - source_position,
                false, &stopped);
        compiled_reclaim_worklist_state(
            result, state, &worklist_live_bytes);
    }
    if (!healthy) {
        result->outcome = CETTA_GSLT_HORN_FAULT;
        if (!error || error_size == 0u || error[0] == '\0')
            compiled_error(error, error_size,
                           "compiled GSLT worklist exhausted host resources");
    }
    while (queue.head < queue.count)
        compiled_reclaim_worklist_state(
            result, queue.items[queue.head++], &worklist_live_bytes);
    free(queue.items);
    free(variable_epochs);
    free(variable_slots);
    arena_free(&scratch);
    return healthy;
}
