#define _POSIX_C_SOURCE 200809L

#include "parser_pack_abi_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "native_sha256.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PPABI_V1_MAX_DEPTH = 4096 };

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} PPABIV1Buffer;

typedef struct {
    Atom *term;
    char *canonical;
    bool defined;
} PPABIV1IdentityTemp;

typedef struct {
    PPABIV1IdentityTemp *data;
    size_t len;
    size_t cap;
} PPABIV1IdentityVec;

typedef struct {
    Atom *term;
    char *canonical;
} PPABIV1CanonicalInput;

typedef struct {
    Atom *term;
    Atom *label;
    Atom *lhs;
    Atom *items;
    Atom *action;
    uint32_t item_len;
} PPABIV1ParsedProduction;

typedef struct {
    Atom *term;
    Atom *class_identity;
    PPABIV1ClassClauseKind kind;
    uint32_t point;
    uint32_t *excluded_points;
    uint32_t excluded_len;
} PPABIV1ParsedClassClause;

typedef struct {
    const PPABIV1DerivationInput *input;
    char *canonical;
    uint32_t artifact_index;
} PPABIV1EvidenceTemp;

static void ppabi_v1_set_error(char *buf, size_t size, const char *fmt, ...) {
    va_list args;

    if (!buf || size == 0u)
        return;
    va_start(args, fmt);
    (void)vsnprintf(buf, size, fmt, args);
    va_end(args);
}

static char *ppabi_v1_text_dup(const char *text) {
    size_t len;
    char *copy;

    if (!text)
        return NULL;
    len = strlen(text);
    copy = (char *)malloc(len + 1u);
    if (!copy)
        return NULL;
    memcpy(copy, text, len + 1u);
    return copy;
}

static bool ppabi_v1_buffer_reserve(PPABIV1Buffer *buffer, size_t extra) {
    size_t needed;
    size_t cap;
    char *next;

    if (extra > SIZE_MAX - buffer->len)
        return false;
    needed = buffer->len + extra;
    if (needed <= buffer->cap)
        return true;
    cap = buffer->cap ? buffer->cap : 256u;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2u) {
            cap = needed;
            break;
        }
        cap *= 2u;
    }
    next = (char *)realloc(buffer->data, cap);
    if (!next)
        return false;
    buffer->data = next;
    buffer->cap = cap;
    return true;
}

static bool ppabi_v1_buffer_bytes(PPABIV1Buffer *buffer,
                                  const char *bytes,
                                  size_t len) {
    if (!ppabi_v1_buffer_reserve(buffer, len))
        return false;
    if (len > 0u)
        memcpy(buffer->data + buffer->len, bytes, len);
    buffer->len += len;
    return true;
}

static bool ppabi_v1_buffer_literal(PPABIV1Buffer *buffer,
                                    const char *text) {
    return ppabi_v1_buffer_bytes(buffer, text, strlen(text));
}

static bool ppabi_v1_buffer_char(PPABIV1Buffer *buffer, char value) {
    return ppabi_v1_buffer_bytes(buffer, &value, 1u);
}

static char *ppabi_v1_canonical_atom(Atom *atom) {
    uint8_t *bytes = NULL;
    size_t len = 0u;

    if (!fh_ground_term_v1_render(atom, &bytes, &len, NULL, 0u))
        return NULL;
    return (char *)bytes;
}

static bool ppabi_v1_expr_head(Atom *atom,
                               const char *head,
                               CettaExprLen argument_len) {
    return atom && atom->kind == ATOM_EXPR &&
           atom->expr.len == argument_len + 1u &&
           atom_is_symbol(atom->expr.elems[0], head);
}

static bool ppabi_v1_codepoint(Atom *atom, uint32_t *out) {
    int64_t value;

    if (!ppabi_v1_expr_head(atom, "cp", 1u) ||
        !atom->expr.elems[1] || atom->expr.elems[1]->kind != ATOM_GROUNDED ||
        atom->expr.elems[1]->ground.gkind != GV_INT) {
        return false;
    }
    value = atom->expr.elems[1]->ground.ival;
    if (value < 0 || value > 0x10ffff ||
        (value >= 0xd800 && value <= 0xdfff)) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool ppabi_v1_qindex(Atom *atom, uint32_t *out) {
    uint32_t value = 0u;

    while (!atom_is_symbol(atom, "q-zero")) {
        if (!ppabi_v1_expr_head(atom, "q-succ", 1u) || value == UINT32_MAX)
            return false;
        value++;
        atom = atom->expr.elems[1];
    }
    *out = value;
    return true;
}

static bool ppabi_v1_action_list_valid(Atom *list,
                                       uint32_t arity,
                                       unsigned depth);

static bool ppabi_v1_action_valid(Atom *action,
                                  uint32_t arity,
                                  unsigned depth) {
    uint32_t slot;

    if (!action || depth > PPABI_V1_MAX_DEPTH)
        return false;
    if (ppabi_v1_expr_head(action, "pa-slot", 1u))
        return ppabi_v1_qindex(action->expr.elems[1], &slot) && slot < arity;
    if (ppabi_v1_expr_head(action, "pa-const", 1u)) {
        char *canonical = ppabi_v1_canonical_atom(action->expr.elems[1]);
        bool valid = canonical != NULL;
        free(canonical);
        return valid;
    }
    if (ppabi_v1_expr_head(action, "pa-apply", 2u)) {
        return action->expr.elems[1]->kind == ATOM_SYMBOL &&
               ppabi_v1_action_list_valid(action->expr.elems[2], arity,
                                          depth + 1u);
    }
    return false;
}

static bool ppabi_v1_action_list_valid(Atom *list,
                                       uint32_t arity,
                                       unsigned depth) {
    while (!atom_is_symbol(list, "pa-nil")) {
        if (depth > PPABI_V1_MAX_DEPTH ||
            !ppabi_v1_expr_head(list, "pa-cons", 2u) ||
            !ppabi_v1_action_valid(list->expr.elems[1], arity, depth + 1u)) {
            return false;
        }
        list = list->expr.elems[2];
        depth++;
    }
    return true;
}

static bool ppabi_v1_identity_push(PPABIV1IdentityVec *vec,
                                   Atom *term,
                                   bool defined) {
    PPABIV1IdentityTemp *next;
    char *canonical = ppabi_v1_canonical_atom(term);

    if (!canonical)
        return false;
    if (vec->len == vec->cap) {
        size_t cap = vec->cap ? vec->cap * 2u : 16u;
        if (cap < vec->cap || cap > SIZE_MAX / sizeof(*vec->data)) {
            free(canonical);
            return false;
        }
        next = (PPABIV1IdentityTemp *)realloc(
            vec->data, cap * sizeof(*vec->data));
        if (!next) {
            free(canonical);
            return false;
        }
        vec->data = next;
        vec->cap = cap;
    }
    vec->data[vec->len].term = term;
    vec->data[vec->len].canonical = canonical;
    vec->data[vec->len].defined = defined;
    vec->len++;
    return true;
}

static int ppabi_v1_identity_compare(const void *lhs, const void *rhs) {
    const PPABIV1IdentityTemp *left = (const PPABIV1IdentityTemp *)lhs;
    const PPABIV1IdentityTemp *right = (const PPABIV1IdentityTemp *)rhs;
    return strcmp(left->canonical, right->canonical);
}

static int ppabi_v1_evidence_compare(const void *lhs, const void *rhs) {
    const PPABIV1EvidenceTemp *left = (const PPABIV1EvidenceTemp *)lhs;
    const PPABIV1EvidenceTemp *right = (const PPABIV1EvidenceTemp *)rhs;
    return strcmp(left->canonical, right->canonical);
}

static void ppabi_v1_identity_vec_free(PPABIV1IdentityVec *vec) {
    size_t index;

    for (index = 0u; index < vec->len; index++)
        free(vec->data[index].canonical);
    free(vec->data);
    memset(vec, 0, sizeof(*vec));
}

static bool ppabi_v1_identity_vec_sort_unique(PPABIV1IdentityVec *vec,
                                              bool reject_duplicates,
                                              char *error_buf,
                                              size_t error_buf_size,
                                              const char *what) {
    size_t read;
    size_t write = 0u;

    if (vec->len > 1u) {
        qsort(vec->data, vec->len, sizeof(*vec->data),
              ppabi_v1_identity_compare);
    }
    for (read = 0u; read < vec->len; read++) {
        if (write > 0u &&
            strcmp(vec->data[write - 1u].canonical,
                   vec->data[read].canonical) == 0) {
            if (!atom_eq(vec->data[write - 1u].term, vec->data[read].term)) {
                ppabi_v1_set_error(error_buf, error_buf_size,
                                   "%s canonical-render collision", what);
                return false;
            }
            if (reject_duplicates) {
                ppabi_v1_set_error(error_buf, error_buf_size,
                                   "duplicate %s identity: %s", what,
                                   vec->data[read].canonical);
                return false;
            }
            vec->data[write - 1u].defined =
                vec->data[write - 1u].defined || vec->data[read].defined;
            free(vec->data[read].canonical);
            vec->data[read].canonical = NULL;
            continue;
        }
        if (write != read)
            vec->data[write] = vec->data[read];
        write++;
    }
    vec->len = write;
    return true;
}

static int32_t ppabi_v1_identity_find(const PPABIV1IdentityTemp *data,
                                      size_t len,
                                      Atom *term) {
    char *canonical = ppabi_v1_canonical_atom(term);
    size_t low = 0u;
    size_t high = len;

    if (!canonical)
        return -1;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        int order = strcmp(canonical, data[middle].canonical);
        if (order < 0)
            high = middle;
        else if (order > 0)
            low = middle + 1u;
        else {
            int32_t result = atom_eq(term, data[middle].term)
                ? (int32_t)middle : -1;
            free(canonical);
            return result;
        }
    }
    free(canonical);
    return -1;
}

static bool ppabi_v1_terminal_valid(Atom *matcher,
                                    PPABIV1TerminalKind *kind,
                                    uint32_t *codepoint,
                                    Atom **class_expression) {
    if (atom_is_symbol(matcher, "pp-terminal-any")) {
        *kind = PPABI_V1_TERMINAL_ANY;
        return true;
    }
    if (atom_is_symbol(matcher, "pp-terminal-eof")) {
        *kind = PPABI_V1_TERMINAL_EOF;
        return true;
    }
    if (ppabi_v1_expr_head(matcher, "pp-terminal-char", 1u) &&
        ppabi_v1_codepoint(matcher->expr.elems[1], codepoint)) {
        *kind = PPABI_V1_TERMINAL_CHAR;
        return true;
    }
    if (ppabi_v1_expr_head(matcher, "pp-terminal-class", 1u)) {
        char *canonical =
            ppabi_v1_canonical_atom(matcher->expr.elems[1]);
        if (canonical) {
            free(canonical);
            *kind = PPABI_V1_TERMINAL_CLASS;
            *class_expression = matcher->expr.elems[1];
            return true;
        }
    }
    return false;
}

static bool ppabi_v1_scan_items(Atom *items,
                                PPABIV1IdentityVec *states,
                                PPABIV1IdentityVec *terminals,
                                uint32_t *item_len,
                                char *error_buf,
                                size_t error_buf_size) {
    uint32_t count = 0u;

    while (!atom_is_symbol(items, "pp-items-nil")) {
        Atom *item;
        PPABIV1TerminalKind terminal_kind;
        uint32_t codepoint = 0u;
        Atom *class_expression = NULL;

        if (!ppabi_v1_expr_head(items, "pp-items-cons", 2u) ||
            count == UINT32_MAX) {
            ppabi_v1_set_error(error_buf, error_buf_size,
                               "ParserPack items are not a finite proper list");
            return false;
        }
        item = items->expr.elems[1];
        if (ppabi_v1_expr_head(item, "pp-nonterminal", 1u)) {
            if (!ppabi_v1_identity_push(states, item->expr.elems[1], false))
                return false;
        } else if (ppabi_v1_expr_head(item, "pp-terminal", 1u) &&
                   ppabi_v1_terminal_valid(item->expr.elems[1],
                                           &terminal_kind, &codepoint,
                                           &class_expression)) {
            if (!ppabi_v1_identity_push(terminals, item->expr.elems[1], false))
                return false;
        } else {
            ppabi_v1_set_error(error_buf, error_buf_size,
                               "invalid ParserPack item");
            return false;
        }
        items = items->expr.elems[2];
        count++;
    }
    *item_len = count;
    return true;
}

static bool ppabi_v1_points(Atom *points,
                            uint32_t **out,
                            uint32_t *out_len,
                            char *error_buf,
                            size_t error_buf_size) {
    uint32_t *values = NULL;
    uint32_t len = 0u;
    uint32_t cap = 0u;
    uint32_t previous = 0u;

    while (!atom_is_symbol(points, "pp-points-nil")) {
        uint32_t value;
        uint32_t *next;

        if (!ppabi_v1_expr_head(points, "pp-points-cons", 2u) ||
            !ppabi_v1_codepoint(points->expr.elems[1], &value) ||
            (len > 0u && value <= previous)) {
            free(values);
            ppabi_v1_set_error(
                error_buf, error_buf_size,
                "class exclusions must be strictly increasing Unicode scalars");
            return false;
        }
        if (len == cap) {
            uint32_t next_cap = cap ? cap * 2u : 8u;
            if (next_cap < cap) {
                free(values);
                return false;
            }
            next = (uint32_t *)realloc(
                values, (size_t)next_cap * sizeof(*values));
            if (!next) {
                free(values);
                return false;
            }
            values = next;
            cap = next_cap;
        }
        values[len++] = value;
        previous = value;
        points = points->expr.elems[2];
    }
    *out = values;
    *out_len = len;
    return true;
}

static bool ppabi_v1_digest_valid(const char *digest) {
    size_t index;

    if (!digest || strlen(digest) != 64u)
        return false;
    for (index = 0u; index < 64u; index++) {
        if (!((digest[index] >= '0' && digest[index] <= '9') ||
              (digest[index] >= 'a' && digest[index] <= 'f'))) {
            return false;
        }
    }
    return true;
}

static bool ppabi_v1_inputs_canonical(Atom *const *terms,
                                      size_t len,
                                      PPABIV1CanonicalInput **out,
                                      char *error_buf,
                                      size_t error_buf_size,
                                      const char *what) {
    PPABIV1CanonicalInput *inputs;
    size_t index;

    if (len > UINT32_MAX || (len > 0u && !terms)) {
        ppabi_v1_set_error(error_buf, error_buf_size,
                           "%s collection is too large or absent", what);
        return false;
    }
    inputs = (PPABIV1CanonicalInput *)calloc(
        len ? len : 1u, sizeof(*inputs));
    if (!inputs)
        return false;
    for (index = 0u; index < len; index++) {
        inputs[index].term = terms[index];
        inputs[index].canonical = ppabi_v1_canonical_atom(terms[index]);
        if (!inputs[index].canonical) {
            ppabi_v1_set_error(
                error_buf, error_buf_size,
                "%s contains a non-ground or non-finite-Horn term", what);
            goto fail;
        }
        if (index > 0u &&
            strcmp(inputs[index - 1u].canonical,
                   inputs[index].canonical) >= 0) {
            ppabi_v1_set_error(error_buf, error_buf_size,
                               "%s is not in strict canonical byte order",
                               what);
            goto fail;
        }
    }
    *out = inputs;
    return true;

fail:
    for (index = 0u; index < len; index++)
        free(inputs[index].canonical);
    free(inputs);
    return false;
}

static void ppabi_v1_canonical_inputs_free(PPABIV1CanonicalInput *inputs,
                                           size_t len) {
    size_t index;

    if (!inputs)
        return;
    for (index = 0u; index < len; index++)
        free(inputs[index].canonical);
    free(inputs);
}

static int32_t ppabi_v1_pack_artifact_find(const PPABIV1Pack *pack,
                                           PPABIV1EvidenceKind kind,
                                           Atom *artifact,
                                           const char *canonical) {
    uint32_t index;

    if (kind == PPABI_V1_EVIDENCE_PRODUCTION) {
        for (index = 0u; index < pack->production_len; index++) {
            if (strcmp(canonical, pack->productions[index].canonical) == 0 &&
                atom_eq(artifact, pack->productions[index].identity)) {
                return (int32_t)index;
            }
        }
    } else if (kind == PPABI_V1_EVIDENCE_CLASS) {
        for (index = 0u; index < pack->class_clause_len; index++) {
            if (strcmp(canonical, pack->class_clauses[index].canonical) == 0 &&
                atom_eq(artifact, pack->class_clauses[index].identity)) {
                return (int32_t)index;
            }
        }
    }
    return -1;
}

static bool ppabi_v1_answer_matches(PPABIV1EvidenceKind kind,
                                    Atom *answer,
                                    Atom *artifact) {
    const char *relation = kind == PPABI_V1_EVIDENCE_PRODUCTION
        ? "compile-pack-production" : "compile-pack-class-clause";
    return ppabi_v1_expr_head(answer, relation, 2u) &&
           atom_eq(answer->expr.elems[2], artifact);
}

static bool ppabi_v1_evidence_key(PPABIV1EvidenceKind kind,
                                  const char *artifact,
                                  Atom *answer,
                                  Atom *certificate,
                                  char **out) {
    PPABIV1Buffer buffer = {0};
    char *answer_text = ppabi_v1_canonical_atom(answer);
    char *certificate_text = ppabi_v1_canonical_atom(certificate);
    char kind_byte = kind == PPABI_V1_EVIDENCE_PRODUCTION ? 'p' : 'c';
    char length_text[64];
    int length;

    if (!answer_text || !certificate_text ||
        !ppabi_v1_buffer_char(&buffer, kind_byte))
        goto fail;
    length = snprintf(length_text, sizeof(length_text), "%zu:", strlen(artifact));
    if (length <= 0 || (size_t)length >= sizeof(length_text) ||
        !ppabi_v1_buffer_bytes(&buffer, length_text, (size_t)length) ||
        !ppabi_v1_buffer_literal(&buffer, artifact))
        goto fail;
    length = snprintf(length_text, sizeof(length_text), "%zu:", strlen(answer_text));
    if (length <= 0 || (size_t)length >= sizeof(length_text) ||
        !ppabi_v1_buffer_bytes(&buffer, length_text, (size_t)length) ||
        !ppabi_v1_buffer_literal(&buffer, answer_text))
        goto fail;
    length = snprintf(length_text, sizeof(length_text), "%zu:",
                      strlen(certificate_text));
    if (length <= 0 || (size_t)length >= sizeof(length_text) ||
        !ppabi_v1_buffer_bytes(&buffer, length_text, (size_t)length) ||
        !ppabi_v1_buffer_literal(&buffer, certificate_text) ||
        !ppabi_v1_buffer_char(&buffer, '\0'))
        goto fail;
    free(answer_text);
    free(certificate_text);
    *out = buffer.data;
    return true;

fail:
    free(answer_text);
    free(certificate_text);
    free(buffer.data);
    return false;
}

static bool ppabi_v1_class_leaf_defined(const PPABIV1Pack *pack,
                                        Atom *expression,
                                        unsigned depth) {
    uint32_t index;

    if (depth > PPABI_V1_MAX_DEPTH)
        return false;
    if (ppabi_v1_expr_head(expression, "c-union", 2u)) {
        return ppabi_v1_class_leaf_defined(
                   pack, expression->expr.elems[1], depth + 1u) &&
               ppabi_v1_class_leaf_defined(
                   pack, expression->expr.elems[2], depth + 1u);
    }
    for (index = 0u; index < pack->class_clause_len; index++) {
        if (atom_eq(expression, pack->class_clauses[index].class_identity))
            return true;
    }
    return false;
}

static bool ppabi_v1_render_term_list(PPABIV1Buffer *buffer,
                                      const char *const *terms,
                                      size_t len) {
    size_t index;

    for (index = 0u; index < len; index++) {
        if (!ppabi_v1_buffer_literal(buffer, "(cons ") ||
            !ppabi_v1_buffer_literal(buffer, terms[index]) ||
            !ppabi_v1_buffer_char(buffer, ' ')) {
            return false;
        }
    }
    if (!ppabi_v1_buffer_literal(buffer, "nil"))
        return false;
    for (index = 0u; index < len; index++) {
        if (!ppabi_v1_buffer_char(buffer, ')'))
            return false;
    }
    return true;
}

static bool ppabi_v1_compute_pack_digest(PPABIV1Pack *pack) {
    PPABIV1Buffer buffer = {0};
    const char **productions = NULL;
    const char **classes = NULL;
    uint32_t index;
    bool ok = false;

    productions = (const char **)malloc(
        (pack->production_len ? pack->production_len : 1u) *
        sizeof(*productions));
    classes = (const char **)malloc(
        (pack->class_clause_len ? pack->class_clause_len : 1u) *
        sizeof(*classes));
    if (!productions || !classes)
        goto done;
    for (index = 0u; index < pack->production_len; index++)
        productions[index] = pack->productions[index].canonical;
    for (index = 0u; index < pack->class_clause_len; index++)
        classes[index] = pack->class_clauses[index].canonical;
    if (!ppabi_v1_buffer_literal(&buffer, "(parser-pack-v1 ") ||
        !ppabi_v1_render_term_list(&buffer, productions,
                                   pack->production_len) ||
        !ppabi_v1_buffer_char(&buffer, ' ') ||
        !ppabi_v1_render_term_list(&buffer, classes,
                                   pack->class_clause_len) ||
        !ppabi_v1_buffer_char(&buffer, ')')) {
        goto done;
    }
    cetta_native_sha256_hex((const uint8_t *)buffer.data,
                            buffer.len, pack->pack_digest);
    ok = true;

done:
    free(productions);
    free(classes);
    free(buffer.data);
    return ok;
}

void ppabi_v1_pack_init(PPABIV1Pack *pack) {
    if (!pack)
        return;
    memset(pack, 0, sizeof(*pack));
    arena_init(&pack->arena);
}

void ppabi_v1_pack_free(PPABIV1Pack *pack) {
    uint32_t index;

    if (!pack)
        return;
    for (index = 0u; index < pack->state_len; index++)
        free(pack->states[index].canonical);
    for (index = 0u; index < pack->terminal_len; index++)
        free(pack->terminals[index].canonical);
    for (index = 0u; index < pack->production_len; index++) {
        free(pack->productions[index].canonical);
        free(pack->productions[index].items);
    }
    for (index = 0u; index < pack->class_clause_len; index++) {
        free(pack->class_clauses[index].canonical);
        free(pack->class_clauses[index].excluded_points);
    }
    for (index = 0u; index < pack->derivation_len; index++)
        free(pack->derivations[index].canonical);
    free(pack->states);
    free(pack->terminals);
    free(pack->productions);
    free(pack->class_clauses);
    free(pack->derivations);
    arena_free(&pack->arena);
    memset(pack, 0, sizeof(*pack));
}

static bool ppabi_v1_build_identity_tables(
    PPABIV1Pack *pack,
    const PPABIV1IdentityVec *states,
    const PPABIV1IdentityVec *terminals) {
    size_t index;

    pack->state_len = (uint32_t)states->len;
    pack->terminal_len = (uint32_t)terminals->len;
    pack->states = (PPABIV1State *)calloc(
        states->len ? states->len : 1u, sizeof(*pack->states));
    pack->terminals = (PPABIV1Terminal *)calloc(
        terminals->len ? terminals->len : 1u, sizeof(*pack->terminals));
    if (!pack->states || !pack->terminals)
        return false;
    for (index = 0u; index < states->len; index++) {
        pack->states[index].identity =
            atom_deep_copy(&pack->arena, states->data[index].term);
        pack->states[index].canonical =
            ppabi_v1_text_dup(states->data[index].canonical);
        pack->states[index].dense_id = (uint32_t)index;
        pack->states[index].defined = states->data[index].defined;
        if (!pack->states[index].identity ||
            !pack->states[index].canonical) {
            return false;
        }
    }
    for (index = 0u; index < terminals->len; index++) {
        PPABIV1TerminalKind kind;
        uint32_t codepoint = 0u;
        Atom *class_expression = NULL;

        if (!ppabi_v1_terminal_valid(terminals->data[index].term,
                                     &kind, &codepoint,
                                     &class_expression)) {
            return false;
        }
        pack->terminals[index].identity =
            atom_deep_copy(&pack->arena, terminals->data[index].term);
        pack->terminals[index].canonical =
            ppabi_v1_text_dup(terminals->data[index].canonical);
        pack->terminals[index].dense_id = (uint32_t)index;
        pack->terminals[index].kind = kind;
        pack->terminals[index].codepoint = codepoint;
        if (class_expression) {
            pack->terminals[index].class_expression =
                atom_deep_copy(&pack->arena, class_expression);
        }
        if (!pack->terminals[index].identity ||
            !pack->terminals[index].canonical) {
            return false;
        }
    }
    return true;
}

static bool ppabi_v1_build_productions(
    PPABIV1Pack *pack,
    const PPABIV1ParsedProduction *parsed,
    const PPABIV1CanonicalInput *inputs,
    size_t len,
    const PPABIV1IdentityVec *labels,
    const PPABIV1IdentityVec *states,
    const PPABIV1IdentityVec *terminals) {
    size_t index;

    pack->production_len = (uint32_t)len;
    pack->productions = (PPABIV1Production *)calloc(
        len ? len : 1u, sizeof(*pack->productions));
    if (!pack->productions)
        return false;
    for (index = 0u; index < len; index++) {
        const PPABIV1ParsedProduction *source = &parsed[index];
        PPABIV1Production *production = &pack->productions[index];
        Atom *items = source->items;
        uint32_t item_index = 0u;
        int32_t dense =
            ppabi_v1_identity_find(labels->data, labels->len, source->label);

        if (dense < 0)
            return false;
        production->identity =
            atom_deep_copy(&pack->arena, source->term);
        production->canonical =
            ppabi_v1_text_dup(inputs[index].canonical);
        production->dense_id = (uint32_t)dense;
        dense = ppabi_v1_identity_find(
            states->data, states->len, source->lhs);
        if (dense < 0)
            return false;
        production->lhs_state_id = (uint32_t)dense;
        production->item_len = source->item_len;
        production->items = (PPABIV1Item *)calloc(
            source->item_len ? source->item_len : 1u,
            sizeof(*production->items));
        production->action =
            atom_deep_copy(&pack->arena, source->action);
        if (!production->identity || !production->canonical ||
            !production->items || !production->action) {
            return false;
        }
        while (!atom_is_symbol(items, "pp-items-nil")) {
            Atom *item = items->expr.elems[1];
            if (ppabi_v1_expr_head(item, "pp-nonterminal", 1u)) {
                dense = ppabi_v1_identity_find(
                    states->data, states->len, item->expr.elems[1]);
                production->items[item_index].kind =
                    PPABI_V1_ITEM_NONTERMINAL;
            } else {
                dense = ppabi_v1_identity_find(
                    terminals->data, terminals->len, item->expr.elems[1]);
                production->items[item_index].kind =
                    PPABI_V1_ITEM_TERMINAL;
            }
            if (dense < 0)
                return false;
            production->items[item_index].dense_id = (uint32_t)dense;
            item_index++;
            items = items->expr.elems[2];
        }
    }
    return true;
}

static bool ppabi_v1_build_classes(
    PPABIV1Pack *pack,
    const PPABIV1ParsedClassClause *parsed,
    const PPABIV1CanonicalInput *inputs,
    size_t len) {
    size_t index;

    pack->class_clause_len = (uint32_t)len;
    pack->class_clauses = (PPABIV1ClassClause *)calloc(
        len ? len : 1u, sizeof(*pack->class_clauses));
    if (!pack->class_clauses)
        return false;
    for (index = 0u; index < len; index++) {
        const PPABIV1ParsedClassClause *source = &parsed[index];
        PPABIV1ClassClause *clause = &pack->class_clauses[index];

        clause->identity = atom_deep_copy(&pack->arena, source->term);
        clause->canonical = ppabi_v1_text_dup(inputs[index].canonical);
        clause->class_identity =
            atom_deep_copy(&pack->arena, source->class_identity);
        clause->kind = source->kind;
        clause->point = source->point;
        clause->excluded_len = source->excluded_len;
        if (source->excluded_len > 0u) {
            clause->excluded_points = (uint32_t *)malloc(
                (size_t)source->excluded_len *
                sizeof(*clause->excluded_points));
            if (clause->excluded_points) {
                memcpy(clause->excluded_points, source->excluded_points,
                       (size_t)source->excluded_len *
                       sizeof(*clause->excluded_points));
            }
        }
        if (!clause->identity || !clause->canonical ||
            !clause->class_identity ||
            (source->excluded_len > 0u && !clause->excluded_points)) {
            return false;
        }
    }
    return true;
}

static bool ppabi_v1_build_evidence(
    PPABIV1Pack *pack,
    const PPABIV1ProvenanceInput *provenance,
    char *error_buf,
    size_t error_buf_size) {
    PPABIV1EvidenceTemp *evidence;
    size_t index;
    bool ok = false;

    evidence = (PPABIV1EvidenceTemp *)calloc(
        provenance->derivation_len ? provenance->derivation_len : 1u,
        sizeof(*evidence));
    pack->derivation_len = (uint32_t)provenance->derivation_len;
    pack->derivations = (PPABIV1Derivation *)calloc(
        provenance->derivation_len ? provenance->derivation_len : 1u,
        sizeof(*pack->derivations));
    if (!evidence || !pack->derivations)
        goto done;

    for (index = 0u; index < provenance->derivation_len; index++) {
        const PPABIV1DerivationInput *derivation =
            &provenance->derivations[index];
        char *artifact_key;
        int32_t artifact_index;

        if ((derivation->kind != PPABI_V1_EVIDENCE_PRODUCTION &&
             derivation->kind != PPABI_V1_EVIDENCE_CLASS) ||
            !derivation->artifact || !derivation->answer ||
            !derivation->certificate) {
            ppabi_v1_set_error(error_buf, error_buf_size,
                               "malformed ParserPack derivation root");
            goto done;
        }
        artifact_key = ppabi_v1_canonical_atom(derivation->artifact);
        if (!artifact_key)
            goto done;
        artifact_index = ppabi_v1_pack_artifact_find(
            pack, derivation->kind, derivation->artifact, artifact_key);
        if (artifact_index < 0 ||
            !ppabi_v1_answer_matches(
                derivation->kind, derivation->answer,
                derivation->artifact) ||
            !ppabi_v1_evidence_key(
                derivation->kind, artifact_key,
                derivation->answer, derivation->certificate,
                &evidence[index].canonical)) {
            free(artifact_key);
            ppabi_v1_set_error(
                error_buf, error_buf_size,
                "derivation root does not identify an admitted artifact");
            goto done;
        }
        free(artifact_key);
        evidence[index].input = derivation;
        evidence[index].artifact_index = (uint32_t)artifact_index;
    }
    qsort(evidence, provenance->derivation_len,
          sizeof(*evidence), ppabi_v1_evidence_compare);
    for (index = 1u; index < provenance->derivation_len; index++) {
        if (strcmp(evidence[index - 1u].canonical,
                   evidence[index].canonical) == 0) {
            ppabi_v1_set_error(error_buf, error_buf_size,
                               "duplicate ParserPack derivation root");
            goto done;
        }
    }
    for (index = 0u; index < provenance->derivation_len; index++) {
        PPABIV1Derivation *derivation = &pack->derivations[index];
        const PPABIV1DerivationInput *input = evidence[index].input;
        derivation->kind = input->kind;
        derivation->artifact_index = evidence[index].artifact_index;
        derivation->answer = atom_deep_copy(&pack->arena, input->answer);
        derivation->certificate =
            atom_deep_copy(&pack->arena, input->certificate);
        derivation->canonical = evidence[index].canonical;
        evidence[index].canonical = NULL;
        if (!derivation->answer || !derivation->certificate ||
            !derivation->canonical) {
            goto done;
        }
        if (derivation->kind == PPABI_V1_EVIDENCE_PRODUCTION) {
            PPABIV1Production *artifact =
                &pack->productions[derivation->artifact_index];
            if (artifact->evidence_len == 0u)
                artifact->evidence_begin = (uint32_t)index;
            artifact->evidence_len++;
        } else {
            PPABIV1ClassClause *artifact =
                &pack->class_clauses[derivation->artifact_index];
            if (artifact->evidence_len == 0u)
                artifact->evidence_begin = (uint32_t)index;
            artifact->evidence_len++;
        }
    }
    for (index = 0u; index < pack->production_len; index++) {
        if (pack->productions[index].evidence_len == 0u) {
            ppabi_v1_set_error(error_buf, error_buf_size,
                               "production lacks a derivation root: %s",
                               pack->productions[index].canonical);
            goto done;
        }
    }
    for (index = 0u; index < pack->class_clause_len; index++) {
        if (pack->class_clauses[index].evidence_len == 0u) {
            ppabi_v1_set_error(error_buf, error_buf_size,
                               "class clause lacks a derivation root: %s",
                               pack->class_clauses[index].canonical);
            goto done;
        }
    }
    ok = true;

done:
    if (evidence) {
        for (index = 0u; index < provenance->derivation_len; index++)
            free(evidence[index].canonical);
    }
    free(evidence);
    return ok;
}

bool ppabi_v1_pack_load(PPABIV1Pack *out,
                        Atom *const *production_terms,
                        size_t production_len,
                        Atom *const *class_clause_terms,
                        size_t class_clause_len,
                        const PPABIV1ProvenanceInput *provenance,
                        char *error_buf,
                        size_t error_buf_size) {
    PPABIV1Pack pack;
    PPABIV1CanonicalInput *production_inputs = NULL;
    PPABIV1CanonicalInput *class_inputs = NULL;
    PPABIV1ParsedProduction *parsed_productions = NULL;
    PPABIV1ParsedClassClause *parsed_classes = NULL;
    PPABIV1IdentityVec states = {0};
    PPABIV1IdentityVec terminals = {0};
    PPABIV1IdentityVec labels = {0};
    size_t index;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!out || !provenance || production_len == 0u ||
        !ppabi_v1_digest_valid(provenance->source_digest) ||
        !ppabi_v1_digest_valid(provenance->compiler_digest) ||
        !ppabi_v1_digest_valid(provenance->environment_digest) ||
        provenance->derivation_len > UINT32_MAX ||
        (provenance->derivation_len > 0u && !provenance->derivations)) {
        ppabi_v1_set_error(error_buf, error_buf_size,
                           "bad ParserPack ABI or provenance arguments");
        return false;
    }
    ppabi_v1_pack_init(&pack);
    if (!ppabi_v1_inputs_canonical(
            production_terms, production_len, &production_inputs,
            error_buf, error_buf_size, "production stream") ||
        !ppabi_v1_inputs_canonical(
            class_clause_terms, class_clause_len, &class_inputs,
            error_buf, error_buf_size, "class stream")) {
        goto done;
    }
    parsed_productions = (PPABIV1ParsedProduction *)calloc(
        production_len, sizeof(*parsed_productions));
    parsed_classes = (PPABIV1ParsedClassClause *)calloc(
        class_clause_len ? class_clause_len : 1u, sizeof(*parsed_classes));
    if (!parsed_productions || !parsed_classes)
        goto done;

    for (index = 0u; index < production_len; index++) {
        Atom *term = production_inputs[index].term;
        PPABIV1ParsedProduction *parsed = &parsed_productions[index];

        if (!ppabi_v1_expr_head(term, "pp-production", 4u)) {
            ppabi_v1_set_error(error_buf, error_buf_size,
                               "invalid pp-production at canonical index %zu",
                               index);
            goto done;
        }
        parsed->term = term;
        parsed->label = term->expr.elems[1];
        parsed->lhs = term->expr.elems[2];
        parsed->items = term->expr.elems[3];
        parsed->action = term->expr.elems[4];
        if (!ppabi_v1_identity_push(&labels, parsed->label, false) ||
            !ppabi_v1_identity_push(&states, parsed->lhs, true) ||
            !ppabi_v1_scan_items(parsed->items, &states, &terminals,
                                 &parsed->item_len,
                                 error_buf, error_buf_size) ||
            !ppabi_v1_action_valid(parsed->action, parsed->item_len, 0u)) {
            if (!error_buf || error_buf[0] == '\0') {
                ppabi_v1_set_error(error_buf, error_buf_size,
                                   "invalid production action or identity");
            }
            goto done;
        }
    }
    if (!ppabi_v1_identity_vec_sort_unique(
            &labels, true, error_buf, error_buf_size, "production label") ||
        !ppabi_v1_identity_vec_sort_unique(
            &states, false, error_buf, error_buf_size, "state") ||
        !ppabi_v1_identity_vec_sort_unique(
            &terminals, false, error_buf, error_buf_size, "terminal")) {
        goto done;
    }

    for (index = 0u; index < class_clause_len; index++) {
        Atom *term = class_inputs[index].term;
        PPABIV1ParsedClassClause *parsed = &parsed_classes[index];

        parsed->term = term;
        if (ppabi_v1_expr_head(term, "pp-class-point", 2u) &&
            ppabi_v1_codepoint(term->expr.elems[2], &parsed->point)) {
            parsed->kind = PPABI_V1_CLASS_POINT;
            parsed->class_identity = term->expr.elems[1];
        } else if (ppabi_v1_expr_head(term, "pp-class-except", 2u)) {
            parsed->kind = PPABI_V1_CLASS_EXCEPT;
            parsed->class_identity = term->expr.elems[1];
            if (!ppabi_v1_points(term->expr.elems[2],
                                 &parsed->excluded_points,
                                 &parsed->excluded_len,
                                 error_buf, error_buf_size)) {
                goto done;
            }
        } else {
            ppabi_v1_set_error(error_buf, error_buf_size,
                               "invalid ParserPack class clause");
            goto done;
        }
        {
            char *class_key =
                ppabi_v1_canonical_atom(parsed->class_identity);
            if (!class_key) {
                ppabi_v1_set_error(
                    error_buf, error_buf_size,
                    "class identity is outside the ground carrier");
                goto done;
            }
            free(class_key);
        }
    }

    if (!ppabi_v1_build_identity_tables(&pack, &states, &terminals) ||
        !ppabi_v1_build_productions(
            &pack, parsed_productions, production_inputs, production_len,
            &labels, &states, &terminals) ||
        !ppabi_v1_build_classes(
            &pack, parsed_classes, class_inputs, class_clause_len)) {
        goto done;
    }
    for (index = 0u; index < pack.terminal_len; index++) {
        if (pack.terminals[index].kind == PPABI_V1_TERMINAL_CLASS &&
            !ppabi_v1_class_leaf_defined(
                &pack, pack.terminals[index].class_expression, 0u)) {
            ppabi_v1_set_error(
                error_buf, error_buf_size,
                "terminal class expression has an undefined leaf: %s",
                pack.terminals[index].canonical);
            goto done;
        }
    }

    (void)memcpy(pack.source_digest, provenance->source_digest, 65u);
    (void)memcpy(pack.compiler_digest, provenance->compiler_digest, 65u);
    (void)memcpy(pack.environment_digest,
                 provenance->environment_digest, 65u);
    if (!ppabi_v1_build_evidence(
            &pack, provenance, error_buf, error_buf_size) ||
        !ppabi_v1_compute_pack_digest(&pack)) {
        goto done;
    }

    ppabi_v1_pack_free(out);
    *out = pack;
    memset(&pack, 0, sizeof(pack));
    ok = true;

done:
    if (!ok)
        ppabi_v1_pack_free(&pack);
    if (parsed_classes) {
        for (index = 0u; index < class_clause_len; index++)
            free(parsed_classes[index].excluded_points);
    }
    free(parsed_classes);
    free(parsed_productions);
    ppabi_v1_identity_vec_free(&labels);
    ppabi_v1_identity_vec_free(&states);
    ppabi_v1_identity_vec_free(&terminals);
    ppabi_v1_canonical_inputs_free(production_inputs, production_len);
    ppabi_v1_canonical_inputs_free(class_inputs, class_clause_len);
    return ok;
}

bool ppabi_v1_pack_start_is_closed(const PPABIV1Pack *pack,
                                   const Atom *start_state,
                                   char *error_buf,
                                   size_t error_buf_size) {
    char *canonical;
    uint8_t *reachable = NULL;
    uint32_t *work = NULL;
    uint32_t work_len = 0u;
    uint32_t work_head = 0u;
    uint32_t start = UINT32_MAX;
    uint32_t index;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (!pack || !start_state) {
        ppabi_v1_set_error(error_buf, error_buf_size,
                           "bad ParserPack start-closure arguments");
        return false;
    }
    canonical = ppabi_v1_canonical_atom((Atom *)start_state);
    if (!canonical)
        return false;
    for (index = 0u; index < pack->state_len; index++) {
        if (strcmp(canonical, pack->states[index].canonical) == 0 &&
            atom_eq((Atom *)start_state, pack->states[index].identity)) {
            start = index;
            break;
        }
    }
    free(canonical);
    if (start == UINT32_MAX) {
        ppabi_v1_set_error(error_buf, error_buf_size,
                           "start state is absent from ParserPack");
        return false;
    }
    reachable = (uint8_t *)calloc(
        pack->state_len ? pack->state_len : 1u, 1u);
    work = (uint32_t *)malloc(
        (pack->state_len ? pack->state_len : 1u) * sizeof(*work));
    if (!reachable || !work)
        goto done;
    reachable[start] = 1u;
    work[work_len++] = start;
    while (work_head < work_len) {
        uint32_t state = work[work_head++];
        uint32_t production_index;

        if (!pack->states[state].defined) {
            ppabi_v1_set_error(
                error_buf, error_buf_size,
                "reachable ParserPack state is undefined: %s",
                pack->states[state].canonical);
            goto done;
        }
        for (production_index = 0u;
             production_index < pack->production_len;
             production_index++) {
            const PPABIV1Production *production =
                &pack->productions[production_index];
            uint32_t item_index;
            if (production->lhs_state_id != state)
                continue;
            for (item_index = 0u; item_index < production->item_len;
                 item_index++) {
                const PPABIV1Item *item = &production->items[item_index];
                if (item->kind != PPABI_V1_ITEM_NONTERMINAL ||
                    reachable[item->dense_id]) {
                    continue;
                }
                reachable[item->dense_id] = 1u;
                work[work_len++] = item->dense_id;
            }
        }
    }
    ok = true;

done:
    free(reachable);
    free(work);
    return ok;
}
