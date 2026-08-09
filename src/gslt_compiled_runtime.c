#include "gslt_compiled_runtime.h"

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
};

typedef struct {
    uint8_t kind;
    uint32_t child_offset;
    uint32_t child_count;
    int64_t integer;
    uint32_t variable;
    char *text;
} GsltCompiledNode;

typedef struct {
    uint32_t head;
    uint32_t body_offset;
    uint32_t body_count;
    uint32_t variable_count;
    char *name;
} GsltCompiledRule;

typedef struct {
    const char *head;
    uint32_t arity;
    uint32_t *rules;
    uint32_t rule_count;
    uint32_t rule_cap;
} GsltCompiledBucket;

struct CettaGsltCompiledProgram {
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
};

typedef struct {
    const uint8_t *cursor;
    const uint8_t *end;
} GsltPlanReader;

typedef struct {
    Atom *query;
    Atom **goals;
    uint32_t goal_count;
    uint32_t depth;
} GsltCompiledState;

typedef struct {
    GsltCompiledState *items;
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

static bool compiled_bucket_add(GsltCompiledBucket *bucket,
                                uint32_t rule) {
    if (bucket->rule_count == bucket->rule_cap) {
        uint32_t next = bucket->rule_cap ? bucket->rule_cap * 2u : 8u;
        if (next < bucket->rule_cap)
            return false;
        bucket->rules = cetta_realloc(
            bucket->rules, sizeof(*bucket->rules) * next);
        bucket->rule_cap = next;
    }
    bucket->rules[bucket->rule_count++] = rule;
    return true;
}

static GsltCompiledBucket *compiled_bucket(
    CettaGsltCompiledProgram *program, const char *head, uint32_t arity,
    bool create) {
    for (uint32_t index = 0u; index < program->bucket_count; index++) {
        GsltCompiledBucket *bucket = &program->buckets[index];
        if (bucket->arity == arity && strcmp(bucket->head, head) == 0)
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
    return bucket;
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
    const CettaGsltCompiledProgram *program,
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
    bool valid = true;
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
        uint8_t *variables = rule->variable_count
            ? cetta_malloc(rule->variable_count) : NULL;
        if (variables)
            memset(variables, 0, rule->variable_count);
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
                if (valid)
                    variables[node->variable] = 1u;
            } else if (node->kind == GSLT_PLAN_APPLICATION) {
                for (uint32_t child = 0u; child < node->child_count; child++)
                    stack[stack_count++] = program->children[
                        node->child_offset + child];
            }
        }
        for (uint32_t variable = 0u;
             valid && variable < rule->variable_count; variable++)
            valid = variables[variable] != 0u;
        free(variables);
    }
    for (uint32_t node = 0u; valid && node < program->node_count; node++)
        valid = claimed[node] != 0u;
    for (uint32_t slot = 0u; valid && slot < program->body_count; slot++)
        valid = body_slots[slot] != 0u;
    free(stack);
    free(body_slots);
    free(claimed);
    if (!valid)
        return compiled_error(error, error_size,
                              "compiled GSLT rule forest is malformed");
    return true;
}

static bool compiled_build_buckets(CettaGsltCompiledProgram *program,
                                   char *error, size_t error_size) {
    for (uint32_t index = 0u; index < program->rule_count; index++) {
        GsltCompiledRule *rule = &program->rules[index];
        const GsltCompiledNode *head = &program->nodes[rule->head];
        GsltCompiledBucket *bucket = compiled_bucket(
            program, head->text, head->child_count, true);
        if (!bucket || !compiled_bucket_add(bucket, index))
            return compiled_error(error, error_size,
                                  "cannot index compiled GSLT rules");
    }
    return true;
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
        compiled_validate_rule_forest(program, error, error_size) &&
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
    for (uint32_t index = 0u; index < program->bucket_count; index++)
        free(program->buckets[index].rules);
    free(program->buckets);
    free(program->bodies);
    free(program->rules);
    free(program->children);
    free(program->nodes);
    free(program);
}

size_t cetta_gslt_compiled_program_rule_count(
    const CettaGsltCompiledProgram *program) {
    return program ? program->rule_count : 0u;
}

static Atom *compiled_materialize(
    const CettaGsltCompiledProgram *program, uint32_t node_index,
    Atom **variables, uint32_t variable_count,
    Arena *arena, uint32_t depth) {
    if (!program || node_index >= program->node_count || !arena ||
        depth > GSLT_PLAN_DEPTH_LIMIT)
        return NULL;
    const GsltCompiledNode *node = &program->nodes[node_index];
    switch (node->kind) {
    case GSLT_PLAN_SYMBOL:
        return atom_symbol(arena, node->text);
    case GSLT_PLAN_STRING:
        return atom_string(arena, node->text);
    case GSLT_PLAN_INTEGER:
        return atom_int(arena, node->integer);
    case GSLT_PLAN_VARIABLE:
        if (node->variable >= variable_count)
            return NULL;
        if (!variables[node->variable])
            variables[node->variable] = atom_var_with_id(
                arena, "gslt-plan", fresh_var_id());
        return variables[node->variable];
    case GSLT_PLAN_APPLICATION: {
        Atom **elements = arena_alloc(
            arena, sizeof(*elements) * (size_t)(node->child_count + 1u));
        elements[0] = atom_symbol(arena, node->text);
        for (uint32_t index = 0u; index < node->child_count; index++) {
            elements[index + 1u] = compiled_materialize(
                program, program->children[node->child_offset + index],
                variables, variable_count, arena, depth + 1u);
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
    const char *head = atom_name_cstr(goal->expr.elems[0]);
    uint32_t arity = (uint32_t)(goal->expr.len - 1u);
    for (uint32_t index = 0u; index < program->bucket_count; index++) {
        const GsltCompiledBucket *bucket = &program->buckets[index];
        if (bucket->arity == arity && strcmp(bucket->head, head) == 0)
            return bucket;
    }
    return NULL;
}

static bool compiled_queue_push(GsltCompiledQueue *queue,
                                GsltCompiledState state) {
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
    if (!program || !output_arena || !query || !result)
        return compiled_error(error, error_size,
                              "invalid compiled GSLT query request");
    memset(result, 0, sizeof(*result));
    if (limits.max_rule_attempts == 0u || limits.max_answers == 0u ||
        limits.max_depth == 0u)
        return compiled_error(error, error_size,
                              "compiled GSLT limits must be positive");
    Arena states;
    Arena scratch;
    arena_init(&states);
    arena_set_runtime_kind(&states, CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    arena_init(&scratch);
    arena_set_runtime_kind(&scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    GsltCompiledQueue queue = {0};
    Atom *initial_query = atom_deep_copy(&states, query);
    Atom **initial_goals = arena_alloc(&states, sizeof(*initial_goals));
    initial_goals[0] = initial_query;
    bool healthy = initial_query && compiled_queue_push(
        &queue, (GsltCompiledState){
            .query = initial_query,
            .goals = initial_goals,
            .goal_count = 1u,
            .depth = 0u,
        });
    bool stopped = false;
    while (healthy && !stopped && queue.head < queue.count) {
        GsltCompiledState state = queue.items[queue.head++];
        if (state.depth > result->max_depth_observed)
            result->max_depth_observed = state.depth;
        if (state.depth > limits.max_depth) {
            result->outcome = CETTA_GSLT_HORN_DEPTH_LIMIT;
            stopped = true;
            break;
        }
        if (state.goal_count == 0u) {
            if (!compiled_result_push(
                    result, output_arena, state.query, limits))
                stopped = true;
            continue;
        }
        const GsltCompiledBucket *bucket = compiled_find_bucket(
            program, state.goals[0]);
        if (!bucket)
            continue;
        for (uint32_t candidate = 0u;
             healthy && !stopped && candidate < bucket->rule_count;
             candidate++) {
            if (result->rule_attempts >= limits.max_rule_attempts) {
                result->outcome = CETTA_GSLT_HORN_RULE_LIMIT;
                stopped = true;
                break;
            }
            result->rule_attempts++;
            ArenaMark mark = arena_mark(&scratch);
            const GsltCompiledRule *rule =
                &program->rules[bucket->rules[candidate]];
            Atom **variables = rule->variable_count
                ? arena_alloc(
                    &scratch, sizeof(*variables) * rule->variable_count)
                : NULL;
            if (variables)
                memset(variables, 0,
                       sizeof(*variables) * rule->variable_count);
            Atom *head = compiled_materialize(
                program, rule->head, variables, rule->variable_count,
                &scratch, 0u);
            Atom **body = rule->body_count
                ? arena_alloc(&scratch, sizeof(*body) * rule->body_count)
                : NULL;
            bool materialized = head != NULL;
            for (uint32_t index = 0u;
                 materialized && index < rule->body_count; index++) {
                body[index] = compiled_materialize(
                    program, program->bodies[rule->body_offset + index],
                    variables, rule->variable_count, &scratch, 0u);
                materialized = body[index] != NULL;
            }
            Bindings bindings;
            bindings_init(&bindings);
            bool matched = materialized && match_atoms(
                state.goals[0], head, &bindings);
            if (matched && bindings.eq_len != 0u) {
                healthy = false;
                compiled_error(
                    error, error_size,
                    "compiled GSLT plan produced unsupported residual constraints");
                matched = false;
            }
            if (matched) {
                result->rule_matches++;
                uint64_t next_count_wide = (uint64_t)rule->body_count +
                    (uint64_t)state.goal_count - 1u;
                if (next_count_wide > UINT32_MAX ||
                    state.depth == UINT32_MAX) {
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
                        &states, sizeof(*next_goals) * next_count)
                    : NULL;
                Atom *next_query_scratch = bindings_apply_if_vars(
                    &bindings, &scratch, state.query);
                Atom *next_query = atom_deep_copy(
                    &states, next_query_scratch);
                healthy = next_query != NULL;
                for (uint32_t index = 0u;
                     healthy && index < rule->body_count; index++) {
                    Atom *resolved = bindings_apply_if_vars(
                        &bindings, &scratch, body[index]);
                    next_goals[index] = atom_deep_copy(&states, resolved);
                    healthy = next_goals[index] != NULL;
                }
                for (uint32_t index = 1u;
                     healthy && index < state.goal_count; index++) {
                    Atom *resolved = bindings_apply_if_vars(
                        &bindings, &scratch, state.goals[index]);
                    next_goals[rule->body_count + index - 1u] =
                        atom_deep_copy(&states, resolved);
                    healthy = next_goals[
                        rule->body_count + index - 1u] != NULL;
                }
                if (healthy)
                    healthy = compiled_queue_push(
                        &queue, (GsltCompiledState){
                            .query = next_query,
                            .goals = next_goals,
                            .goal_count = next_count,
                            .depth = state.depth + 1u,
                        });
            }
            bindings_free(&bindings);
            arena_reset(&scratch, mark);
        }
    }
    if (!healthy) {
        result->outcome = CETTA_GSLT_HORN_FAULT;
        if (!error || error_size == 0u || error[0] == '\0')
            compiled_error(error, error_size,
                           "compiled GSLT worklist exhausted host resources");
    }
    free(queue.items);
    arena_free(&scratch);
    arena_free(&states);
    return healthy;
}
