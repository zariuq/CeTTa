#include "parser_token_runtime_v1.h"

#include "deterministic_equation_plan_v1.h"
#include "finite_horn_ground_term_v1.h"
#include "regular_span_nfa_v1.h"
#include "native_sha256.h"
#include "regular_token_compiler_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const uint8_t cetta_regular_token_compiler_v1_source[];
extern const size_t cetta_regular_token_compiler_v1_source_len;

typedef enum {
    TOKEN_RULE_REGULAR, TOKEN_RULE_KEYWORD, TOKEN_RULE_TRIVIA,
    TOKEN_RULE_LINE_COMMENT, TOKEN_RULE_BLOCK_COMMENT
} TokenRuleKind;

typedef struct {
    Atom *tag;
    Atom *regex;
    Atom *value_kind;
    TokenRuleKind kind;
    uint32_t terminal_id;
    uint32_t priority;
    uint32_t *comment_suffix;
    uint32_t comment_suffix_len;
} TokenRule;

typedef struct {
    Arena arena;
    const CettaLdParserPackV1 *compiled;
    const CettaParserTokenProfileV1 *profile;
    char binding[65];
    char authority_binding[65];
    CettaBnfcLexicalPolicyV1 policy_kind;
    TokenRule *rules;
    uint32_t rule_len;
    uint32_t rule_capacity;
    uint32_t *tag_rules;
    RSNFAV1Plan nfa;
    RSDFAV1Program program;
    CettaLpNativeGrammar grammar;
    PPNativeV1PreparedStartFamily prepared;
    const CettaLpNativeGllPrepared *gll;
    const CettaLpNativeGlrPrepared *glr;
    uint32_t start_state_id;
    uint32_t trivia_terminal;
    uint32_t eof_terminal;
} TokenRuntime;

static bool token_error(char *error, size_t size, const char *format, ...) {
    if (error && size) {
        va_list args;
        va_start(args, format);
        (void)vsnprintf(error, size, format, args);
        va_end(args);
    }
    return false;
}

static bool form(const Atom *a, const char *name, uint32_t arity) {
    return a && a->kind == ATOM_EXPR && a->expr.len == arity + 1u &&
        atom_is_symbol(a->expr.elems[0], name);
}

static Atom *unary(Arena *a, const char *name, Atom *arg) {
    return atom_expr2(a, atom_symbol(a, name), arg);
}

static Atom *regex_char(Arena *arena, uint32_t scalar) {
    return unary(arena, "rx-char", unary(arena, "cp", atom_int(arena, scalar)));
}

static Atom *regex_sequence(Arena *arena, const uint32_t *scalars, uint32_t len) {
    Atom *result = atom_symbol(arena, "rx-eps");
    for (uint32_t i = len; i > 0u; --i)
        result = atom_expr3(arena, atom_symbol(arena, "rx-seq"),
            regex_char(arena, scalars[i - 1u]), result);
    return result;
}

static Atom *literal_regex(TokenRuntime *runtime, const uint32_t *scalars, uint32_t len) {
    if (runtime->policy_kind != CETTA_BNFC_C_FLEX_2_9_6_3_V1)
        return regex_sequence(&runtime->arena, scalars, len);
    if (len > UINT32_MAX / 4u) return NULL;
    uint32_t *bytes = arena_alloc(&runtime->arena, ((size_t)len * 4u + 1u) * sizeof(*bytes));
    if (!bytes) return NULL;
    uint32_t n = 0u;
    for (uint32_t i = 0u; i < len; ++i) {
        uint32_t cp = scalars[i];
        if (cp < 0x80u) bytes[n++] = cp;
        else if (cp < 0x800u) { bytes[n++] = 0xc0u | (cp >> 6u); bytes[n++] = 0x80u | (cp & 63u); }
        else if (cp < 0x10000u) {
            bytes[n++] = 0xe0u | (cp >> 12u); bytes[n++] = 0x80u | ((cp >> 6u) & 63u);
            bytes[n++] = 0x80u | (cp & 63u);
        } else {
            bytes[n++] = 0xf0u | (cp >> 18u); bytes[n++] = 0x80u | ((cp >> 12u) & 63u);
            bytes[n++] = 0x80u | ((cp >> 6u) & 63u); bytes[n++] = 0x80u | (cp & 63u);
        }
    }
    return regex_sequence(&runtime->arena, bytes, n);
}

static bool scalar_text(Arena *arena, const Atom *text,
                         uint32_t **out, uint32_t *length) {
    const Atom *cursor = text;
    uint32_t len = 0u;
    if (text && text->kind == ATOM_GROUNDED && text->ground.gkind == GV_STRING) {
        CettaLpNativeUtf8ScalarBuffer buffer;
        cetta_lp_native_utf8_scalar_buffer_init(&buffer);
        if (!cetta_lp_native_utf8_scalar_buffer_decode(&buffer,
                (const uint8_t *)text->ground.sval, strlen(text->ground.sval), NULL, 0u))
            return false;
        len = buffer.view.scalar_len;
        *out = arena_alloc(arena, ((size_t)len + 1u) * sizeof(**out));
        if (*out && len) memcpy(*out, buffer.view.codepoints, (size_t)len * sizeof(**out));
        *length = len;
        cetta_lp_native_utf8_scalar_buffer_free(&buffer);
        return *out != NULL;
    }
    while (form(cursor, "bnf-v1:text-cons", 2u)) {
        Atom *cp = cursor->expr.elems[1];
        if (!cp || cp->kind != ATOM_GROUNDED || cp->ground.gkind != GV_INT ||
            cp->ground.ival < 0 || cp->ground.ival > 0x10ffff ||
            (cp->ground.ival >= 0xd800 && cp->ground.ival <= 0xdfff) || len == UINT32_MAX)
            return false;
        ++len;
        cursor = cursor->expr.elems[2];
    }
    if (!form(cursor, "bnf-v1:text-nil", 0u)) return false;
    *out = arena_alloc(arena, ((size_t)len + 1u) * sizeof(**out));
    if (!*out) return false;
    cursor = text;
    for (uint32_t i = 0u; i < len; ++i, cursor = cursor->expr.elems[2])
        (*out)[i] = (uint32_t)cursor->expr.elems[1]->ground.ival;
    *length = len;
    return true;
}

static Atom *text_value(Arena *arena, const uint32_t *scalars, uint32_t len) {
    Atom *nil[] = {atom_symbol(arena, "bnf-v1:text-nil")};
    Atom *result = atom_expr(arena, nil, 1u);
    for (uint32_t i = len; i > 0u; --i)
        result = atom_expr3(arena, atom_symbol(arena, "bnf-v1:text-cons"),
            atom_int(arena, scalars[i - 1u]), result);
    return result;
}

static bool terminal_for(TokenRuntime *runtime, const Atom *tag, uint32_t *out) {
    const PPNativeV1ForestExtension *extension = &runtime->compiled->token_bindings.extension;
    for (uint32_t i = 0u; i < extension->terminal_len; ++i) {
        Atom *identity = extension->terminals[i].identity;
        if (form(identity, "pp-span-terminal", 1u) && atom_eq(identity->expr.elems[1], (Atom *)tag)) {
            *out = extension->terminals[i].terminal_id;
            return true;
        }
    }
    return false;
}

static bool add_rule(TokenRuntime *runtime, Atom *tag, Atom *regex,
                      Atom *kind, TokenRuleKind rule_kind, uint32_t terminal,
                      uint32_t priority, char *error, size_t error_size) {
    if (runtime->rule_len == runtime->rule_capacity) {
        uint32_t cap = runtime->rule_capacity ? runtime->rule_capacity * 2u : 16u;
        if (cap < runtime->rule_capacity || cap > UINT32_MAX / sizeof(TokenRule))
            return token_error(error, error_size, "token rule capacity exceeded");
        TokenRule *rules = realloc(runtime->rules, (size_t)cap * sizeof(*rules));
        if (!rules) return token_error(error, error_size, "token rule allocation failed");
        runtime->rules = rules;
        runtime->rule_capacity = cap;
    }
    for (uint32_t i = 0u; i < runtime->rule_len; ++i)
        if (atom_eq(runtime->rules[i].tag, tag))
            return token_error(error, error_size, "duplicate lexical rule identity");
    runtime->rules[runtime->rule_len++] = (TokenRule){
        .tag = tag, .regex = regex, .value_kind = kind, .kind = rule_kind,
        .terminal_id = terminal, .priority = priority,
    };
    return true;
}

static uint32_t builtin_rank(Atom *kind) {
    if (form(kind, "TokenPositionV1", 1u)) kind = kind->expr.elems[1];
    if (atom_is_symbol(kind, "TokenStringV1")) return 0u;
    if (atom_is_symbol(kind, "TokenCharV1")) return 1u;
    if (atom_is_symbol(kind, "TokenDoubleV1")) return 2u;
    if (atom_is_symbol(kind, "TokenIntegerV1")) return 3u;
    if (atom_is_symbol(kind, "TokenIdentV1")) return 4u;
    return UINT32_MAX;
}

static bool runtime_rules(TokenRuntime *runtime, char *error, size_t error_size) {
    Arena *arena = &runtime->arena;
    const CettaParserTokenProfileV1 *profile = runtime->profile;
    const PPNativeV1ForestExtension *extension = &runtime->compiled->token_bindings.extension;
    if (!terminal_for(runtime, atom_symbol(arena, "pp-trivia"), &runtime->trivia_terminal))
        return token_error(error, error_size, "token grammar has no trivia binding");
    runtime->eof_terminal = UINT32_MAX;
    for (uint32_t i = 0u; i < runtime->compiled->pack.terminal_len; ++i) {
        if (runtime->compiled->pack.terminals[i].kind == PPABI_V1_TERMINAL_EOF)
            runtime->eof_terminal = i;
    }
    if (runtime->eof_terminal == UINT32_MAX)
        return token_error(error, error_size, "token grammar has no source EOF boundary");
    for (uint32_t i = 0u; i < extension->terminal_len; ++i) {
        Atom *identity = extension->terminals[i].identity;
        Atom *tag = form(identity, "pp-span-terminal", 1u) ? identity->expr.elems[1] : NULL;
        if (form(tag, "pp-keyword", 1u)) {
            uint32_t *points, len;
            if (!scalar_text(arena, tag->expr.elems[1], &points, &len) || !len ||
                !add_rule(runtime, atom_deep_copy(arena, tag), literal_regex(runtime, points, len),
                    atom_symbol(arena, "TokenTextV1"), TOKEN_RULE_KEYWORD,
                    extension->terminals[i].terminal_id,
                    runtime->policy_kind == CETTA_BNFC_DOCUMENTED_V1 ? 0x40000000u : 0u,
                    error, error_size)) return false;
        }
    }
    uint32_t declaration_index = 0u;
    for (Atom *p = profile->declarations; form(p, "LCons", 2u);
         p = p->expr.elems[2], ++declaration_index) {
        Atom *row = p->expr.elems[1];
        bool trivia = form(row, "RegularTriviaDeclV1", 3u);
        Atom *tag = row->expr.elems[trivia ? 1u : 2u];
        Atom *kind = trivia ? atom_symbol(arena, "TokenTextV1") : row->expr.elems[4];
        Atom *regex = row->expr.elems[trivia ? 2u : 3u];
        uint32_t terminal = runtime->trivia_terminal, rank = builtin_rank(kind);
        if (!trivia && !terminal_for(runtime, tag, &terminal))
            return token_error(error, error_size, "declared token has no grammar binding");
        uint32_t priority = trivia ?
            (runtime->policy_kind == CETTA_BNFC_DOCUMENTED_V1 ? 0x30000000u : 0xe0000000u) + declaration_index
            : rank == UINT32_MAX ? 0x60000000u + declaration_index : 0xc0000000u + rank;
        if (profile->policy_kind == CETTA_BNFC_C_FLEX_2_9_6_3_V1 && rank <= 1u)
            regex = regex_char(arena, rank == 0u ? '"' : '\'');
        if (!add_rule(runtime, atom_deep_copy(arena, tag), atom_deep_copy(arena, regex),
                atom_deep_copy(arena, kind), trivia ? TOKEN_RULE_TRIVIA : TOKEN_RULE_REGULAR,
                terminal, priority, error, error_size)) return false;
    }
    uint32_t comment_index = 0u;
    for (Atom *p = profile->comments; form(p, "LCons", 2u); p = p->expr.elems[2], ++comment_index) {
        Atom *row = p->expr.elems[1];
        bool block = form(row, "ParserBlockCommentV1", 3u);
        uint32_t *points, len;
        if (!scalar_text(arena, row->expr.elems[1], &points, &len) || !len)
            return token_error(error, error_size, "invalid comment opener");
        Atom *tag = atom_expr3(arena, atom_symbol(arena, "parser-comment"),
            atom_int(arena, comment_index), atom_deep_copy(arena, row->expr.elems[block ? 3u : 2u]));
        if (!add_rule(runtime, tag, literal_regex(runtime, points, len),
                atom_symbol(arena, "TokenTextV1"), block ? TOKEN_RULE_BLOCK_COMMENT : TOKEN_RULE_LINE_COMMENT,
                runtime->trivia_terminal,
                (runtime->policy_kind == CETTA_BNFC_DOCUMENTED_V1 ? 0u : 0x20000000u) +
                    (block ? 0x10000000u : 0u) + comment_index,
                error, error_size)) return false;
        if (block && (!scalar_text(arena, row->expr.elems[2],
                &runtime->rules[runtime->rule_len - 1u].comment_suffix,
                &runtime->rules[runtime->rule_len - 1u].comment_suffix_len) ||
                !runtime->rules[runtime->rule_len - 1u].comment_suffix_len))
            return token_error(error, error_size, "invalid comment closer");
    }
    if (!runtime->rule_len && !add_rule(runtime, atom_symbol(arena, "pp-trivia"),
            atom_symbol(arena, "rx-empty"), atom_symbol(arena, "TokenTextV1"),
            TOKEN_RULE_TRIVIA, runtime->trivia_terminal, UINT32_MAX, error, error_size)) return false;
    return true;
}

static bool runtime_automaton(TokenRuntime *runtime, uint32_t work_limit,
                               char *error, size_t error_size) {
    CettaRegularTokenCompilerV1 *compiler = NULL;
    Atom **answers = NULL;
    size_t answer_len = 0u, capacity = 0u;
    RSDFAV1Plan dfa;
    RSDFAV1BuildOutcome build;
    bool ok = false;
    rsdfa_v1_plan_init(&dfa);
    if (!cetta_regular_token_compiler_v1_load(&runtime->compiled->pack, work_limit,
            &compiler, error, error_size)) goto done;
    for (uint32_t i = 0u; i < runtime->rule_len; ++i) {
        Atom *result = NULL;
        if (!cetta_regular_token_compiler_v1_answers(compiler, &runtime->arena,
                runtime->rules[i].tag, runtime->rules[i].regex, &result, error, error_size)) goto done;
        Atom *cursor = result;
        while (form(cursor, "LCons", 2u)) {
            if (answer_len == capacity) {
                size_t cap = capacity ? capacity * 2u : 128u;
                if (cap > work_limit || cap > SIZE_MAX / sizeof(*answers)) cap = work_limit;
                if (cap <= capacity) { token_error(error, error_size, "lexical compilation work limit"); goto done; }
                Atom **items = realloc(answers, cap * sizeof(*answers));
                if (!items) { token_error(error, error_size, "lexical answer allocation failed"); goto done; }
                answers = items;
                capacity = cap;
            }
            answers[answer_len++] = cursor->expr.elems[1];
            cursor = cursor->expr.elems[2];
        }
        if (!atom_is_symbol(cursor, "LNil")) { token_error(error, error_size, "malformed authored lexical output"); goto done; }
    }
    if (!rsnfa_v1_plan_load(&runtime->compiled->pack, answers, answer_len,
            &runtime->nfa, error, error_size) ||
        !rsdfa_v1_plan_build(&runtime->nfa.nfa, work_limit, work_limit,
            &dfa, &build, error, error_size) || build != RSDFA_V1_BUILD_COMPLETED ||
        !rsdfa_v1_plan_export_program(&dfa, &runtime->program, error, error_size)) goto done;
    runtime->tag_rules = calloc(runtime->program.tag_len, sizeof(*runtime->tag_rules));
    if (!runtime->tag_rules) { token_error(error, error_size, "lexical tag map allocation failed"); goto done; }
    for (uint32_t t = 0u; t < runtime->program.tag_len; ++t) {
        RSDFAV1TagLanguage language;
        rsdfa_v1_tag_language_init(&language);
        bool valid = rsdfa_v1_plan_tag_language(&dfa, t, &language, error, error_size);
        bool nullable = language.accepts_empty || language.accepts_eof_empty;
        rsdfa_v1_tag_language_free(&language);
        if (!valid || nullable) { token_error(error, error_size, "a lexer rule accepts empty input"); goto done; }
        uint32_t found = UINT32_MAX;
        for (uint32_t i = 0u; i < runtime->rule_len; ++i)
            if (atom_eq(runtime->nfa.tags[t], runtime->rules[i].tag)) found = i;
        if (found == UINT32_MAX) { token_error(error, error_size, "lexical tag authority is incomplete"); goto done; }
        runtime->tag_rules[t] = found;
    }
    ok = true;
done:
    cetta_regular_token_compiler_v1_free(compiler);
    rsdfa_v1_plan_free(&dfa);
    free(answers);
    return ok;
}

void cetta_parser_token_runtime_v1_init(CettaParserTokenRuntimeV1 *runtime) {
    if (runtime) runtime->implementation = NULL;
}

void cetta_parser_token_runtime_v1_free(CettaParserTokenRuntimeV1 *runtime) {
    TokenRuntime *owned = runtime ? runtime->implementation : NULL;
    if (!owned) return;
    ppnative_v1_prepared_start_family_free(&owned->prepared);
    cetta_lp_native_grammar_free(&owned->grammar);
    rsdfa_v1_program_free(&owned->program);
    rsnfa_v1_plan_free(&owned->nfa);
    free(owned->rules);
    free(owned->tag_rules);
    arena_free(&owned->arena);
    free(owned);
    runtime->implementation = NULL;
}

bool cetta_parser_token_runtime_v1_prepare(
    CettaParserTokenRuntimeV1 *out, const CettaLdParserPackV1 *compiled,
    const CettaParserTokenProfileV1 *profile, uint32_t work_limit,
    char *error, size_t error_size) {
    CettaParserTokenRuntimeV1 candidate = {0};
    if (!out || !compiled || !compiled->tokenized || !profile || !work_limit)
        return token_error(error, error_size, "invalid token runtime authorities");
    TokenRuntime *runtime = calloc(1u, sizeof(*runtime));
    if (!runtime) return token_error(error, error_size, "token runtime allocation failed");
    candidate.implementation = runtime;
    arena_init(&runtime->arena);
    runtime->arena.hashcons = NULL;
    rsnfa_v1_plan_init(&runtime->nfa);
    rsdfa_v1_program_init(&runtime->program);
    cetta_lp_native_grammar_init(&runtime->grammar);
    ppnative_v1_prepared_start_family_init(&runtime->prepared);
    runtime->compiled = compiled;
    runtime->profile = profile;
    runtime->policy_kind = profile->policy_kind;
    memcpy(runtime->authority_binding, compiled->binding_sha256, sizeof(runtime->authority_binding));
    CettaNativeSha256 sha;
    cetta_native_sha256_init(&sha);
    cetta_native_sha256_update(&sha, (const uint8_t *)"ParserTokenRuntimeV1", 20u);
    cetta_native_sha256_update(&sha, (const uint8_t *)compiled->binding_sha256, 64u);
    cetta_native_sha256_update(&sha, cetta_regular_token_compiler_v1_source,
        cetta_regular_token_compiler_v1_source_len);
    cetta_native_sha256_finish_hex(&sha, runtime->binding);
    int32_t start = ppnative_v1_state_find_extended(&compiled->pack,
        &compiled->token_bindings.extension, compiled->start_state);
    if (start < 0 || !runtime_rules(runtime, error, error_size) ||
        !runtime_automaton(runtime, work_limit, error, error_size) ||
        !ppnative_v1_grammar_build(&compiled->pack, &runtime->grammar) ||
        !ppnative_v1_grammar_extend(&compiled->pack, &compiled->token_bindings.extension,
            &runtime->grammar, error, error_size)) goto fail;
    runtime->start_state_id = (uint32_t)start;
    if (!ppnative_v1_prepared_start_family_prepare(&runtime->prepared,
            &runtime->grammar, runtime->binding, &runtime->start_state_id, 1u,
            error, error_size)) goto fail;
    if (!ppnative_v1_prepared_start_family_lookup(&runtime->prepared,
            runtime->binding, runtime->start_state_id, &runtime->gll,
            &runtime->glr, error, error_size)) goto fail;
    cetta_parser_token_runtime_v1_free(out);
    *out = candidate;
    return true;
fail:
    cetta_parser_token_runtime_v1_free(&candidate);
    return false;
}

static Atom *stored_form(Arena *arena, const char *name, Atom **fields, uint32_t len) {
    Atom **items = arena_alloc(arena, ((size_t)len + 1u) * sizeof(*items));
    if (!items) return NULL;
    items[0] = atom_symbol(arena, name);
    if (len) memcpy(items + 1u, fields, (size_t)len * sizeof(*items));
    return atom_expr(arena, items, (size_t)len + 1u);
}

static bool stored_uint(Atom *value, uint32_t *out) {
    if (!value || value->kind != ATOM_GROUNDED || value->ground.gkind != GV_INT ||
        value->ground.ival < 0 || (uint64_t)value->ground.ival > UINT32_MAX) return false;
    *out = (uint32_t)value->ground.ival;
    return true;
}

static bool stored_digest(Atom *value, char out[65]) {
    if (!value || value->kind != ATOM_GROUNDED || value->ground.gkind != GV_STRING ||
        strlen(value->ground.sval) != 64u) return false;
    for (uint32_t i = 0u; i < 64u; ++i) {
        char c = value->ground.sval[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    memcpy(out, value->ground.sval, 65u);
    return true;
}

static bool stored_vector(Atom *value, const char *name, uint32_t *len) {
    if (!value || value->kind != ATOM_EXPR || !value->expr.len ||
        value->expr.len - 1u > UINT32_MAX || !atom_is_symbol(value->expr.elems[0], name)) return false;
    *len = (uint32_t)(value->expr.len - 1u);
    return true;
}

bool cetta_parser_token_runtime_v1_export_program(
    const CettaParserTokenRuntimeV1 *runtime_value, PPTableSnapshotV1 *out,
    Arena *arena, Atom **metadata, char *error, size_t error_size) {
    const TokenRuntime *runtime = runtime_value ? runtime_value->implementation : NULL;
    PPTableSnapshotV1 candidate;
    pp_table_snapshot_v1_init(&candidate);
    if (metadata) *metadata = NULL;
    if (!runtime || !out || !arena || !metadata) {
        token_error(error, error_size, "missing immutable token program");
        goto failed;
    }
    candidate.kernel = PP_TABLE_SNAPSHOT_V1_KERNEL_GENERAL;
    strcpy(candidate.profile, "parserpack-token");
    memcpy(candidate.syntax_digest, runtime->compiled->pack.source_digest, 65u);
    memcpy(candidate.artifact_digest, runtime->compiled->pack.pack_digest, 65u);
    if (!rsdfa_v1_program_copy(&candidate.dfa, &runtime->program, error, error_size) ||
        !cetta_lp_native_gll_prepared_export_program(runtime->gll, &candidate.gll, error, error_size) ||
        !cetta_lp_native_glr_prepared_export_program(runtime->glr, &candidate.slr, error, error_size))
        goto failed;
    Atom **rows = arena_alloc(arena, ((size_t)runtime->rule_len + 1u) * sizeof(*rows));
    if (!rows) goto failed;
    for (uint32_t i = 0u; i < runtime->rule_len; ++i) {
        const TokenRule *rule = &runtime->rules[i];
        Atom **points = arena_alloc(arena, ((size_t)rule->comment_suffix_len + 1u) * sizeof(*points));
        if (!points) goto failed;
        for (uint32_t j = 0u; j < rule->comment_suffix_len; ++j)
            points[j] = atom_int(arena, rule->comment_suffix[j]);
        Atom *suffix = stored_form(arena, "ParserLexerSuffixV1", points, rule->comment_suffix_len);
        Atom *fields[] = {rule->tag, atom_int(arena, rule->kind), rule->value_kind,
            atom_int(arena, rule->terminal_id), atom_int(arena, rule->priority), suffix};
        rows[i] = stored_form(arena, "ParserLexerRuleV1", fields, 6u);
    }
    Atom *rules = stored_form(arena, "ParserLexerRulesV1", rows, runtime->rule_len);
    Atom **tags = arena_alloc(arena, ((size_t)runtime->program.tag_len + 1u) * sizeof(*tags));
    if (!tags) goto failed;
    for (uint32_t i = 0u; i < runtime->program.tag_len; ++i)
        tags[i] = atom_int(arena, runtime->tag_rules[i]);
    Atom *fields[] = {atom_int(arena, runtime->policy_kind), atom_int(arena, runtime->start_state_id),
        atom_int(arena, runtime->trivia_terminal), atom_int(arena, runtime->eof_terminal),
        atom_string(arena, runtime->binding), atom_string(arena, runtime->authority_binding),
        rules, stored_form(arena, "ParserLexerTagsV1", tags, runtime->program.tag_len)};
    Atom *stored = stored_form(arena, "ParserLexerStoredV1", fields, 8u);
    if (!stored || !(*metadata = atom_deep_copy(arena, stored))) goto failed;
    pp_table_snapshot_v1_free(out);
    *out = candidate;
    return true;
failed:
    pp_table_snapshot_v1_free(&candidate);
    if (error && error_size && !error[0]) token_error(error, error_size, "token program export allocation failed");
    return false;
}

bool cetta_parser_token_runtime_v1_import_program(
    CettaParserTokenRuntimeV1 *out, const CettaLdParserPackV1 *compiled,
    const CettaParserTokenProfileV1 *profile, const PPTableSnapshotV1 *snapshot,
    Atom *metadata, char *error, size_t error_size) {
    CettaParserTokenRuntimeV1 candidate = {0};
    if (!out || !compiled || !compiled->tokenized || !profile || !snapshot ||
        snapshot->kernel != PP_TABLE_SNAPSHOT_V1_KERNEL_GENERAL ||
        !form(metadata, "ParserLexerStoredV1", 8u))
        return token_error(error, error_size, "missing stored token program");
    TokenRuntime *runtime = calloc(1u, sizeof(*runtime));
    if (!runtime) return token_error(error, error_size, "token program import allocation failed");
    candidate.implementation = runtime;
    arena_init(&runtime->arena);
    runtime->arena.hashcons = NULL;
    runtime->compiled = compiled;
    runtime->profile = profile;
    metadata = atom_deep_copy(&runtime->arena, metadata);
    uint32_t policy, rules_len, tags_len;
    Atom *rules = metadata ? metadata->expr.elems[7] : NULL;
    Atom *tags = metadata ? metadata->expr.elems[8] : NULL;
    if (!metadata || !stored_uint(metadata->expr.elems[1], &policy) ||
        policy > CETTA_BNFC_C_FLEX_2_9_6_3_V1 || policy != (uint32_t)profile->policy_kind ||
        !stored_uint(metadata->expr.elems[2], &runtime->start_state_id) ||
        runtime->start_state_id != snapshot->gll.start_nonterminal ||
        !stored_uint(metadata->expr.elems[3], &runtime->trivia_terminal) ||
        !stored_uint(metadata->expr.elems[4], &runtime->eof_terminal) ||
        !stored_digest(metadata->expr.elems[5], runtime->binding) ||
        !stored_digest(metadata->expr.elems[6], runtime->authority_binding) ||
        !stored_vector(rules, "ParserLexerRulesV1", &rules_len) || !rules_len ||
        !stored_vector(tags, "ParserLexerTagsV1", &tags_len) || tags_len != snapshot->dfa.tag_len)
        goto failed;
    uint64_t terminal_count = (uint64_t)compiled->pack.terminal_len + compiled->token_bindings.extension.terminal_len;
    if (runtime->eof_terminal >= compiled->pack.terminal_len ||
        runtime->trivia_terminal >= terminal_count) goto failed;
    runtime->policy_kind = (CettaBnfcLexicalPolicyV1)policy;
    runtime->rules = calloc(rules_len, sizeof(*runtime->rules));
    runtime->tag_rules = calloc(tags_len ? tags_len : 1u, sizeof(*runtime->tag_rules));
    if (!runtime->rules || !runtime->tag_rules) goto failed;
    runtime->rule_len = runtime->rule_capacity = rules_len;
    for (uint32_t i = 0u; i < rules_len; ++i) {
        Atom *row = rules->expr.elems[i + 1u];
        uint32_t kind, len;
        TokenRule *rule = &runtime->rules[i];
        if (!form(row, "ParserLexerRuleV1", 6u) ||
            !stored_uint(row->expr.elems[2], &kind) || kind > TOKEN_RULE_BLOCK_COMMENT ||
            !stored_uint(row->expr.elems[4], &rule->terminal_id) || rule->terminal_id >= terminal_count ||
            !stored_uint(row->expr.elems[5], &rule->priority) ||
            !stored_vector(row->expr.elems[6], "ParserLexerSuffixV1", &len)) goto failed;
        rule->tag = row->expr.elems[1];
        rule->kind = (TokenRuleKind)kind;
        rule->value_kind = row->expr.elems[3];
        rule->comment_suffix = arena_alloc(&runtime->arena, ((size_t)len + 1u) * sizeof(uint32_t));
        if (!rule->comment_suffix) goto failed;
        rule->comment_suffix_len = len;
        for (uint32_t j = 0u; j < len; ++j)
            if (!stored_uint(row->expr.elems[6]->expr.elems[j + 1u], &rule->comment_suffix[j])) goto failed;
    }
    for (uint32_t i = 0u; i < tags_len; ++i)
        if (!stored_uint(tags->expr.elems[i + 1u], &runtime->tag_rules[i]) ||
            runtime->tag_rules[i] >= rules_len) goto failed;
    if (!rsdfa_v1_program_copy(&runtime->program, &snapshot->dfa, error, error_size) ||
        !ppnative_v1_prepared_start_family_import_programs(&runtime->prepared, runtime->binding,
            &snapshot->gll, &snapshot->slr, error, error_size) ||
        !ppnative_v1_prepared_start_family_lookup(&runtime->prepared, runtime->binding,
            runtime->start_state_id, &runtime->gll, &runtime->glr, error, error_size)) goto failed;
    cetta_parser_token_runtime_v1_free(out);
    *out = candidate;
    return true;
failed:
    cetta_parser_token_runtime_v1_free(&candidate);
    if (error && error_size && !error[0]) token_error(error, error_size, "malformed stored token program");
    return false;
}

void cetta_parser_token_scan_v1_init(CettaParserTokenScanV1 *scan) {
    if (!scan) return;
    memset(scan, 0, sizeof(*scan));
    arena_init(&scan->arena);
    scan->arena.hashcons = NULL;
    cetta_lp_native_utf8_scalar_buffer_init(&scan->source);
}

bool cetta_parser_token_runtime_v1_check_metadata(
    const CettaParserTokenRuntimeV1 *value, char *error, size_t error_size) {
    const TokenRuntime *runtime = value ? value->implementation : NULL;
    TokenRuntime expected = {0};
    bool ok = false;
    if (!runtime || !runtime->compiled || !runtime->profile ||
        memcmp(runtime->authority_binding, runtime->compiled->binding_sha256, 65u) ||
        runtime->policy_kind != runtime->profile->policy_kind)
        return token_error(error, error_size, "stored lexer authority differs from its profile");
    arena_init(&expected.arena);
    expected.arena.hashcons = NULL;
    expected.compiled = runtime->compiled;
    expected.profile = runtime->profile;
    expected.policy_kind = runtime->policy_kind;
    if (!runtime_rules(&expected, error, error_size)) goto done;
    if (runtime->rule_len != expected.rule_len ||
        runtime->trivia_terminal != expected.trivia_terminal ||
        runtime->eof_terminal != expected.eof_terminal) goto inconsistent;
    for (uint32_t i = 0u; i < runtime->rule_len; ++i) {
        const TokenRule *left = &runtime->rules[i], *right = &expected.rules[i];
        if (left->kind != right->kind || left->terminal_id != right->terminal_id ||
            left->priority != right->priority || !atom_eq(left->tag, right->tag) ||
            !atom_eq(left->value_kind, right->value_kind) ||
            left->comment_suffix_len != right->comment_suffix_len ||
            (left->comment_suffix_len && memcmp(left->comment_suffix, right->comment_suffix,
                (size_t)left->comment_suffix_len * sizeof(*left->comment_suffix)))) goto inconsistent;
    }
    ok = true;
    goto done;
inconsistent:
    token_error(error, error_size, "stored lexer metadata differs from its profile");
done:
    free(expected.rules);
    arena_free(&expected.arena);
    return ok;
}

void cetta_parser_token_scan_v1_free(CettaParserTokenScanV1 *scan) {
    if (!scan) return;
    cetta_lp_native_utf8_scalar_buffer_free(&scan->source);
    free(scan->edges);
    free(scan->start_offsets);
    free(scan->terminal_ids);
    free(scan->witness_values);
    arena_free(&scan->arena);
    memset(scan, 0, sizeof(*scan));
}

static uint32_t scalar_boundary(const CettaLpNativeUtf8ScalarView *view, uint32_t byte) {
    uint32_t low = 0u, high = view->scalar_len + 1u;
    while (low < high) {
        uint32_t mid = low + (high - low) / 2u;
        if (cetta_lp_native_utf8_scalar_view_byte_offset(view, mid) < byte) low = mid + 1u;
        else high = mid;
    }
    return low <= view->scalar_len && cetta_lp_native_utf8_scalar_view_byte_offset(view, low) == byte
        ? low : UINT32_MAX;
}

static uint32_t comment_end(const TokenRule *rule,
                             const CettaLpNativeUtf8ScalarView *view,
                             uint32_t from, bool *closed) {
    *closed = rule->kind != TOKEN_RULE_BLOCK_COMMENT;
    if (rule->kind == TOKEN_RULE_LINE_COMMENT) {
        while (from < view->scalar_len &&
               cetta_lp_native_utf8_scalar_view_scalar_at(view, from) != '\n') ++from;
        return from;
    }
    for (; from <= view->scalar_len; ++from) {
        if (rule->comment_suffix_len > view->scalar_len - from) break;
        uint32_t n = 0u;
        while (n < rule->comment_suffix_len &&
            cetta_lp_native_utf8_scalar_view_scalar_at(view, from + n) == rule->comment_suffix[n]) ++n;
        if (n == rule->comment_suffix_len) { *closed = true; return from + n; }
    }
    return view->scalar_len;
}

static uint32_t escape_scalar(uint32_t cp) {
    switch (cp) {
    case 'n': return '\n'; case 'r': return '\r';
    case 't': return '\t'; case 'f': return '\f'; default: return cp;
    }
}

static Atom *token_value(CettaParserTokenScanV1 *scan, const TokenRule *rule,
                          uint32_t left, uint32_t right, bool c_flex,
                          bool forced_character, uint32_t character_scalar,
                          uint32_t *consumed, char *error, size_t error_size) {
    const CettaLpNativeUtf8ScalarView *view = &scan->source.view;
    Atom *kind = rule->value_kind;
    bool positioned = form(kind, "TokenPositionV1", 1u);
    if (positioned) kind = kind->expr.elems[1];
    uint32_t n = right - left;
    uint32_t *decoded = arena_alloc(&scan->arena, ((size_t)n + 1u) * sizeof(*decoded));
    if (!decoded) return NULL;
    uint32_t payload_len = 0u;
    bool quoted = atom_is_symbol(kind, "TokenStringV1") || atom_is_symbol(kind, "TokenCharV1");
    bool character = atom_is_symbol(kind, "TokenCharV1");
    uint32_t begin = left + (quoted ? 1u : 0u), end = right - (quoted && !(c_flex && character) ? 1u : 0u);
    if (forced_character) decoded[payload_len++] = character_scalar;
    for (uint32_t i = begin; !forced_character && i < end; ++i) {
        uint32_t cp = cetta_lp_native_utf8_scalar_view_scalar_at(view, i);
        if (c_flex && !character && quoted && cp == '\n') continue;
        if (quoted && cp == '\\' && i + 1u < end) {
            if (c_flex && !character)
                while (i + 1u < end && cetta_lp_native_utf8_scalar_view_scalar_at(view, i + 1u) == '\n') ++i;
            if (i + 1u < end) cp = escape_scalar(cetta_lp_native_utf8_scalar_view_scalar_at(view, ++i));
        }
        decoded[payload_len++] = cp;
    }
    if (atom_is_symbol(kind, "TokenCharV1") && payload_len != 1u) {
        token_error(error, error_size, "character token does not contain one scalar");
        return NULL;
    }
    *consumed = right;
    Atom *payload = text_value(&scan->arena, decoded, payload_len);
    uint32_t *raw = arena_alloc(&scan->arena, ((size_t)n + 1u) * sizeof(*raw));
    if (!raw) return NULL;
    for (uint32_t i = 0u; i < n; ++i) raw[i] = cetta_lp_native_utf8_scalar_view_scalar_at(view, left + i);
    Atom *lexeme = text_value(&scan->arena, raw, n);
    Atom *args[] = {
        atom_symbol(&scan->arena, "ParserTokenV1"), atom_deep_copy(&scan->arena, kind),
        lexeme, payload, atom_int(&scan->arena, left), atom_int(&scan->arena, right),
        atom_int(&scan->arena, cetta_lp_native_utf8_scalar_view_byte_offset(view, left)),
        atom_int(&scan->arena, cetta_lp_native_utf8_scalar_view_byte_offset(view, right)),
    };
    Atom *value = atom_expr(&scan->arena, args, sizeof(args) / sizeof(*args));
    if (positioned) {
        uint32_t line = 1u, column = 1u;
        for (uint32_t i = 0u; i < left; ++i) {
            uint32_t cp = cetta_lp_native_utf8_scalar_view_scalar_at(view, i);
            if (cp == '\n') { ++line; column = c_flex ? 0u : 1u; }
            else if (!c_flex && cp == '\t') column += 8u - ((column - 1u) % 8u);
            else column += c_flex
                ? cetta_lp_native_utf8_scalar_view_byte_offset(view, i + 1u) -
                  cetta_lp_native_utf8_scalar_view_byte_offset(view, i) : 1u;
        }
        Atom *position[] = {atom_symbol(&scan->arena, "ParserPositionTokenV1"),
            atom_int(&scan->arena, line), atom_int(&scan->arena, column), value};
        value = atom_expr(&scan->arena, position, 4u);
    }
    return value;
}

static bool c_quoted_end(const CettaLpNativeUtf8ScalarView *view,
                          const TokenRule *rule, uint32_t left,
                          uint32_t *right, uint32_t *trailing) {
    Atom *kind = rule->value_kind;
    if (form(kind, "TokenPositionV1", 1u)) kind = kind->expr.elems[1];
    if (!atom_is_symbol(kind, "TokenStringV1")) return true;
    uint32_t p = left + 1u;
    bool escaped = false;
    for (; p < view->scalar_len; ++p) {
        uint32_t cp = cetta_lp_native_utf8_scalar_view_scalar_at(view, p);
        if (!escaped && cp == '"') { *right = p + 1u; *trailing = *right; return true; }
        if (cp == '\n') continue;
        if (!escaped && cp == '\\') escaped = true;
        else escaped = false;
    }
    return false;
}

/* Flex's CHAR/CHARESC emit the first payload byte. CHAREND consumes until
 * the next quote; its default rule has no AST effect. EOF before a payload
 * is the scanner's ordinary EOF, unlike STRING's explicit error action. */
static bool c_character_end(const uint8_t *bytes, uint32_t length, uint32_t left,
                              uint32_t *right, uint32_t *trailing,
                              uint32_t *value, bool *has_token) {
    uint32_t p = left + 1u;
    bool escaped = false;
    *has_token = false;
    while (p < length) {
        uint32_t cp = bytes[p++];
        if (!escaped && cp == '\'') continue;
        if (!escaped && cp == '\\') { escaped = true; continue; }
        if (escaped && cp == '\n') continue;
        *has_token = true;
        *value = escaped ? escape_scalar(cp) : cp;
        *right = p;
        while (p < length && bytes[p++] != '\'') {}
        *trailing = p;
        return true;
    }
    *right = *trailing = length;
    return true;
}

static bool token_runtime_scan(
    const CettaParserTokenRuntimeV1 *runtime_value,
    const uint8_t *bytes, size_t length, uint64_t work_limit,
    bool audit, CettaParserTokenScanV1 *out, char *error, size_t error_size) {
    const TokenRuntime *runtime = runtime_value ? runtime_value->implementation : NULL;
    CettaParserTokenScanV1 scan;
    RSDFAV1Token *matches = NULL;
    RSDFAV1ByteToken *byte_matches = NULL;
    bool ok = false;
    cetta_parser_token_scan_v1_init(&scan);
    if (!runtime || !out || (!bytes && length) || length > UINT32_MAX ||
        !work_limit || (audit &&
        (memcmp(runtime->authority_binding, runtime->compiled->binding_sha256, 65u) ||
         runtime->policy_kind != runtime->profile->policy_kind))) {
        token_error(error, error_size, "unbound or changed token runtime"); goto done;
    }
    if (!cetta_lp_native_utf8_scalar_buffer_decode(&scan.source, bytes, length, error, error_size)) goto done;
    const CettaLpNativeUtf8ScalarView *view = &scan.source.view;
    uint32_t capacity = view->scalar_len + 2u;
    if (capacity < view->scalar_len || (size_t)capacity > SIZE_MAX / sizeof(*scan.edges)) goto done;
    scan.edges = calloc(capacity, sizeof(*scan.edges));
    scan.witness_values = calloc(capacity, sizeof(*scan.witness_values));
    scan.start_offsets = calloc(capacity, sizeof(*scan.start_offsets));
    uint32_t terminal_len = runtime->compiled->pack.terminal_len + runtime->compiled->token_bindings.extension.terminal_len;
    scan.terminal_ids = calloc(terminal_len, sizeof(*scan.terminal_ids));
    matches = calloc(runtime->program.tag_len, sizeof(*matches));
    byte_matches = calloc(runtime->program.tag_len, sizeof(*byte_matches));
    if (!scan.edges || !scan.witness_values || !scan.start_offsets ||
        !scan.terminal_ids || !matches || !byte_matches) { token_error(error, error_size, "token scan allocation failed"); goto done; }
    for (uint32_t i = 0u; i < terminal_len; ++i) scan.terminal_ids[i] = i;
    bool c_flex = runtime->policy_kind == CETTA_BNFC_C_FLEX_2_9_6_3_V1;
    uint32_t cursor = 0u, edge_len = 0u;
    while (cursor < view->scalar_len) {
        uint32_t accept_len, best = UINT32_MAX, right = cursor, best_priority = UINT32_MAX;
        uint32_t best_byte = view->byte_offsets[cursor];
        uint64_t used;
        if (c_flex) {
            RSDFAV1ByteCursorScanResult result;
            if (!rsdfa_v1_program_scan_byte_cursor_longest_prevalidated(&runtime->program,
                    bytes, (uint32_t)length, view->byte_offsets[cursor], work_limit - scan.work_used,
                    byte_matches, runtime->program.tag_len, &result, error, error_size)) goto done;
            accept_len = result.accept_len; used = result.work_item_len;
            if (result.outcome != RSDFA_V1_CURSOR_SCAN_COMPLETED) { scan.outcome = CETTA_PARSER_TOKEN_SCAN_V1_WORK_LIMIT; break; }
        } else {
            RSDFAV1CursorScanResult result;
            if (!rsdfa_v1_program_scan_cursor_longest_prevalidated(&runtime->program, view,
                    cursor, work_limit - scan.work_used, matches, runtime->program.tag_len,
                    &result, error, error_size)) goto done;
            accept_len = result.accept_len; used = result.work_item_len;
            if (result.outcome != RSDFA_V1_CURSOR_SCAN_COMPLETED) { scan.outcome = CETTA_PARSER_TOKEN_SCAN_V1_WORK_LIMIT; break; }
        }
        scan.work_used += used;
        for (uint32_t i = 0u; i < accept_len; ++i) {
            uint32_t tag = c_flex ? byte_matches[i].tag : matches[i].tag;
            const TokenRule *rule = &runtime->rules[runtime->tag_rules[tag]];
            uint32_t end_byte = c_flex ? byte_matches[i].end_byte : matches[i].end_byte;
            uint32_t end = c_flex ? scalar_boundary(view, end_byte) : matches[i].end_scalar;
            if (rule->kind == TOKEN_RULE_LINE_COMMENT || (!c_flex && rule->kind == TOKEN_RULE_BLOCK_COMMENT)) {
                bool closed;
                if (end == UINT32_MAX) continue;
                end = comment_end(rule, view, end, &closed);
                if (!closed) continue;
                end_byte = view->byte_offsets[end];
            }
            if (end_byte > best_byte || (end_byte == best_byte && rule->priority < best_priority)) {
                best = runtime->tag_rules[tag]; right = end; best_priority = rule->priority; best_byte = end_byte;
            }
        }
        if (scan.outcome != CETTA_PARSER_TOKEN_SCAN_V1_COMPLETED) break;
        if (best != UINT32_MAX && right == UINT32_MAX) {
            scan.outcome = CETTA_PARSER_TOKEN_SCAN_V1_BYTE_SPLIT;
            scan.failure_byte = best_byte;
            (void)snprintf(scan.detail, sizeof(scan.detail), "C/Flex token boundary is inside a Unicode scalar");
            break;
        }
        if (best == UINT32_MAX || right <= cursor) {
            scan.outcome = CETTA_PARSER_TOKEN_SCAN_V1_REJECTED;
            (void)snprintf(scan.detail, sizeof(scan.detail), "no lexical rule accepts the next source span");
            break;
        }
        const TokenRule *rule = &runtime->rules[best];
        uint32_t trailing = right;
        bool forced_character = false, character_has_token = true;
        uint32_t character_scalar = 0u;
        if (c_flex && rule->kind == TOKEN_RULE_REGULAR && builtin_rank(rule->value_kind) == 1u) {
            uint32_t right_byte, trailing_byte;
            c_character_end(bytes, (uint32_t)length, view->byte_offsets[cursor],
                &right_byte, &trailing_byte, &character_scalar, &character_has_token);
            right = scalar_boundary(view, right_byte);
            trailing = scalar_boundary(view, trailing_byte);
            if (right == UINT32_MAX || trailing == UINT32_MAX) {
                scan.outcome = CETTA_PARSER_TOKEN_SCAN_V1_BYTE_SPLIT;
                scan.failure_byte = right_byte;
                (void)snprintf(scan.detail, sizeof(scan.detail), "C/Flex character payload ends inside a Unicode scalar");
                break;
            }
            forced_character = character_has_token;
        }
        if (c_flex && rule->kind == TOKEN_RULE_BLOCK_COMMENT) {
            bool closed;
            right = comment_end(rule, view, right, &closed); trailing = right;
        }
        if (c_flex && !forced_character && character_has_token &&
            !c_quoted_end(view, rule, cursor, &right, &trailing)) {
            scan.outcome = CETTA_PARSER_TOKEN_SCAN_V1_REJECTED;
            (void)snprintf(scan.detail, sizeof(scan.detail), "unterminated C/Flex quoted literal");
            break;
        }
        Atom *value = !character_has_token || rule->kind == TOKEN_RULE_TRIVIA || rule->kind == TOKEN_RULE_LINE_COMMENT ||
            rule->kind == TOKEN_RULE_BLOCK_COMMENT
            ? atom_symbol(&scan.arena, "TokenLayoutV1")
            : token_value(&scan, rule, cursor, right, c_flex, forced_character,
                character_scalar, &right, error, error_size);
        if (!value) goto done;
        scan.witness_values[scan.witness_len] = value;
        scan.edges[edge_len++] = (CettaLpNativeUtf8LatticeEdge){
            .terminal_id = character_has_token ? rule->terminal_id : runtime->trivia_terminal,
            .scalar_left = cursor, .scalar_right = right,
            .byte_left = view->byte_offsets[cursor], .byte_right = view->byte_offsets[right],
            .value_kind = CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS, .value = scan.witness_len++,
        };
        if (trailing > right) {
            scan.witness_values[scan.witness_len] = atom_symbol(&scan.arena, "TokenLayoutV1");
            scan.edges[edge_len++] = (CettaLpNativeUtf8LatticeEdge){
                .terminal_id = runtime->trivia_terminal, .scalar_left = right, .scalar_right = trailing,
                .byte_left = view->byte_offsets[right], .byte_right = view->byte_offsets[trailing],
                .value_kind = CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_WITNESS, .value = scan.witness_len++,
            };
        }
        cursor = trailing;
        if (scan.work_used >= work_limit && cursor < view->scalar_len) {
            scan.outcome = CETTA_PARSER_TOKEN_SCAN_V1_WORK_LIMIT; break;
        }
    }
    if (scan.outcome == CETTA_PARSER_TOKEN_SCAN_V1_COMPLETED)
        scan.edges[edge_len++] = (CettaLpNativeUtf8LatticeEdge){
            .terminal_id = runtime->eof_terminal, .scalar_left = cursor, .scalar_right = cursor,
            .byte_left = view->byte_offsets[cursor], .byte_right = view->byte_offsets[cursor],
            .value_kind = CETTA_LP_NATIVE_UTF8_TERMINAL_VALUE_EOF,
        };
    else {
        scan.failure_scalar = cursor;
        if (scan.outcome != CETTA_PARSER_TOKEN_SCAN_V1_BYTE_SPLIT) scan.failure_byte = view->byte_offsets[cursor];
    }
    uint32_t edge = 0u;
    for (uint32_t p = 0u; p <= view->scalar_len; ++p) {
        scan.start_offsets[p] = edge;
        while (edge < edge_len && scan.edges[edge].scalar_left == p) ++edge;
    }
    scan.start_offsets[view->scalar_len + 1u] = edge_len;
    scan.lattice = (CettaLpNativeUtf8Lattice){
        .terminal_ids = scan.terminal_ids, .terminal_len = terminal_len,
        .edges = scan.edges, .edge_len = edge_len, .start_offsets = scan.start_offsets,
        .start_offset_len = capacity, .codepoints = view->codepoints, .byte_offsets = view->byte_offsets,
        .scalar_len = view->scalar_len, .input_byte_len = view->input_byte_len,
        .decoded_byte_len = view->decoded_byte_len, .source_pass_count = view->source_pass_count,
    };
    if (audit) {
        memcpy(scan.binding_sha256, runtime->binding, sizeof(scan.binding_sha256));
        if (!cetta_lp_native_utf8_lattice_validate(&scan.lattice, error, error_size)) goto done;
    }
    cetta_parser_token_scan_v1_free(out);
    *out = scan;
    memset(&scan, 0, sizeof(scan));
    ok = true;
done:
    free(matches); free(byte_matches);
    cetta_parser_token_scan_v1_free(&scan);
    return ok;
}

bool cetta_parser_token_runtime_v1_scan(
    const CettaParserTokenRuntimeV1 *runtime_value,
    const uint8_t *bytes, size_t length, uint64_t work_limit,
    CettaParserTokenScanV1 *out, char *error, size_t error_size) {
    return token_runtime_scan(runtime_value, bytes, length, work_limit,
                              true, out, error, error_size);
}

bool cetta_parser_token_runtime_v1_scan_values(
    const CettaParserTokenRuntimeV1 *runtime_value,
    const uint8_t *bytes, size_t length, uint64_t work_limit,
    CettaParserTokenScanV1 *out, char *error, size_t error_size) {
    return token_runtime_scan(runtime_value, bytes, length, work_limit,
                              false, out, error, error_size);
}

static bool token_runtime_parse_into(
    const CettaParserTokenRuntimeV1 *runtime_value,
    const CettaParserTokenScanV1 *scan, bool use_glr,
    uint32_t work_limit, uint32_t replay_depth, uint32_t result_limit,
    bool values, PPNativeV1Result *out, char *error, size_t error_size) {
    const TokenRuntime *runtime = runtime_value ? runtime_value->implementation : NULL;
    const CettaLpNativeGllPrepared *gll;
    const CettaLpNativeGlrPrepared *glr;
    if (!runtime || !scan || !out || !work_limit)
        return token_error(error, error_size, "unbound token parser");
    gll = runtime->gll;
    glr = runtime->glr;
    if (!values &&
        (memcmp(runtime->authority_binding, runtime->compiled->binding_sha256, 65u) ||
        memcmp(runtime->binding, scan->binding_sha256, 65u) ||
        !ppnative_v1_prepared_start_family_lookup(&runtime->prepared, runtime->binding,
            runtime->start_state_id, &gll, &glr, error, error_size)))
        return token_error(error, error_size, "unbound or changed token parser");
    /* The recognizers first decide completions and reductions by the next
     * non-layout terminal.  That keeps long lists linear and leaves an
     * accepted forest unchanged.  A rejection is recomputed without it, so
     * its farthest position and expected terminals are the complete
     * search's. */
    CettaLpNativeUtf8Lattice lattice = scan->lattice;
    lattice.layout_lookahead = true;
    lattice.layout_terminal_id = runtime->trivia_terminal;
    for (;;) {
        if (!(use_glr ? (values
                ? cetta_lp_native_glr_prepared_parse_owned_utf8_lattice_forest_from
                : cetta_lp_native_glr_prepared_parse_utf8_lattice_forest_from)(glr,
                &lattice, 0u, work_limit, &out->forest, error, error_size)
            : (values
                ? cetta_lp_native_gll_prepared_parse_owned_utf8_lattice_forest_from_complete
                : cetta_lp_native_gll_prepared_parse_utf8_lattice_forest_from_complete)(gll,
                &lattice, 0u, work_limit, &out->forest, error, error_size))) return false;
        if (!lattice.layout_lookahead || out->forest.root_len != 0u ||
            out->forest.outcome != CETTA_LP_NATIVE_UTF8_FOREST_COMPLETED) break;
        cetta_lp_native_utf8_forest_free(&out->forest);
        cetta_lp_native_utf8_forest_init(&out->forest);
        lattice.layout_lookahead = false;
    }
    /* An exhausted recognizer has no roots, which is not a rejection. */
    if (out->forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT) {
        out->outcome = PPNATIVE_V1_RECOGNIZER_LIMIT;
        out->accepted = false;
        (void)snprintf(out->detail, sizeof(out->detail), "%s work limit %u",
                       use_glr ? "GLR" : "GLL", work_limit);
        return true;
    }
    PPNativeV1ForestExtension extension = runtime->compiled->token_bindings.extension;
    extension.witness_values = scan->witness_values;
    extension.witness_len = scan->witness_len;
    if (!(values ? ppnative_v1_finish_values(out, &runtime->compiled->pack,
            &extension, replay_depth, result_limit, error, error_size)
        : ppnative_v1_finish_extended(out, &runtime->compiled->pack,
            runtime->compiled->start_state, &extension, replay_depth, result_limit, error, error_size))) return false;
    out->accepted = out->forest.root_len != 0u;
    return true;
}

static bool token_runtime_parse(
    const CettaParserTokenRuntimeV1 *runtime,
    const CettaParserTokenScanV1 *scan, bool use_glr,
    uint32_t work_limit, uint32_t replay_depth, uint32_t result_limit,
    bool values, PPNativeV1Result *out, char *error, size_t error_size) {
    PPNativeV1Result result;
    if (!out) return token_error(error, error_size, "missing parser result destination");
    ppnative_v1_result_init(&result);
    bool ok = token_runtime_parse_into(runtime, scan, use_glr, work_limit,
        replay_depth, result_limit, values, &result, error, error_size);
    if (ok) {
        ppnative_v1_result_free(out);
        *out = result;
        memset(&result, 0, sizeof(result));
    }
    ppnative_v1_result_free(&result);
    return ok;
}

bool cetta_parser_token_runtime_v1_parse(
    const CettaParserTokenRuntimeV1 *runtime_value,
    const CettaParserTokenScanV1 *scan, bool use_glr,
    uint32_t work_limit, uint32_t replay_depth, uint32_t result_limit,
    PPNativeV1Result *out, char *error, size_t error_size) {
    return token_runtime_parse(runtime_value, scan, use_glr, work_limit,
        replay_depth, result_limit, false, out, error, error_size);
}

bool cetta_parser_token_runtime_v1_parse_values(
    const CettaParserTokenRuntimeV1 *runtime_value,
    const CettaParserTokenScanV1 *scan, bool use_glr,
    uint32_t work_limit, uint32_t replay_depth, uint32_t result_limit,
    PPNativeV1Result *out, char *error, size_t error_size) {
    return token_runtime_parse(runtime_value, scan, use_glr, work_limit,
        replay_depth, result_limit, true, out, error, error_size);
}

bool cetta_parser_token_runtime_v1_build_predictive(
    const CettaParserTokenRuntimeV1 *value,
    CettaLpNativePredictiveProgram *out, char *error, size_t error_size) {
    const TokenRuntime *runtime = value ? value->implementation : NULL;
    if (!runtime) return token_error(error, error_size, "missing token runtime for specialization");
    return cetta_lp_native_predictive_build(runtime->gll, NULL, 0u, true,
        runtime->eof_terminal, out, error, error_size);
}

bool cetta_parser_token_runtime_v1_check_predictive(
    const CettaParserTokenRuntimeV1 *value, const CettaLpNativePredictiveProgram *program,
    char *error, size_t error_size) {
    const TokenRuntime *runtime = value ? value->implementation : NULL;
    return runtime && program && program->token_chain && program->eof_terminal_id == runtime->eof_terminal &&
        cetta_lp_native_predictive_check(runtime->gll, program, NULL, 0u, error, error_size);
}

bool cetta_parser_token_runtime_v1_parse_predictive_values(
    const CettaParserTokenRuntimeV1 *value, const CettaLpNativePredictiveProgram *program,
    const CettaParserTokenScanV1 *scan, uint32_t work_limit, uint32_t replay_depth,
    uint32_t result_limit, PPNativeV1Result *out, char *error, size_t error_size) {
    const TokenRuntime *runtime = value ? value->implementation : NULL;
    PPNativeV1Result result;
    ppnative_v1_result_init(&result);
    if (!runtime || !scan || !out)
        return token_error(error, error_size, "missing predictive token input binding");
    bool ok = cetta_lp_native_predictive_parse_owned_lattice_forest(runtime->gll, program,
        &scan->lattice, work_limit, &result.forest, error, error_size);
    if (ok && result.forest.outcome == CETTA_LP_NATIVE_UTF8_FOREST_RESOURCE_LIMIT) {
        result.outcome = PPNATIVE_V1_RECOGNIZER_LIMIT;
        (void)snprintf(result.detail, sizeof(result.detail), "predictive work limit %u", work_limit);
    } else if (ok) {
        result.accepted = result.forest.root_len != 0u;
        PPNativeV1ForestExtension extension = runtime->compiled->token_bindings.extension;
        extension.witness_values = scan->witness_values; extension.witness_len = scan->witness_len;
        ok = ppnative_v1_finish_values(&result, &runtime->compiled->pack, &extension,
            replay_depth, result_limit, error, error_size);
    }
    if (ok) {
        ppnative_v1_result_free(out); *out = result; memset(&result, 0, sizeof(result));
    }
    ppnative_v1_result_free(&result);
    return ok;
}

bool cetta_parser_token_runtime_v1_materialize(
    const CettaParserTokenRuntimeV1 *runtime_value,
    const CettaParserTokenScanV1 *scan, PPNativeV1Result *result,
    uint32_t item_limit, char *error, size_t error_size) {
    const TokenRuntime *runtime = runtime_value ? runtime_value->implementation : NULL;
    if (!runtime || !scan || !result || memcmp(runtime->binding, scan->binding_sha256, 65u))
        return token_error(error, error_size, "unbound token forest");
    PPNativeV1ForestExtension extension = runtime->compiled->token_bindings.extension;
    extension.witness_values = scan->witness_values;
    extension.witness_len = scan->witness_len;
    return ppnative_v1_materialize_canonical_forest_extended(result, &runtime->compiled->pack,
        runtime->compiled->start_state, &extension, item_limit, error, error_size);
}
