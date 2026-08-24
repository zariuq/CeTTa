#include "petta_typecheck_v3.h"

#include "generated/petta_typecheck_v3_core_provider_catalog_v1.generated.h"
#include "generated/petta_typecheck_v3_core_v1.generated.h"
#include "petta_type_fact_provider_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CettaPettaTypecheckV3 {
    CettaGsltHornProgram *program;
};

static void v3_copy_text(char *target, size_t target_size, const char *text);

static CettaGsltHornLimits v3_product_limits(void) {
    return (CettaGsltHornLimits){
        .max_rule_attempts = 100000u,
        .max_answers = 32u,
        .max_depth = 256u,
    };
}

static bool v3_head_is(const Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
        atom->expr.elems && atom->expr.elems[0]->kind == ATOM_SYMBOL &&
        strcmp(atom_name_cstr(atom->expr.elems[0]), name) == 0;
}

static uint32_t v3_block_hash(Atom *const *forms, size_t form_count) {
    uint32_t hash = UINT32_C(2166136261);
    for (size_t index = 0u; index < form_count; index++) {
        uint32_t form_hash = atom_hash(forms[index]);
        hash ^= form_hash;
        hash *= UINT32_C(16777619);
        hash ^= (uint32_t)index;
        hash *= UINT32_C(16777619);
    }
    hash ^= (uint32_t)form_count;
    hash *= UINT32_C(16777619);
    return hash;
}

static const Atom *v3_equation_subject(const Atom *form) {
    if (!v3_head_is(form, "=") || form->expr.len != 3u)
        return NULL;
    const Atom *lhs = form->expr.elems[1];
    if (lhs->kind == ATOM_SYMBOL)
        return lhs;
    if (lhs->kind == ATOM_EXPR && lhs->expr.len > 0u &&
        lhs->expr.elems[0]->kind == ATOM_SYMBOL) {
        return lhs->expr.elems[0];
    }
    return NULL;
}

static bool v3_symbol_is(const Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_SYMBOL && name &&
        strcmp(atom_name_cstr((Atom *)atom), name) == 0;
}

static bool v3_same_symbol(const Atom *left, const Atom *right) {
    return left && right && left->kind == ATOM_SYMBOL &&
        right->kind == ATOM_SYMBOL &&
        strcmp(atom_name_cstr((Atom *)left),
               atom_name_cstr((Atom *)right)) == 0;
}

static bool v3_arrow_syntax(const Atom *type) {
    if (!type || type->kind != ATOM_EXPR || type->expr.len < 2u ||
        type->expr.elems[0]->kind != ATOM_SYMBOL) {
        return false;
    }
    const char *name = atom_name_cstr(type->expr.elems[0]);
    size_t length = strlen(name);
    return strcmp(name, "->") == 0 ||
        (length >= 2u && strcmp(name + length - 2u, "->") == 0);
}

static bool v3_arrow_type(const Atom *type, size_t arity) {
    return v3_arrow_syntax(type) &&
        type->expr.len == (CettaExprLen)(arity + 2u);
}

static bool v3_explicit_det_arrow(const Atom *type) {
    if (!type || type->kind != ATOM_EXPR || type->expr.len == 0u ||
        type->expr.elems[0]->kind != ATOM_SYMBOL) {
        return false;
    }
    const char *name = atom_name_cstr(type->expr.elems[0]);
    return strcmp(name, "-[det]->") == 0 ||
        strcmp(name, "-[deterministic]->") == 0;
}

static bool v3_effect_arrow(const Atom *type) {
    if (!type || type->kind != ATOM_EXPR || type->expr.len == 0u ||
        type->expr.elems[0]->kind != ATOM_SYMBOL) {
        return false;
    }
    const char *name = atom_name_cstr(type->expr.elems[0]);
    size_t length = strlen(name);
    return length > 6u && strncmp(name, "-[$", 3u) == 0 &&
        strcmp(name + length - 3u, "]->") == 0;
}

static const Atom *v3_relation_signature(
    Atom *const *forms, size_t form_count, const Atom *subject, size_t arity,
    bool *ambiguous) {
    const Atom *signature = NULL;
    if (ambiguous)
        *ambiguous = false;
    for (size_t index = 0u; index < form_count; index++) {
        const Atom *form = forms[index];
        if (!v3_head_is(form, ":") || form->expr.len != 3u ||
            !v3_same_symbol(form->expr.elems[1], subject) ||
            !v3_arrow_type(form->expr.elems[2], arity)) {
            continue;
        }
        if (signature) {
            if (ambiguous)
                *ambiguous = true;
            return NULL;
        }
        signature = form->expr.elems[2];
    }
    return signature;
}

static bool v3_lhs_group(
    const Atom *form, const Atom *subject, size_t arity,
    const Atom **lhs_out) {
    if (!v3_head_is(form, "=") || form->expr.len != 3u)
        return false;
    const Atom *lhs = form->expr.elems[1];
    if (!lhs || lhs->kind != ATOM_EXPR ||
        lhs->expr.len != (CettaExprLen)(arity + 1u) ||
        !v3_same_symbol(lhs->expr.elems[0], subject)) {
        return false;
    }
    if (lhs_out)
        *lhs_out = lhs;
    return true;
}

typedef struct {
    const Atom *name;
    size_t arity;
} V3CoverageKey;

static bool v3_coverage_key_equal(
    const V3CoverageKey *left, const V3CoverageKey *right) {
    return left && right && left->arity == right->arity &&
        v3_same_symbol(left->name, right->name);
}

static bool v3_relation_has_equation(
    Atom *const *forms, size_t form_count,
    const Atom *subject, size_t arity) {
    for (size_t index = 0u; index < form_count; index++) {
        if (v3_lhs_group(forms[index], subject, arity, NULL))
            return true;
    }
    return false;
}

static size_t v3_nominal_candidates(
    Atom *const *forms, size_t form_count, const Atom *domain,
    V3CoverageKey *candidates, size_t capacity) {
    size_t count = 0u;
    for (size_t index = 0u; index < form_count; index++) {
        const Atom *form = forms[index];
        if (!v3_head_is(form, ":") || form->expr.len != 3u ||
            form->expr.elems[1]->kind != ATOM_SYMBOL) {
            continue;
        }
        const Atom *constructor = form->expr.elems[1];
        const Atom *type = form->expr.elems[2];
        size_t arity = 0u;
        bool result_matches = v3_same_symbol(type, domain);
        if (v3_arrow_syntax(type)) {
            arity = (size_t)type->expr.len - 2u;
            result_matches = v3_same_symbol(
                type->expr.elems[type->expr.len - 1u], domain);
        }
        if (!result_matches ||
            v3_relation_has_equation(
                forms, form_count, constructor, arity)) {
            continue;
        }
        V3CoverageKey candidate = {
            .name = constructor,
            .arity = arity,
        };
        bool duplicate = false;
        for (size_t prior = 0u; prior < count; prior++) {
            if (v3_coverage_key_equal(&candidates[prior], &candidate)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate && count < capacity)
            candidates[count++] = candidate;
    }
    return count;
}

static bool v3_nominal_pattern_key(
    const Atom *pattern, const V3CoverageKey *candidates,
    size_t candidate_count, size_t *candidate_index) {
    V3CoverageKey observed = {0};
    if (pattern && pattern->kind == ATOM_SYMBOL) {
        observed.name = pattern;
    } else if (pattern && pattern->kind == ATOM_EXPR &&
               pattern->expr.len > 0u &&
               pattern->expr.elems[0]->kind == ATOM_SYMBOL) {
        observed.name = pattern->expr.elems[0];
        observed.arity = (size_t)pattern->expr.len - 1u;
    } else {
        return false;
    }
    for (size_t index = 0u; index < candidate_count; index++) {
        if (v3_coverage_key_equal(&observed, &candidates[index])) {
            if (candidate_index)
                *candidate_index = index;
            return true;
        }
    }
    return false;
}

static bool v3_numeric_literal(const Atom *atom) {
    if (!atom || atom->kind != ATOM_GROUNDED)
        return false;
    return atom->ground.gkind == GV_INT || atom->ground.gkind == GV_FLOAT ||
        atom->ground.gkind == GV_BIGINT ||
        atom->ground.gkind == GV_RATIONAL;
}

static bool v3_string_literal(const Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED &&
        atom->ground.gkind == GV_STRING;
}

static int v3_bool_pattern(const Atom *pattern) {
    if (!pattern)
        return -1;
    if (pattern->kind == ATOM_GROUNDED &&
        pattern->ground.gkind == GV_BOOL) {
        return pattern->ground.bval ? 1 : 0;
    }
    if (v3_symbol_is(pattern, "True") || v3_symbol_is(pattern, "true"))
        return 1;
    if (v3_symbol_is(pattern, "False") || v3_symbol_is(pattern, "false"))
        return 0;
    return -1;
}

static void v3_remember_coverage_conflict(
    CettaPettaTypecheckV3BlockResult *result, const Atom *subject,
    size_t position, const char *missing) {
    result->verdict = PETTA_ANALYSIS_REFUTED;
    result->boundary = CETTA_PETTA_V3_BOUNDARY_CARDINALITY;
    result->search_outcome = CETTA_GSLT_HORN_COMPLETED;
    v3_copy_text(result->relation, sizeof result->relation,
                 "V3RelationCoverage");
    v3_copy_text(result->subject, sizeof result->subject,
                 atom_name_cstr((Atom *)subject));
    (void)snprintf(
        result->diagnostic, sizeof result->diagnostic,
        "V3RelationCoverage at cardinality evidence boundary for definition "
        "%s: argument %zu does not cover %s",
        result->subject, position + 1u, missing ? missing : "a known value");
}

/* Whole-relation negative coverage: refute only when every clause in one
 * argument column has a known top-level key and a definitely inhabiting key
 * remains uncovered.  This runs once over the mutually visible block; the
 * evaluator never replays it. */
static bool v3_relation_coverage_conflict(
    Atom *const *forms, size_t form_count,
    CettaPettaTypecheckV3Policy policy,
    CettaPettaTypecheckV3BlockResult *result) {
    V3CoverageKey *candidates = calloc(
        form_count ? form_count : 1u, sizeof(*candidates));
    size_t coverage_capacity = form_count > 2u ? form_count : 2u;
    bool *covered = calloc(coverage_capacity, sizeof(*covered));
    if (!candidates || !covered) {
        free(candidates);
        free(covered);
        return false;
    }
    for (size_t form_index = 0u; form_index < form_count; form_index++) {
        const Atom *form = forms[form_index];
        const Atom *lhs = NULL;
        const Atom *subject = v3_equation_subject(form);
        if (!subject || form->expr.elems[1]->kind != ATOM_EXPR)
            continue;
        size_t arity = (size_t)form->expr.elems[1]->expr.len - 1u;
        if (!v3_lhs_group(form, subject, arity, &lhs) || arity == 0u)
            continue;
        bool seen = false;
        for (size_t prior = 0u; prior < form_index; prior++) {
            if (v3_lhs_group(forms[prior], subject, arity, NULL)) {
                seen = true;
                break;
            }
        }
        if (seen)
            continue;
        bool ambiguous = false;
        const Atom *signature = v3_relation_signature(
            forms, form_count, subject, arity, &ambiguous);
        bool coverage_required = signature && !ambiguous &&
            (v3_explicit_det_arrow(signature) ||
             (policy == CETTA_PETTA_TYPECHECK_V3_POLICY_STRICT_DET &&
              v3_effect_arrow(signature)));
        if (!coverage_required)
            continue;

        for (size_t position = 0u; position < arity; position++) {
            const Atom *domain = signature->expr.elems[position + 1u];
            bool open = false;
            size_t clause_count = 0u;
            if (v3_symbol_is(domain, "Number") ||
                v3_symbol_is(domain, "String")) {
                for (size_t index = 0u; index < form_count; index++) {
                    const Atom *group_lhs = NULL;
                    if (!v3_lhs_group(
                            forms[index], subject, arity, &group_lhs)) {
                        continue;
                    }
                    clause_count++;
                    const Atom *pattern = group_lhs->expr.elems[position + 1u];
                    bool literal = v3_symbol_is(domain, "Number")
                        ? v3_numeric_literal(pattern)
                        : v3_string_literal(pattern);
                    if (!literal) {
                        open = true;
                        break;
                    }
                }
                if (clause_count > 0u && !open) {
                    char missing[96];
                    (void)snprintf(
                        missing, sizeof missing, "a %s outside the matched literals",
                        atom_name_cstr((Atom *)domain));
                    v3_remember_coverage_conflict(
                        result, subject, position, missing);
                    free(candidates);
                    free(covered);
                    return true;
                }
                continue;
            }

            if (v3_symbol_is(domain, "Bool")) {
                covered[0] = false;
                covered[1] = false;
                for (size_t index = 0u; index < form_count; index++) {
                    const Atom *group_lhs = NULL;
                    if (!v3_lhs_group(
                            forms[index], subject, arity, &group_lhs)) {
                        continue;
                    }
                    clause_count++;
                    int value = v3_bool_pattern(
                        group_lhs->expr.elems[position + 1u]);
                    if (value < 0) {
                        open = true;
                        break;
                    }
                    covered[(size_t)value] = true;
                }
                if (clause_count > 0u && !open &&
                    (!covered[0] || !covered[1])) {
                    v3_remember_coverage_conflict(
                        result, subject, position,
                        covered[0] ? "True" : "False");
                    free(candidates);
                    free(covered);
                    return true;
                }
                continue;
            }

            if (!domain || domain->kind != ATOM_SYMBOL)
                continue;
            size_t candidate_count = v3_nominal_candidates(
                forms, form_count, domain, candidates, form_count);
            if (candidate_count == 0u)
                continue;
            memset(covered, 0, sizeof(*covered) * candidate_count);
            for (size_t index = 0u; index < form_count; index++) {
                const Atom *group_lhs = NULL;
                if (!v3_lhs_group(forms[index], subject, arity, &group_lhs))
                    continue;
                clause_count++;
                size_t candidate_index = 0u;
                if (!v3_nominal_pattern_key(
                        group_lhs->expr.elems[position + 1u],
                        candidates, candidate_count, &candidate_index)) {
                    open = true;
                    break;
                }
                covered[candidate_index] = true;
            }
            if (!open && clause_count > 0u) {
                for (size_t index = 0u; index < candidate_count; index++) {
                    if (covered[index])
                        continue;
                    char missing[160];
                    if (candidates[index].arity == 0u) {
                        (void)snprintf(
                            missing, sizeof missing, "%s",
                            atom_name_cstr((Atom *)candidates[index].name));
                    } else {
                        (void)snprintf(
                            missing, sizeof missing, "%s/%zu",
                            atom_name_cstr((Atom *)candidates[index].name),
                            candidates[index].arity);
                    }
                    v3_remember_coverage_conflict(
                        result, subject, position, missing);
                    free(candidates);
                    free(covered);
                    return true;
                }
            }
        }
    }
    free(candidates);
    free(covered);
    return false;
}

static void v3_copy_text(char *target, size_t target_size, const char *text) {
    if (!target || target_size == 0u)
        return;
    (void)snprintf(target, target_size, "%s", text ? text : "");
}

static PettaAnalysisVerdict v3_analysis_verdict(
    CettaNikOutcomeV1 outcome) {
    switch (outcome) {
    case CETTA_NIK_OUTCOME_ESTABLISHED:
        return PETTA_ANALYSIS_ESTABLISHED;
    case CETTA_NIK_OUTCOME_REFUTED:
        return PETTA_ANALYSIS_REFUTED;
    case CETTA_NIK_OUTCOME_OUTSIDE_FRAGMENT:
        return PETTA_ANALYSIS_UNDETERMINED;
    case CETTA_NIK_OUTCOME_INCOMPLETE:
        return PETTA_ANALYSIS_INCOMPLETE;
    }
    return PETTA_ANALYSIS_INCOMPLETE;
}

const char *cetta_petta_typecheck_v3_verdict_name(
    PettaAnalysisVerdict verdict) {
    switch (verdict) {
    case PETTA_ANALYSIS_ESTABLISHED:
        return "established";
    case PETTA_ANALYSIS_REFUTED:
        return "refuted";
    case PETTA_ANALYSIS_UNDETERMINED:
        return "undetermined";
    case PETTA_ANALYSIS_INCOMPLETE:
        return "incomplete";
    }
    return "invalid";
}

const char *cetta_petta_typecheck_v3_boundary_name(
    CettaPettaV3EvidenceBoundaryV1 boundary) {
    switch (boundary) {
    case CETTA_PETTA_V3_BOUNDARY_NONE:
        return "none";
    case CETTA_PETTA_V3_BOUNDARY_SHAPE:
        return "shape";
    case CETTA_PETTA_V3_BOUNDARY_CARDINALITY:
        return "cardinality";
    case CETTA_PETTA_V3_BOUNDARY_STAGE:
        return "stage";
    }
    return "invalid";
}

CettaPettaTypecheckV3 *cetta_petta_typecheck_v3_create(
    char *error, size_t error_size) {
    const CettaGsltEmbeddedLanguageV1 *language =
        &cetta_petta_typecheck_v3_core_v1;
    const CettaGsltProviderCatalogV1 *catalog =
        &cetta_petta_typecheck_v3_core_provider_catalog_v1;
    if (!cetta_gslt_provider_catalog_validate_v1(
            catalog, error, error_size) ||
        !language->semantic_sources || language->semantic_source_count == 0u) {
        return NULL;
    }
    CettaGsltHornInput *inputs = calloc(
        language->semantic_source_count, sizeof(*inputs));
    if (!inputs) {
        if (error && error_size > 0u)
            (void)snprintf(error, error_size,
                           "could not allocate v3 semantic inputs");
        return NULL;
    }
    for (size_t index = 0u; index < language->semantic_source_count; index++)
        inputs[index] = language->semantic_sources[index].input;
    CettaPettaTypecheckV3 *checker = calloc(1u, sizeof(*checker));
    bool loaded = checker && cetta_gslt_horn_program_load_inputs(
        inputs, language->semantic_source_count,
        &checker->program, error, error_size);
    free(inputs);
    if (!loaded || cetta_gslt_horn_program_rule_count(checker->program) != 149u) {
        if (loaded && error && error_size > 0u)
            (void)snprintf(error, error_size,
                           "generated v3 calculus does not contain 149 rules");
        cetta_petta_typecheck_v3_free(checker);
        return NULL;
    }
    return checker;
}

void cetta_petta_typecheck_v3_free(CettaPettaTypecheckV3 *checker) {
    if (!checker)
        return;
    cetta_gslt_horn_program_free(checker->program);
    free(checker);
}

size_t cetta_petta_typecheck_v3_rule_count(
    const CettaPettaTypecheckV3 *checker) {
    return checker && checker->program
        ? cetta_gslt_horn_program_rule_count(checker->program) : 0u;
}

static void v3_remember_decision(
    CettaPettaTypecheckV3BlockResult *result,
    const Atom *subject,
    const CettaPettaV3EvidenceDecisionV1 *decision) {
    if (!result || !decision)
        return;
    result->boundary = decision->boundary;
    result->search_outcome = decision->search_outcome;
    v3_copy_text(result->relation, sizeof result->relation,
                 decision->relation);
    v3_copy_text(result->subject, sizeof result->subject,
                 subject ? atom_name_cstr((Atom *)subject) : "<dynamic-head>");
    (void)snprintf(
        result->diagnostic, sizeof result->diagnostic,
        "%s at %s evidence boundary for definition %s",
        decision->relation ? decision->relation : "v3-decision",
        cetta_petta_typecheck_v3_boundary_name(decision->boundary),
        result->subject);
}

bool cetta_petta_typecheck_v3_opt_license_is_current(
    const CettaPettaTypecheckV3OptLicense *license,
    const Space *space,
    Atom *const *forms,
    size_t form_count,
    CettaPettaTypecheckV3Policy policy) {
    if (!license || !license->issued || !space ||
        (!forms && form_count != 0u)) {
        return false;
    }
    uint32_t equation_count = 0u;
    for (size_t index = 0u; index < form_count; index++) {
        if (v3_head_is(forms[index], "=") && forms[index]->expr.len == 3u)
            equation_count++;
    }
    return license->space_instance == space_instance_id(space) &&
        license->admitted_revision == space_revision(space) &&
        license->block_hash == v3_block_hash(forms, form_count) &&
        license->policy == (uint32_t)policy &&
        license->equation_count == equation_count;
}

bool cetta_petta_typecheck_v3_declaration_block(
    CettaPettaTypecheckV3 *checker,
    PettaProgram *program,
    Space *space,
    Atom *const *forms,
    size_t form_count,
    CettaPettaTypecheckV3Policy policy,
    CettaPettaTypecheckV3BlockResult *result) {
    if (result)
        *result = (CettaPettaTypecheckV3BlockResult){
            .verdict = PETTA_ANALYSIS_ESTABLISHED,
            .search_outcome = CETTA_GSLT_HORN_COMPLETED,
        };
    if (!checker || !checker->program || !program || !space ||
        (!forms && form_count != 0u) || !result) {
        if (result)
            v3_copy_text(result->diagnostic, sizeof result->diagnostic,
                         "invalid typecheck-v3 block request");
        return false;
    }

    char error[512] = {0};
    CettaPettaTypeFactProviderV1 *provider =
        cetta_petta_type_fact_provider_create_with_overlay_v1(
            program, space, forms, form_count, error, sizeof error);
    CettaGsltAuthorizedProviderRegistryV1 authorized = {0};
    bool ready = provider && cetta_gslt_provider_registry_authorize_v1(
        &cetta_petta_typecheck_v3_core_provider_catalog_v1,
        cetta_petta_type_fact_provider_registry_v1(provider),
        &authorized, error, sizeof error);
    if (!ready) {
        v3_copy_text(result->diagnostic, sizeof result->diagnostic,
                     error[0] ? error : "could not admit v3 provider revision");
        cetta_gslt_authorized_provider_registry_free_v1(&authorized);
        cetta_petta_type_fact_provider_free_v1(provider);
        return false;
    }

    for (size_t index = 0u; index < form_count; index++) {
        const Atom *form = forms[index];
        if (v3_head_is(form, ":") && form->expr.len == 3u)
            result->declarations_seen++;
        if (!v3_head_is(form, "=") || form->expr.len != 3u)
            continue;
        result->equations_checked++;
        const Atom *subject = v3_equation_subject(form);
        if (!subject) {
            result->undetermined_equations++;
            result->gradual_equations++;
            result->verdict = petta_analysis_verdict_all(
                result->verdict, PETTA_ANALYSIS_UNDETERMINED);
            if (!result->diagnostic[0]) {
                v3_copy_text(result->relation, sizeof result->relation,
                             "V3DefinitionHead");
                v3_copy_text(result->subject, sizeof result->subject,
                             "<dynamic-head>");
                v3_copy_text(result->diagnostic, sizeof result->diagnostic,
                             "V3DefinitionHead is undetermined for a dynamic definition head");
            }
            continue;
        }
        CettaPettaV3EvidenceDecisionV1 decision = {0};
        error[0] = '\0';
        if (!cetta_petta_typecheck_v3_decide_equation_v1(
                checker->program, &authorized.registry,
                form->expr.elems[1], form->expr.elems[2],
                v3_product_limits(),
                &decision, error, sizeof error)) {
            v3_copy_text(result->diagnostic, sizeof result->diagnostic,
                         error[0] ? error : "v3 definition decision faulted");
            cetta_gslt_authorized_provider_registry_free_v1(&authorized);
            cetta_petta_type_fact_provider_free_v1(provider);
            return false;
        }
        PettaAnalysisVerdict verdict = v3_analysis_verdict(decision.outcome);
        result->verdict = petta_analysis_verdict_all(result->verdict, verdict);
        if (decision.seam_kind == CETTA_PETTA_V3_SEAM_EXACT &&
            decision.optimization_license.issued) {
            result->exact_equations++;
        } else if (decision.seam_kind == CETTA_PETTA_V3_SEAM_CONFLICT) {
            result->conflict_equations++;
        } else {
            result->gradual_equations++;
        }
        if (verdict == PETTA_ANALYSIS_ESTABLISHED) {
            result->established_equations++;
        } else if (verdict == PETTA_ANALYSIS_UNDETERMINED) {
            result->undetermined_equations++;
        } else if (verdict == PETTA_ANALYSIS_INCOMPLETE) {
            result->incomplete_equations++;
        }
        if (verdict == PETTA_ANALYSIS_REFUTED ||
            (verdict != PETTA_ANALYSIS_ESTABLISHED &&
             !result->diagnostic[0])) {
            v3_remember_decision(result, subject, &decision);
        }
        if (verdict == PETTA_ANALYSIS_REFUTED)
            break;
    }

    PettaAnalysisVerdict before_coverage = result->verdict;
    if (result->verdict != PETTA_ANALYSIS_REFUTED)
        (void)v3_relation_coverage_conflict(
            forms, form_count, policy, result);
    if (before_coverage != PETTA_ANALYSIS_REFUTED &&
        result->verdict == PETTA_ANALYSIS_REFUTED) {
        result->conflict_equations++;
    }

    if (result->verdict == PETTA_ANALYSIS_ESTABLISHED &&
        result->equations_checked > 0u &&
        result->exact_equations == result->equations_checked &&
        result->gradual_equations == 0u &&
        result->conflict_equations == 0u &&
        result->undetermined_equations == 0u &&
        result->incomplete_equations == 0u) {
        result->optimization_license = (CettaPettaTypecheckV3OptLicense){
            .issued = true,
            .space_instance = space_instance_id(space),
            .admitted_revision = space_revision(space),
            .block_hash = v3_block_hash(forms, form_count),
            .policy = (uint32_t)policy,
            .equation_count = result->equations_checked,
        };
    }

    if (!result->diagnostic[0]) {
        (void)snprintf(
            result->diagnostic, sizeof result->diagnostic,
            "%s across %u v3 definition judgments",
            cetta_petta_typecheck_v3_verdict_name(result->verdict),
            result->equations_checked);
    }
    cetta_gslt_authorized_provider_registry_free_v1(&authorized);
    cetta_petta_type_fact_provider_free_v1(provider);
    return true;
}

#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
static CettaPettaTypecheckV3Policy v3_policy_from_legacy(
    PettaTypecheckPolicy policy) {
    switch (policy) {
    case PETTA_TYPECHECK_POLICY_STRICT:
        return CETTA_PETTA_TYPECHECK_V3_POLICY_STRICT;
    case PETTA_TYPECHECK_POLICY_STRICT_DET:
        return CETTA_PETTA_TYPECHECK_V3_POLICY_STRICT_DET;
    case PETTA_TYPECHECK_POLICY_DEFAULT:
        return CETTA_PETTA_TYPECHECK_V3_POLICY_DEFAULT;
    }
    return CETTA_PETTA_TYPECHECK_V3_POLICY_DEFAULT;
}
#endif

const char *cetta_petta_typecheck_v3_route_name(
    CettaPettaTypecheckV3Route route) {
    switch (route) {
    case CETTA_PETTA_TYPECHECK_V3_ROUTE_NATIVE_AGREEMENT:
        return "native-agreement";
    case CETTA_PETTA_TYPECHECK_V3_ROUTE_NAMED_REFINEMENT:
        return "named-refinement";
    case CETTA_PETTA_TYPECHECK_V3_ROUTE_LEGACY_V2:
        return "legacy-v2";
    }
    return "invalid";
}

bool cetta_petta_typecheck_v3_compatibility_block(
    CettaPettaTypecheckV3 *checker,
    PettaProgram *program,
    Space *space,
    Registry *registry,
    Atom *const *forms,
    size_t form_count,
    PettaTypecheckPolicy policy,
    bool declaration_admission,
    CettaPettaTypecheckV3CompatibilityResult *result) {
    if (result)
        *result = (CettaPettaTypecheckV3CompatibilityResult){0};
    if (!checker || !program || !space || !registry ||
        (!forms && form_count != 0u) || !result) {
        if (result)
            v3_copy_text(result->diagnostic, sizeof result->diagnostic,
                         "invalid typecheck-v3 compatibility request");
        return false;
    }
#if CETTA_BUILD_WITH_PETTA_TYPECHECK_V2
    CettaPettaTypecheckV3Policy native_policy =
        v3_policy_from_legacy(policy);
    if (!cetta_petta_typecheck_v3_declaration_block(
            checker, program, space, forms, form_count,
            native_policy, &result->native)) {
        v3_copy_text(
            result->diagnostic, sizeof result->diagnostic,
            result->native.diagnostic[0]
                ? result->native.diagnostic
                : "native typecheck-v3 judgment faulted");
        return false;
    }
    bool legacy_judged = declaration_admission
        ? petta_typecheck_declaration_admission_selected(
              program, space, registry, forms, form_count,
              policy, &result->legacy)
        : petta_typecheck_declaration_block_selected(
              program, space, registry, forms, form_count,
              policy, &result->legacy);
    if (!legacy_judged) {
        v3_copy_text(
            result->diagnostic, sizeof result->diagnostic,
            result->legacy.diagnostic[0]
                ? result->legacy.diagnostic
                : "legacy typecheck-v2 judgment faulted");
        return false;
    }

    bool decisive_native =
        result->native.verdict == PETTA_ANALYSIS_ESTABLISHED ||
        result->native.verdict == PETTA_ANALYSIS_REFUTED;
    bool agrees = decisive_native &&
        result->native.verdict == result->legacy.verdict;
    result->route = agrees
        ? CETTA_PETTA_TYPECHECK_V3_ROUTE_NATIVE_AGREEMENT
        : CETTA_PETTA_TYPECHECK_V3_ROUTE_LEGACY_V2;
    result->verdict = agrees
        ? result->native.verdict : result->legacy.verdict;
    result->fault = result->legacy.fault;
    result->native_optimization_authorized = agrees &&
        result->verdict == PETTA_ANALYSIS_ESTABLISHED &&
        cetta_petta_typecheck_v3_opt_license_is_current(
            &result->native.optimization_license, space,
            forms, form_count, native_policy);
    (void)snprintf(
        result->diagnostic, sizeof result->diagnostic,
        "%s: native=%s; legacy=%s; %.400s",
        cetta_petta_typecheck_v3_route_name(result->route),
        cetta_petta_typecheck_v3_verdict_name(result->native.verdict),
        petta_typecheck_verdict_name(result->legacy.verdict),
        agrees
            ? result->native.diagnostic
            : result->legacy.diagnostic);
    return true;
#else
    (void)policy;
    (void)declaration_admission;
    v3_copy_text(result->diagnostic, sizeof result->diagnostic,
                 "typecheck-v3 compatibility requires typecheck-v2");
    return false;
#endif
}
