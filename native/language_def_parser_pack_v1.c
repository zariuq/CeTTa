#include "language_def_parser_pack_v1.h"

#include "finite_horn_ground_term_v1.h"
#include "deterministic_equation_plan_v1.h"
#include "lib_parse_native_grammar.h"
#include "native_sha256.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t used;
    uint32_t limit;
} LdppWorkV1;

typedef struct {
    Arena arena;
    Atom **productions;
    size_t production_len;
    size_t production_cap;
    Atom **classes;
    size_t class_len;
    size_t class_cap;
} LdppTermsV1;

typedef struct {
    Atom *term;
    char *canonical;
} LdppCanonicalTermV1;

static bool ldpp_error(char *buffer, size_t size, const char *format, ...) {
    if (buffer && size > 0u) {
        va_list arguments;
        va_start(arguments, format);
        (void)vsnprintf(buffer, size, format, arguments);
        va_end(arguments);
    }
    return false;
}

static bool ldpp_take_work(LdppWorkV1 *work, uint32_t amount,
                           CettaLdParserPackV1Status *status,
                           char *error, size_t error_size) {
    if (!work || amount > work->limit - work->used) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_RESOURCE_LIMIT;
        return ldpp_error(error, error_size,
                          "LanguageDef parser compilation work limit exceeded");
    }
    work->used += amount;
    return true;
}

static bool ldpp_digest_valid(const char digest[65]) {
    size_t index;
    if (!digest || digest[64] != '\0') return false;
    for (index = 0u; index < 64u; index++) {
        char value = digest[index];
        if (!((value >= '0' && value <= '9') ||
              (value >= 'a' && value <= 'f'))) {
            return false;
        }
    }
    return true;
}

static int ldpp_canonical_term_compare(const void *left,
                                       const void *right) {
    const LdppCanonicalTermV1 *lhs =
        (const LdppCanonicalTermV1 *)left;
    const LdppCanonicalTermV1 *rhs =
        (const LdppCanonicalTermV1 *)right;
    return strcmp(lhs->canonical, rhs->canonical);
}

static bool ldpp_terms_canonicalize(Atom **terms, size_t len,
                                    CettaLdParserPackV1Status *status,
                                    char *error, size_t error_size) {
    LdppCanonicalTermV1 *entries;
    size_t index;
    bool ok = false;
    if (len == 0u) return true;
    entries = (LdppCanonicalTermV1 *)calloc(len, sizeof(*entries));
    if (!entries) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_ALLOCATION_FAILURE;
        return ldpp_error(error, error_size,
                          "out of memory canonicalizing ParserPack inputs");
    }
    for (index = 0u; index < len; index++) {
        uint8_t *bytes = NULL;
        size_t byte_len = 0u;
        entries[index].term = terms[index];
        if (!fh_ground_term_v1_render(
                terms[index], &bytes, &byte_len, error, error_size)) {
            if (status) *status = CETTA_LD_PARSER_PACK_V1_ABI_REJECTED;
            goto done;
        }
        entries[index].canonical = (char *)bytes;
    }
    qsort(entries, len, sizeof(*entries), ldpp_canonical_term_compare);
    for (index = 0u; index < len; index++)
        terms[index] = entries[index].term;
    ok = true;

done:
    for (index = 0u; index < len; index++)
        free(entries[index].canonical);
    free(entries);
    return ok;
}

static void ldpp_sha_text(CettaNativeSha256 *sha, const char *text) {
    uint8_t length[8];
    size_t len = text ? strlen(text) : 0u;
    size_t index;
    for (index = 0u; index < sizeof(length); index++)
        length[index] = (uint8_t)(len >> (index * 8u));
    cetta_native_sha256_update(sha, length, sizeof(length));
    if (len > 0u)
        cetta_native_sha256_update(sha, (const uint8_t *)text, len);
}

static void ldpp_combine_digests(const char *domain,
                                 const char *first,
                                 const char *second,
                                 const char *third,
                                 char output[65]) {
    CettaNativeSha256 sha;
    cetta_native_sha256_init(&sha);
    ldpp_sha_text(&sha, domain);
    ldpp_sha_text(&sha, first);
    ldpp_sha_text(&sha, second);
    ldpp_sha_text(&sha, third);
    cetta_native_sha256_finish_hex(&sha, output);
}

static void ldpp_text_free(CettaLdTextV1 *text) {
    if (!text) return;
    free(text->bytes);
    memset(text, 0, sizeof(*text));
}

static bool ldpp_text_copy(CettaLdTextV1 *out,
                           const uint8_t *bytes, uint32_t len) {
    uint8_t *copy = NULL;
    if (!out || (len > 0u && !bytes)) return false;
    if (len > 0u) {
        copy = (uint8_t *)malloc(len);
        if (!copy) return false;
        (void)memcpy(copy, bytes, len);
    }
    out->bytes = copy;
    out->len = len;
    return true;
}

static bool ldpp_text_equal(const CettaLdTextV1 *left,
                            const CettaLdTextV1 *right) {
    return left && right && left->len == right->len &&
        (left->len == 0u ||
         (left->bytes && right->bytes &&
          memcmp(left->bytes, right->bytes, left->len) == 0));
}

static char *ldpp_text_cstring(const CettaLdTextV1 *text) {
    char *copy;
    if (!text || (text->len > 0u && !text->bytes) ||
        (text->len > 0u && memchr(text->bytes, 0, text->len))) {
        return NULL;
    }
    copy = (char *)malloc((size_t)text->len + 1u);
    if (!copy) return NULL;
    if (text->len > 0u)
        (void)memcpy(copy, text->bytes, text->len);
    copy[text->len] = '\0';
    return copy;
}

static bool ldpp_expr_is(const CettaOpLangV1SExpr *expression,
                         const char *head, uint32_t arguments) {
    return cetta_op_lang_v1_application_is(expression, head, arguments);
}

static bool ldpp_decode_string(CettaLdTextV1 *out,
                               const CettaOpLangV1SExpr *expression) {
    return out && expression &&
        expression->kind == CETTA_OP_LANG_V1_SEXPR_STRING &&
        ldpp_text_copy(out, expression->as.string.bytes,
                       expression->as.string.len);
}

static bool ldpp_decode_scalar(const CettaOpLangV1SExpr *expression,
                               uint32_t *out) {
    char *end = NULL;
    unsigned long long value;
    if (!expression || !out ||
        expression->kind != CETTA_OP_LANG_V1_SEXPR_NATURAL ||
        !expression->as.natural || expression->as.natural[0] == '\0') {
        return false;
    }
    errno = 0;
    value = strtoull(expression->as.natural, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value > 0x10ffffu ||
        (value >= 0xd800u && value <= 0xdfffu)) {
        return false;
    }
    *out = (uint32_t)value;
    return true;
}

static bool ldpp_list_len(const CettaOpLangV1SExpr *list,
                          uint32_t *out,
                          LdppWorkV1 *work,
                          CettaLdParserPackV1Status *status,
                          char *error, size_t error_size) {
    uint32_t len = 0u;
    while (!cetta_op_lang_v1_symbol_is(list, "LNil")) {
        if (!ldpp_take_work(work, 1u, status, error, error_size))
            return false;
        if (!ldpp_expr_is(list, "LCons", 2u) || len == UINT32_MAX) {
            if (status) *status = CETTA_LD_PARSER_PACK_V1_MALFORMED_PROFILE;
            return ldpp_error(error, error_size,
                              "parser profile contains a non-list field");
        }
        list = list->as.application.arguments[1];
        len++;
    }
    *out = len;
    return true;
}

static const CettaOpLangV1SExpr *ldpp_list_head(
    const CettaOpLangV1SExpr *list) {
    return ldpp_expr_is(list, "LCons", 2u)
        ? list->as.application.arguments[0] : NULL;
}

static const CettaOpLangV1SExpr *ldpp_list_tail(
    const CettaOpLangV1SExpr *list) {
    return ldpp_expr_is(list, "LCons", 2u)
        ? list->as.application.arguments[1] : NULL;
}

void cetta_ld_parser_profile_v1_init(CettaLdParserProfileV1 *profile) {
    if (profile) memset(profile, 0, sizeof(*profile));
}

void cetta_ld_parser_profile_v1_free(CettaLdParserProfileV1 *profile) {
    uint32_t index;
    if (!profile) return;
    ldpp_text_free(&profile->name);
    ldpp_text_free(&profile->start_sort);
    for (index = 0u; index < profile->class_len; index++) {
        ldpp_text_free(&profile->classes[index].name);
        free(profile->classes[index].points);
    }
    for (index = 0u; index < profile->state_len; index++) {
        ldpp_text_free(&profile->states[index].sort);
        ldpp_text_free(&profile->states[index].class_name);
        ldpp_text_free(&profile->states[index].label);
    }
    free(profile->classes);
    free(profile->states);
    memset(profile, 0, sizeof(*profile));
}

static bool ldpp_decode_point_list(CettaLdLexicalClassV1 *klass,
                                   const CettaOpLangV1SExpr *list,
                                   LdppWorkV1 *work,
                                   CettaLdParserPackV1Status *status,
                                   char *error, size_t error_size) {
    uint32_t len = 0u;
    uint32_t index;
    uint32_t previous = 0u;
    const CettaOpLangV1SExpr *cursor = list;
    if (!ldpp_list_len(cursor, &len, work, status, error, error_size))
        return false;
    if (len == 0u) {
        if (klass->kind == CETTA_LD_LEXICAL_EXCEPT_V1)
            return true;
        if (status) *status = CETTA_LD_PARSER_PACK_V1_MALFORMED_PROFILE;
        return ldpp_error(error, error_size,
                          "positive lexical scalar class must not be empty");
    }
    klass->points = (uint32_t *)calloc(len, sizeof(*klass->points));
    if (!klass->points) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_ALLOCATION_FAILURE;
        return ldpp_error(error, error_size,
                          "out of memory decoding lexical scalar class");
    }
    klass->point_len = len;
    for (index = 0u; index < len; index++) {
        const CettaOpLangV1SExpr *point = ldpp_list_head(cursor);
        uint32_t value;
        if (!ldpp_decode_scalar(point, &value) ||
            (index > 0u && value <= previous)) {
            if (status) *status = CETTA_LD_PARSER_PACK_V1_MALFORMED_PROFILE;
            return ldpp_error(error, error_size,
                              "lexical scalar points must be strictly increasing Unicode scalars");
        }
        klass->points[index] = value;
        previous = value;
        cursor = ldpp_list_tail(cursor);
    }
    return true;
}

static bool ldpp_profile_unique(const CettaLdParserProfileV1 *profile,
                                CettaLdParserPackV1Status *status,
                                char *error, size_t error_size) {
    uint32_t left;
    uint32_t right;
    for (left = 0u; left < profile->class_len; left++) {
        if (profile->classes[left].name.len == 0u) {
            if (status) *status = CETTA_LD_PARSER_PACK_V1_MALFORMED_PROFILE;
            return ldpp_error(error, error_size,
                              "lexical class name must not be empty");
        }
        for (right = left + 1u; right < profile->class_len; right++) {
            if (ldpp_text_equal(&profile->classes[left].name,
                                &profile->classes[right].name)) {
                if (status) *status = CETTA_LD_PARSER_PACK_V1_MALFORMED_PROFILE;
                return ldpp_error(error, error_size,
                                  "duplicate lexical class name");
            }
        }
    }
    for (left = 0u; left < profile->state_len; left++) {
        bool class_found = false;
        if (profile->states[left].sort.len == 0u ||
            profile->states[left].label.len == 0u) {
            if (status) *status = CETTA_LD_PARSER_PACK_V1_MALFORMED_PROFILE;
            return ldpp_error(error, error_size,
                              "lexical state sort and label must not be empty");
        }
        for (right = 0u; right < profile->class_len; right++) {
            if (ldpp_text_equal(&profile->states[left].class_name,
                                &profile->classes[right].name)) {
                class_found = true;
                break;
            }
        }
        if (!class_found) {
            if (status) *status = CETTA_LD_PARSER_PACK_V1_MALFORMED_PROFILE;
            return ldpp_error(error, error_size,
                              "lexical state references an unknown class");
        }
        for (right = left + 1u; right < profile->state_len; right++) {
            if (ldpp_text_equal(&profile->states[left].sort,
                                &profile->states[right].sort) ||
                ldpp_text_equal(&profile->states[left].label,
                                &profile->states[right].label)) {
                if (status) *status = CETTA_LD_PARSER_PACK_V1_MALFORMED_PROFILE;
                return ldpp_error(error, error_size,
                                  "duplicate lexical state sort or label");
            }
        }
    }
    return true;
}

bool cetta_ld_parser_profile_v1_decode(
    CettaLdParserProfileV1 *out,
    const CettaOpLangV1Document *document,
    uint32_t work_limit,
    CettaLdParserPackV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaLdParserProfileV1 candidate;
    const CettaOpLangV1SExpr *root;
    const CettaOpLangV1SExpr *class_list;
    const CettaOpLangV1SExpr *state_list;
    const CettaOpLangV1SExpr *cursor;
    LdppWorkV1 work = {.used = 0u, .limit = work_limit};
    uint32_t index;
    bool ok = false;

    cetta_ld_parser_profile_v1_init(&candidate);
    if (error_buf && error_buf_size > 0u) error_buf[0] = '\0';
    if (status) *status = CETTA_LD_PARSER_PACK_V1_BAD_ARGUMENT;
    if (!out || !document || !document->root || work_limit == 0u ||
        !ldpp_digest_valid(document->authority_sha256)) {
        ldpp_error(error_buf, error_buf_size,
                   "bad parser-profile decode arguments");
        goto done;
    }
    root = document->root;
    if (!ldpp_expr_is(root, "GSLTParserProfileLayerV1", 4u)) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_MALFORMED_PROFILE;
        ldpp_error(error_buf, error_buf_size,
                   "expected GSLTParserProfileLayerV1 document");
        goto done;
    }
    if (!ldpp_take_work(&work, 1u, status, error_buf, error_buf_size) ||
        !ldpp_decode_string(&candidate.name, root->as.application.arguments[0]) ||
        !ldpp_decode_string(&candidate.start_sort,
                            root->as.application.arguments[1])) {
        if (status && *status != CETTA_LD_PARSER_PACK_V1_RESOURCE_LIMIT)
            *status = CETTA_LD_PARSER_PACK_V1_MALFORMED_PROFILE;
        ldpp_error(error_buf, error_buf_size,
                   "parser profile name and start sort must be strings");
        goto done;
    }
    class_list = root->as.application.arguments[2];
    state_list = root->as.application.arguments[3];
    if (!ldpp_list_len(class_list, &candidate.class_len, &work, status,
                       error_buf, error_buf_size) ||
        !ldpp_list_len(state_list, &candidate.state_len, &work, status,
                       error_buf, error_buf_size)) {
        goto done;
    }
    if ((candidate.class_len == 0u) != (candidate.state_len == 0u)) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_MALFORMED_PROFILE;
        ldpp_error(error_buf, error_buf_size,
                   "parser profile lexical classes and states must either both be empty or both be present");
        goto done;
    }
    candidate.classes = candidate.class_len == 0u
        ? NULL
        : (CettaLdLexicalClassV1 *)calloc(
            candidate.class_len, sizeof(*candidate.classes));
    candidate.states = candidate.state_len == 0u
        ? NULL
        : (CettaLdLexicalStateV1 *)calloc(
            candidate.state_len, sizeof(*candidate.states));
    if ((candidate.class_len > 0u && !candidate.classes) ||
        (candidate.state_len > 0u && !candidate.states)) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_ALLOCATION_FAILURE;
        ldpp_error(error_buf, error_buf_size,
                   "out of memory decoding parser profile");
        goto done;
    }
    cursor = class_list;
    for (index = 0u; index < candidate.class_len; index++) {
        const CettaOpLangV1SExpr *entry = ldpp_list_head(cursor);
        CettaLdLexicalClassV1 *klass = &candidate.classes[index];
        if (!entry ||
            !(ldpp_expr_is(entry, "LexicalClassPoints", 2u) ||
              ldpp_expr_is(entry, "LexicalClassExcept", 2u)) ||
            !ldpp_decode_string(&klass->name,
                                entry->as.application.arguments[0])) {
            if (status) *status = CETTA_LD_PARSER_PACK_V1_MALFORMED_PROFILE;
            ldpp_error(error_buf, error_buf_size,
                       "malformed lexical class declaration");
            goto done;
        }
        klass->kind = ldpp_expr_is(entry, "LexicalClassPoints", 2u)
            ? CETTA_LD_LEXICAL_POINTS_V1 : CETTA_LD_LEXICAL_EXCEPT_V1;
        if (!ldpp_decode_point_list(
                klass, entry->as.application.arguments[1], &work, status,
                error_buf, error_buf_size)) {
            goto done;
        }
        cursor = ldpp_list_tail(cursor);
    }
    cursor = state_list;
    for (index = 0u; index < candidate.state_len; index++) {
        const CettaOpLangV1SExpr *entry = ldpp_list_head(cursor);
        CettaLdLexicalStateV1 *state = &candidate.states[index];
        if (!entry || !ldpp_expr_is(entry, "LexicalState", 3u) ||
            !ldpp_decode_string(&state->sort,
                                entry->as.application.arguments[0]) ||
            !ldpp_decode_string(&state->class_name,
                                entry->as.application.arguments[1]) ||
            !ldpp_decode_string(&state->label,
                                entry->as.application.arguments[2])) {
            if (status) *status = CETTA_LD_PARSER_PACK_V1_MALFORMED_PROFILE;
            ldpp_error(error_buf, error_buf_size,
                       "malformed lexical state declaration");
            goto done;
        }
        cursor = ldpp_list_tail(cursor);
    }
    if (!ldpp_profile_unique(&candidate, status,
                             error_buf, error_buf_size)) {
        goto done;
    }
    (void)memcpy(
        candidate.authority_sha256, document->authority_sha256, 65u);
    cetta_ld_parser_profile_v1_free(out);
    *out = candidate;
    memset(&candidate, 0, sizeof(candidate));
    if (status) *status = CETTA_LD_PARSER_PACK_V1_OK;
    ok = true;

done:
    cetta_ld_parser_profile_v1_free(&candidate);
    return ok;
}

void cetta_ld_parser_pack_v1_init(CettaLdParserPackV1 *compiled) {
    if (!compiled) return;
    memset(compiled, 0, sizeof(*compiled));
    ppabi_v1_pack_init(&compiled->pack);
}

void cetta_ld_parser_pack_v1_free(CettaLdParserPackV1 *compiled) {
    if (!compiled) return;
    ppabi_v1_pack_free(&compiled->pack);
    memset(compiled, 0, sizeof(*compiled));
}

static void ldpp_terms_init(LdppTermsV1 *terms) {
    memset(terms, 0, sizeof(*terms));
    arena_init(&terms->arena);
}

static void ldpp_terms_free(LdppTermsV1 *terms) {
    if (!terms) return;
    free(terms->productions);
    free(terms->classes);
    arena_free(&terms->arena);
    memset(terms, 0, sizeof(*terms));
}

static bool ldpp_atom_vec_push(Atom ***data, size_t *len, size_t *cap,
                               Atom *value) {
    Atom **next;
    size_t next_cap;
    if (!data || !len || !cap || !value) return false;
    if (*len == *cap) {
        next_cap = *cap ? *cap * 2u : 32u;
        if (next_cap < *cap || next_cap > SIZE_MAX / sizeof(**data))
            return false;
        next = (Atom **)realloc(*data, next_cap * sizeof(**data));
        if (!next) return false;
        *data = next;
        *cap = next_cap;
    }
    (*data)[(*len)++] = value;
    return true;
}

static Atom *ldpp_app(Arena *arena, const char *head,
                      size_t argument_len, Atom **arguments) {
    Atom **elements;
    Atom *result;
    size_t index;
    if (!arena || !head || argument_len > UINT32_MAX) return NULL;
    elements = (Atom **)malloc((argument_len + 1u) * sizeof(*elements));
    if (!elements) return NULL;
    elements[0] = atom_symbol(arena, head);
    for (index = 0u; index < argument_len; index++)
        elements[index + 1u] = arguments[index];
    result = atom_expr(arena, elements, argument_len + 1u);
    free(elements);
    return result;
}

static Atom *ldpp_unary(Arena *arena, const char *head, Atom *argument) {
    Atom *arguments[1] = {argument};
    return ldpp_app(arena, head, 1u, arguments);
}

static Atom *ldpp_binary(Arena *arena, const char *head,
                         Atom *left, Atom *right) {
    Atom *arguments[2] = {left, right};
    return ldpp_app(arena, head, 2u, arguments);
}

/* Exact source bytes are embedded data, not generated compiler machine code. */
extern const uint8_t cetta_ldpp_compiler_v1_source[];
extern const size_t cetta_ldpp_compiler_v1_source_len;

static Atom *ldpp_name(Arena *arena, const CettaLdTextV1 *text,
    CettaLdParserPackV1Status *status, char *error, size_t error_size) {
    char *name;
    Atom *result;
    if (!text || (text->len && (!text->bytes || memchr(text->bytes, 0, text->len)))) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_OUTSIDE_FRAGMENT;
        ldpp_error(error, error_size, "parser names must be representable without embedded NUL");
        return NULL;
    }
    name = ldpp_text_cstring(text);
    if (!name) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_ALLOCATION_FAILURE;
        ldpp_error(error, error_size, "cannot allocate compiler name");
        return NULL;
    }
    result = atom_string(arena, name);
    free(name);
    return result;
}

static const CettaLdTypeDeclV1 *ldpp_type_find(
    const CettaLanguageDefCoreV1 *language,
    const CettaLdTextV1 *name) {
    uint32_t index;
    for (index = 0u; index < language->type_len; index++) {
        if (ldpp_text_equal(&language->types[index].name, name))
            return &language->types[index];
    }
    return NULL;
}

static const CettaLdTermParamV1 *ldpp_param_find(
    const CettaLdGrammarRuleV1 *rule,
    const CettaLdTextV1 *name,
    uint32_t *index_out) {
    uint32_t index;
    for (index = 0u; index < rule->param_len; index++) {
        if (ldpp_text_equal(&rule->params[index].body_name, name)) {
            if (index_out) *index_out = index;
            return &rule->params[index];
        }
    }
    return NULL;
}

static bool ldpp_rule_fragment_valid(
    const CettaLanguageDefCoreV1 *language,
    const CettaLdGrammarRuleV1 *rule,
    CettaLdParserPackV1Status *status,
    char *error, size_t error_size) {
    uint32_t item;
    uint32_t expected_param = 0u;
    if (!rule || rule->label.len == 0u || rule->category.len == 0u ||
        !ldpp_type_find(language, &rule->category) ||
        rule->eval_policy.present) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_OUTSIDE_FRAGMENT;
        return ldpp_error(error, error_size,
                          "grammar row is outside the first-order parser fragment");
    }
    for (item = 0u; item < rule->param_len; item++) {
        const CettaLdTermParamV1 *parameter = &rule->params[item];
        if (parameter->kind != CETTA_LD_PARAM_SIMPLE_V1 ||
            parameter->type.kind != CETTA_LD_TYPE_BASE_V1 ||
            parameter->body_name.len == 0u ||
            !ldpp_type_find(language, &parameter->type.as.base)) {
            if (status) *status = CETTA_LD_PARSER_PACK_V1_OUTSIDE_FRAGMENT;
            return ldpp_error(error, error_size,
                              "grammar parameter is outside the simple base-typed fragment");
        }
    }
    for (item = 0u; item < rule->syntax_pattern.len; item++) {
        const CettaLdSyntaxItemV1 *syntax = &rule->syntax_pattern.items[item];
        if (syntax->kind == CETTA_LD_SYNTAX_TERMINAL_V1) {
            if (syntax->as.text.len == 0u) {
                if (status) *status = CETTA_LD_PARSER_PACK_V1_OUTSIDE_FRAGMENT;
                return ldpp_error(error, error_size,
                                  "empty literal terminal is outside the parser fragment");
            }
        } else if (syntax->kind == CETTA_LD_SYNTAX_NONTERMINAL_V1) {
            uint32_t parameter_index = UINT32_MAX;
            const CettaLdTermParamV1 *parameter =
                ldpp_param_find(rule, &syntax->as.text, &parameter_index);
            if (!parameter || parameter_index != expected_param) {
                if (status) *status = CETTA_LD_PARSER_PACK_V1_OUTSIDE_FRAGMENT;
                return ldpp_error(error, error_size,
                                  "nonterminals must use parameters exactly once and in order");
            }
            expected_param++;
        } else {
            if (status) *status = CETTA_LD_PARSER_PACK_V1_OUTSIDE_FRAGMENT;
            return ldpp_error(error, error_size,
                              "syntax separator, delimiter, or operator requires a later compiler fragment");
        }
    }
    if (expected_param != rule->param_len) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_OUTSIDE_FRAGMENT;
        return ldpp_error(error, error_size,
                          "grammar row has an unused parser parameter");
    }
    return true;
}

static bool ldpp_labels_unique(const CettaLanguageDefCoreV1 *language,
                               const CettaLdParserProfileV1 *profile,
                               CettaLdParserPackV1Status *status,
                               char *error, size_t error_size) {
    uint32_t left;
    uint32_t right;
    for (left = 0u; left < language->term_len; left++) {
        for (right = left + 1u; right < language->term_len; right++) {
            if (ldpp_text_equal(&language->terms[left].label,
                                &language->terms[right].label)) {
                if (status) *status = CETTA_LD_PARSER_PACK_V1_OUTSIDE_FRAGMENT;
                return ldpp_error(error, error_size,
                                  "grammar production labels must be unique");
            }
        }
        for (right = 0u; right < profile->state_len; right++) {
            if (ldpp_text_equal(&language->terms[left].label,
                                &profile->states[right].label)) {
                if (status) *status = CETTA_LD_PARSER_PACK_V1_OUTSIDE_FRAGMENT;
                return ldpp_error(error, error_size,
                                  "grammar and lexical production labels collide");
            }
        }
    }
    return true;
}

static bool ldpp_terminal_scalars(const CettaLdTextV1 *text,
                                  uint32_t **out, uint32_t *out_len,
                                  CettaLdParserPackV1Status *status,
                                  char *error, size_t error_size) {
    CettaLpNativeUtf8ScalarBuffer buffer;
    const CettaLpNativeUtf8ScalarView *view;
    uint32_t *scalars = NULL;
    uint32_t index;
    cetta_lp_native_utf8_scalar_buffer_init(&buffer);
    if (!cetta_lp_native_utf8_scalar_buffer_prepare(
            &buffer, text->bytes, text->len, error, error_size)) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_INVALID_UTF8;
        cetta_lp_native_utf8_scalar_buffer_free(&buffer);
        return false;
    }
    view = &buffer.view;
    if (view->scalar_len > UINT32_MAX) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_RESOURCE_LIMIT;
        cetta_lp_native_utf8_scalar_buffer_free(&buffer);
        return ldpp_error(error, error_size,
                          "literal terminal contains too many scalars");
    }
    scalars = (uint32_t *)calloc(
        view->scalar_len ? view->scalar_len : 1u, sizeof(*scalars));
    if (!scalars) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_ALLOCATION_FAILURE;
        cetta_lp_native_utf8_scalar_buffer_free(&buffer);
        return ldpp_error(error, error_size,
                          "out of memory decoding literal terminal");
    }
    for (index = 0u; index < view->scalar_len; index++)
        scalars[index] = cetta_lp_native_utf8_scalar_view_scalar_at(view, index);
    *out = scalars;
    *out_len = view->scalar_len;
    cetta_lp_native_utf8_scalar_buffer_free(&buffer);
    return true;
}

/* Scalar-text is a representation view of SyntaxTerminal, not another
 * grammar IR: all scalar occurrences, including U+0000, remain explicit.
 * The authored equations alone expand these values into parser items. */
static Atom *ldpp_literal_view(
    Arena *arena, const CettaLdTextV1 *text, LdppWorkV1 *work,
    CettaLdParserPackV1Status *status, char *error, size_t error_size) {
    uint32_t *scalars = NULL, count = 0u;
    Atom *result;
    if (!ldpp_terminal_scalars(text, &scalars, &count, status, error, error_size))
        return NULL;
    if (!ldpp_take_work(work, count, status, error, error_size)) {
        free(scalars);
        return NULL;
    }
    result = ldpp_app(arena, "bnf-v1:text-nil", 0u, NULL);
    while (count > 0u)
        result = ldpp_binary(arena, "bnf-v1:text-cons",
            atom_int(arena, scalars[--count]), result);
    free(scalars);
    return result;
}

static Atom *ldpp_rule_view(
    Arena *arena, const CettaLdGrammarRuleV1 *rule, LdppWorkV1 *work,
    CettaLdParserPackV1Status *status, char *error, size_t error_size) {
    Atom *params = atom_symbol(arena, "LNil");
    Atom *syntax = atom_symbol(arena, "LNil");
    Atom *arguments[5];
    uint32_t index;
    for (index = rule->param_len; index > 0u; index--) {
        const CettaLdTermParamV1 *parameter = &rule->params[index - 1u];
        Atom *name = ldpp_name(arena, &parameter->body_name, status, error, error_size);
        Atom *sort = ldpp_name(arena, &parameter->type.as.base, status, error, error_size);
        if (!name || !sort ||
            !ldpp_take_work(work, 1u, status, error, error_size)) return NULL;
        params = ldpp_binary(arena, "LCons",
            ldpp_binary(arena, "TermSimple", name,
                ldpp_unary(arena, "TBase", sort)), params);
    }
    for (index = rule->syntax_pattern.len; index > 0u; index--) {
        const CettaLdSyntaxItemV1 *item = &rule->syntax_pattern.items[index - 1u];
        Atom *field;
        if (!ldpp_take_work(work, 1u, status, error, error_size)) return NULL;
        field = item->kind == CETTA_LD_SYNTAX_TERMINAL_V1
            ? ldpp_literal_view(arena, &item->as.text, work, status, error, error_size)
            : ldpp_name(arena, &item->as.text, status, error, error_size);
        if (!field) return NULL;
        syntax = ldpp_binary(arena, "LCons",
            ldpp_unary(arena, item->kind == CETTA_LD_SYNTAX_TERMINAL_V1
                ? "SyntaxTerminal" : "SyntaxNonTerminal", field), syntax);
    }
    arguments[0] = ldpp_name(arena, &rule->label, status, error, error_size);
    arguments[1] = ldpp_name(arena, &rule->category, status, error, error_size);
    arguments[2] = params;
    arguments[3] = syntax;
    arguments[4] = atom_symbol(arena, "EvalNone");
    if (!arguments[0] || !arguments[1]) return NULL;
    return ldpp_app(arena, "GrammarRule", 5u, arguments);
}

static Atom *ldpp_class_view(
    Arena *arena, const CettaLdLexicalClassV1 *klass, LdppWorkV1 *work,
    CettaLdParserPackV1Status *status, char *error, size_t error_size) {
    Atom *name = ldpp_name(arena, &klass->name, status, error, error_size);
    Atom *points = atom_symbol(arena, "LNil");
    uint32_t index;
    if (!name || !ldpp_take_work(work, klass->point_len, status, error, error_size))
        return NULL;
    for (index = klass->point_len; index > 0u; index--)
        points = ldpp_binary(arena, "LCons",
            atom_int(arena, klass->points[index - 1u]), points);
    return ldpp_binary(arena, klass->kind == CETTA_LD_LEXICAL_POINTS_V1
        ? "LexicalClassPoints" : "LexicalClassExcept", name, points);
}

static Atom *ldpp_lexical_view(Arena *arena, const CettaLdLexicalStateV1 *state,
    CettaLdParserPackV1Status *status, char *error, size_t error_size) {
    Atom *arguments[3] = {
        ldpp_name(arena, &state->sort, status, error, error_size),
        ldpp_name(arena, &state->class_name, status, error, error_size),
        ldpp_name(arena, &state->label, status, error, error_size)
    };
    if (!arguments[0] || !arguments[1] || !arguments[2]) return NULL;
    return ldpp_app(arena, "LexicalState", 3u, arguments);
}

static CettaDeterministicPrimitiveResultV1 ldpp_primitive(
    void *context, const char *head, Atom *const *arguments,
    uint32_t count, Arena *arena, Atom **out, char *error, size_t error_size) {
    (void)context;
    if (strcmp(head, "ldpp-v1:text-equal") != 0)
        return CETTA_DETERMINISTIC_PRIMITIVE_V1_NOT_HANDLED;
    if (count != 2u || arguments[0]->kind != ATOM_GROUNDED ||
        arguments[1]->kind != ATOM_GROUNDED ||
        arguments[0]->ground.gkind != GV_STRING ||
        arguments[1]->ground.gkind != GV_STRING) {
        ldpp_error(error, error_size, "compiler text equality requires two strings");
        return CETTA_DETERMINISTIC_PRIMITIVE_V1_FAULT;
    }
    *out = atom_symbol(arena, atom_eq(arguments[0], arguments[1]) ? "True" : "False");
    return CETTA_DETERMINISTIC_PRIMITIVE_V1_HANDLED;
}

static bool ldpp_source_run(
    const CettaDeterministicEquationPlanV1 *plan, LdppTermsV1 *terms,
    const char *head, Atom *argument, LdppWorkV1 *work, Atom **result,
    CettaLdParserPackV1Status *status, char *error, size_t error_size) {
    CettaDeterministicEquationStatusV1 execution_status;
    Atom *call;
    uint64_t consumed = 0u;
    uint32_t remaining;
    bool ran;
    if (!argument) {
        if (status && *status == CETTA_LD_PARSER_PACK_V1_BAD_ARGUMENT)
            *status = CETTA_LD_PARSER_PACK_V1_ALLOCATION_FAILURE;
        if (!error || !error_size || !error[0])
            ldpp_error(error, error_size,
                "cannot allocate compiler input view");
        return false;
    }
    if (!ldpp_take_work(work, 1u, status, error, error_size))
        return false;
    call = ldpp_unary(&terms->arena, head, argument);
    if (!call) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_ALLOCATION_FAILURE;
        return ldpp_error(error, error_size, "cannot allocate compiler call");
    }
    remaining = work->limit - work->used;
    if (remaining == 0u)
        return ldpp_take_work(work, 1u, status, error, error_size);
    ran = cetta_deterministic_equation_plan_v1_run_counted(
            plan, call, ldpp_primitive, NULL, &terms->arena,
            work->limit, remaining, &consumed, result, &execution_status,
            error, error_size);
    if (consumed > remaining) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_COMPILER_REJECTED;
        return ldpp_error(error, error_size, "compiler returned an invalid work count");
    }
    work->used += (uint32_t)consumed;
    if (!ran) {
        if (status) *status = execution_status == CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT
            ? CETTA_LD_PARSER_PACK_V1_RESOURCE_LIMIT
            : CETTA_LD_PARSER_PACK_V1_COMPILER_REJECTED;
        return false;
    }
    return true;
}

static bool ldpp_retain_production(
    LdppTermsV1 *terms, Atom *production, CettaLdParserPackV1Status *status,
    char *error, size_t error_size) {
    if (!ldpp_atom_vec_push(&terms->productions, &terms->production_len,
            &terms->production_cap, production)) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_ALLOCATION_FAILURE;
        return ldpp_error(error, error_size, "cannot retain compiler production");
    }
    return true;
}

static bool ldpp_is_app(const Atom *atom, const char *head, uint32_t arity) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == arity + 1u &&
        atom_is_symbol(atom->expr.elems[0], head);
}

/* Only decode the returned class stream; class selection is authored. */
static bool ldpp_collect_classes(
    LdppTermsV1 *terms, Atom *list, LdppWorkV1 *work,
    CettaLdParserPackV1Status *status, char *error, size_t error_size) {
    while (ldpp_is_app(list, "LCons", 2u)) {
        if (!ldpp_take_work(work, 1u, status, error, error_size)) return false;
        if (!ldpp_atom_vec_push(&terms->classes, &terms->class_len,
                &terms->class_cap, list->expr.elems[1])) {
            if (status) *status = CETTA_LD_PARSER_PACK_V1_ALLOCATION_FAILURE;
            return ldpp_error(error, error_size, "cannot retain compiler class clause");
        }
        list = list->expr.elems[2];
    }
    if (!atom_is_symbol(list, "LNil")) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_COMPILER_REJECTED;
        return ldpp_error(error, error_size, "compiler returned a malformed class stream");
    }
    return true;
}

static bool ldpp_profile_sorts_valid(
    const CettaLanguageDefCoreV1 *language,
    const CettaLdParserProfileV1 *profile,
    CettaLdParserPackV1Status *status,
    char *error, size_t error_size) {
    uint32_t index;
    if (!ldpp_type_find(language, &profile->start_sort)) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_OUTSIDE_FRAGMENT;
        return ldpp_error(error, error_size,
                          "parser start sort is absent from LanguageDef types");
    }
    for (index = 0u; index < profile->state_len; index++) {
        if (!ldpp_type_find(language, &profile->states[index].sort)) {
            if (status) *status = CETTA_LD_PARSER_PACK_V1_OUTSIDE_FRAGMENT;
            return ldpp_error(error, error_size,
                              "lexical state sort is absent from LanguageDef types");
        }
    }
    return true;
}

static Atom *ldpp_pack_state(const PPABIV1Pack *pack, Atom *needle) {
    uint32_t index;
    for (index = 0u; index < pack->state_len; index++) {
        if (atom_eq(pack->states[index].identity, needle))
            return pack->states[index].identity;
    }
    return NULL;
}

bool cetta_language_def_parser_pack_v1_compile_source(
    CettaLdParserPackV1 *out,
    const CettaLanguageDefCoreV1 *language,
    const char language_authority_sha256[65],
    const CettaLdParserProfileV1 *profile,
    const uint8_t *compiler_source, size_t compiler_source_len,
    uint32_t work_limit,
    CettaLdParserPackV1Status *status,
    char *error_buf,
    size_t error_buf_size) {
    CettaLdParserPackV1 candidate;
    LdppTermsV1 terms;
    LdppWorkV1 work = {.used = 0u, .limit = work_limit};
    CettaDeterministicEquationPlanV1 *plan = NULL;
    CettaDeterministicEquationStatusV1 plan_status;
    CettaDeterministicEquationInputV1 source_input = {
        .bytes = compiler_source, .length = compiler_source_len,
        .source = "LanguageDefParserCompilerV1"
    };
    char source_digest[65];
    char environment_digest[65];
    Atom *start_needle = NULL;
    uint32_t index;
    bool ok = false;

    cetta_ld_parser_pack_v1_init(&candidate);
    ldpp_terms_init(&terms);
    if (error_buf && error_buf_size > 0u) error_buf[0] = '\0';
    if (status) *status = CETTA_LD_PARSER_PACK_V1_BAD_ARGUMENT;
    if (!out || !language || !profile || !compiler_source ||
        compiler_source_len == 0u || work_limit == 0u ||
        !ldpp_digest_valid(language_authority_sha256) ||
        !ldpp_digest_valid(profile->authority_sha256) ||
        language->term_len == 0u) {
        ldpp_error(error_buf, error_buf_size,
                   "bad LanguageDef-to-ParserPack compile arguments");
        goto done;
    }
    if (language->equation_len != 0u || language->rewrite_len != 0u) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_OUTSIDE_FRAGMENT;
        ldpp_error(error_buf, error_buf_size,
                   "syntax compiler does not erase LanguageDef equations or rewrites");
        goto done;
    }
    if (!ldpp_profile_sorts_valid(language, profile, status,
                                  error_buf, error_buf_size) ||
        !ldpp_labels_unique(language, profile, status,
                            error_buf, error_buf_size)) {
        goto done;
    }
    if (!cetta_deterministic_equation_plan_v1_load_inputs(
            &source_input, 1u, &plan, &plan_status, error_buf, error_buf_size)) {
        if (status) *status = plan_status == CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT
            ? CETTA_LD_PARSER_PACK_V1_RESOURCE_LIMIT
            : CETTA_LD_PARSER_PACK_V1_COMPILER_REJECTED;
        goto done;
    }
    for (index = 0u; index < language->term_len; index++) {
        Atom *production = NULL;
        const CettaLdGrammarRuleV1 *rule = &language->terms[index];
        if (!ldpp_rule_fragment_valid(language, rule, status, error_buf, error_buf_size) ||
            !ldpp_source_run(plan, &terms, "ldpp-v1:compile-rule",
                ldpp_rule_view(&terms.arena, rule, &work, status, error_buf, error_buf_size),
                &work, &production, status, error_buf, error_buf_size) ||
            !ldpp_retain_production(&terms, production, status,
                error_buf, error_buf_size)) goto done;
    }
    {
        Atom *entry = NULL;
        if (!ldpp_source_run(plan, &terms, "ldpp-v1:compile-entry",
                ldpp_name(&terms.arena, &profile->start_sort,
                    status, error_buf, error_buf_size), &work, &entry,
                status, error_buf, error_buf_size)) goto done;
        if (!ldpp_is_app(entry, "pp-production", 4u)) {
            if (status) *status = CETTA_LD_PARSER_PACK_V1_COMPILER_REJECTED;
            ldpp_error(error_buf, error_buf_size, "compiler returned a malformed entry production");
            goto done;
        }
        if (!ldpp_retain_production(&terms, entry, status,
                error_buf, error_buf_size)) goto done;
        start_needle = entry->expr.elems[2];
    }
    for (index = 0u; index < profile->class_len; index++) {
        Atom *clauses = NULL;
        if (!ldpp_source_run(plan, &terms, "ldpp-v1:compile-class",
                ldpp_class_view(&terms.arena, &profile->classes[index],
                    &work, status, error_buf, error_buf_size),
                &work, &clauses, status, error_buf, error_buf_size) ||
            !ldpp_collect_classes(&terms, clauses, &work, status, error_buf, error_buf_size))
            goto done;
    }
    for (index = 0u; index < profile->state_len; index++) {
        Atom *production = NULL;
        if (!ldpp_source_run(plan, &terms, "ldpp-v1:compile-lexical",
                ldpp_lexical_view(&terms.arena, &profile->states[index],
                    status, error_buf, error_buf_size),
                &work, &production, status, error_buf, error_buf_size) ||
            !ldpp_retain_production(&terms, production, status,
                error_buf, error_buf_size)) goto done;
    }
    /* ParserPack's ABI is intentionally order-independent and accepts only a
       strict canonical stream.  Authored order and multiplicity remain bound
       in the input structure and distinct production labels. A digest binds
       source identity; it does not itself prove occurrence correspondence. */
    if (!ldpp_terms_canonicalize(
            terms.productions, terms.production_len,
            status, error_buf, error_buf_size) ||
        !ldpp_terms_canonicalize(
            terms.classes, terms.class_len,
            status, error_buf, error_buf_size)) {
        goto done;
    }
    ldpp_combine_digests("LanguageDefParserPackV1/authority",
                         language_authority_sha256,
                         profile->authority_sha256, "", source_digest);
    cetta_native_sha256_hex(
        compiler_source, compiler_source_len,
        candidate.compiler_sha256);
    cetta_native_sha256_hex(
        (const uint8_t *)"ParserPackABIV1/environment/1",
        sizeof("ParserPackABIV1/environment/1") - 1u,
        environment_digest);
    /* Structural admission and source provenance are not proof replay.
     * This evaluator supplies no derivation certificate. */
    if (!ppabi_v1_pack_load_structural(
            &candidate.pack, terms.productions, terms.production_len,
            terms.classes, terms.class_len, source_digest,
            candidate.compiler_sha256, environment_digest,
            error_buf, error_buf_size)) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_ABI_REJECTED;
        goto done;
    }
    candidate.start_state = ldpp_pack_state(&candidate.pack, start_needle);
    if (!candidate.start_state || !ppabi_v1_pack_start_is_closed(
            &candidate.pack, candidate.start_state,
            error_buf, error_buf_size)) {
        if (status) *status = CETTA_LD_PARSER_PACK_V1_OPEN_GRAMMAR;
        goto done;
    }
    (void)memcpy(candidate.language_authority_sha256,
                 language_authority_sha256, 65u);
    (void)memcpy(candidate.profile_authority_sha256,
                 profile->authority_sha256, 65u);
    ldpp_combine_digests("LanguageDefParserPackV1/binding",
                         language_authority_sha256,
                         profile->authority_sha256,
                         candidate.pack.pack_digest,
                         candidate.binding_sha256);
    candidate.authored_rule_len = language->term_len;
    candidate.lexical_rule_len = profile->state_len;
    candidate.entry_rule_len = 1u;
    cetta_ld_parser_pack_v1_free(out);
    *out = candidate;
    memset(&candidate, 0, sizeof(candidate));
    if (status) *status = CETTA_LD_PARSER_PACK_V1_OK;
    ok = true;

done:
    cetta_deterministic_equation_plan_v1_free(plan);
    cetta_ld_parser_pack_v1_free(&candidate);
    ldpp_terms_free(&terms);
    return ok;
}

bool cetta_language_def_parser_pack_v1_compile(
    CettaLdParserPackV1 *out,
    const CettaLanguageDefCoreV1 *language,
    const char language_authority_sha256[65],
    const CettaLdParserProfileV1 *profile,
    uint32_t work_limit, CettaLdParserPackV1Status *status,
    char *error_buf, size_t error_buf_size) {
    return cetta_language_def_parser_pack_v1_compile_source(
        out, language, language_authority_sha256, profile,
        cetta_ldpp_compiler_v1_source, cetta_ldpp_compiler_v1_source_len,
        work_limit, status, error_buf, error_buf_size);
}

const char *cetta_ld_parser_pack_v1_status_name(
    CettaLdParserPackV1Status status) {
    switch (status) {
    case CETTA_LD_PARSER_PACK_V1_OK: return "ok";
    case CETTA_LD_PARSER_PACK_V1_BAD_ARGUMENT: return "bad-argument";
    case CETTA_LD_PARSER_PACK_V1_MALFORMED_PROFILE: return "malformed-profile";
    case CETTA_LD_PARSER_PACK_V1_OUTSIDE_FRAGMENT: return "outside-fragment";
    case CETTA_LD_PARSER_PACK_V1_RESOURCE_LIMIT: return "resource-limit";
    case CETTA_LD_PARSER_PACK_V1_ALLOCATION_FAILURE: return "allocation-failure";
    case CETTA_LD_PARSER_PACK_V1_INVALID_UTF8: return "invalid-utf8";
    case CETTA_LD_PARSER_PACK_V1_OPEN_GRAMMAR: return "open-grammar";
    case CETTA_LD_PARSER_PACK_V1_ABI_REJECTED: return "abi-rejected";
    case CETTA_LD_PARSER_PACK_V1_COMPILER_REJECTED: return "compiler-rejected";
    }
    return "unknown";
}
