#include "parser_token_profile_v1.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool token_error(char *error, size_t size, const char *message) {
    if (error && size) (void)snprintf(error, size, "%s", message);
    return false;
}

static bool token_form(const Atom *value, const char *name, uint32_t arity) {
    return value && value->kind == ATOM_EXPR && value->expr.len == arity + 1u &&
        atom_is_symbol(value->expr.elems[0], name);
}

static bool token_string(const Atom *value) {
    return value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_STRING &&
        value->ground.sval && value->ground.sval[0];
}

static bool token_nonempty_text(Atom *value) {
    if (token_string(value)) return true;
    if (!token_form(value, "bnf-v1:text-cons", 2u)) return false;
    for (; token_form(value, "bnf-v1:text-cons", 2u); value = value->expr.elems[2]) {
        Atom *cp = value->expr.elems[1];
        if (!cp || cp->kind != ATOM_GROUNDED || cp->ground.gkind != GV_INT ||
            cp->ground.ival < 0 || cp->ground.ival > 0x10ffff ||
            (cp->ground.ival >= 0xd800 && cp->ground.ival <= 0xdfff)) return false;
    }
    return token_form(value, "bnf-v1:text-nil", 0u);
}

static bool token_origin(const Atom *value) {
    if (!token_form(value, "SyntaxOriginV1", 2u) || !token_string(value->expr.elems[1]))
        return false;
    Atom *site = value->expr.elems[2];
    return token_string(site) || (site && site->kind == ATOM_GROUNDED &&
        site->ground.gkind == GV_INT && site->ground.ival >= 0);
}

static Atom *token_value(Arena *arena, const CettaOpLangV1SExpr *node,
                         uint32_t *remaining, uint32_t depth,
                         char *error, size_t size) {
    if (!node || !*remaining || depth > 512u) {
        token_error(error, size, "regular-token profile structural work limit exceeded");
        return NULL;
    }
    --*remaining;
    switch (node->kind) {
    case CETTA_OP_LANG_V1_SEXPR_SYMBOL:
        if (node->as.symbol) return atom_symbol(arena, node->as.symbol);
        break;
    case CETTA_OP_LANG_V1_SEXPR_STRING: {
        const uint8_t *bytes = node->as.string.bytes;
        uint32_t len = node->as.string.len;
        if ((len && !bytes) || (len && memchr(bytes, 0, len))) {
            token_error(error, size, "token-profile strings require the NUL-free Atom string domain");
            return NULL;
        }
        char *copy = arena_alloc(arena, (size_t)len + 1u);
        if (!copy) break;
        if (len) memcpy(copy, bytes, len);
        copy[len] = '\0';
        return atom_string(arena, copy);
    }
    case CETTA_OP_LANG_V1_SEXPR_NATURAL: {
        char *end = NULL;
        if (node->as.natural) {
            for (const char *p = node->as.natural; *p; ++p) {
                if (*p < '0' || *p > '9') {
                    token_error(error, size, "malformed token-profile natural");
                    return NULL;
                }
            }
        }
        errno = 0;
        unsigned long long value = node->as.natural
            ? strtoull(node->as.natural, &end, 10) : ULLONG_MAX;
        if (!node->as.natural || !node->as.natural[0] || !end || *end ||
            errno == ERANGE || value > INT64_MAX) {
            token_error(error, size, "token-profile natural is outside the Atom integer domain");
            return NULL;
        }
        return atom_int(arena, (int64_t)value);
    }
    case CETTA_OP_LANG_V1_SEXPR_APPLICATION: {
        uint32_t len = node->as.application.argument_len;
        if (!node->as.application.head || (len && !node->as.application.arguments) ||
            len > *remaining || (size_t)len + 1u > SIZE_MAX / sizeof(Atom *)) break;
        Atom **items = arena_alloc(arena, ((size_t)len + 1u) * sizeof(*items));
        if (!items) break;
        items[0] = atom_symbol(arena, node->as.application.head);
        for (uint32_t i = 0u; i < len; ++i) {
            items[i + 1u] = token_value(arena, node->as.application.arguments[i],
                remaining, depth + 1u, error, size);
            if (!items[i + 1u]) return NULL;
        }
        return atom_expr(arena, items, (size_t)len + 1u);
    }
    }
    token_error(error, size, "malformed token-profile structural value");
    return NULL;
}

static bool token_list_len(Atom *list, uint32_t *len) {
    *len = 0u;
    while (token_form(list, "LCons", 2u)) {
        if (*len == UINT32_MAX) return false;
        ++*len;
        list = list->expr.elems[2];
    }
    return list && atom_is_symbol(list, "LNil");
}

static bool token_kind(Atom *value) {
    static const char *const kinds[] = {
        "TokenTextV1", "TokenIdentV1", "TokenIntegerV1", "TokenDoubleV1",
        "TokenStringV1", "TokenCharV1",
    };
    if (token_form(value, "TokenPositionV1", 1u)) value = value->expr.elems[1];
    for (size_t i = 0u; i < sizeof(kinds) / sizeof(*kinds); ++i)
        if (atom_is_symbol(value, kinds[i])) return true;
    return false;
}

static bool token_projection(Atom *value) {
    if (token_form(value, "BnfcConstructorV1", 3u)) {
        uint32_t len;
        if (!token_string(value->expr.elems[1]) || !token_string(value->expr.elems[2]) ||
            !token_list_len(value->expr.elems[3], &len)) return false;
        for (Atom *p = value->expr.elems[3]; token_form(p, "LCons", 2u); p = p->expr.elems[2])
            if (!token_string(p->expr.elems[1])) return false;
        return true;
    }
    return (token_form(value, "BnfcCoercionV1", 1u) ||
            token_form(value, "BnfcListEmptyV1", 1u) ||
            token_form(value, "BnfcListConsV1", 1u) ||
            token_form(value, "BnfcListSingletonV1", 1u)) &&
        token_string(value->expr.elems[1]);
}

void cetta_parser_token_profile_v1_init(CettaParserTokenProfileV1 *profile) {
    if (!profile) return;
    memset(profile, 0, sizeof(*profile));
    arena_init(&profile->arena);
}

void cetta_parser_token_profile_v1_free(CettaParserTokenProfileV1 *profile) {
    if (!profile) return;
    arena_free(&profile->arena);
    memset(profile, 0, sizeof(*profile));
}

bool cetta_parser_token_profile_v1_decode(
    CettaParserTokenProfileV1 *out, const CettaOpLangV1SExpr *root,
    uint32_t work_limit, char *error, size_t error_size) {
    CettaParserTokenProfileV1 candidate;
    bool ok = false;
    uint32_t remaining = work_limit;
    cetta_parser_token_profile_v1_init(&candidate);
    if (error && error_size) error[0] = '\0';
    if (!out || !root || !work_limit) {
        token_error(error, error_size, "bad regular-token profile decode arguments");
        goto done;
    }
    candidate.document = token_value(&candidate.arena, root, &remaining, 0u,
                                      error, error_size);
    if (!candidate.document) goto done;
    if (!token_form(candidate.document, "ParserTokenLayerV1", 5u)) {
        token_error(error, error_size, "expected ParserTokenLayerV1");
        goto done;
    }
    candidate.policy = candidate.document->expr.elems[1];
    candidate.declarations = candidate.document->expr.elems[2];
    candidate.comments = candidate.document->expr.elems[3];
    candidate.productions = candidate.document->expr.elems[4];
    candidate.entrypoints = candidate.document->expr.elems[5];
    if (!token_form(candidate.policy, "SyntaxLexicalPolicyV1", 1u)) {
        token_error(error, error_size, "expected a named lexical-policy value");
        goto done;
    }
    Atom *policy = candidate.policy->expr.elems[1];
    if (atom_is_symbol(policy, "bnfc-documented")) {
        candidate.policy_kind = CETTA_BNFC_DOCUMENTED_V1;
    } else if (atom_is_symbol(policy, "bnfc-2.9.6.3-c-flex-2.6.4")) {
        candidate.policy_kind = CETTA_BNFC_C_FLEX_2_9_6_3_V1;
    } else {
        token_error(error, error_size, "unsupported named lexical policy");
        goto done;
    }
    if (!token_list_len(candidate.declarations, &candidate.declaration_len) ||
        !token_list_len(candidate.comments, &candidate.comment_len) ||
        !token_list_len(candidate.productions, &candidate.production_len) ||
        !token_list_len(candidate.entrypoints, &candidate.entrypoint_len)) {
        token_error(error, error_size, "token-profile fields must be finite LCons/LNil lists");
        goto done;
    }
    for (Atom *p = candidate.declarations; token_form(p, "LCons", 2u); p = p->expr.elems[2]) {
        Atom *row = p->expr.elems[1];
        bool regular = token_form(row, "RegularTokenDeclV1", 5u);
        bool trivia = token_form(row, "RegularTriviaDeclV1", 3u);
        if ((!regular && !trivia) || (regular &&
                (!token_string(row->expr.elems[1]) || !token_kind(row->expr.elems[4]))) ||
            !token_origin(row->expr.elems[regular ? 5u : 3u])) {
            token_error(error, error_size, "malformed origin-bearing regular-token declaration");
            goto done;
        }
    }
    for (Atom *p = candidate.comments; token_form(p, "LCons", 2u); p = p->expr.elems[2]) {
        Atom *row = p->expr.elems[1];
        bool line = token_form(row, "ParserLineCommentV1", 2u);
        bool block = token_form(row, "ParserBlockCommentV1", 3u);
        if ((!line && !block) || !token_nonempty_text(row->expr.elems[1]) ||
            (block && !token_nonempty_text(row->expr.elems[2])) ||
            !token_origin(row->expr.elems[line ? 2u : 3u])) {
            token_error(error, error_size, "malformed origin-bearing comment declaration");
            goto done;
        }
    }
    for (Atom *p = candidate.productions; token_form(p, "LCons", 2u); p = p->expr.elems[2]) {
        Atom *row = p->expr.elems[1];
        if (!token_form(row, "ParserProductionMetadataV1", 3u) ||
            (!atom_is_symbol(row->expr.elems[2], "ParserVisibleV1") &&
             !atom_is_symbol(row->expr.elems[2], "ParserInternalV1")) ||
            !token_projection(row->expr.elems[3])) {
            token_error(error, error_size, "malformed production occurrence or AST projection");
            goto done;
        }
    }
    for (Atom *p = candidate.entrypoints; token_form(p, "LCons", 2u); p = p->expr.elems[2]) {
        if (!token_string(p->expr.elems[1])) {
            token_error(error, error_size, "entrypoint category must be a nonempty string");
            goto done;
        }
    }
    cetta_parser_token_profile_v1_free(out);
    *out = candidate;
    memset(&candidate, 0, sizeof(candidate));
    ok = true;
done:
    cetta_parser_token_profile_v1_free(&candidate);
    return ok;
}

static void category_parts(const CettaLdTextV1 *text, uint32_t *right, uint32_t *brackets) {
    *right = text->len; *brackets = 0u;
    while (*right && text->bytes[*right - 1u] == ']') { --*right; ++*brackets; }
    while (*right && text->bytes[*right - 1u] >= '0' && text->bytes[*right - 1u] <= '9') --*right;
}

Atom *cetta_parser_token_category_v1_normalize(Arena *arena, const CettaLdTextV1 *text) {
    if (!arena || !text || (text->len && (!text->bytes || memchr(text->bytes, 0, text->len)))) return NULL;
    uint32_t right, brackets;
    category_parts(text, &right, &brackets);
    char *name = arena_alloc(arena, (size_t)right + brackets + 1u);
    if (!name) return NULL;
    if (right) memcpy(name, text->bytes, right);
    for (uint32_t i = 0u; i < brackets; ++i) name[right + i] = ']';
    name[right + brackets] = '\0';
    return atom_string(arena, name);
}

static bool category_equal(const CettaLdTextV1 *text, const char *name,
                            bool normalized) {
    if (!text || !name || (text->len && !text->bytes)) return false;
    uint32_t right = text->len, brackets = 0u;
    if (normalized) category_parts(text, &right, &brackets);
    if (strlen(name) != (size_t)right + brackets ||
        (right && memcmp(text->bytes, name, right))) return false;
    for (uint32_t i = 0u; i < brackets; ++i)
        if (name[right + i] != ']') return false;
    return true;
}

static bool profile_type_exists(const CettaLanguageDefCoreV1 *language, const char *name) {
    for (uint32_t i = 0u; i < language->type_len; ++i)
        if (category_equal(&language->types[i].name, name, false)) return true;
    return false;
}

bool cetta_parser_token_profile_v1_validate_language(
    const CettaParserTokenProfileV1 *profile,
    const CettaLanguageDefCoreV1 *language, char *error, size_t error_size) {
    if (!profile || !language || profile->production_len != language->term_len)
        return token_error(error, error_size, "one metadata row is required for each production occurrence");
    Atom *cursor = profile->productions;
    for (uint32_t i = 0u; i < language->term_len; ++i, cursor = cursor->expr.elems[2]) {
        if (!token_form(cursor, "LCons", 2u) ||
            !token_form(cursor->expr.elems[1], "ParserProductionMetadataV1", 3u) ||
            !token_projection(cursor->expr.elems[1]->expr.elems[3]))
            return token_error(error, error_size, "malformed production metadata during signature admission");
        Atom *metadata = cursor->expr.elems[1], *projection = metadata->expr.elems[3];
        const CettaLdGrammarRuleV1 *rule = &language->terms[i];
        Atom *previous = profile->productions;
        for (uint32_t j = 0u; j < i; ++j, previous = previous->expr.elems[2]) {
            Atom *other = previous->expr.elems[1];
            if (atom_eq(metadata->expr.elems[1], other->expr.elems[1]))
                return token_error(error, error_size, "duplicate production occurrence identity");
            Atom *other_projection = other->expr.elems[3];
            if (token_form(projection, "BnfcConstructorV1", 3u) &&
                token_form(other_projection, "BnfcConstructorV1", 3u) &&
                atom_eq(projection->expr.elems[1], other_projection->expr.elems[1]) &&
                !atom_eq(projection, other_projection))
                return token_error(error, error_size, "incompatible constructor category skeletons");
        }
        if (token_form(projection, "BnfcConstructorV1", 3u)) {
            uint32_t arity;
            Atom *params = projection->expr.elems[3];
            if (category_equal(&rule->label, "_", false) || category_equal(&rule->label, "[]", false) ||
                category_equal(&rule->label, "(:)", false) || category_equal(&rule->label, "(:[])", false) ||
                !category_equal(&rule->label, projection->expr.elems[1]->ground.sval, false) ||
                !category_equal(&rule->category, projection->expr.elems[2]->ground.sval, true) ||
                !token_list_len(params, &arity) || arity != rule->param_len)
                return token_error(error, error_size, "constructor projection disagrees with its LanguageDef signature");
            for (uint32_t j = 0u; j < rule->param_len; ++j, params = params->expr.elems[2]) {
                if (rule->params[j].kind != CETTA_LD_PARAM_SIMPLE_V1 ||
                    rule->params[j].type.kind != CETTA_LD_TYPE_BASE_V1 ||
                    !category_equal(&rule->params[j].type.as.base,
                        params->expr.elems[1]->ground.sval, true))
                    return token_error(error, error_size, "constructor parameter skeleton disagrees with its LanguageDef type");
            }
        } else {
            const char *category = projection->expr.elems[1]->ground.sval;
            if (!category_equal(&rule->category, category, true))
                return token_error(error, error_size, "special label result category disagrees with its projection");
            if (token_form(projection, "BnfcCoercionV1", 1u)) {
                if (!category_equal(&rule->label, "_", false) || rule->param_len != 1u ||
                    rule->params[0].type.kind != CETTA_LD_TYPE_BASE_V1 ||
                    !category_equal(&rule->params[0].type.as.base, category, true))
                    return token_error(error, error_size, "a coercion must preserve one normalized category");
            } else {
                size_t len = strlen(category);
                const char *label = token_form(projection, "BnfcListEmptyV1", 1u) ? "[]"
                    : token_form(projection, "BnfcListConsV1", 1u) ? "(:)" : "(:[])";
                uint32_t arity = !strcmp(label, "[]") ? 0u : !strcmp(label, "(:)") ? 2u : 1u;
                if (len < 3u || category[0] != '[' || category[len - 1u] != ']' ||
                    !category_equal(&rule->label, label, false) || rule->param_len != arity)
                    return token_error(error, error_size, "invalid list-label category skeleton");
                if (arity) {
                    char *element = malloc(len - 1u);
                    if (!element) return token_error(error, error_size, "list category allocation failed");
                    memcpy(element, category + 1u, len - 2u);
                    element[len - 2u] = '\0';
                    bool matches = rule->params[0].type.kind == CETTA_LD_TYPE_BASE_V1 &&
                        category_equal(&rule->params[0].type.as.base, element, true);
                    free(element);
                    if (!matches || (arity == 2u &&
                            (rule->params[1].type.kind != CETTA_LD_TYPE_BASE_V1 ||
                             !category_equal(&rule->params[1].type.as.base, category, true))))
                        return token_error(error, error_size, "list constructor parameter skeleton is inconsistent");
                }
            }
        }
    }
    for (Atom *p = profile->declarations; token_form(p, "LCons", 2u); p = p->expr.elems[2]) {
        Atom *row = p->expr.elems[1];
        if (token_form(row, "RegularTokenDeclV1", 5u) &&
            !profile_type_exists(language, row->expr.elems[1]->ground.sval))
            return token_error(error, error_size, "regular token references an undeclared category");
    }
    for (Atom *p = profile->entrypoints; token_form(p, "LCons", 2u); p = p->expr.elems[2])
        if (!profile_type_exists(language, p->expr.elems[1]->ground.sval))
            return token_error(error, error_size, "entrypoint references an undeclared category");
    return true;
}
