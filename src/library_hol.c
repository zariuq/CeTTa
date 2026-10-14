#include "library_hol.h"

#include <string.h>

#include "eval.h"
#include "prime_scoped_judgments.h"

/* The HE and PeTTa extended profiles reach Prime's native higher-order logic
 * through this module.  A judgment names its theory space first: a space
 * whose theory is a profile of the logic, selected in it by
 * (hol:profile core) or (hol:profile megalodon-hotg).  The program's own
 * space is not read as a theory.  The judgment is Prime's own, with the same
 * checker and the same records, and its answer is data that the dialect does
 * not evaluate again:
 *   a publication       the published name, its record admitted to the space;
 *   a value             the value;
 *   another success     True;
 *   a refutation        False;
 *   anything undecided  the judgment as written.
 * (hol:try J) answers the verdict of J itself: (Established evidence),
 * (Refuted reason), (Undetermined reason) or (Incomplete reason).
 * The declarations in lib/hol.metta hand a judgment to this module as
 * (__cetta_lib_hol_judge_v1 (WORD SPACE OPERAND ...)), its operands never
 * evaluated, and a tried one as (__cetta_lib_hol_try_v1 J).  Prime reads
 * hol: judgments itself; there the module declines. */

static bool hol_module_enabled(const CettaLibraryContext *context) {
    if (!context || !(context->active_mask & CETTA_NATIVE_IMPORT_HOL)) return false;
    const CettaProfile *profile = context->session.profile;
    return profile &&
        ((context->session.language_id == CETTA_LANGUAGE_HE &&
          profile->id == CETTA_PROFILE_HE_EXTENDED) ||
         (context->session.language_id == CETTA_LANGUAGE_PETTA &&
          profile->id == CETTA_PROFILE_PETTA_EXTENDED));
}

static Atom *hol_truth(const CettaLibraryContext *context, Arena *arena, bool value) {
    return context->session.language_id == CETTA_LANGUAGE_PETTA
        ? atom_symbol(arena, value ? "true" : "false") : atom_bool(arena, value);
}

static bool hol_named(Atom *atom, const char *name) {
    return atom && atom->kind == ATOM_SYMBOL && strcmp(atom_name_cstr(atom), name) == 0;
}

/* The status of a verdict and its evidence. */
static const char *hol_verdict(Atom *verdict, Atom **evidence_out) {
    *evidence_out = NULL;
    if (!verdict || verdict->kind != ATOM_EXPR || verdict->expr.len != 4u ||
        !hol_named(verdict->expr.elems[0], "PrimeVerdict") ||
        !verdict->expr.elems[1] || verdict->expr.elems[1]->kind != ATOM_SYMBOL)
        return NULL;
    Atom *evidence = verdict->expr.elems[3];
    if (evidence && evidence->kind == ATOM_EXPR && evidence->expr.len >= 2u &&
        hol_named(evidence->expr.elems[0], "PrimeEvidenceV1"))
        evidence = evidence->expr.elems[1];
    *evidence_out = evidence;
    return atom_name_cstr(verdict->expr.elems[1]);
}

/* The judgment a library call asks for: `(__cetta_lib_hol_judge_v1 (WORD
 * SPACE OPERAND ...))` asks for `(hol:WORD SPACE OPERAND ...)`, and
 * `(__cetta_lib_hol_try_v1 J)` for the verdict of the hol: judgment J.  NULL
 * when the call names no judgment of the logic. */
static Atom *hol_requested_judgment(Arena *arena, Atom *head, Atom **arguments,
                                    uint32_t count, bool *try_out) {
    *try_out = false;
    if (count != 1u || !arguments[0] || arguments[0]->kind != ATOM_EXPR ||
        arguments[0]->expr.len < 2u)
        return NULL;
    Atom *request = arguments[0];
    Atom *word = request->expr.elems[0];
    if (!word || word->kind != ATOM_SYMBOL) return NULL;
    if (hol_named(head, "__cetta_lib_hol_try_v1")) {
        if (strncmp(atom_name_cstr(word), "hol:", 4u) != 0 ||
            !prime_scoped_judgment_is_head_id(atom_symbol_current_id(word)))
            return NULL;
        *try_out = true;
        return request;
    }
    if (!hol_named(head, "__cetta_lib_hol_judge_v1")) return NULL;
    const char *bare = atom_name_cstr(word);
    size_t length = strlen(bare);
    char *spelled = arena_alloc(arena, length + 5u);
    if (!spelled) return NULL;
    memcpy(spelled, "hol:", 4u);
    memcpy(spelled + 4u, bare, length + 1u);
    Atom *judgment_head = atom_symbol(arena, spelled);
    if (!prime_scoped_judgment_is_head_id(atom_symbol_current_id(judgment_head))) return NULL;
    Atom **items = arena_alloc(arena, sizeof(Atom *) * request->expr.len);
    if (!items) return NULL;
    items[0] = judgment_head;
    for (CettaExprIndex i = 1u; i < request->expr.len; i++)
        items[i] = request->expr.elems[i];
    return atom_expr(arena, items, request->expr.len);
}

bool cetta_hol_module_call(CettaLibraryContext *context, Space *space,
    Arena *arena, Atom *head, Atom **arguments, uint32_t count, CettaCallOutcome *out) {
    (void)space;
    if (!head || head->kind != ATOM_SYMBOL || !hol_module_enabled(context))
        return false;
    bool try_judgment = false;
    Atom *written = hol_requested_judgment(arena, head, arguments, count, &try_judgment);
    if (!written) return false;
    /* The theory space is the judgment's first operand. */
    Space *theory = prime_public_space_argument(arena, written->expr.elems[1]);
    if (!theory || !prime_scoped_space_is_hol(theory)) {
        *out = cetta_call_completed_value(
            try_judgment ? atom_expr2(arena, atom_symbol(arena, "hol:try"), written) : written);
        return true;
    }
    Atom *judgment = try_judgment
        ? atom_expr2(arena, atom_symbol(arena, "try"), written) : written;
    Atom *verdict = prime_scoped_judgment_judge(arena, theory, judgment, false, 0u);
    Atom *evidence = NULL;
    const char *status = hol_verdict(verdict, &evidence);
    Atom *answer = try_judgment
        ? atom_expr2(arena, atom_symbol(arena, "hol:try"), written) : written;
    if (status && strcmp(status, "Established") == 0) {
        if (evidence && evidence->kind == ATOM_EXPR && evidence->expr.len == 2u &&
            hol_named(evidence->expr.elems[0], "PrimeScopedValue")) {
            answer = evidence->expr.elems[1];
        } else if (evidence && evidence->kind == ATOM_EXPR && evidence->expr.len >= 2u &&
                   hol_named(evidence->expr.elems[0], "HolPublish")) {
            Atom *record = evidence->expr.elems[1];
            bool published = record && record->kind == ATOM_EXPR && record->expr.len >= 2u &&
                eval_admit_scoped_record(theory, arena, record);
            for (CettaExprIndex i = 2u; published && i < evidence->expr.len; i++)
                if (evidence->expr.elems[i])
                    eval_admit_scoped_record(theory, arena, evidence->expr.elems[i]);
            if (published) answer = record->expr.elems[1];
        } else {
            answer = hol_truth(context, arena, true);
        }
    } else if (status && strcmp(status, "Refuted") == 0) {
        answer = hol_truth(context, arena, false);
    }
    *out = cetta_call_completed_value(answer);
    return true;
}
