#include "gslt_horn_runtime.h"

#include "finite_horn_gslt_v1.h"
#include "match.h"
#include "parser.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    GSLT_HORN_QUOTE_DEPTH_LIMIT = 1048576,
};

typedef struct {
    Atom *clause;
    SymbolId head;
    CettaExprLen arity;
} GsltHornRule;

typedef struct {
    SymbolId head;
    CettaExprLen arity;
    uint32_t *rules;
    uint32_t rule_count;
    uint32_t rule_cap;
} GsltHornBucket;

struct CettaGsltHornProgram {
    Arena arena;
    GsltHornRule *rules;
    uint32_t rule_count;
    uint32_t rule_cap;
    GsltHornBucket *buckets;
    uint32_t bucket_count;
    uint32_t bucket_cap;
};

typedef struct {
    Atom **variables;
    uint32_t count;
    uint32_t cap;
} GsltHornQuoteVariables;

typedef struct {
    VarId *ids;
    uint32_t count;
    uint32_t cap;
} GsltHornSourceVariables;

typedef struct {
    const CettaGsltHornProgram *program;
    Arena *output;
    Arena scratch;
    Atom *query;
    CettaGsltHornLimits limits;
    CettaGsltHornResult *result;
    bool stopped;
    bool faulted;
} GsltHornRun;

static bool horn_error(char *buffer, size_t size, const char *format, ...) {
    if (buffer && size > 0u) {
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(buffer, size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool horn_symbol(const Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_SYMBOL &&
           strcmp(atom_name_cstr((Atom *)atom), name) == 0;
}

static bool horn_expr(const Atom *atom, const char *head,
                      CettaExprLen length) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == length &&
           horn_symbol(atom->expr.elems[0], head);
}

static bool horn_qindex(const Atom *atom, uint32_t *index) {
    uint64_t value = 0u;
    const Atom *cursor = atom;
    while (!horn_symbol(cursor, "q-zero")) {
        if (!horn_expr(cursor, "q-succ", 2u) || value == UINT32_MAX)
            return false;
        value++;
        cursor = cursor->expr.elems[1];
    }
    *index = (uint32_t)value;
    return true;
}

static Atom *horn_quote_variable(Arena *arena,
                                 GsltHornQuoteVariables *variables,
                                 uint32_t index) {
    if (index >= variables->cap) {
        uint32_t next = variables->cap ? variables->cap : 8u;
        while (next <= index) {
            if (next > UINT32_MAX / 2u)
                return NULL;
            next *= 2u;
        }
        variables->variables = cetta_realloc(
            variables->variables, sizeof(*variables->variables) * next);
        memset(variables->variables + variables->cap, 0,
               sizeof(*variables->variables) * (next - variables->cap));
        variables->cap = next;
    }
    if (!variables->variables[index]) {
        variables->variables[index] = atom_var_with_id(
            arena, "gslt", fresh_var_id());
        if (!variables->variables[index])
            return NULL;
    }
    if (index >= variables->count)
        variables->count = index + 1u;
    return variables->variables[index];
}

static bool horn_qlist_count(const Atom *list, uint32_t *count) {
    uint32_t length = 0u;
    const Atom *cursor = list;
    while (!horn_symbol(cursor, "q-nil")) {
        if (!horn_expr(cursor, "q-cons", 3u) || length == UINT32_MAX)
            return false;
        length++;
        cursor = cursor->expr.elems[2];
    }
    *count = length;
    return true;
}

static Atom *horn_materialize_quote(Arena *arena, const Atom *quoted,
                                    GsltHornQuoteVariables *variables,
                                    uint32_t depth);

static bool horn_materialize_qlist(Arena *arena, const Atom *list,
                                   GsltHornQuoteVariables *variables,
                                   Atom **items, uint32_t count,
                                   uint32_t depth) {
    const Atom *cursor = list;
    for (uint32_t index = 0u; index < count; index++) {
        if (!horn_expr(cursor, "q-cons", 3u))
            return false;
        items[index] = horn_materialize_quote(
            arena, cursor->expr.elems[1], variables, depth + 1u);
        if (!items[index])
            return false;
        cursor = cursor->expr.elems[2];
    }
    return horn_symbol(cursor, "q-nil");
}

static const char *horn_quote_text(const Atom *payload) {
    if (!payload)
        return NULL;
    if (horn_expr(payload, "q-str", 2u))
        return horn_quote_text(payload->expr.elems[1]);
    if (payload->kind == ATOM_SYMBOL)
        return atom_name_cstr((Atom *)payload);
    if (payload->kind == ATOM_GROUNDED &&
        payload->ground.gkind == GV_STRING)
        return payload->ground.sval;
    return NULL;
}

static Atom *horn_materialize_quote(Arena *arena, const Atom *quoted,
                                    GsltHornQuoteVariables *variables,
                                    uint32_t depth) {
    if (!quoted || depth > GSLT_HORN_QUOTE_DEPTH_LIMIT)
        return NULL;
    if (horn_expr(quoted, "q-var", 2u)) {
        uint32_t index;
        if (!horn_qindex(quoted->expr.elems[1], &index))
            return NULL;
        return horn_quote_variable(arena, variables, index);
    }
    if (horn_symbol(quoted, "q-empty"))
        return atom_expr(arena, NULL, 0u);
    if (horn_expr(quoted, "q-ground", 2u))
        return atom_deep_copy(arena, quoted->expr.elems[1]);
    if (horn_expr(quoted, "q-sym", 2u)) {
        const char *name = horn_quote_text(quoted->expr.elems[1]);
        return name ? atom_symbol(arena, name) : NULL;
    }
    if (horn_expr(quoted, "q-int", 2u)) {
        const Atom *value = quoted->expr.elems[1];
        return value && value->kind == ATOM_GROUNDED &&
                       value->ground.gkind == GV_INT
            ? atom_int(arena, value->ground.ival)
            : NULL;
    }
    if (horn_expr(quoted, "q-str", 2u)) {
        const char *value = horn_quote_text(quoted->expr.elems[1]);
        return value ? atom_string(arena, value) : NULL;
    }
    if (horn_expr(quoted, "q-app", 3u)) {
        uint32_t argument_count;
        if (!horn_qlist_count(quoted->expr.elems[2], &argument_count) ||
            argument_count == UINT32_MAX)
            return NULL;
        Atom **elements = arena_alloc(
            arena, sizeof(*elements) * (size_t)(argument_count + 1u));
        elements[0] = horn_materialize_quote(
            arena, quoted->expr.elems[1], variables, depth + 1u);
        if (!elements[0] ||
            !horn_materialize_qlist(
                arena, quoted->expr.elems[2], variables,
                elements + 1u, argument_count, depth + 1u))
            return NULL;
        return atom_expr(arena, elements,
                         (CettaExprLen)(argument_count + 1u));
    }
    return NULL;
}

static Atom *horn_quote_index(Arena *arena, uint32_t index) {
    Atom *result = atom_symbol(arena, "q-zero");
    for (uint32_t current = 0u; current < index; current++)
        result = atom_expr2(
            arena, atom_symbol(arena, "q-succ"), result);
    return result;
}

static bool horn_source_variable_index(
    GsltHornSourceVariables *variables, VarId id, uint32_t *index) {
    for (uint32_t current = 0u; current < variables->count; current++) {
        if (variables->ids[current] == id) {
            *index = current;
            return true;
        }
    }
    if (variables->count == variables->cap) {
        uint32_t next = variables->cap ? variables->cap * 2u : 8u;
        if (next < variables->cap)
            return false;
        variables->ids = cetta_realloc(
            variables->ids, sizeof(*variables->ids) * next);
        variables->cap = next;
    }
    *index = variables->count;
    variables->ids[variables->count++] = id;
    return true;
}

static Atom *horn_quote_atom(Arena *arena, const Atom *atom,
                             GsltHornSourceVariables *variables,
                             uint32_t depth);

static Atom *horn_quote_list(Arena *arena, Atom *const *items,
                             CettaExprLen count,
                             GsltHornSourceVariables *variables,
                             uint32_t depth) {
    Atom *result = atom_symbol(arena, "q-nil");
    for (CettaExprLen index = count; index > 0u; index--) {
        Atom *quoted = horn_quote_atom(
            arena, items[index - 1u], variables, depth + 1u);
        if (!quoted)
            return NULL;
        result = atom_expr3(
            arena, atom_symbol(arena, "q-cons"), quoted, result);
    }
    return result;
}

static Atom *horn_quote_atom(Arena *arena, const Atom *atom,
                             GsltHornSourceVariables *variables,
                             uint32_t depth) {
    if (!atom || depth > GSLT_HORN_QUOTE_DEPTH_LIMIT)
        return NULL;
    switch (atom->kind) {
    case ATOM_SYMBOL:
        return atom_expr2(
            arena, atom_symbol(arena, "q-sym"),
            atom_expr2(
                arena, atom_symbol(arena, "q-str"),
                atom_string(arena, atom_name_cstr((Atom *)atom))));
    case ATOM_VAR: {
        uint32_t index;
        if (!horn_source_variable_index(variables, atom->var_id, &index))
            return NULL;
        return atom_expr2(
            arena, atom_symbol(arena, "q-var"),
            horn_quote_index(arena, index));
    }
    case ATOM_EXPR: {
        if (atom->expr.len == 0u)
            return atom_symbol(arena, "q-empty");
        Atom *head = horn_quote_atom(
            arena, atom->expr.elems[0], variables, depth + 1u);
        Atom *arguments = horn_quote_list(
            arena, atom->expr.elems + 1u,
            (CettaExprLen)(atom->expr.len - 1u),
            variables, depth + 1u);
        if (!head || !arguments)
            return NULL;
        return atom_expr3(
            arena, atom_symbol(arena, "q-app"),
            head, arguments);
    }
    case ATOM_GROUNDED:
        if (atom->ground.gkind == GV_INT)
            return atom_expr2(
                arena, atom_symbol(arena, "q-int"),
                atom_int(arena, atom->ground.ival));
        if (atom->ground.gkind == GV_STRING)
            return atom_expr2(
                arena, atom_symbol(arena, "q-str"),
                atom_string(arena, atom->ground.sval));
        return atom_expr2(
            arena, atom_symbol(arena, "q-ground"),
            atom_deep_copy(arena, (Atom *)atom));
    }
    return NULL;
}

Atom *cetta_gslt_quote_atom_v1(Arena *arena, const Atom *atom) {
    if (!arena || !atom)
        return NULL;
    GsltHornSourceVariables variables = {0};
    Atom *result = horn_quote_atom(arena, atom, &variables, 0u);
    free(variables.ids);
    return result;
}

Atom *cetta_gslt_unquote_atom_v1(Arena *arena, const Atom *quoted) {
    if (!arena || !quoted)
        return NULL;
    GsltHornQuoteVariables variables = {0};
    Atom *result = horn_materialize_quote(arena, quoted, &variables, 0u);
    free(variables.variables);
    return result;
}

static bool horn_qlist_items(const Atom *list, const Atom ***items,
                             uint32_t *count) {
    if (!horn_qlist_count(list, count))
        return false;
    const Atom **result = *count
        ? cetta_malloc(sizeof(*result) * *count)
        : NULL;
    const Atom *cursor = list;
    for (uint32_t index = 0u; index < *count; index++) {
        result[index] = cursor->expr.elems[1];
        cursor = cursor->expr.elems[2];
    }
    *items = result;
    return true;
}

static GsltHornBucket *horn_bucket(
    CettaGsltHornProgram *program, SymbolId head, CettaExprLen arity,
    bool create) {
    for (uint32_t index = 0u; index < program->bucket_count; index++) {
        GsltHornBucket *bucket = &program->buckets[index];
        if (bucket->head == head && bucket->arity == arity)
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
    GsltHornBucket *bucket = &program->buckets[program->bucket_count++];
    bucket->head = head;
    bucket->arity = arity;
    return bucket;
}

static bool horn_bucket_add(GsltHornBucket *bucket, uint32_t rule) {
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

static bool horn_program_add_quoted_rule(
    CettaGsltHornProgram *program, const Atom *quoted,
    char *error, size_t error_size) {
    if (!horn_expr(quoted, "q-rule", 4u))
        return horn_error(error, error_size, "malformed q-rule artifact");
    const Atom **quoted_body = NULL;
    uint32_t body_count = 0u;
    GsltHornQuoteVariables variables = {0};
    if (!horn_qlist_items(quoted->expr.elems[3], &quoted_body, &body_count))
        return horn_error(error, error_size, "malformed q-rule body");

    Atom **clause_items = arena_alloc(
        &program->arena, sizeof(*clause_items) * (size_t)(body_count + 2u));
    clause_items[0] = atom_symbol(&program->arena, "gslt-horn-clause");
    clause_items[1] = horn_materialize_quote(
        &program->arena, quoted->expr.elems[2], &variables, 0u);
    bool ok = clause_items[1] != NULL;
    for (uint32_t index = 0u; ok && index < body_count; index++) {
        clause_items[index + 2u] = horn_materialize_quote(
            &program->arena, quoted_body[index], &variables, 0u);
        ok = clause_items[index + 2u] != NULL;
    }
    free(variables.variables);
    free(quoted_body);
    if (!ok || clause_items[1]->kind != ATOM_EXPR ||
        clause_items[1]->expr.len == 0u ||
        clause_items[1]->expr.elems[0]->kind != ATOM_SYMBOL)
        return horn_error(error, error_size,
                          "q-rule did not materialize a relational head");

    if (program->rule_count == program->rule_cap) {
        uint32_t next = program->rule_cap ? program->rule_cap * 2u : 32u;
        if (next < program->rule_cap)
            return horn_error(error, error_size, "too many GSLT rules");
        program->rules = cetta_realloc(
            program->rules, sizeof(*program->rules) * next);
        program->rule_cap = next;
    }
    Atom *head = clause_items[1];
    GsltHornRule *rule = &program->rules[program->rule_count];
    *rule = (GsltHornRule){
        .clause = atom_expr(
            &program->arena, clause_items,
            (CettaExprLen)(body_count + 2u)),
        .head = head->expr.elems[0]->sym_id,
        .arity = (CettaExprLen)(head->expr.len - 1u),
    };
    GsltHornBucket *bucket = horn_bucket(
        program, rule->head, rule->arity, true);
    if (!rule->clause || !bucket ||
        !horn_bucket_add(bucket, program->rule_count))
        return horn_error(error, error_size,
                          "cannot index GSLT rule");
    program->rule_count++;
    return true;
}

static bool horn_program_add_quoted_list(
    CettaGsltHornProgram *program, const Atom *list,
    char *error, size_t error_size) {
    const Atom *cursor = list;
    while (!horn_symbol(cursor, "q-nil")) {
        if (!horn_expr(cursor, "q-cons", 3u) ||
            !horn_program_add_quoted_rule(
                program, cursor->expr.elems[1], error, error_size))
            return false;
        cursor = cursor->expr.elems[2];
    }
    return true;
}

static bool horn_program_from_package(
    FHGSLTPackage *package, CettaGsltHornProgram **out,
    char *error, size_t error_size) {
    CettaGsltHornProgram *program = NULL;
    program = cetta_malloc(sizeof(*program));
    memset(program, 0, sizeof(*program));
    arena_init(&program->arena);
    arena_set_runtime_kind(&program->arena,
                           CETTA_ARENA_RUNTIME_KIND_PERSISTENT);
    for (size_t index = 0u;
         index < fhgslt_package_presentation_count(package);
         index++) {
        uint8_t *quoted = NULL;
        size_t quoted_len = 0u;
        Atom **forms = NULL;
        if (!fhgslt_package_quoted_rules(
                package, index, &quoted, &quoted_len,
                error, error_size))
            goto fail;
        char *text = cetta_malloc(quoted_len + 1u);
        memcpy(text, quoted, quoted_len);
        text[quoted_len] = '\0';
        free(quoted);
        int form_count = parse_metta_text(text, &program->arena, &forms);
        free(text);
        if (form_count != 1 || !forms ||
            !horn_program_add_quoted_list(
                program, forms[0], error, error_size)) {
            if (!error || error_size == 0u || error[0] == '\0')
                horn_error(error, error_size,
                           "cannot read quoted GSLT rules");
            free(forms);
            goto fail;
        }
        free(forms);
    }
    *out = program;
    return true;

fail:
    cetta_gslt_horn_program_free(program);
    return false;
}

bool cetta_gslt_horn_program_load_paths(
    const char *const *paths, size_t path_count,
    CettaGsltHornProgram **out, char *error, size_t error_size) {
    FHGSLTPackage *package = NULL;
    if (!out || !paths || path_count == 0u)
        return horn_error(error, error_size,
                          "invalid GSLT Horn program request");
    *out = NULL;
    if (!fhgslt_package_from_paths(
            paths, path_count, &package, error, error_size))
        return false;
    bool ok = horn_program_from_package(package, out, error, error_size);
    fhgslt_package_free(package);
    return ok;
}

bool cetta_gslt_horn_program_load_inputs(
    const CettaGsltHornInput *inputs, size_t input_count,
    CettaGsltHornProgram **out, char *error, size_t error_size) {
    FHGSLTPackage *package = NULL;
    FHGSLTInput *native_inputs = NULL;
    if (!out || !inputs || input_count == 0u)
        return horn_error(error, error_size,
                          "invalid GSLT Horn input request");
    *out = NULL;
    native_inputs = cetta_malloc(sizeof(*native_inputs) * input_count);
    for (size_t index = 0u; index < input_count; index++) {
        native_inputs[index] = (FHGSLTInput){
            .bytes = inputs[index].bytes,
            .len = inputs[index].length,
            .source = inputs[index].source,
        };
    }
    bool loaded = fhgslt_package_from_inputs(
        native_inputs, input_count, &package, error, error_size);
    free(native_inputs);
    if (!loaded)
        return false;
    bool ok = horn_program_from_package(package, out, error, error_size);
    fhgslt_package_free(package);
    return ok;
}

void cetta_gslt_horn_program_free(CettaGsltHornProgram *program) {
    if (!program)
        return;
    for (uint32_t index = 0u; index < program->bucket_count; index++)
        free(program->buckets[index].rules);
    free(program->buckets);
    free(program->rules);
    arena_free(&program->arena);
    free(program);
}

size_t cetta_gslt_horn_program_rule_count(
    const CettaGsltHornProgram *program) {
    return program ? program->rule_count : 0u;
}

static const GsltHornBucket *horn_find_bucket(
    const CettaGsltHornProgram *program, Atom *goal) {
    if (!goal || goal->kind != ATOM_EXPR || goal->expr.len == 0u ||
        goal->expr.elems[0]->kind != ATOM_SYMBOL)
        return NULL;
    SymbolId head = goal->expr.elems[0]->sym_id;
    CettaExprLen arity = (CettaExprLen)(goal->expr.len - 1u);
    for (uint32_t index = 0u; index < program->bucket_count; index++) {
        const GsltHornBucket *bucket = &program->buckets[index];
        if (bucket->head == head && bucket->arity == arity)
            return bucket;
    }
    return NULL;
}

static bool horn_result_push(GsltHornRun *run,
                             const Bindings *bindings) {
    if (run->result->answer_count >= run->limits.max_answers) {
        run->result->outcome = CETTA_GSLT_HORN_ANSWER_LIMIT;
        run->stopped = true;
        return false;
    }
    Atom *resolved = bindings_apply_if_vars(
        bindings, run->output, run->query);
    Atom *answer = resolved
        ? atom_deep_copy(run->output, resolved)
        : NULL;
    if (!answer) {
        run->result->outcome = CETTA_GSLT_HORN_FAULT;
        run->faulted = true;
        run->stopped = true;
        return false;
    }
    run->result->answers = cetta_realloc(
        run->result->answers,
        sizeof(*run->result->answers) *
            (run->result->answer_count + 1u));
    run->result->answers[run->result->answer_count++] = answer;
    return true;
}

static void horn_solve(GsltHornRun *run, Atom *const *goals,
                       uint32_t goal_count, BindingsBuilder *builder,
                       uint32_t depth) {
    if (run->stopped)
        return;
    if (depth > run->result->max_depth_observed)
        run->result->max_depth_observed = depth;
    if (depth > run->limits.max_depth) {
        run->result->outcome = CETTA_GSLT_HORN_DEPTH_LIMIT;
        run->stopped = true;
        return;
    }
    if (goal_count == 0u) {
        (void)horn_result_push(run, bindings_builder_bindings(builder));
        return;
    }

    ArenaMark goal_mark = arena_mark(&run->scratch);
    Atom *goal = bindings_apply_if_vars(
        bindings_builder_bindings(builder), &run->scratch, goals[0]);
    const GsltHornBucket *bucket = horn_find_bucket(run->program, goal);
    if (!bucket) {
        arena_reset(&run->scratch, goal_mark);
        return;
    }
    for (uint32_t candidate = 0u;
         candidate < bucket->rule_count && !run->stopped;
         candidate++) {
        if (run->result->rule_attempts >= run->limits.max_rule_attempts) {
            run->result->outcome = CETTA_GSLT_HORN_RULE_LIMIT;
            run->stopped = true;
            break;
        }
        run->result->rule_attempts++;
        uint32_t binding_mark = bindings_builder_save(builder);
        ArenaMark rule_mark = arena_mark(&run->scratch);
        const GsltHornRule *rule =
            &run->program->rules[bucket->rules[candidate]];
        Atom *fresh_clause = rename_vars(
            &run->scratch, rule->clause, fresh_var_suffix());
        if (!fresh_clause || fresh_clause->kind != ATOM_EXPR ||
            fresh_clause->expr.len < 2u) {
            run->result->outcome = CETTA_GSLT_HORN_FAULT;
            run->faulted = true;
            run->stopped = true;
        } else if (match_atoms_builder(
                       goal, fresh_clause->expr.elems[1], builder)) {
            run->result->rule_matches++;
            uint32_t body_count =
                (uint32_t)(fresh_clause->expr.len - 2u);
            uint32_t next_count = body_count + goal_count - 1u;
            Atom **next = next_count
                ? arena_alloc(&run->scratch, sizeof(*next) * next_count)
                : NULL;
            for (uint32_t index = 0u; index < body_count; index++)
                next[index] = fresh_clause->expr.elems[index + 2u];
            for (uint32_t index = 1u; index < goal_count; index++)
                next[body_count + index - 1u] = goals[index];
            horn_solve(run, next, next_count, builder, depth + 1u);
        }
        bindings_builder_rollback(builder, binding_mark);
        arena_reset(&run->scratch, rule_mark);
    }
    arena_reset(&run->scratch, goal_mark);
}

bool cetta_gslt_horn_query(
    const CettaGsltHornProgram *program, Arena *output_arena,
    Atom *query, CettaGsltHornLimits limits,
    CettaGsltHornResult *result, char *error, size_t error_size) {
    if (!program || !output_arena || !query || !result)
        return horn_error(error, error_size,
                          "invalid GSLT Horn query request");
    memset(result, 0, sizeof(*result));
    if (limits.max_rule_attempts == 0u || limits.max_answers == 0u ||
        limits.max_depth == 0u)
        return horn_error(error, error_size,
                          "GSLT Horn limits must be positive");

    GsltHornRun run = {
        .program = program,
        .output = output_arena,
        .query = query,
        .limits = limits,
        .result = result,
    };
    arena_init(&run.scratch);
    arena_set_runtime_kind(&run.scratch, CETTA_ARENA_RUNTIME_KIND_SCRATCH);
    Bindings empty;
    BindingsBuilder builder;
    bindings_init(&empty);
    if (!bindings_builder_init(&builder, &empty)) {
        bindings_free(&empty);
        arena_free(&run.scratch);
        return horn_error(error, error_size,
                          "cannot initialize GSLT Horn bindings");
    }
    Atom *goals[1] = {query};
    horn_solve(&run, goals, 1u, &builder, 0u);
    bindings_builder_free(&builder);
    bindings_free(&empty);
    arena_free(&run.scratch);
    if (run.faulted) {
        horn_error(error, error_size,
                   "GSLT Horn execution faulted");
        return false;
    }
    return true;
}

void cetta_gslt_horn_result_free(CettaGsltHornResult *result) {
    if (!result)
        return;
    free(result->answers);
    memset(result, 0, sizeof(*result));
}
