#include "syntax_gslt_v1.h"

#include <stdlib.h>
#include <string.h>

#define SYNTAX_GSLT_DECLARATION_LIMIT_V1 200000u

typedef struct {
    Atom *language;
    Atom *profile;
    Atom *policy;
    Atom *origins;
    CettaGsltOriginEntryV1 *entries;
    size_t len;
} SyntaxViewV1;

static bool head_is(Atom *value, const char *name, CettaExprLen arity) {
    return value && value->kind == ATOM_EXPR && value->expr.len == arity + 1u &&
        value->expr.elems[0] && value->expr.elems[0]->kind == ATOM_SYMBOL &&
        !strcmp(atom_name_cstr(value->expr.elems[0]), name);
}

static bool symbol_is(Atom *value, const char *name) {
    return value && value->kind == ATOM_SYMBOL &&
        !strcmp(atom_name_cstr(value), name);
}

static bool nonempty_string(Atom *value) {
    return value && value->kind == ATOM_GROUNDED &&
        value->ground.gkind == GV_STRING && value->ground.sval &&
        value->ground.sval[0] != '\0';
}

static Atom *form(Arena *arena, const char *name, Atom **arguments, size_t len) {
    Atom **items = arena_alloc(arena, (len + 1u) * sizeof(*items));
    items[0] = atom_symbol(arena, name);
    for (size_t i = 0u; i < len; ++i) items[i + 1u] = arguments[i];
    return atom_expr(arena, items, len + 1u);
}

static Atom *refused(Arena *arena, const char *reason, Atom *witness) {
    Atom *arguments[] = {atom_symbol(arena, reason), witness};
    return atom_deep_copy(arena, form(arena, "SyntaxGSLTRefusedV1", arguments, 2u));
}

static Atom *message(Arena *arena, const char *reason, const char *detail) {
    Atom *argument = atom_string(arena, detail);
    return refused(arena, reason, form(arena, "SyntaxDiagnosticV1", &argument, 1u));
}

static bool origin_valid(Atom *origin) {
    if (!head_is(origin, "SyntaxOriginV1", 2u) ||
        !nonempty_string(origin->expr.elems[1])) return false;
    Atom *site = origin->expr.elems[2];
    return nonempty_string(site) ||
        (site && site->kind == ATOM_GROUNDED &&
         site->ground.gkind == GV_INT && site->ground.ival >= 0);
}

static const char *group_name(unsigned group) {
    static const char *const names[] = {"category", "production", "lexical-class", "production"};
    return names[group];
}

static Atom *declaration_label(Atom *body, unsigned group) {
    if (group == 0u && head_is(body, "TypeDecl", 2u)) return body->expr.elems[1];
    if (group == 1u && head_is(body, "GrammarRule", 5u)) return body->expr.elems[1];
    if (group == 2u &&
        (head_is(body, "LexicalClassPoints", 2u) ||
         head_is(body, "LexicalClassExcept", 2u))) return body->expr.elems[1];
    if (group == 3u && head_is(body, "LexicalState", 3u)) return body->expr.elems[3];
    return NULL;
}

static int declaration_group(Atom *body) {
    if (head_is(body, "TypeDecl", 2u)) return 0;
    if (head_is(body, "GrammarRule", 5u)) return 1;
    if (head_is(body, "LexicalClassPoints", 2u) ||
        head_is(body, "LexicalClassExcept", 2u)) return 2;
    if (head_is(body, "LexicalState", 3u)) return 3;
    return -1;
}

static Atom *group_list(SyntaxViewV1 *view, unsigned group) {
    return group < 2u ? view->language->expr.elems[group + 2u]
                     : view->profile->expr.elems[group + 1u];
}

static bool unpack(Atom *value, SyntaxViewV1 *view) {
    if (!head_is(value, "SyntaxGSLTAdmittedV1", 1u)) return false;
    value = value->expr.elems[1];
    if (!head_is(value, "SyntaxGSLTV1", 4u)) return false;
    view->language = value->expr.elems[1];
    view->profile = value->expr.elems[2];
    view->policy = value->expr.elems[3];
    view->origins = value->expr.elems[4];
    return true;
}

static Atom *validate(Arena *arena, SyntaxViewV1 *view,
                       CettaSyntaxGsltAdmissionV1 admit) {
    char error[512] = {0};
    size_t count = 0u;
    if (!head_is(view->language, "GSLTLanguageDefWireV1", 5u) ||
        !head_is(view->profile, "GSLTParserProfileLayerV1", 4u) ||
        !head_is(view->origins, "SyntaxOriginsV1", 4u))
        return message(arena, "malformed-envelope", "expected LanguageDef, parser profile and four origin lists");
    if (!head_is(view->policy, "SyntaxLexicalPolicyV1", 1u) ||
        !symbol_is(view->policy->expr.elems[1], "ScalarScannerlessAllV1"))
        return message(arena, "unsupported-policy", "the admitted fragment retains all scannerless scalar parses");
    if (!admit || !admit(view->language, view->profile, error, sizeof(error)))
        return message(arena, "grammar-admission", error[0] ? error : "LanguageDef/profile admission failed");
    /* The compiler has already bounded and admitted every declaration body.
     * Origin annotations must correspond one for one to those occurrences. */
    for (unsigned group = 0u; group < 4u; ++group) {
        Atom *rows = group_list(view, group);
        Atom *origins = view->origins->expr.elems[group + 1u];
        while (head_is(rows, "LCons", 2u) && head_is(origins, "LCons", 2u)) {
            if (count++ >= SYNTAX_GSLT_DECLARATION_LIMIT_V1)
                return message(arena, "resource-limit", "too many annotated declarations");
            if (!origin_valid(origins->expr.elems[1]))
                return message(arena, "invalid-origin", "origins require a source namespace and a declaration-site identity");
            if (!nonempty_string(declaration_label(rows->expr.elems[1], group)))
                return message(arena, "malformed-declaration", "declaration label is absent");
            rows = rows->expr.elems[2];
            origins = origins->expr.elems[2];
        }
        if (!symbol_is(rows, "LNil") || !symbol_is(origins, "LNil"))
            return message(arena, "origin-occurrence-mismatch", "each declaration needs exactly one origin, in its field order");
    }
    if (count > SIZE_MAX / sizeof(*view->entries))
        return message(arena, "resource-limit", "declaration index is too large");
    view->entries = count ? malloc(count * sizeof(*view->entries)) : NULL;
    if (count && !view->entries)
        return message(arena, "allocation-failure", "cannot index syntax declarations");
    for (unsigned group = 0u; group < 4u; ++group) {
        Atom *rows = group_list(view, group);
        Atom *origins = view->origins->expr.elems[group + 1u];
        while (head_is(rows, "LCons", 2u)) {
            Atom *body = rows->expr.elems[1];
            Atom *name[] = {atom_symbol(arena, group_name(group)), declaration_label(body, group)};
            view->entries[view->len++] = (CettaGsltOriginEntryV1){
                origins->expr.elems[1], form(arena, "SyntaxNameV1", name, 2u), body};
            rows = rows->expr.elems[2];
            origins = origins->expr.elems[2];
        }
    }
    return NULL;
}

static Atom *entry_witness(Arena *arena, const CettaGsltOriginEntryV1 *entry,
                            size_t position) {
    Atom *arguments[] = {atom_int(arena, (int64_t)position), entry->origin,
                         entry->label, entry->body};
    return form(arena, "SyntaxDeclarationOccurrenceV1", arguments, 4u);
}

static Atom *join_failure(Arena *arena, SyntaxViewV1 *first, SyntaxViewV1 *second,
                           CettaGsltOriginJoinWitnessV1 witness) {
    if (witness.status == CETTA_GSLT_ORIGIN_JOIN_LABEL_CONFLICT_V1 ||
        witness.status == CETTA_GSLT_ORIGIN_JOIN_ORIGIN_CONFLICT_V1) {
        const CettaGsltOriginEntryV1 *a = witness.first < first->len
            ? &first->entries[witness.first]
            : &second->entries[witness.first - first->len];
        const CettaGsltOriginEntryV1 *b = witness.second < first->len
            ? &first->entries[witness.second]
            : &second->entries[witness.second - first->len];
        Atom *arguments[] = {entry_witness(arena, a, witness.first),
                             entry_witness(arena, b, witness.second)};
        return refused(arena,
            witness.status == CETTA_GSLT_ORIGIN_JOIN_LABEL_CONFLICT_V1
                ? "label-conflict" : "origin-conflict",
            form(arena, "SyntaxConflictWitnessV1", arguments, 2u));
    }
    return message(arena,
        witness.status == CETTA_GSLT_ORIGIN_JOIN_ALLOCATION_FAILURE_V1
            ? "allocation-failure" : "resource-limit",
        "cannot construct checked declaration union");
}

static Atom *admitted(Arena *arena, SyntaxViewV1 *view) {
    Atom *arguments[] = {view->language, view->profile, view->policy, view->origins};
    Atom *value = form(arena, "SyntaxGSLTV1", arguments, 4u);
    return atom_deep_copy(arena, form(arena, "SyntaxGSLTAdmittedV1", &value, 1u));
}

Atom *cetta_syntax_gslt_admit_v1(
    Arena *arena, Atom *language, Atom *profile, Atom *policy, Atom *origins,
    CettaSyntaxGsltAdmissionV1 admit) {
    SyntaxViewV1 view = {language, profile, policy, origins, NULL, 0u};
    SyntaxViewV1 empty = {0};
    Atom *failure = validate(arena, &view, admit);
    CettaGsltOriginEntryV1 *checked = NULL;
    size_t checked_len;
    CettaGsltOriginJoinWitnessV1 witness;
    if (!failure && !cetta_gslt_origin_join_v1(view.entries, view.len, NULL, 0u,
            CETTA_GSLT_ORIGIN_VIEW_V1, &checked, &checked_len, &witness))
        failure = join_failure(arena, &view, &empty, witness);
    Atom *result = failure ? failure : admitted(arena, &view);
    free(checked);
    free(view.entries);
    return result;
}

Atom *cetta_syntax_gslt_inspect_v1(
    Arena *arena, Atom *value, CettaSyntaxGsltAdmissionV1 admit) {
    SyntaxViewV1 view = {0};
    if (!unpack(value, &view))
        return message(arena, "unadmitted-value", "expected an admitted syntax GSLT envelope");
    Atom *checked = cetta_syntax_gslt_admit_v1(arena, view.language, view.profile,
                                              view.policy, view.origins, admit);
    if (!head_is(checked, "SyntaxGSLTAdmittedV1", 1u)) return checked;
    Atom *arguments[] = {view.language, view.profile, view.policy, view.origins};
    return atom_deep_copy(arena, form(arena, "SyntaxGSLTInspectionV1", arguments, 4u));
}

Atom *cetta_syntax_gslt_join_v1(
    Arena *arena, Atom *name, Atom *start,
    Atom *first_value, Atom *second_value, CettaGsltOriginPolicyV1 origin_policy,
    CettaSyntaxGsltAdmissionV1 admit) {
    SyntaxViewV1 first = {0}, second = {0};
    CettaGsltOriginEntryV1 *joined = NULL;
    size_t joined_len = 0u;
    CettaGsltOriginJoinWitnessV1 witness;
    Atom *result = NULL;
    if (!nonempty_string(name) || !nonempty_string(start))
        return message(arena, "bad-argument", "join requires an explicit name and start category");
    if (!unpack(first_value, &first) || !unpack(second_value, &second))
        return message(arena, "unadmitted-value", "join operands must be admitted syntax GSLT envelopes");
    if (!atom_eq(first.policy, second.policy)) {
        Atom *arguments[] = {first.policy, second.policy};
        return refused(arena, "policy-mismatch", form(arena, "SyntaxPolicyMismatchV1", arguments, 2u));
    }
    result = validate(arena, &first, admit);
    if (result) goto done;
    result = validate(arena, &second, admit);
    if (result) goto done;
    if (!cetta_gslt_origin_join_v1(first.entries, first.len, second.entries, second.len,
            origin_policy, &joined, &joined_len, &witness)) {
        result = join_failure(arena, &first, &second, witness);
        goto done;
    }
    /* LanguageDef/profile have separate ordered fields, not a global source
     * chronology. Each field retains its stable-first union subsequence. The
     * full declaration set is exactly the checked module-algebra union. */
    Atom *rows[4], *origins[4];
    for (unsigned group = 0u; group < 4u; ++group)
        rows[group] = origins[group] = atom_symbol(arena, "LNil");
    for (size_t i = joined_len; i > 0u; --i) {
        CettaGsltOriginEntryV1 entry = joined[i - 1u];
        int group = declaration_group(entry.body);
        rows[group] = atom_expr3(arena, atom_symbol(arena, "LCons"), entry.body, rows[group]);
        origins[group] = atom_expr3(arena, atom_symbol(arena, "LCons"), entry.origin, origins[group]);
    }
    Atom *language_args[] = {name, rows[0], rows[1], atom_symbol(arena, "LNil"), atom_symbol(arena, "LNil")};
    Atom *profile_args[] = {name, start, rows[2], rows[3]};
    SyntaxViewV1 merged = {
        .language = form(arena, "GSLTLanguageDefWireV1", language_args, 5u),
        .profile = form(arena, "GSLTParserProfileLayerV1", profile_args, 4u),
        .policy = first.policy,
        .origins = form(arena, "SyntaxOriginsV1", origins, 4u),
    };
    result = cetta_syntax_gslt_admit_v1(arena, merged.language, merged.profile,
                                      merged.policy, merged.origins, admit);
done:
    free(joined);
    free(first.entries);
    free(second.entries);
    return result;
}

bool cetta_syntax_gslt_data_arg_v1(const char *operation, uint32_t index) {
    /* Consulted for every argument check of a grounded call: one shared
     * prefix test first, then short suffixes. */
    static const char library[] = "__cetta_lib_", syntax[] = "syntax_gslt_";
    if (!operation || strncmp(operation, library, sizeof library - 1u)) return false;
    operation += sizeof library - 1u;
    if (!strcmp(operation, "authored_parser_load_values")) return index < 2u;
    if (strncmp(operation, syntax, sizeof syntax - 1u)) return false;
    operation += sizeof syntax - 1u;
    if (!strcmp(operation, "admit_v1")) return index < 4u;
    if (!strcmp(operation, "inspect_v1")) return index == 0u;
    if (!strcmp(operation, "join_v1") || !strcmp(operation, "join_coherent_v1"))
        return index < 4u;
    return false;
}
