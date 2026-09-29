/* Check an interpretation of canonical terms against the grammar that
 * derives them.  The interpretation is a deterministic equation presentation
 * whose function HEAD takes a policy and a canonical value.  It must have
 * exactly one equation for each form a canonical value takes: each
 * constructor with its arity and each literal text by one policy-generic
 * equation, and each token by one equation per policy (or one generic one).
 * No equation of HEAD may interpret anything else, and every application in
 * every equation must be declared by the presentation's signature. */
#include "native/deterministic_equation_plan_v1.h"
#include "native/grammar_canonical_term_v1.h"
#include "native/langdef_module.h"
#include "native/tptp_official_snapshot_v1.h"
#include "parser.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    CettaGrammarCanonicalFormKindV1 kind;
    const char *name;
    uint32_t arity;
    const char *policy; /* NULL: generic */
    const char *rule;
} Case;

typedef struct {
    Case *items;
    size_t len;
    size_t cap;
} Cases;

static unsigned long g_problems;

static void problem(const char *format, const char *a, const char *b, unsigned long n) {
    printf("(CanonicalMorphismGapV1 ");
    printf(format, a, b, n);
    printf(")\n");
    g_problems++;
}

static bool push_case(Cases *cases, Case item) {
    if (cases->len == cases->cap) {
        size_t next = cases->cap ? cases->cap * 2u : 64u;
        Case *grown = realloc(cases->items, next * sizeof(*grown));
        if (!grown)
            return false;
        cases->items = grown;
        cases->cap = next;
    }
    cases->items[cases->len++] = item;
    return true;
}

static const char *symbol_name(const Atom *atom) {
    return atom && atom->kind == ATOM_SYMBOL ? symbol_bytes(g_symbols, atom->sym_id) : NULL;
}

static const char *string_value(const Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_STRING
        ? atom->ground.sval : NULL;
}

/* Every application in a rule term must be declared. */
static void check_declared(const CettaDeterministicEquationPlanV1 *plan, const Atom *term,
                           const char *rule) {
    const char *head;
    if (!term || term->kind != ATOM_EXPR)
        return;
    if (term->expr.len == 0u) {
        problem("empty-expression %s%s %lu", rule, "", 0u);
        return;
    }
    head = symbol_name(term->expr.elems[0]);
    if (!head) {
        problem("unheaded-application %s%s %lu", rule, "", 0u);
    } else if (!cetta_deterministic_equation_plan_v1_declares(plan, head,
                                                              term->expr.len - 1u)) {
        printf("(CanonicalMorphismGapV1 undeclared %s %lu %s)\n", head,
               (unsigned long)(term->expr.len - 1u), rule);
        g_problems++;
    }
    for (CettaExprLen i = 1u; i < term->expr.len; i++)
        check_declared(plan, term->expr.elems[i], rule);
}

/* The canonical form an equation of HEAD interprets, if any. */
static bool rule_case(const Atom *left, const char *head, const char *rule, Case *out) {
    const Atom *policy;
    const Atom *value;
    const char *name;
    if (!left || left->kind != ATOM_EXPR || left->expr.len == 0u)
        return false;
    name = symbol_name(left->expr.elems[0]);
    if (!name || strcmp(name, head) != 0)
        return false;
    memset(out, 0, sizeof(*out));
    out->rule = rule;
    if (left->expr.len != 3u) {
        problem("wrong-arity %s%s %lu", rule, "", (unsigned long)(left->expr.len - 1u));
        return false;
    }
    policy = left->expr.elems[1];
    value = left->expr.elems[2];
    if (policy->kind == ATOM_VAR) {
        out->policy = NULL;
    } else if ((out->policy = symbol_name(policy)) == NULL) {
        problem("policy-not-symbol %s%s %lu", rule, "", 0u);
        return false;
    }
    if (string_value(value)) {
        out->kind = CETTA_GRAMMAR_CANONICAL_LITERAL_FORM_V1;
        out->name = string_value(value);
        return true;
    }
    if (value->kind == ATOM_EXPR && value->expr.len >= 2u &&
        symbol_name(value->expr.elems[0]) &&
        strcmp(symbol_name(value->expr.elems[0]), "bnf") == 0) {
        const Atom *constructor = value->expr.elems[1];
        out->name = symbol_name(constructor) ? symbol_name(constructor)
                                             : string_value(constructor);
        if (!out->name) {
            problem("unnamed-node %s%s %lu", rule, "", 0u);
            return false;
        }
        for (CettaExprLen i = 2u; i < value->expr.len; i++) {
            if (value->expr.elems[i]->kind != ATOM_VAR) {
                problem("nested-pattern %s%s %lu", rule, "", (unsigned long)(i - 1u));
                return false;
            }
        }
        out->kind = CETTA_GRAMMAR_CANONICAL_CONSTRUCTOR_FORM_V1;
        out->arity = (uint32_t)(value->expr.len - 2u);
        return true;
    }
    problem("not-a-canonical-form %s%s %lu", rule, "", 0u);
    return false;
}

static bool form_matches(const CettaGrammarCanonicalFormV1 *form, const Case *item) {
    if (strcmp(form->name, item->name) != 0)
        return false;
    if (form->kind == CETTA_GRAMMAR_CANONICAL_LITERAL_FORM_V1)
        return item->kind == CETTA_GRAMMAR_CANONICAL_LITERAL_FORM_V1;
    return item->kind == CETTA_GRAMMAR_CANONICAL_CONSTRUCTOR_FORM_V1 &&
           item->arity == form->arity;
}

static const char *form_kind_name(CettaGrammarCanonicalFormKindV1 kind) {
    return kind == CETTA_GRAMMAR_CANONICAL_CONSTRUCTOR_FORM_V1 ? "constructor"
         : kind == CETTA_GRAMMAR_CANONICAL_LITERAL_FORM_V1 ? "literal"
         : "token";
}

int main(int argc, char **argv) {
    SymbolTable symbols;
    CettaTptpPreparedReaderV1 reader;
    CettaGrammarCanonicalTableV1 *table = NULL;
    CettaGrammarCanonicalFormV1 *forms = NULL;
    uint32_t form_len = 0u;
    CettaDeterministicEquationPlanV1 *plan = NULL;
    CettaDeterministicEquationStatusV1 status;
    const char *head;
    const char *const *rule_paths;
    size_t rule_path_len;
    const char **policies = NULL;
    size_t policy_len = 0u;
    Cases cases = {0};
    unsigned long constructors = 0u, literals = 0u, tokens = 0u;
    char error[512] = {0};

    if (argc == 4 && strcmp(argv[1], "manifest") == 0) {
        /* manifest SNAPSHOT.tpp1 MANIFEST: the prepared equations beside the
         * snapshot load only while every listed file has its digest. */
        symbol_table_init(&symbols);
        symbol_table_init_builtins(&symbols, &g_builtin_syms);
        g_symbols = &symbols;
        g_hashcons = NULL;
        g_var_intern = NULL;
        plan = cetta_tptp_compact_manifest_plan_v1(argv[2], argv[3], error,
                                                   sizeof(error));
        if (!plan) {
            printf("(CompactManifestV1 refused \"%s\")\n", error);
            return 1;
        }
        printf("(CompactManifestV1 loaded %u)\n",
               cetta_deterministic_equation_plan_v1_rule_count(plan));
        cetta_deterministic_equation_plan_v1_free(plan);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "collisions") == 0) {
        /* collisions SNAPSHOT.tpp1: the sorts at which two productions
         * reachable through transparent productions give terms of one shape. */
        CettaGrammarCanonicalCollisionV1 *collisions = NULL;
        uint32_t collision_len = 0u;
        symbol_table_init(&symbols);
        symbol_table_init_builtins(&symbols, &g_builtin_syms);
        g_symbols = &symbols;
        g_hashcons = NULL;
        g_var_intern = NULL;
        cetta_tptp_prepared_reader_init_v1(&reader);
        if (!cetta_tptp_prepared_reader_load_v1(&reader, argv[2],
                                                CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1, error,
                                                sizeof(error)) ||
            !cetta_tptp_snapshot_canonical_table_v1(&reader.snapshot, &table, error,
                                                    sizeof(error)) ||
            !cetta_grammar_canonical_collisions_v1(table, &collisions, &collision_len, error,
                                                   sizeof(error))) {
            printf("(CanonicalCollisionsV1 grammar-failure \"%s\")\n", error);
            return 1;
        }
        for (uint32_t c = 0u; c < collision_len; c++) {
            const CettaGrammarCanonicalCollisionV1 *item = &collisions[c];
            static const char *const kinds[] = {"sequence", "literal", "token", "path"};
            printf("(CanonicalCollisionV1 %s %s %s%s%s)\n", kinds[item->kind], item->sort,
                   item->first, item->second ? " " : "", item->second ? item->second : "");
        }
        printf("(CanonicalCollisionsV1 %u)\n", collision_len);
        free(collisions);
        cetta_grammar_canonical_table_free_v1(table);
        return collision_len == 0u ? 0 : 1;
    }
    if (argc < 4) {
        fprintf(stderr, "usage: %s SNAPSHOT.tpp1 HEAD RULES.metta...\n"
                        "       %s manifest SNAPSHOT.tpp1 MANIFEST\n"
                        "       %s collisions SNAPSHOT.tpp1\n",
                argv[0], argv[0], argv[0]);
        return 2;
    }
    head = argv[2];
    rule_paths = (const char *const *)(argv + 3);
    rule_path_len = (size_t)(argc - 3);
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    cetta_tptp_prepared_reader_init_v1(&reader);
    if (!cetta_tptp_prepared_reader_load_v1(&reader, argv[1],
                                            CETTA_TPTP_OFFICIAL_SYNTAXBNF_DIGEST_V1, error,
                                            sizeof(error)) ||
        !cetta_tptp_snapshot_canonical_table_v1(&reader.snapshot, &table, error,
                                                sizeof(error)) ||
        !cetta_grammar_canonical_forms_v1(table, &forms, &form_len, error, sizeof(error))) {
        printf("(CanonicalMorphismCheckV1 grammar-failure \"%s\")\n", error);
        return 1;
    }
    if (!cetta_deterministic_equation_plan_v1_load(rule_paths, rule_path_len,
                                                   &cetta_langdef_deterministic_vocabulary_v1,
                                                   &plan, &status, error, sizeof(error))) {
        printf("(CanonicalMorphismCheckV1 %s \"%s\")\n",
               cetta_deterministic_equation_status_name_v1(status), error);
        return 1;
    }

    for (uint32_t r = 0u; r < cetta_deterministic_equation_plan_v1_rule_count(plan); r++) {
        const char *name = NULL;
        const Atom *left = NULL, *right = NULL;
        Case item;
        (void)cetta_deterministic_equation_plan_v1_rule_view(plan, r, &name, &left, &right);
        check_declared(plan, left, name);
        check_declared(plan, right, name);
        if (!rule_case(left, head, name, &item))
            continue;
        if (!push_case(&cases, item)) {
            fprintf(stderr, "out of memory\n");
            return 1;
        }
        if (item.policy) {
            bool seen = false;
            for (size_t p = 0u; p < policy_len && !seen; p++)
                seen = strcmp(policies[p], item.policy) == 0;
            if (!seen) {
                const char **grown = realloc(policies, (policy_len + 1u) * sizeof(*grown));
                if (!grown) {
                    fprintf(stderr, "out of memory\n");
                    return 1;
                }
                policies = grown;
                policies[policy_len++] = item.policy;
            }
        }
    }
    if (policy_len == 0u)
        problem("no-policy %s%s %lu", head, "", 0u);

    /* Each form has its equations; each equation of HEAD has its form. */
    for (uint32_t f = 0u; f < form_len; f++) {
        const CettaGrammarCanonicalFormV1 *form = &forms[f];
        if (form->kind == CETTA_GRAMMAR_CANONICAL_TOKEN_FORM_V1) {
            unsigned long generic = 0u;
            tokens++;
            for (size_t c = 0u; c < cases.len; c++) {
                const Case *item = &cases.items[c];
                if (!item->policy && item->kind == CETTA_GRAMMAR_CANONICAL_CONSTRUCTOR_FORM_V1 &&
                    item->arity == 1u && strcmp(item->name, form->name) == 0)
                    generic++;
            }
            for (size_t p = 0u; p < policy_len; p++) {
                unsigned long count = generic;
                for (size_t c = 0u; c < cases.len; c++) {
                    const Case *item = &cases.items[c];
                    if (item->policy && strcmp(item->policy, policies[p]) == 0 &&
                        item->kind == CETTA_GRAMMAR_CANONICAL_CONSTRUCTOR_FORM_V1 &&
                        item->arity == 1u && strcmp(item->name, form->name) == 0)
                        count++;
                }
                if (count == 0u)
                    problem("missing token %s %s %lu", form->name, policies[p], 1u);
                else if (count > 1u)
                    problem("duplicate token %s %s %lu", form->name, policies[p], count);
            }
            continue;
        }
        {
            unsigned long count = 0u;
            if (form->kind == CETTA_GRAMMAR_CANONICAL_CONSTRUCTOR_FORM_V1)
                constructors++;
            else
                literals++;
            for (size_t c = 0u; c < cases.len; c++) {
                if (!form_matches(form, &cases.items[c]))
                    continue;
                if (cases.items[c].policy)
                    problem("policy-specific %s %s %lu", form_kind_name(form->kind),
                            form->name, form->arity);
                count++;
            }
            if (count == 0u)
                problem("missing %s \"%s\" %lu", form_kind_name(form->kind), form->name,
                        form->arity);
            else if (count > 1u)
                problem("duplicate %s \"%s\" %lu", form_kind_name(form->kind), form->name,
                        count);
        }
    }
    for (size_t c = 0u; c < cases.len; c++) {
        const Case *item = &cases.items[c];
        bool known = false;
        for (uint32_t f = 0u; f < form_len && !known; f++) {
            const CettaGrammarCanonicalFormV1 *form = &forms[f];
            if (form->kind == CETTA_GRAMMAR_CANONICAL_TOKEN_FORM_V1)
                known = item->kind == CETTA_GRAMMAR_CANONICAL_CONSTRUCTOR_FORM_V1 &&
                        item->arity == 1u && strcmp(item->name, form->name) == 0;
            else
                known = form_matches(form, item);
        }
        if (!known)
            problem("no-such-form %s \"%s\" %lu", item->rule, item->name, item->arity);
    }

    if (g_problems == 0u) {
        printf("(CanonicalMorphismCheckV1 accepted %lu %lu %lu %lu %lu)\n",
               (unsigned long)cases.len, constructors, literals, tokens,
               (unsigned long)policy_len);
    } else {
        printf("(CanonicalMorphismCheckV1 rejected %lu)\n", g_problems);
    }
    free(cases.items);
    free(policies);
    free(forms);
    cetta_deterministic_equation_plan_v1_free(plan);
    cetta_grammar_canonical_table_free_v1(table);
    cetta_tptp_prepared_reader_free_v1(&reader);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return g_problems == 0u ? 0 : 1;
}
