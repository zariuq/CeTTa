#include "petta_semantics.h"

#include "space.h"
#include "stats.h"
#include "symbol.h"
#include "term_graph.h"
#include "term_universe.h"

#include <math.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The rational-term module (term_graph.c).  A link unit without it builds
 * no graph, so no node reaches the code that calls these. */
#pragma weak term_graph_assume
#pragma weak term_graph_assumption_key
#pragma weak term_graph_assumptions_free

#define PETTA_FORM_DENSE_CAP 4096u
#define PETTA_FORM_OVERFLOW_CAP 128u

_Static_assert(PETTA_FORM_TRACE <= UINT8_MAX,
               "PeTTa semantic forms must fit in the dense fact byte");
_Static_assert(
    (PETTA_FORM_OVERFLOW_CAP & (PETTA_FORM_OVERFLOW_CAP - 1u)) == 0u,
    "PeTTa semantic-form overflow capacity must be a power of two");

typedef struct {
    SymbolId symbol;
    PeTTaForm form;
} PeTTaFormOverflowSlot;

/* Room for every special form whose symbol lies above the dense range. */
#define PETTA_RUNTIME_HEAD_OVERFLOW_CAP 48u

typedef struct {
    SymbolId symbol;
    PeTTaRuntimeHead head;
} PeTTaRuntimeHeadOverflow;

typedef struct {
    const SymbolTable *table;
    uint64_t table_instance_id;
    uint8_t form_by_symbol[PETTA_FORM_DENSE_CAP];
    PeTTaFormOverflowSlot form_overflow[PETTA_FORM_OVERFLOW_CAP];
    bool form_by_symbol_ready;
    bool cons_shape_facts_ready;
    SymbolId true_text;
    SymbolId false_text;
    SymbolId test;
    SymbolId progn;
    SymbolId prog1;
    SymbolId foldall;
    SymbolId forall;
    SymbolId maplist;
    SymbolId foldl;
    SymbolId id;
    SymbolId append;
    SymbolId cons;
    SymbolId int_add;
    SymbolId stream_unique;
    SymbolId stream_alpha_unique;
    SymbolId stream_union;
    SymbolId stream_intersection;
    SymbolId stream_subtraction;
    SymbolId length;
    SymbolId msort;
    SymbolId sort_atom;
    SymbolId first_from_pair;
    SymbolId first;
    SymbolId second_from_pair;
    SymbolId is_var;
    SymbolId is_ground;
    SymbolId is_expr;
    SymbolId is_space;
    SymbolId is_member;
    SymbolId is_alpha_member;
    SymbolId alpha_unique_atom;
    SymbolId list_to_set;
    SymbolId exclude_item;
    SymbolId repra;
    SymbolId sread;
    SymbolId call;
    SymbolId eval;
    SymbolId predicate;
    SymbolId translate_predicate;
    SymbolId import_prolog_function;
    SymbolId process_metta_string;
    SymbolId call_predicate;
    SymbolId asserta_predicate;
    SymbolId assertz_predicate;
    SymbolId retract_predicate;
    SymbolId tabled;
    SymbolId add_translator_rule;
    SymbolId remove_translator_rule;
    SymbolId cut;
    SymbolId catch_text;
    SymbolId lambda;
    SymbolId canonical_lam;
    SymbolId library;
    SymbolId value_let;
    SymbolId value_chain;
    SymbolId and_then;
    SymbolId or_else;
    SymbolId mm2_exec;
    SymbolId mork_space;
    uint8_t runtime_head_by_symbol[PETTA_FORM_DENSE_CAP];
    PeTTaRuntimeHeadOverflow runtime_head_overflow[
        PETTA_RUNTIME_HEAD_OVERFLOW_CAP];
    uint8_t runtime_head_overflow_len;
    uint32_t open_cons_tag_arena_id;
    uint64_t open_cons_tag_arena_reset_epoch;
    Atom *open_cons_tag;
} PeTTaSymbolIds;

static _Thread_local PeTTaSymbolIds g_petta_symbol_ids;

static bool petta_form_overflow_insert(
    PeTTaSymbolIds *ids, SymbolId symbol, PeTTaForm form) {
    uint32_t slot =
        ((uint32_t)symbol * UINT32_C(2654435761)) &
        (PETTA_FORM_OVERFLOW_CAP - 1u);
    for (uint32_t probe = 0u;
         probe < PETTA_FORM_OVERFLOW_CAP; probe++) {
        PeTTaFormOverflowSlot *entry = &ids->form_overflow[slot];
        if (entry->form == PETTA_FORM_NONE) {
            entry->symbol = symbol;
            entry->form = form;
            return true;
        }
        if (entry->symbol == symbol)
            return true;
        slot = (slot + 1u) & (PETTA_FORM_OVERFLOW_CAP - 1u);
    }
    return false;
}

static PeTTaForm petta_form_overflow_lookup(
    const PeTTaSymbolIds *ids, SymbolId symbol) {
    uint32_t slot =
        ((uint32_t)symbol * UINT32_C(2654435761)) &
        (PETTA_FORM_OVERFLOW_CAP - 1u);
    for (uint32_t probe = 0u;
         probe < PETTA_FORM_OVERFLOW_CAP; probe++) {
        const PeTTaFormOverflowSlot *entry =
            &ids->form_overflow[slot];
        if (entry->form == PETTA_FORM_NONE)
            return PETTA_FORM_NONE;
        if (entry->symbol == symbol)
            return entry->form;
        slot = (slot + 1u) & (PETTA_FORM_OVERFLOW_CAP - 1u);
    }
    return PETTA_FORM_NONE;
}

#define PETTA_SEMANTIC_FORM_ROWS(X)                                      \
    X(ids->test, PETTA_FORM_TEST);                                       \
    X(g_builtin_syms.if_text, PETTA_FORM_IF);                            \
    X(ids->progn, PETTA_FORM_PROGN);                                     \
    X(ids->prog1, PETTA_FORM_PROG1);                                     \
    X(g_builtin_syms.trace_bang, PETTA_FORM_TRACE);                       \
    X(ids->foldall, PETTA_FORM_FOLDALL);                                 \
    X(ids->forall, PETTA_FORM_FORALL);                                   \
    X(ids->maplist, PETTA_FORM_MAPLIST);                                 \
    X(g_builtin_syms.map_atom, PETTA_FORM_MAP_ATOM);                     \
    X(ids->foldl, PETTA_FORM_FOLDL);                                     \
    X(ids->id, PETTA_FORM_ID);                                           \
    X(ids->append, PETTA_FORM_APPEND);                                   \
    X(ids->cons, PETTA_FORM_CONS);                                       \
    X(ids->int_add, PETTA_FORM_INT_ADD);                                 \
    X(ids->stream_unique, PETTA_FORM_STREAM_UNIQUE);                     \
    X(ids->stream_alpha_unique, PETTA_FORM_STREAM_ALPHA_UNIQUE);         \
    X(ids->stream_union, PETTA_FORM_STREAM_UNION);                       \
    X(ids->stream_intersection, PETTA_FORM_STREAM_INTERSECTION);         \
    X(ids->stream_subtraction, PETTA_FORM_STREAM_SUBTRACTION);           \
    X(ids->length, PETTA_FORM_LENGTH);                                   \
    X(ids->msort, PETTA_FORM_MSORT);                                     \
    X(ids->sort_atom, PETTA_FORM_MSORT);                                 \
    X(ids->first_from_pair, PETTA_FORM_FIRST_FROM_PAIR);                 \
    X(ids->first, PETTA_FORM_FIRST_FROM_PAIR);                           \
    X(ids->second_from_pair, PETTA_FORM_SECOND_FROM_PAIR);               \
    X(ids->is_var, PETTA_FORM_IS_VAR);                                   \
    X(ids->is_ground, PETTA_FORM_IS_GROUND);                             \
    X(ids->is_expr, PETTA_FORM_IS_EXPR);                                 \
    X(ids->is_space, PETTA_FORM_IS_SPACE);                               \
    X(ids->is_member, PETTA_FORM_IS_MEMBER);                             \
    X(ids->is_alpha_member, PETTA_FORM_IS_ALPHA_MEMBER);                 \
    X(ids->alpha_unique_atom, PETTA_FORM_ALPHA_UNIQUE);                  \
    X(ids->list_to_set, PETTA_FORM_LIST_TO_SET);                         \
    X(ids->exclude_item, PETTA_FORM_EXCLUDE_ITEM);                       \
    X(ids->repra, PETTA_FORM_REPRA);                                     \
    X(ids->sread, PETTA_FORM_SREAD);                                     \
    X(g_builtin_syms.bind_bang, PETTA_FORM_BIND_STATE);                  \
    X(g_builtin_syms.get_state, PETTA_FORM_GET_STATE);                   \
    X(g_builtin_syms.change_state_bang, PETTA_FORM_CHANGE_STATE);        \
    X(g_builtin_syms.new_state, PETTA_FORM_NEW_STATE);                   \
    X(ids->call, PETTA_FORM_CALL);                                       \
    X(ids->eval, PETTA_FORM_EVAL);                                       \
    X(g_builtin_syms.reduce, PETTA_FORM_REDUCE);                         \
    X(ids->predicate, PETTA_FORM_PREDICATE);                             \
    X(ids->translate_predicate, PETTA_FORM_TRANSLATE_PREDICATE);         \
    X(ids->import_prolog_function, PETTA_FORM_IMPORT_PROLOG_FUNCTION);   \
    X(ids->process_metta_string, PETTA_FORM_PROCESS_METTA_STRING);       \
    X(ids->call_predicate, PETTA_FORM_CALL_PREDICATE);                   \
    X(ids->asserta_predicate, PETTA_FORM_ASSERTA_PREDICATE);             \
    X(ids->assertz_predicate, PETTA_FORM_ASSERTZ_PREDICATE);             \
    X(ids->retract_predicate, PETTA_FORM_RETRACT_PREDICATE);             \
    X(ids->tabled, PETTA_FORM_TABLED);                                   \
    X(ids->add_translator_rule, PETTA_FORM_ADD_TRANSLATOR_RULE);         \
    X(ids->remove_translator_rule, PETTA_FORM_REMOVE_TRANSLATOR_RULE);   \
    X(ids->cut, PETTA_FORM_CUT);                                         \
    X(ids->catch_text, PETTA_FORM_CATCH);                                \
    X(ids->lambda, PETTA_FORM_LAMBDA);                                   \
    X(g_builtin_syms.let, PETTA_FORM_LET);                               \
    X(g_builtin_syms.chain, PETTA_FORM_CHAIN)

static bool petta_form_dense_build(PeTTaSymbolIds *ids) {
    bool ready = true;
#define PETTA_DENSE_FORM(symbol_expression, semantic_form)               \
    do {                                                                 \
        SymbolId symbol = (symbol_expression);                           \
        if (symbol == SYMBOL_ID_NONE) {                                  \
            ready = false;                                               \
        } else if (symbol < PETTA_FORM_DENSE_CAP &&                      \
                   ids->form_by_symbol[symbol] == PETTA_FORM_NONE) {     \
            ids->form_by_symbol[symbol] = (uint8_t)(semantic_form);      \
        } else if (symbol >= PETTA_FORM_DENSE_CAP &&                     \
                   !petta_form_overflow_insert(                          \
                       ids, symbol, (semantic_form))) {                  \
            ready = false;                                               \
        }                                                                \
    } while (0)
    PETTA_SEMANTIC_FORM_ROWS(PETTA_DENSE_FORM);
#undef PETTA_DENSE_FORM
    return ready;
}

static PeTTaForm petta_form_lookup_ids(
    const PeTTaSymbolIds *ids, SymbolId head) {
    if (head == SYMBOL_ID_NONE || !ids || !ids->table)
        return PETTA_FORM_NONE;
    if (ids->form_by_symbol_ready) {
        return head < PETTA_FORM_DENSE_CAP
            ? (PeTTaForm)ids->form_by_symbol[head]
            : petta_form_overflow_lookup(ids, head);
    }
#define PETTA_MATCH_FORM(symbol_expression, semantic_form)               \
    do {                                                                 \
        if (head == (symbol_expression))                                 \
            return (semantic_form);                                      \
    } while (0)
    PETTA_SEMANTIC_FORM_ROWS(PETTA_MATCH_FORM);
#undef PETTA_MATCH_FORM
    return PETTA_FORM_NONE;
}

static bool petta_cons_shape_facts_ready(
    const PeTTaSymbolIds *ids) {
    SymbolId cons_preimage = SYMBOL_ID_NONE;
    uint32_t cons_preimage_count = 0u;
#define PETTA_CAPTURE_CONS_PREIMAGE(symbol_expression, semantic_form)    \
    do {                                                                 \
        if ((semantic_form) == PETTA_FORM_CONS) {                        \
            SymbolId symbol = (symbol_expression);                       \
            if (symbol != SYMBOL_ID_NONE &&                              \
                symbol != cons_preimage &&                               \
                petta_form_lookup_ids(ids, symbol) ==                    \
                    PETTA_FORM_CONS) {                                   \
                cons_preimage = symbol;                                  \
                cons_preimage_count++;                                   \
            }                                                            \
        }                                                                \
    } while (0)
    PETTA_SEMANTIC_FORM_ROWS(PETTA_CAPTURE_CONS_PREIMAGE);
#undef PETTA_CAPTURE_CONS_PREIMAGE
    return ids && ids->table && ids->table_instance_id != 0u &&
           ids->cons != SYMBOL_ID_NONE &&
           cons_preimage_count == 1u &&
           cons_preimage == ids->cons;
}

static void petta_runtime_head_note(
    PeTTaSymbolIds *ids, SymbolId symbol, PeTTaRuntimeHead head) {
    if (symbol == SYMBOL_ID_NONE)
        return;
    if (symbol < PETTA_FORM_DENSE_CAP) {
        ids->runtime_head_by_symbol[symbol] = (uint8_t)head;
        return;
    }
    for (uint8_t index = 0u;
         index < ids->runtime_head_overflow_len; index++) {
        if (ids->runtime_head_overflow[index].symbol == symbol) {
            ids->runtime_head_overflow[index].head = head;
            return;
        }
    }
    if (ids->runtime_head_overflow_len < PETTA_RUNTIME_HEAD_OVERFLOW_CAP) {
        ids->runtime_head_overflow[ids->runtime_head_overflow_len++] =
            (PeTTaRuntimeHeadOverflow){.symbol = symbol, .head = head};
    }
}

/* The special forms of SWI-PeTTa's translator, and its stream rewrites.  Of
 * these, PeTTa defines a function of the same name only for the ones marked
 * FUNCTION; every other one, reached at run time, is data.  SWI-PeTTa also
 * registers `let` and `let*` as functions without defining one, which makes
 * their run-time application a partial that no argument completes; here
 * they are data like the other binding forms. */
static void petta_runtime_heads_build(PeTTaSymbolIds *ids) {
    const SymbolId data[] = {
        g_builtin_syms.quote, g_builtin_syms.if_text,
        g_builtin_syms.case_text, g_builtin_syms.let,
        g_builtin_syms.let_star, g_builtin_syms.chain,
        g_builtin_syms.collapse, g_builtin_syms.once,
        g_builtin_syms.hyperpose, g_builtin_syms.sealed_text,
        g_builtin_syms.unify, g_builtin_syms.trace_bang,
        g_builtin_syms.petta_transaction, g_builtin_syms.petta_with_mutex,
        ids->progn, ids->prog1, ids->catch_text, ids->lambda,
        ids->foldall, ids->forall, ids->call, ids->cut,
        ids->translate_predicate, ids->and_then, ids->or_else,
        ids->stream_unique, ids->stream_alpha_unique,
        ids->stream_union, ids->stream_intersection,
        ids->stream_subtraction,
    };
    const SymbolId function[] = {
        g_builtin_syms.eval, g_builtin_syms.reduce,
        g_builtin_syms.superpose, g_builtin_syms.match,
        g_builtin_syms.add_atom, g_builtin_syms.remove_atom,
        g_builtin_syms.map_atom, g_builtin_syms.filter_atom,
        g_builtin_syms.foldl_atom, ids->test,
    };
    for (size_t index = 0u; index < sizeof(data) / sizeof(data[0]);
         index++) {
        petta_runtime_head_note(
            ids, data[index], PETTA_RUNTIME_HEAD_DATA);
    }
    for (size_t index = 0u;
         index < sizeof(function) / sizeof(function[0]); index++) {
        petta_runtime_head_note(
            ids, function[index], PETTA_RUNTIME_HEAD_FUNCTION);
    }
}

static const PeTTaSymbolIds *petta_symbol_ids_refresh(void) {
    uint64_t table_instance_id = symbol_table_instance_id(g_symbols);
    PeTTaSymbolIds ids = {
        .table = g_symbols,
        .table_instance_id = table_instance_id,
    };
    if (g_symbols) {
        ids.true_text = symbol_intern_cstr(g_symbols, "true");
        ids.false_text = symbol_intern_cstr(g_symbols, "false");
        ids.test = symbol_intern_cstr(g_symbols, "test");
        ids.progn = symbol_intern_cstr(g_symbols, "progn");
        ids.prog1 = symbol_intern_cstr(g_symbols, "prog1");
        ids.foldall = symbol_intern_cstr(g_symbols, "foldall");
        ids.forall = symbol_intern_cstr(g_symbols, "forall");
        ids.maplist = symbol_intern_cstr(g_symbols, "maplist");
        ids.foldl = symbol_intern_cstr(g_symbols, "foldl");
        ids.id = symbol_intern_cstr(g_symbols, "id");
        ids.append = symbol_intern_cstr(g_symbols, "append");
        ids.cons = symbol_intern_cstr(g_symbols, "cons");
        ids.int_add = symbol_intern_cstr(g_symbols, "#+");
        ids.stream_unique = symbol_intern_cstr(g_symbols, "unique");
        ids.stream_alpha_unique =
            symbol_intern_cstr(g_symbols, "alpha-unique");
        ids.stream_union = symbol_intern_cstr(g_symbols, "union");
        ids.stream_intersection =
            symbol_intern_cstr(g_symbols, "intersection");
        ids.stream_subtraction =
            symbol_intern_cstr(g_symbols, "subtraction");
        ids.length = symbol_intern_cstr(g_symbols, "length");
        ids.msort = symbol_intern_cstr(g_symbols, "msort");
        ids.sort_atom = symbol_intern_cstr(g_symbols, "sort-atom");
        ids.first_from_pair =
            symbol_intern_cstr(g_symbols, "first-from-pair");
        ids.first = symbol_intern_cstr(g_symbols, "first");
        ids.second_from_pair =
            symbol_intern_cstr(g_symbols, "second-from-pair");
        ids.is_var = symbol_intern_cstr(g_symbols, "is-var");
        ids.is_ground = symbol_intern_cstr(g_symbols, "is-ground");
        ids.is_expr = symbol_intern_cstr(g_symbols, "is-expr");
        ids.is_space = symbol_intern_cstr(g_symbols, "is-space");
        ids.is_member = symbol_intern_cstr(g_symbols, "is-member");
        ids.is_alpha_member =
            symbol_intern_cstr(g_symbols, "is-alpha-member");
        ids.alpha_unique_atom =
            symbol_intern_cstr(g_symbols, "alpha-unique-atom");
        ids.list_to_set =
            symbol_intern_cstr(g_symbols, "list_to_set");
        ids.exclude_item =
            symbol_intern_cstr(g_symbols, "exclude-item");
        ids.repra = symbol_intern_cstr(g_symbols, "repra");
        ids.sread = symbol_intern_cstr(g_symbols, "sread");
        ids.call = symbol_intern_cstr(g_symbols, "call");
        ids.eval = symbol_intern_cstr(g_symbols, "eval");
        ids.predicate =
            symbol_intern_cstr(g_symbols, "Predicate");
        ids.translate_predicate =
            symbol_intern_cstr(g_symbols, "translatePredicate");
        ids.import_prolog_function =
            symbol_intern_cstr(
                g_symbols, "import_prolog_function");
        ids.process_metta_string =
            symbol_intern_cstr(
                g_symbols, "process_metta_string");
        ids.call_predicate =
            symbol_intern_cstr(g_symbols, "callPredicate");
        ids.asserta_predicate =
            symbol_intern_cstr(g_symbols, "assertaPredicate");
        ids.assertz_predicate =
            symbol_intern_cstr(g_symbols, "assertzPredicate");
        ids.retract_predicate =
            symbol_intern_cstr(g_symbols, "retractPredicate");
        ids.tabled = symbol_intern_cstr(g_symbols, "tabled");
        ids.add_translator_rule =
            symbol_intern_cstr(g_symbols, "add-translator-rule!");
        ids.remove_translator_rule =
            symbol_intern_cstr(g_symbols, "remove-translator-rule!");
        ids.cut = symbol_intern_cstr(g_symbols, "cut");
        ids.catch_text = symbol_intern_cstr(g_symbols, "catch");
        ids.lambda = symbol_intern_cstr(g_symbols, "|->");
        ids.canonical_lam = symbol_intern_cstr(g_symbols, "Lam");
        ids.library = symbol_intern_cstr(g_symbols, "library");
        ids.value_let =
            symbol_intern_cstr(g_symbols, "PeTTa.ValueLetV1");
        ids.value_chain =
            symbol_intern_cstr(g_symbols, "PeTTa.ValueChainV1");
        ids.and_then = symbol_intern_cstr(g_symbols, "and-then");
        ids.or_else = symbol_intern_cstr(g_symbols, "or-else");
        ids.mm2_exec = symbol_intern_cstr(g_symbols, "mm2-exec");
        ids.mork_space = symbol_intern_cstr(g_symbols, "&mork");
        petta_runtime_heads_build(&ids);
    }
    ids.form_by_symbol_ready = petta_form_dense_build(&ids);
    ids.cons_shape_facts_ready =
        petta_cons_shape_facts_ready(&ids);
    g_petta_symbol_ids = ids;
    return &g_petta_symbol_ids;
}

/*
 * Keep the table-identity guard inline.  The cold symbol-interning path is
 * deliberately split out so a semantic-form query is only two comparisons
 * after the per-thread table has been initialized.
 */
static inline const PeTTaSymbolIds *petta_symbol_ids(void) {
    uint64_t table_instance_id =
        symbol_table_instance_id(g_symbols);
    if (g_petta_symbol_ids.table == g_symbols &&
        g_petta_symbol_ids.table_instance_id ==
            table_instance_id) {
        return &g_petta_symbol_ids;
    }
    return petta_symbol_ids_refresh();
}

bool petta_semantics_cons_shape_facts(PeTTaConsShapeFacts *facts) {
    if (!facts)
        return false;
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    *facts = (PeTTaConsShapeFacts){
        .symbol_table = ids->table,
        .symbol_table_instance_id = ids->table_instance_id,
        .cons = ids->cons,
    };
    return ids->form_by_symbol_ready
        ? ids->cons_shape_facts_ready
        : petta_cons_shape_facts_ready(ids);
}

bool petta_semantics_cons_shape_facts_current(
    const PeTTaConsShapeFacts *facts) {
    return facts && facts->symbol_table &&
           facts->symbol_table == g_symbols &&
           facts->symbol_table_instance_id != 0u &&
           facts->symbol_table_instance_id ==
               symbol_table_instance_id(g_symbols);
}

PeTTaForm petta_semantics_form(SymbolId head) {
    return petta_form_lookup_ids(petta_symbol_ids(), head);
}

bool petta_semantics_special_form_reads(SymbolId head, CettaExprLen nargs) {
    if (head == g_builtin_syms.if_text)
        return nargs == 2u || nargs == 3u;
    if (head == g_builtin_syms.trace_bang ||
        head == g_builtin_syms.case_text || head == g_builtin_syms.let_star ||
        head == g_builtin_syms.sealed_text)
        return nargs == 2u;
    if (head == g_builtin_syms.let || head == g_builtin_syms.chain)
        return nargs == 3u;
    if (head == g_builtin_syms.collapse || head == g_builtin_syms.once)
        return nargs == 1u;
    return true;
}

PeTTaRuntimeHead petta_semantics_runtime_head(SymbolId head) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    if (head == SYMBOL_ID_NONE || !ids->table)
        return PETTA_RUNTIME_HEAD_ORDINARY;
    if (head < PETTA_FORM_DENSE_CAP)
        return (PeTTaRuntimeHead)ids->runtime_head_by_symbol[head];
    for (uint8_t index = 0u;
         index < ids->runtime_head_overflow_len; index++) {
        if (ids->runtime_head_overflow[index].symbol == head)
            return ids->runtime_head_overflow[index].head;
    }
    return PETTA_RUNTIME_HEAD_ORDINARY;
}

PeTTaNamedArity petta_semantics_named_arity(
    Space *space, Arena *scratch, Atom *head_atom,
    CettaExprLen supplied) {
    PeTTaNamedArity info = {0};
    if (!space || !scratch || !head_atom ||
        head_atom->kind != ATOM_SYMBOL) {
        return info;
    }

    SymbolId head = head_atom->sym_id;
    CettaExprLen minimum = 0u;
    CettaExprLen maximum = 0u;
    bool has_exact = false;
    bool found = space_equation_head_arity_bounds(
        space, head, &minimum, &maximum, &has_exact, supplied);
    info.known = found;
    info.exact = has_exact;
    info.larger = found && maximum > supplied;
    info.smaller = found && minimum < supplied;

    /* A special form's intrinsic arity is the form's.  Once the program
     * defines equations of the form's name, the name has the arities of
     * those equations, as any function the program defines. */
    bool program_names_form =
        found && petta_semantics_runtime_head(head) ==
                     PETTA_RUNTIME_HEAD_DATA;
    CettaExprLen intrinsic = 0u;
    if (!program_names_form &&
        petta_semantics_intrinsic_partial_arity(
            head, &intrinsic)) {
        info.known = true;
        info.exact = info.exact || intrinsic == supplied;
        info.larger = info.larger || intrinsic > supplied;
        info.smaller = info.smaller || intrinsic < supplied;
    }

    Atom **types = NULL;
    uint32_t count = space_get_declared_types(
        space, scratch, head_atom, &types);
    for (uint32_t index = 0u; index < count; index++) {
        Atom *type = types[index];
        if (!type || type->kind != ATOM_EXPR ||
            type->expr.len < 2u ||
            !atom_is_symbol_id(
                type->expr.elems[0], g_builtin_syms.arrow)) {
            continue;
        }
        CettaExprLen arity = type->expr.len - 2u;
        info.known = true;
        info.exact = info.exact || arity == supplied;
        info.larger = info.larger || arity > supplied;
        info.smaller = info.smaller || arity < supplied;
    }
    free(types);
    return info;
}

Atom *petta_semantics_function_overapplication_error(
    Arena *arena, Atom *head,
    const CettaExprLen *known_input_arities,
    size_t known_arity_count, CettaExprLen actual_input_arity) {
    if (!arena || !head ||
        (known_arity_count > 0u && !known_input_arities) ||
        known_arity_count > SIZE_MAX / sizeof(Atom *) ||
        actual_input_arity > (CettaExprLen)INT64_MAX) {
        return NULL;
    }

    Atom **arity_atoms = known_arity_count
        ? arena_alloc(arena, sizeof(*arity_atoms) * known_arity_count)
        : NULL;
    if (known_arity_count > 0u && !arity_atoms)
        return NULL;
    for (size_t index = 0u; index < known_arity_count; index++) {
        if (known_input_arities[index] > (CettaExprLen)INT64_MAX)
            return NULL;
        arity_atoms[index] = atom_int(
            arena, (int64_t)known_input_arities[index]);
        if (!arity_atoms[index])
            return NULL;
    }

    Atom *known = atom_expr(
        arena, arity_atoms, (CettaExprLen)known_arity_count);
    Atom *arity_domain = known
        ? atom_expr3(
              arena, atom_symbol(arena, "function_input_arities"),
              head, known)
        : NULL;
    Atom *domain_error = arity_domain
        ? atom_expr3(
              arena, atom_symbol(arena, "domain_error"),
              arity_domain,
              atom_int(arena, (int64_t)actual_input_arity))
        : NULL;
    return domain_error
        ? atom_error(arena, domain_error, atom_symbol(arena, "none"))
        : NULL;
}

bool petta_semantics_is_cons_constraint(const Atom *atom) {
    return petta_semantics_is_open_cons_value(atom) ||
           (atom && atom->kind == ATOM_EXPR &&
            atom->expr.len == 3u &&
            atom->expr.elems[0]->kind == ATOM_SYMBOL &&
            petta_semantics_form(atom->expr.elems[0]->sym_id) ==
                PETTA_FORM_CONS);
}

bool petta_semantics_is_open_cons_value(const Atom *atom) {
    return atom && atom->kind == ATOM_EXPR &&
           atom->expr.len == 3u &&
           atom_is_internal_tag(
               atom->expr.elems[0],
               CETTA_INTERNAL_TAG_PETTA_OPEN_CONS);
}

void petta_semantics_logical_list_cursor_init(
    PeTTaLogicalListCursor *cursor, Atom *list) {
    if (!cursor)
        return;
    *cursor = (PeTTaLogicalListCursor){
        .rest = list,
    };
}

PeTTaLogicalListStep petta_semantics_logical_list_cursor_next(
    PeTTaLogicalListCursor *cursor, Atom **item) {
    if (item)
        *item = NULL;
    if (!cursor || !item || cursor->invalid)
        return PETTA_LOGICAL_LIST_INVALID;

    if (cursor->in_flat_tail) {
        if (!cursor->rest || cursor->rest->kind != ATOM_EXPR) {
            cursor->invalid = true;
            return PETTA_LOGICAL_LIST_INVALID;
        }
        if (cursor->flat_index < cursor->flat_end) {
            *item = cursor->rest->expr.elems[cursor->flat_index++];
            return PETTA_LOGICAL_LIST_ITEM;
        }
        if (atom_is_list_rest(cursor->rest)) {
            /* [x... | r] continues as r. */
            cursor->rest = cursor->rest->expr.elems[cursor->flat_end];
            cursor->in_flat_tail = false;
            return petta_semantics_logical_list_cursor_next(cursor, item);
        }
        cursor->rest = NULL;
        return PETTA_LOGICAL_LIST_END;
    }

    if (!cursor->rest) {
        cursor->invalid = true;
        return PETTA_LOGICAL_LIST_INVALID;
    }
    if (petta_semantics_is_open_cons_value(cursor->rest)) {
        *item = cursor->rest->expr.elems[1];
        cursor->rest = cursor->rest->expr.elems[2];
        return PETTA_LOGICAL_LIST_ITEM;
    }
    if (cursor->rest->kind != ATOM_EXPR ||
        petta_semantics_is_nonlist_carrier(cursor->rest)) {
        cursor->invalid = true;
        return PETTA_LOGICAL_LIST_INVALID;
    }

    /* An expression is read from its first element, a list or list pattern
     * from the element after its tag. */
    cursor->in_flat_tail = true;
    cursor->flat_index = atom_is_list_form(cursor->rest) ? 1u : 0u;
    cursor->flat_end = atom_is_list_rest(cursor->rest)
        ? cursor->rest->expr.len - 1u : cursor->rest->expr.len;
    return petta_semantics_logical_list_cursor_next(cursor, item);
}

PeTTaListRead petta_semantics_read_list(
    Atom *list, CettaExprLen length, Atom **elements) {
    PeTTaLogicalListCursor cursor;
    petta_semantics_logical_list_cursor_init(&cursor, list);
    for (CettaExprLen index = 0u;; index++) {
        Atom *item = NULL;
        PeTTaLogicalListStep step =
            petta_semantics_logical_list_cursor_next(&cursor, &item);
        if (step == PETTA_LOGICAL_LIST_INVALID)
            return PETTA_LIST_READ_UNDECIDED;
        if (step == PETTA_LOGICAL_LIST_END)
            return index == length
                ? PETTA_LIST_READ_EXACT : PETTA_LIST_READ_OTHER_LENGTH;
        if (index == length)
            return PETTA_LIST_READ_OTHER_LENGTH;
        if (elements)
            elements[index] = item;
    }
}

bool petta_semantics_logical_list_length(
    Atom *list, CettaExprLen *length) {
    if (length)
        *length = 0u;
    if (!list || !length)
        return false;

    PeTTaLogicalListCursor cursor;
    petta_semantics_logical_list_cursor_init(&cursor, list);
    for (;;) {
        Atom *item = NULL;
        PeTTaLogicalListStep step =
            petta_semantics_logical_list_cursor_next(
                &cursor, &item);
        if (step == PETTA_LOGICAL_LIST_END)
            return true;
        if (step == PETTA_LOGICAL_LIST_INVALID ||
            *length == UINT64_MAX) {
            *length = 0u;
            return false;
        }
        (*length)++;
    }
}

/*
 * A logical list is cells, list patterns [x... | r] continued into r, and
 * the atom it ends in.  An expression ends a list of the expression kind,
 * a list value one of the list kind; any other end leaves the list open.
 * Collecting keeps the elements in order and returns that end.
 */
typedef struct {
    Atom **items;
    size_t length;
    size_t capacity;
} PeTTaListItems;

static bool petta_list_items_append(PeTTaListItems *items,
                                    Atom *const *elems, size_t count) {
    if (count > SIZE_MAX / sizeof(Atom *) - items->length)
        return false;
    size_t needed = items->length + count;
    if (needed > items->capacity) {
        size_t next = items->capacity ? items->capacity : 16u;
        while (next < needed) {
            if (next > SIZE_MAX / 2u / sizeof(Atom *))
                return false;
            next *= 2u;
        }
        Atom **grown = realloc(items->items, sizeof(Atom *) * next);
        if (!grown)
            return false;
        items->items = grown;
        items->capacity = next;
    }
    if (count > 0u)
        memcpy(items->items + items->length, elems, sizeof(Atom *) * count);
    items->length = needed;
    return true;
}

/* Collects the elements before the end; `authored` also reads authored
 * (cons Head Tail) cells.  NULL when the list is too long to hold. */
static Atom *petta_logical_list_collect(Atom *list, bool authored,
                                        PeTTaListItems *items) {
    while (list) {
        bool cell = authored
            ? petta_semantics_is_cons_constraint(list)
            : petta_semantics_is_open_cons_value(list);
        if (cell) {
            if (!petta_list_items_append(items, &list->expr.elems[1], 1u))
                return NULL;
            list = list->expr.elems[2];
            continue;
        }
        if (atom_is_list_rest(list)) {
            if (!petta_list_items_append(items, list->expr.elems + 1u,
                                         (size_t)list->expr.len - 2u))
                return NULL;
            list = list->expr.elems[list->expr.len - 1u];
            continue;
        }
        return list;
    }
    return NULL;
}

/* The closed list whose end is `end`, an expression or a list value: the
 * collected elements then the end's, of the end's kind. */
static Atom *petta_logical_list_close(Arena *arena, PeTTaListItems *items,
                                      Atom *end) {
    Atom *const *elems;
    CettaExprLen len;
    if (!petta_semantics_sequence_view(end, &elems, &len) ||
        (uint64_t)len > (uint64_t)SIZE_MAX ||
        !petta_list_items_append(items, elems, (size_t)len) ||
        !cetta_expr_len_fits_size((CettaExprLen)items->length))
        return NULL;
    return atom_is_list(end)
        ? atom_list(arena, items->items, (CettaExprLen)items->length)
        : atom_expr(arena, items->items, (CettaExprLen)items->length);
}

bool petta_semantics_is_closed_list(Atom *atom) {
    for (;;) {
        if (atom && petta_semantics_is_cons_constraint(atom))
            atom = atom->expr.elems[2];
        else if (atom_is_list_rest(atom))
            atom = atom->expr.elems[atom->expr.len - 1u];
        else
            return atom && atom->kind == ATOM_EXPR &&
                   !petta_semantics_is_nonlist_carrier(atom);
    }
}

Atom *petta_semantics_closed_list(Arena *arena, Atom *list) {
    if (!arena || !list)
        return NULL;
    if (!petta_semantics_is_cons_constraint(list) && !atom_is_list_rest(list))
        return list->kind == ATOM_EXPR &&
               !petta_semantics_is_nonlist_carrier(list) ? list : NULL;
    PeTTaListItems items = {0};
    Atom *end = petta_logical_list_collect(list, true, &items);
    Atom *closed = end && end->kind == ATOM_EXPR
        ? petta_logical_list_close(arena, &items, end)
        : NULL;
    free(items.items);
    return closed;
}

static Atom *petta_error_term(Arena *arena, Atom *formal, Atom *context) {
    return formal && context ? atom_error(arena, formal, context) : NULL;
}

static Atom *petta_error_unbound_context(Arena *arena) {
    return atom_var_with_id(arena, "_", fresh_var_id());
}

/* context(Indicator, _), the indicator Name/Arity or Module:Name/Arity. */
static Atom *petta_error_predicate_context(
    Arena *arena, const char *module, const char *name, int64_t arity) {
    Atom *slash = atom_symbol(arena, "/");
    Atom *indicator = slash
        ? atom_expr3(arena, slash, atom_symbol(arena, name),
                     atom_int(arena, arity))
        : NULL;
    if (indicator && module) {
        Atom *colon = atom_symbol(arena, ":");
        indicator = colon
            ? atom_expr3(arena, colon, atom_symbol(arena, module), indicator)
            : NULL;
    }
    Atom *context = atom_symbol(arena, "context");
    Atom *unbound = petta_error_unbound_context(arena);
    return indicator && context && unbound
        ? atom_expr3(arena, context, indicator, unbound) : NULL;
}

Atom *petta_semantics_list_error(Arena *arena, SymbolId operation,
                                 Atom *culprit) {
    if (!arena || !culprit)
        return NULL;
    PeTTaForm form = petta_semantics_form(operation);
    Atom *context = NULL;
    if (form == PETTA_FORM_LENGTH || operation == g_builtin_syms.size_atom)
        context = petta_error_predicate_context(arena, NULL, "length", 2);
    else if (form == PETTA_FORM_MSORT)
        context = petta_error_predicate_context(arena, "system", "msort", 2);
    else if (form == PETTA_FORM_LIST_TO_SET || form == PETTA_FORM_APPEND)
        context = petta_error_unbound_context(arena);
    else
        return NULL;
    Atom *tail = culprit;
    while (petta_semantics_is_cons_constraint(tail))
        tail = tail->expr.elems[2];
    Atom *formal = NULL;
    if (tail->kind == ATOM_VAR) {
        formal = atom_symbol(arena, "instantiation_error");
    } else {
        Atom *type_error = atom_symbol(arena, "type_error");
        Atom *list = atom_symbol(arena, "list");
        formal = type_error && list
            ? atom_expr3(arena, type_error, list, culprit) : NULL;
    }
    return petta_error_term(arena, formal, context);
}

static Atom *petta_static_procedure_error_with_context(
    Arena *arena, const char *module, SymbolId name, int64_t arity,
    const char *predicate, int64_t predicate_arity) {
    if (!arena || name == SYMBOL_ID_NONE || !predicate)
        return NULL;
    Atom *slash = atom_symbol(arena, "/");
    Atom *indicator = slash
        ? atom_expr3(arena, slash, atom_symbol_id(arena, name),
                     atom_int(arena, arity))
        : NULL;
    if (indicator && module)
        indicator = atom_expr3(arena, atom_symbol(arena, ":"),
                              atom_symbol(arena, module), indicator);
    Atom *formal = indicator
        ? atom_expr(arena, (Atom *[]){
              atom_symbol(arena, "permission_error"),
              atom_symbol(arena, "modify"),
              atom_symbol(arena, "static_procedure"), indicator}, 4u)
        : NULL;
    Atom *context = petta_error_predicate_context(
        arena, "system", predicate, predicate_arity);
    return petta_error_term(arena, formal, context);
}

Atom *petta_semantics_static_procedure_error(
    Arena *arena, SymbolId name, int64_t arity, const char *predicate) {
    return petta_static_procedure_error_with_context(
        arena, NULL, name, arity, predicate, 1);
}

Atom *petta_semantics_state_existence_error(Arena *arena, Atom *name) {
    if (!arena || !name)
        return NULL;
    Atom *formal = atom_expr3(
        arena, atom_symbol(arena, "existence_error"),
        atom_symbol(arena, "variable"), name);
    return petta_error_term(
        arena, formal,
        petta_error_predicate_context(arena, "system", "nb_getval", 2));
}

Atom *petta_semantics_instantiation_error(
    Arena *arena, const char *module, const char *name, int64_t arity) {
    if (!arena || !name)
        return NULL;
    return petta_error_term(
        arena, atom_symbol(arena, "instantiation_error"),
        petta_error_predicate_context(arena, module, name, arity));
}

/* The elements of the list `value` spells, in order, whatever its form: a
 * list value, an expression, cons cells, or a list pattern with a rest, each
 * ending in the next.  `end` is where the walk stopped when the list is not
 * proper: a variable (a partial list) or another atom (an improper one). */
typedef enum {
    PETTA_LIST_WALK_PROPER = 0,
    PETTA_LIST_WALK_PARTIAL,
    PETTA_LIST_WALK_IMPROPER,
    PETTA_LIST_WALK_NONE,
    PETTA_LIST_WALK_CAPACITY,
} PeTTaListWalk;

static bool petta_list_walk_push(Arena *arena, Atom ***items, size_t *count,
                                 size_t *capacity, Atom *item) {
    if (*count == *capacity) {
        size_t next = *capacity ? *capacity * 2u : 8u;
        if (next <= *capacity || next > SIZE_MAX / sizeof(Atom *))
            return false;
        Atom **grown = arena_alloc(arena, sizeof(Atom *) * next);
        if (!grown)
            return false;
        if (*count)
            memcpy(grown, *items, sizeof(Atom *) * *count);
        *items = grown;
        *capacity = next;
    }
    (*items)[(*count)++] = item;
    return true;
}

static PeTTaListWalk petta_list_walk(Arena *arena, Atom *value,
                                     Atom ***items, size_t *count,
                                     Atom **end) {
    size_t capacity = 0u;
    bool cells = false;
    *items = NULL;
    *count = 0u;
    *end = NULL;
    Atom *cursor = value;
    for (;;) {
        if (petta_semantics_is_cons_constraint(cursor)) {
            if (!petta_list_walk_push(arena, items, count, &capacity,
                                      cursor->expr.elems[1]))
                return PETTA_LIST_WALK_CAPACITY;
            cursor = cursor->expr.elems[2];
            cells = true;
            continue;
        }
        if (atom_is_list_rest(cursor)) {
            for (CettaExprIndex i = 1u; i + 1u < cursor->expr.len; i++)
                if (!petta_list_walk_push(arena, items, count, &capacity,
                                          cursor->expr.elems[i]))
                    return PETTA_LIST_WALK_CAPACITY;
            cursor = cursor->expr.elems[cursor->expr.len - 1u];
            cells = true;
            continue;
        }
        break;
    }
    Atom *const *elems = NULL;
    CettaExprLen len = 0u;
    if (petta_semantics_sequence_view(cursor, &elems, &len)) {
        for (CettaExprIndex i = 0u; i < len; i++)
            if (!petta_list_walk_push(arena, items, count, &capacity,
                                      elems[i]))
                return PETTA_LIST_WALK_CAPACITY;
        return PETTA_LIST_WALK_PROPER;
    }
    *end = cursor;
    if (cursor->kind == ATOM_VAR)
        return PETTA_LIST_WALK_PARTIAL;
    return cells ? PETTA_LIST_WALK_IMPROPER : PETTA_LIST_WALK_NONE;
}

PeTTaPredicateTerm petta_semantics_predicate_term(
    Arena *arena, Atom *value, Atom **term) {
    if (term)
        *term = NULL;
    if (!arena || !value || !term)
        return PETTA_PREDICATE_TERM_CAPACITY;
    if (petta_semantics_value_contains_observable_open_cons(value)) {
        value = petta_semantics_materialize_value(arena, value);
        if (!value)
            return PETTA_PREDICATE_TERM_CAPACITY;
    }
    Atom **items = NULL;
    size_t count = 0u;
    Atom *end = NULL;
    Atom *formal = NULL;
    switch (petta_list_walk(arena, value, &items, &count, &end)) {
    case PETTA_LIST_WALK_CAPACITY:
        return PETTA_PREDICATE_TERM_CAPACITY;
    case PETTA_LIST_WALK_NONE:
        /* No list, so no [F|Args]: no answer. */
        return PETTA_PREDICATE_TERM_NONE;
    case PETTA_LIST_WALK_PARTIAL:
        /* An unbound value or tail: [F|Args] with F or Args unbound. */
        formal = atom_symbol(arena, "instantiation_error");
        break;
    case PETTA_LIST_WALK_IMPROPER: {
        Atom *type_error = atom_symbol(arena, "type_error");
        Atom *list = atom_symbol(arena, "list");
        formal = type_error && list
            ? atom_expr3(arena, type_error, list, value) : NULL;
        break;
    }
    case PETTA_LIST_WALK_PROPER: {
        if (count == 0u)
            return PETTA_PREDICATE_TERM_NONE;
        Atom *first = items[0];
        if (first->kind == ATOM_VAR) {
            formal = atom_symbol(arena, "instantiation_error");
        } else if (count == 1u) {
            *term = first;
            return PETTA_PREDICATE_TERM_VALUE;
        } else if (first->kind == ATOM_SYMBOL) {
            Atom *body = count <= (size_t)UINT32_MAX
                ? atom_expr(arena, items, (CettaExprLen)count) : NULL;
            *term = body ? atom_petta_prolog_compound(arena, body) : NULL;
            return *term ? PETTA_PREDICATE_TERM_VALUE
                         : PETTA_PREDICATE_TERM_CAPACITY;
        } else {
            Atom *type_error = atom_symbol(arena, "type_error");
            Atom *atom = atom_symbol(arena, "atom");
            formal = type_error && atom
                ? atom_expr3(arena, type_error, atom, first) : NULL;
        }
        break;
    }
    }
    Atom *context = petta_error_predicate_context(arena, "system", "=..", 2);
    *term = petta_error_term(arena, formal, context);
    return *term ? PETTA_PREDICATE_TERM_ERROR
                 : PETTA_PREDICATE_TERM_CAPACITY;
}

bool petta_semantics_parameter_declared_atom(
    Space *space, Arena *arena, Atom *head, CettaExprLen arity,
    CettaExprIndex index) {
    if (!space || !arena || !head || head->kind != ATOM_SYMBOL ||
        index >= arity ||
        !space_head_has_arrow_signature(space, head->sym_id, arity))
        return false;
    Atom **types = NULL;
    uint32_t count = space_get_declared_types(space, arena, head, &types);
    bool declared = false;
    for (uint32_t t = 0u; t < count && !declared; t++) {
        Atom *type = types[t];
        if (!type || type->kind != ATOM_EXPR ||
            type->expr.len != arity + 2u ||
            !atom_is_symbol_id(type->expr.elems[0], g_builtin_syms.arrow))
            continue;
        Atom *domain = type->expr.elems[index + 1u];
        if (domain->kind == ATOM_EXPR && domain->expr.len == 3u &&
            atom_is_symbol_id(domain->expr.elems[0], g_builtin_syms.colon) &&
            domain->expr.elems[1]->kind == ATOM_VAR)
            domain = domain->expr.elems[2];
        declared = atom_is_symbol_id(domain, g_builtin_syms.atom);
    }
    free(types);
    return declared;
}

Atom *petta_semantics_cyclic_term_error(
    Arena *arena, const char *module, const char *name, int64_t arity) {
    if (!arena)
        return NULL;
    Atom *representation_error = atom_symbol(arena, "representation_error");
    Atom *cyclic_term = atom_symbol(arena, "cyclic_term");
    Atom *formal = representation_error && cyclic_term
        ? atom_expr2(arena, representation_error, cyclic_term) : NULL;
    Atom *context = name
        ? petta_error_predicate_context(arena, module, name, arity)
        : petta_error_unbound_context(arena);
    return formal && context ? petta_error_term(arena, formal, context) : NULL;
}

Atom *petta_semantics_syntax_error(Arena *arena, const char *text) {
    if (!arena || !text)
        return NULL;
    static const char prefix[] = "Parse error in form: ";
    size_t length = strlen(text);
    if (length > SIZE_MAX - sizeof(prefix))
        return NULL;
    char *message = arena_alloc(arena, sizeof(prefix) + length);
    if (!message)
        return NULL;
    memcpy(message, prefix, sizeof(prefix) - 1u);
    memcpy(message + sizeof(prefix) - 1u, text, length + 1u);
    Atom *syntax_error = atom_symbol(arena, "syntax_error");
    Atom *formal = syntax_error
        ? atom_expr2(arena, syntax_error, atom_symbol(arena, message))
        : NULL;
    return petta_error_term(arena, formal, atom_symbol(arena, "none"));
}

Atom *petta_semantics_sort_value(Arena *arena, Atom *value, bool total,
                                 bool *type_error) {
    *type_error = false;
    if (!arena || !value)
        return NULL;
    /* A Prolog compound is no list: non_list/1 for sort-atom, a type error
     * for msort. */
    if (petta_semantics_is_nonlist_carrier(value)) {
        if (total)
            return atom_unit(arena);
        *type_error = true;
        return NULL;
    }
    if (petta_semantics_is_cons_constraint(value)) {
        Atom *flat = petta_semantics_closed_list(arena, value);
        if (!flat) {
            *type_error = true;
            return NULL;
        }
        return petta_semantics_msort(arena, flat);
    }
    if (value->kind == ATOM_EXPR)
        return petta_semantics_msort(arena, value);
    if (total && value->kind != ATOM_VAR)
        return atom_unit(arena);
    *type_error = true;
    return NULL;
}

Atom *petta_semantics_materialize_closed_logical_list(
    Arena *arena, Atom *list) {
    if (!arena || !list)
        return NULL;
    if (!petta_semantics_is_open_cons_value(list))
        return list->kind == ATOM_EXPR &&
               !petta_semantics_is_nonlist_carrier(list) ? list : NULL;
    PeTTaListItems items = {0};
    Atom *end = petta_logical_list_collect(list, false, &items);
    Atom *closed = end && end->kind == ATOM_EXPR
        ? petta_logical_list_close(arena, &items, end)
        : NULL;
    free(items.items);
    return closed;
}

Atom *petta_semantics_match_index_pattern(
    Arena *arena, Atom *pattern, bool *exact) {
    if (exact)
        *exact = false;
    if (!arena || !pattern)
        return NULL;
    Atom *query = pattern;
    if (petta_semantics_is_open_cons_value(query))
        query = petta_semantics_materialize_closed_logical_list(arena, query);
    if (query && !atom_structural_may_have_list_carrier(query)) {
        if (exact)
            *exact = true;
        return query;
    }
    /* Partial tails and nested carriers retain their original bindings.
     * Only the index sees this wildcard (MatchIndexProjection). */
    return atom_var_with_literal(arena, "__match_index", fresh_var_id());
}

Atom *petta_semantics_materialize_logical_list(
    Arena *arena, Atom *list) {
    if (!arena || !list)
        return NULL;
    if (!petta_semantics_is_open_cons_value(list))
        return list->kind == ATOM_EXPR &&
               !petta_semantics_is_nonlist_carrier(list) ? list : NULL;

    PeTTaListItems items = {0};
    Atom *end = petta_logical_list_collect(list, false, &items);
    Atom *result = NULL;
    if (end && end->kind == ATOM_EXPR &&
        !petta_semantics_is_nonlist_carrier(end)) {
        result = petta_logical_list_close(arena, &items, end);
    } else if (end) {
        /* An open end, or an improper one such as a Prolog compound, keeps
         * authored (cons Head Tail) syntax. */
        const PeTTaSymbolIds *ids = petta_symbol_ids();
        Atom *cons = ids->cons != SYMBOL_ID_NONE
            ? atom_symbol_id(arena, ids->cons) : NULL;
        result = cons ? end : NULL;
        for (size_t index = items.length; result && index > 0u; index--)
            result = atom_expr3(arena, cons, items.items[index - 1u], result);
    }
    free(items.items);
    return result;
}

Atom *petta_semantics_flatten_closed_open_cons(Arena *arena, Atom *atom) {
    if (!arena || !atom || atom->kind != ATOM_EXPR)
        return atom;
    /* An open cons needs an internal tag. Constructor-derived absence proves
     * this entire subtree unchanged; unknown metadata keeps the exact walk. */
    if (!atom_structural_may_have_internal_tag(atom))
        return atom;
    if (petta_semantics_is_open_cons_value(atom)) {
        Atom *flat =
            petta_semantics_materialize_closed_logical_list(
                arena, atom);
        if (!flat)
            return atom;
        atom = flat;
    }
    Atom **rebuilt = NULL;
    for (CettaExprIndex index = 0u; index < atom->expr.len; index++) {
        Atom *child = petta_semantics_flatten_closed_open_cons(
            arena, atom->expr.elems[index]);
        if (!rebuilt && child != atom->expr.elems[index]) {
            rebuilt = arena_alloc(
                arena, sizeof(Atom *) * atom->expr.len);
            if (!rebuilt)
                return atom;
            for (CettaExprIndex prior = 0u; prior < index; prior++)
                rebuilt[prior] = atom->expr.elems[prior];
        }
        if (rebuilt)
            rebuilt[index] = child;
    }
    return rebuilt
        ? atom_expr(arena, rebuilt, atom->expr.len) : atom;
}

/* Whether an open-cons carrier has been built in this process.  Until one
 * has, no value holds one, so structural matching agrees with list
 * unification on every value (ListCells.subst_eq_iff_unifies).  Carriers
 * built by other threads reach this one's values only through a
 * synchronization that also publishes the flag. */
atomic_bool g_petta_open_cons_built;

Atom *petta_semantics_open_cons_value(
    Arena *arena, Atom *head, Atom *tail) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    if (!arena || !head || !tail || !ids->table)
        return NULL;

    /* The carrier tag is immutable structural data.  Sharing one tag within
     * an arena epoch avoids allocating an identical Atom for every logical
     * list cell.  Arena identity plus reset epoch is the lifetime proof: a
     * reset or a different arena forces a fresh tag before reuse. */
    if (g_petta_symbol_ids.open_cons_tag_arena_id != arena->identity ||
        g_petta_symbol_ids.open_cons_tag_arena_reset_epoch !=
            arena->reset_epoch ||
        !g_petta_symbol_ids.open_cons_tag) {
        g_petta_symbol_ids.open_cons_tag =
            atom_internal_tag(
                arena, CETTA_INTERNAL_TAG_PETTA_OPEN_CONS);
        if (!g_petta_symbol_ids.open_cons_tag)
            return NULL;
        g_petta_symbol_ids.open_cons_tag_arena_id = arena->identity;
        g_petta_symbol_ids.open_cons_tag_arena_reset_epoch =
            arena->reset_epoch;
    }
    if (!atomic_load_explicit(&g_petta_open_cons_built,
                              memory_order_relaxed))
        atomic_store_explicit(&g_petta_open_cons_built, true,
                              memory_order_relaxed);
    return atom_expr3(
        arena, g_petta_symbol_ids.open_cons_tag,
        head, tail);
}

/*
 * A list value or a list pattern with a rest meets a cons as a chain of
 * cells, converted once so that each later tail is a cell of the same
 * spine.  (A plain expression needs no chain: its rest is a suffix view of
 * its storage.)
 */
Atom *petta_semantics_flat_list_spine(
    Arena *arena, Atom *flat_list) {
    if (!arena || !flat_list ||
        flat_list->kind != ATOM_EXPR) {
        return NULL;
    }
    /* A list value's cells end in the empty list and a list pattern's in
     * its rest, so every tail keeps the list's kind; an expression's end
     * in (). */
    CettaExprIndex first = 0u;
    CettaExprIndex end = flat_list->expr.len;
    Atom *tail;
    if (atom_is_list(flat_list)) {
        first = 1u;
        tail = atom_list(arena, NULL, 0u);
    } else if (atom_is_list_rest(flat_list)) {
        first = 1u;
        end = flat_list->expr.len - 1u;
        tail = flat_list->expr.elems[end];
    } else {
        tail = atom_unit(arena);
    }
    if (end == first)
        return flat_list;
    if (!tail)
        return NULL;
    for (CettaExprIndex index = end; index > first; index--) {
        tail = petta_semantics_open_cons_value(
            arena, flat_list->expr.elems[index - 1u], tail);
        if (!tail)
            return NULL;
    }
    return tail;
}

bool petta_semantics_construct_value_allocation_bound(
    CettaExprLen length, size_t *bytes_out) {
    if (!atom_expr_allocation_bound(length, bytes_out))
        return false;
    /* A Cons is one three-place cell, and the first in an arena epoch also
     * allocates its internal carrier tag.  Charging the tag on every
     * three-child construction is conservative independently of the tag
     * cache and of the actual head symbol. */
    if (length == 3u) {
        size_t tag_bytes = (sizeof(Atom) + 7u) & ~(size_t)7u;
        if (*bytes_out > SIZE_MAX - tag_bytes)
            return false;
        *bytes_out += tag_bytes;
    }
    return true;
}

Atom *petta_semantics_construct_value(
    Arena *arena, Atom **elements, CettaExprLen length) {
    if (!arena || (length > 0u && !elements))
        return NULL;
    if (length == 3u && elements[0] &&
        elements[0]->kind == ATOM_SYMBOL &&
        petta_semantics_form(elements[0]->sym_id) == PETTA_FORM_CONS) {
        /* A Cons is a cell sharing its tail, in constant time: a list built
         * by repeated Cons shares every suffix, so copying it anywhere
         * copies only its new cells.  Every reader takes a closed chain of
         * cells as the flat list it spells. */
        return petta_semantics_open_cons_value(
            arena, elements[1], elements[2]);
    }
    return atom_expr(arena, elements, length);
}

bool petta_semantics_construct_value_is_expression(
    const Atom *head, CettaExprLen length) {
    return !(length == 3u && head && head->kind == ATOM_SYMBOL &&
             petta_semantics_form(head->sym_id) == PETTA_FORM_CONS);
}

bool petta_semantics_is_opaque_runtime_value(const Atom *value) {
    if (!value || value->kind != ATOM_EXPR || value->expr.len == 0u)
        return false;
    Atom *body = NULL;
    return petta_semantics_lambda_body(value, &body) ||
           petta_semantics_nullary_lambda_body(value, &body) ||
           petta_semantics_partial_view(value, NULL, NULL);
}

static bool petta_materialize_opaque_value(const Atom *value) {
    if (!value || value->kind != ATOM_EXPR || value->expr.len == 0u)
        return false;
    return petta_semantics_is_opaque_runtime_value(value) ||
           atom_is_symbol_id(value->expr.elems[0], g_builtin_syms.quote);
}

static bool petta_materialize_node_view(Arena *arena, Atom *source,
        Atom **view, bool *complete, void *context) {
    *complete = false;
    *view = petta_semantics_is_open_cons_value(source)
        ? petta_semantics_materialize_logical_list(arena, source) : source;
    if (!*view)
        return false;
    if (petta_materialize_opaque_value(*view)) {
        /* Quoted syntax and closures have a different observation policy.
         * Copy their entire graph without normalizing the enclosed carriers. */
        AtomDeepCopySession **opaque = context;
        if (!*opaque)
            *opaque = atom_deep_copy_session_new(arena);
        *view = *opaque ? atom_deep_copy_session_copy(*opaque, *view) : NULL;
        *complete = true;
    }
    return *view != NULL;
}

Atom *petta_semantics_materialize_value(Arena *arena, Atom *value) {
    if (!arena || !value)
        return NULL;
    if (value->kind != ATOM_EXPR || petta_materialize_opaque_value(value))
        return atom_deep_copy(arena, value);
    ArenaMark mark = arena_mark(arena);
    size_t spare_before = arena->spare_bytes;
    /* Opaque syntax shares a copy context of its own: a previously normalized
     * image must never replace the same source inside a quote or closure. */
    AtomDeepCopySession *opaque = NULL;
    Atom *out = atom_deep_copy_observed(arena, value,
        petta_materialize_node_view, &opaque);
    atom_deep_copy_session_free(opaque);
    if (!out) {
        arena_reset(arena, mark);
        arena_release_spare(arena, spare_before);
    }
    return out;
}

typedef struct {
    BindingValue left;
    BindingValue right;
} PeTTaConsMatchPair;

typedef struct {
    const Atom *pattern;
    const Atom *value;
} PeTTaConsShapePair;

static bool petta_cons_match_pair_push(
    PeTTaConsMatchPair **pairs, size_t *length, size_t *capacity,
    BindingValue left, BindingValue right) {
    if (*length == *capacity) {
        size_t next = *capacity ? *capacity * 2u : 32u;
        if (next <= *capacity ||
            next > SIZE_MAX / sizeof(**pairs)) {
            return false;
        }
        *pairs = *pairs
            ? cetta_realloc(*pairs, sizeof(**pairs) * next)
            : cetta_malloc(sizeof(**pairs) * next);
        *capacity = next;
    }
    (*pairs)[(*length)++] =
        (PeTTaConsMatchPair){left, right};
    return true;
}

static bool petta_cons_shape_pair_push(
    PeTTaConsShapePair **pairs, size_t *length, size_t *capacity,
    const Atom *pattern, const Atom *value) {
    if (*length == *capacity) {
        size_t next = *capacity ? *capacity * 2u : 32u;
        if (next <= *capacity ||
            next > SIZE_MAX / sizeof(**pairs)) {
            return false;
        }
        *pairs = *pairs
            ? cetta_realloc(*pairs, sizeof(**pairs) * next)
            : cetta_malloc(sizeof(**pairs) * next);
        *capacity = next;
    }
    (*pairs)[(*length)++] =
        (PeTTaConsShapePair){
            .pattern = pattern,
            .value = value,
        };
    return true;
}

bool petta_semantics_cons_pattern_may_match(
    const Atom *pattern, const Atom *value) {
    if (!pattern || !value)
        return true;
    PeTTaConsShapeFacts facts;
    if (!petta_semantics_cons_shape_facts(&facts))
        return true;

    PeTTaConsShapePair *pairs = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
    if (!petta_cons_shape_pair_push(
            &pairs, &length, &capacity, pattern, value)) {
        return true;
    }

    while (length > 0u) {
        PeTTaConsShapePair pair = pairs[--length];
        pattern = pair.pattern;
        value = pair.value;
        if (!pattern || !value ||
            pattern->kind == ATOM_VAR ||
            value->kind == ATOM_VAR) {
            continue;
        }

        if (petta_semantics_facts_is_cons_constraint(
                &facts, pattern)) {
            if (petta_semantics_facts_is_cons_constraint(
                    &facts, value))
                continue;
            if (value->kind == ATOM_EXPR &&
                value->expr.len > 0u) {
                continue;
            }
            free(pairs);
            return !petta_semantics_cons_shape_facts_current(&facts);
        }

        /*
         * Descend only through aligned expression positions.  A shape or
         * arity mismatch outside a cons constraint may be handled by
         * relational heads or over-application, so it is deliberately
         * classified as unknown rather than impossible.
         */
        if (pattern->kind != ATOM_EXPR ||
            value->kind != ATOM_EXPR ||
            pattern->expr.len != value->expr.len) {
            continue;
        }
        for (CettaExprIndex index = pattern->expr.len;
             index > 0u; index--) {
            CettaExprIndex child = index - 1u;
                if (!petta_cons_shape_pair_push(
                        &pairs, &length, &capacity,
                        pattern->expr.elems[child],
                        value->expr.elems[child])) {
                free(pairs);
                return true;
            }
        }
    }

    free(pairs);
    return true;
}

/* Field zero can itself be an alias. Observe its root while retaining the
 * independent lexical context of each list's borrowed children. */
static bool petta_semantics_match_value_cons(
    const Bindings *bindings, BindingValue value, bool authored_cons, bool *cons_out) {
    *cons_out = false;
    if (value.skeleton->kind != ATOM_EXPR || value.skeleton->expr.len != 3u)
        return true;
    value.skeleton = value.skeleton->expr.elems[0];
    BindingValue head;
    if (!bindings_resolve_value_preview((Bindings *)bindings, value, &head))
        return false;
    *cons_out = atom_is_internal_tag(
                    head.skeleton, CETTA_INTERNAL_TAG_PETTA_OPEN_CONS) ||
                (authored_cons && head.skeleton->kind == ATOM_SYMBOL &&
                 petta_semantics_form(head.skeleton->sym_id) == PETTA_FORM_CONS);
    return true;
}

/* Whether cells, read through the list patterns they pass, end in an
 * expression that is no list: a list of the expression kind, which a list
 * pattern never opens.  An end still unknown is not one. */
static bool petta_cons_cells_end_in_expression(Atom *cells) {
    for (;;) {
        if (cells && petta_semantics_is_cons_constraint(cells))
            cells = cells->expr.elems[2];
        else if (atom_is_list_rest(cells))
            cells = cells->expr.elems[cells->expr.len - 1u];
        else
            return cells && cells->kind == ATOM_EXPR && !atom_is_list(cells);
    }
}

static bool petta_semantics_match_cons_constraint_mode(
    Arena *arena, Atom *constraint, Atom *value,
    BindingsBuilder *builder, bool authored_cons, uint32_t source_epoch) {
    if (!arena || !constraint || !value || !builder)
        return false;
    uint32_t entry_mark = bindings_builder_save(builder);
    PeTTaConsMatchPair *pairs = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
    CettaTermGraphAssumptions assumed = {0};
    if (!petta_cons_match_pair_push(
            &pairs, &length, &capacity,
            source_epoch ? binding_value_from_context(constraint, source_epoch)
                         : binding_value_from_atom(constraint),
            binding_value_from_atom(value))) {
        return false;
    }

    while (length > 0u) {
        PeTTaConsMatchPair pair = pairs[--length];
        const Bindings *current =
            bindings_builder_bindings(builder);
        BindingValue left_value, right_value;
        if (!bindings_resolve_value_preview((Bindings *)current, pair.left, &left_value) ||
            !bindings_resolve_value_preview((Bindings *)current, pair.right, &right_value))
            goto fail;
        Atom *left = left_value.skeleton;
        Atom *right = right_value.skeleton;
        bool left_cons = false;
        bool right_cons = false;
        if (!petta_semantics_match_value_cons(
                current, left_value, authored_cons, &left_cons) ||
            !petta_semantics_match_value_cons(
                current, right_value, authored_cons, &right_cons)) {
            goto fail;
        }
        /*
         * An open PeTTa list is represented by a variable whose binding is a
         * cons spine.  Bind that variable to the spine before attempting
         * structural decomposition; otherwise the first relational
         * `member/2` clause could inspect closed lists but could never
         * construct an open one.
         */
        if (left->kind == ATOM_VAR || right->kind == ATOM_VAR) {
            /* A later pair may revisit either variable. Reject a cycle
             * before its substitution can be expanded by that next pair. */
            if (!match_binding_values_builder(left_value, right_value, builder, arena)) {
                goto fail;
            }
            continue;
        }
        /*
         * A rational term's node meets the other side as its term one level
         * open.  Two nodes met again are a pair being unified already, which
         * holds by the assumption made when it was first met, as in
         * unification of rational trees: a failure anywhere fails the whole
         * unification, assumption and all.
         */
        if (atom_is_rational_value(left) || atom_is_rational_value(right)) {
            if (atom_is_rational_value(left) &&
                atom_is_rational_value(right)) {
                bool fresh = false;
                const void *left_key = term_graph_assumption_key(
                    &assumed, left,
                    ((uint64_t)left_value.kind << 32) | left_value.epoch);
                const void *right_key = term_graph_assumption_key(
                    &assumed, right,
                    ((uint64_t)right_value.kind << 32) | right_value.epoch);
                if (!left_key || !right_key ||
                    !term_graph_assume(&assumed, left_key, right_key,
                                       &fresh))
                    goto fail;
                if (!fresh)
                    continue;
            }
            BindingValue open_left = left_value;
            BindingValue open_right = right_value;
            open_left.skeleton = atom_rational_open(arena, left);
            open_right.skeleton = atom_rational_open(arena, right);
            if (!open_left.skeleton || !open_right.skeleton ||
                !petta_cons_match_pair_push(
                    &pairs, &length, &capacity, open_left, open_right))
                goto fail;
            continue;
        }
        if (!atom_petta_decomposition_compatible(left, right))
            goto fail;
        /* Two expressions of one kind meet element by element; a list and a
         * list pattern, or a list and an expression, meet as the general
         * matcher decides. */
        if (!left_cons && !right_cons &&
            left->kind == ATOM_EXPR &&
            right->kind == ATOM_EXPR &&
            left->expr.len == right->expr.len &&
            atom_is_list(left) == atom_is_list(right) &&
            atom_is_list_rest(left) == atom_is_list_rest(right)) {
            for (CettaExprIndex index = left->expr.len;
                 index > 0u; index--) {
                CettaExprIndex child = index - 1u;
                BindingValue left_child = left_value, right_child = right_value;
                left_child.skeleton = left->expr.elems[child];
                right_child.skeleton = right->expr.elems[child];
                if (!petta_cons_match_pair_push(
                        &pairs, &length, &capacity, left_child, right_child)) {
                    goto fail;
                }
            }
            continue;
        }
        if (!left_cons && !right_cons) {
            if (!match_binding_values_builder(left_value, right_value, builder, arena)) {
                goto fail;
            }
            continue;
        }

        /*
         * A cons against a non-empty flat list meets the list's first
         * element and its rest, a suffix view sharing the list's storage
         * (ListCells.unifies_cell_flat_rest).  The rest stays the flat list
         * it is, so it meets flat patterns later as one, and a walk down the
         * list allocates no element array.  A list value or a list pattern
         * keeps its kind in its tails: it meets the cons as its chain of
         * cells, ending in the empty list or in its rest.
         */
        if (left_cons && !right_cons &&
            (atom_is_list(right) || atom_is_list_rest(right))) {
            if (atom_is_list_rest(right) &&
                petta_cons_cells_end_in_expression(left))
                goto fail;
            right = petta_semantics_flat_list_spine(arena, right);
            if (!right || !petta_semantics_is_open_cons_value(right))
                goto fail;
            right_cons = true;
        } else if (right_cons && !left_cons &&
                   (atom_is_list(left) || atom_is_list_rest(left))) {
            if (atom_is_list_rest(left) &&
                petta_cons_cells_end_in_expression(right))
                goto fail;
            left = petta_semantics_flat_list_spine(arena, left);
            if (!left || !petta_semantics_is_open_cons_value(left))
                goto fail;
            left_cons = true;
        }

        Atom *left_head = NULL;
        Atom *left_tail = NULL;
        Atom *right_head = NULL;
        Atom *right_tail = NULL;
        if (left_cons) {
            left_head = left->expr.elems[1];
            left_tail = left->expr.elems[2];
        } else if (left->kind == ATOM_EXPR && left->expr.len > 0u) {
            left_head = left->expr.elems[0];
            left_tail = atom_expr_suffix(arena, left, 1u);
            if (!left_tail)
                goto fail;
        } else {
            goto fail;
        }
        if (right_cons) {
            right_head = right->expr.elems[1];
            right_tail = right->expr.elems[2];
        } else if (right->kind == ATOM_EXPR && right->expr.len > 0u) {
            right_head = right->expr.elems[0];
            right_tail = atom_expr_suffix(arena, right, 1u);
            if (!right_tail)
                goto fail;
        } else {
            goto fail;
        }
        if (!petta_cons_match_pair_push(
                &pairs, &length, &capacity,
                (BindingValue){
                    .skeleton = left_tail,
                    .epoch = left_value.epoch,
                    .kind = left_value.kind,
                },
                (BindingValue){
                    .skeleton = right_tail,
                    .epoch = right_value.epoch,
                    .kind = right_value.kind,
                }) ||
            !petta_cons_match_pair_push(
                &pairs, &length, &capacity,
                (BindingValue){
                    .skeleton = left_head,
                    .epoch = left_value.epoch,
                    .kind = left_value.kind,
                },
                (BindingValue){
                    .skeleton = right_head,
                    .epoch = right_value.epoch,
                    .kind = right_value.kind,
                })) {
            goto fail;
        }
    }

    free(pairs);
    if (assumed.slots || assumed.carriers)
        term_graph_assumptions_free(&assumed);
    return true;

fail:
    free(pairs);
    if (assumed.slots || assumed.carriers)
        term_graph_assumptions_free(&assumed);
    bindings_builder_rollback(builder, entry_mark);
    return false;
}

bool petta_semantics_match_cons_constraint(
    Arena *arena, Atom *constraint, Atom *value,
    BindingsBuilder *builder) {
    return petta_semantics_match_cons_constraint_mode(
        arena, constraint, value, builder, true, 0u);
}

bool petta_semantics_match_lowered_head(
    Arena *arena, Atom *head, Atom *value, BindingsBuilder *builder) {
    return petta_semantics_match_cons_constraint_mode(
        arena, head, value, builder, false, 0u);
}

bool petta_semantics_match_lowered_head_epoch(
    Arena *arena, Atom *head, Atom *value,
    BindingsBuilder *builder, uint32_t epoch) {
    return petta_semantics_match_cons_constraint_mode(
        arena, head, value, builder, false, epoch);
}

typedef struct {
    Atom *atom;
} PeTTaConsWalkItem;

static bool petta_semantics_contains_cons_walk(
    const Atom *root, bool observable_open_only) {
    if (!root)
        return false;
    if (observable_open_only &&
        !atom_structural_may_have_list_carrier(root)) {
        return false;
    }
    bool root_matches = observable_open_only
        ? petta_semantics_is_open_cons_value(root)
        : petta_semantics_is_cons_constraint(root);
    if (root_matches)
        return true;
    if (root->kind != ATOM_EXPR ||
        (observable_open_only &&
         petta_materialize_opaque_value(root))) {
        return false;
    }
    /* Small values walk on the stack; a larger one grows onto the heap. */
    PeTTaConsWalkItem inline_stack[32];
    PeTTaConsWalkItem *stack = inline_stack;
    size_t length = 0u;
    size_t capacity = 32u;
    bool found = false;
#define PETTA_CONS_WALK_PUSH(pushed) do { \
    if (length == capacity) { \
        size_t next = capacity * 2u; \
        if (next <= capacity || next > SIZE_MAX / sizeof(*stack)) { \
            /* Unknown must take the conservative cons-capable route. */ \
            found = true; \
            goto done; \
        } \
        PeTTaConsWalkItem *grown = stack == inline_stack \
            ? cetta_malloc(sizeof(*stack) * next) \
            : cetta_realloc(stack, sizeof(*stack) * next); \
        if (stack == inline_stack) \
            memcpy(grown, inline_stack, sizeof(*stack) * length); \
        stack = grown; \
        capacity = next; \
    } \
    stack[length++] = (PeTTaConsWalkItem){ .atom = (pushed) }; \
} while (0)
    for (CettaExprIndex index = root->expr.len; index > 0u; index--)
        PETTA_CONS_WALK_PUSH(root->expr.elems[index - 1u]);
    while (length > 0u) {
        Atom *atom = stack[--length].atom;
        bool matches = observable_open_only
            ? petta_semantics_is_open_cons_value(atom)
            : petta_semantics_is_cons_constraint(atom);
        if (matches) {
            found = true;
            goto done;
        }
        if (!atom || atom->kind != ATOM_EXPR ||
            (observable_open_only &&
             (!atom_structural_may_have_list_carrier(atom) ||
              petta_materialize_opaque_value(atom)))) {
            continue;
        }
        for (CettaExprIndex index = atom->expr.len; index > 0u; index--)
            PETTA_CONS_WALK_PUSH(atom->expr.elems[index - 1u]);
    }
done:
#undef PETTA_CONS_WALK_PUSH
    if (stack != inline_stack)
        free(stack);
    return found;
}

bool petta_semantics_value_contains_observable_open_cons(
    const Atom *value) {
    return petta_semantics_contains_cons_walk(value, true);
}

bool petta_semantics_contains_cons_constraint(const Atom *root) {
    return petta_semantics_contains_cons_walk(root, false);
}

static uint32_t petta_unique_capacity(CettaExprLen len) {
    if (len > UINT32_MAX / 2u)
        return 0u;
    uint32_t needed = len == 0u ? 1u : (uint32_t)len * 2u;
    uint32_t capacity = 1u;
    while (capacity < needed) {
        if (capacity > UINT32_MAX / 2u)
            return 0u;
        capacity <<= 1u;
    }
    return capacity;
}

Atom *petta_semantics_alpha_unique(Arena *arena, Atom *list) {
    if (!arena || !list || list->kind != ATOM_EXPR ||
        !cetta_expr_len_fits_u32(list->expr.len)) {
        return NULL;
    }

    uint32_t capacity =
        petta_unique_capacity(list->expr.len);
    if (capacity == 0u)
        return NULL;

    Arena scratch;
    arena_init(&scratch);
    arena_set_hashcons(&scratch, NULL);
    Atom **slots = arena_alloc(
        &scratch, sizeof(*slots) * (size_t)capacity);
    memset(slots, 0, sizeof(*slots) * (size_t)capacity);

    Atom **unique = arena_alloc(
        arena, sizeof(*unique) * (size_t)list->expr.len);
    CettaExprLen unique_len = 0u;
    uint32_t mask = capacity - 1u;

    for (CettaExprIndex index = 0u;
         index < list->expr.len; index++) {
        Atom *candidate = list->expr.elems[index];
        Atom *key = term_universe_alpha_canonicalize_atom(
            &scratch, candidate);
        if (!key) {
            arena_free(&scratch);
            return NULL;
        }

        uint32_t slot = atom_hash(key) & mask;
        while (slots[slot] && !atom_eq(slots[slot], key))
            slot = (slot + 1u) & mask;
        if (slots[slot])
            continue;

        slots[slot] = key;
        unique[unique_len++] = candidate;
    }

    Atom *result = atom_expr(arena, unique, unique_len);
    arena_free(&scratch);
    return result;
}

Atom *petta_semantics_list_to_set(Arena *arena, Atom *list) {
    if (!arena || !list || list->kind != ATOM_EXPR ||
        !cetta_expr_len_fits_u32(list->expr.len)) {
        return NULL;
    }

    uint32_t capacity = petta_unique_capacity(list->expr.len);
    if (capacity == 0u)
        return NULL;
    Atom **slots = arena_alloc(
        arena, sizeof(*slots) * (size_t)capacity);
    Atom **unique = arena_alloc(
        arena, sizeof(*unique) * (size_t)list->expr.len);
    if (!slots || (list->expr.len > 0u && !unique))
        return NULL;
    memset(slots, 0, sizeof(*slots) * (size_t)capacity);

    CettaExprLen unique_len = 0u;
    uint32_t mask = capacity - 1u;
    for (CettaExprIndex index = 0u;
         index < list->expr.len; index++) {
        Atom *candidate = list->expr.elems[index];
        uint32_t slot = atom_hash(candidate) & mask;
        while (slots[slot] &&
               !atom_eq(slots[slot], candidate)) {
            slot = (slot + 1u) & mask;
        }
        if (slots[slot])
            continue;
        slots[slot] = candidate;
        unique[unique_len++] = candidate;
    }
    return atom_expr(arena, unique, unique_len);
}

Atom *petta_semantics_exclude_item(
    Arena *arena, Atom *item, Atom *list) {
    if (!arena || !item || !list ||
        list->kind != ATOM_EXPR) {
        return NULL;
    }
    Atom **retained = list->expr.len
        ? arena_alloc(
              arena,
              sizeof(*retained) * (size_t)list->expr.len)
        : NULL;
    if (list->expr.len > 0u && !retained)
        return NULL;
    CettaExprLen retained_len = 0u;
    for (CettaExprIndex index = 0u;
         index < list->expr.len; index++) {
        Atom *candidate = list->expr.elems[index];
        if (!atom_eq(item, candidate))
            retained[retained_len++] = candidate;
    }
    return atom_expr(arena, retained, retained_len);
}

bool petta_semantics_boolean_relation_arity(
    SymbolId head, uint32_t *arity) {
    uint32_t result = 0u;
    if (head == g_builtin_syms.op_not) {
        result = 1u;
    } else if (head == g_builtin_syms.op_and ||
               head == g_builtin_syms.op_or ||
               head == g_builtin_syms.op_xor) {
        result = 2u;
    } else {
        return false;
    }
    if (arity)
        *arity = result;
    return true;
}

bool petta_semantics_intrinsic_partial_arity(
    SymbolId head, CettaExprLen *arity) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    bool binary =
        head == g_builtin_syms.op_plus ||
        head == g_builtin_syms.op_minus ||
        head == g_builtin_syms.op_mul ||
        head == g_builtin_syms.op_div ||
        head == g_builtin_syms.op_floor_div ||
        head == g_builtin_syms.op_mod ||
        head == g_builtin_syms.op_lt ||
        head == g_builtin_syms.op_gt ||
        head == g_builtin_syms.op_le ||
        head == g_builtin_syms.op_ge ||
        head == g_builtin_syms.op_eq ||
        head == g_builtin_syms.alpha_eq ||
        head == g_builtin_syms.equals ||
        head == ids->int_add;
    if (binary) {
        if (arity)
            *arity = 2u;
        return true;
    }
    if (head == g_builtin_syms.op_not ||
        head == ids->id ||
        head == ids->length ||
        head == ids->list_to_set ||
        head == ids->repra ||
        head == ids->sread ||
        head == ids->predicate ||
        head == ids->import_prolog_function ||
        head == ids->process_metta_string ||
        head == ids->call_predicate ||
        head == ids->asserta_predicate ||
        head == ids->assertz_predicate ||
        head == ids->retract_predicate) {
        if (arity)
            *arity = 1u;
        return true;
    }
    if (head == ids->exclude_item) {
        if (arity)
            *arity = 2u;
        return true;
    }
    if (head == ids->foldl) {
        if (arity)
            *arity = 3u;
        return true;
    }
    /* SWI-PeTTa registers `let` and `let*` as functions (and `chain` not),
     * so either one written at an arity its translator does not read is a
     * partial. */
    if (head == g_builtin_syms.let) {
        if (arity)
            *arity = 3u;
        return true;
    }
    if (head == g_builtin_syms.let_star) {
        if (arity)
            *arity = 2u;
        return true;
    }
    return false;
}

/* The functions SWI-PeTTa's prelude registers (fun/1, register_fun/1 in
 * metta.pl), each with the input arities its registration records
 * (arity/2): those of the predicates of that name when the prelude loads,
 * less the result argument, in the configuration its run script uses, with
 * the MORK bridge loaded (which gives mm2-exec its arity).  A name
 * registered with none recorded, such as sqrt, is partial at every
 * application (RegisteredArity.reference). */
#define PETTA_ARITY(n) ((uint16_t)(1u << (n)))
static const struct {
    const char *name;
    uint16_t arities;
} petta_registered_builtins[] = {
    {"superpose", PETTA_ARITY(1)},
    {"empty", PETTA_ARITY(0)},
    {"let", 0u},
    {"let*", 0u},
    {"+", PETTA_ARITY(2)},
    {"-", PETTA_ARITY(2)},
    {"*", PETTA_ARITY(2)},
    {"/", PETTA_ARITY(2) | PETTA_ARITY(3) | PETTA_ARITY(4) | PETTA_ARITY(5) | PETTA_ARITY(6) | PETTA_ARITY(7) | PETTA_ARITY(8)},
    {"%", PETTA_ARITY(2)},
    {"min", PETTA_ARITY(2)},
    {"max", PETTA_ARITY(2)},
    {"change-state!", PETTA_ARITY(2)},
    {"get-state", PETTA_ARITY(1)},
    {"bind!", PETTA_ARITY(2)},
    {"<", PETTA_ARITY(2)},
    {">", PETTA_ARITY(2)},
    {"==", PETTA_ARITY(2)},
    {"!=", PETTA_ARITY(2)},
    {"=", PETTA_ARITY(2)},
    {"=?", PETTA_ARITY(2)},
    {"<=", PETTA_ARITY(2)},
    {">=", PETTA_ARITY(2)},
    {"and", PETTA_ARITY(2)},
    {"or", PETTA_ARITY(2)},
    {"xor", PETTA_ARITY(2)},
    {"implies", PETTA_ARITY(2)},
    {"not", PETTA_ARITY(0) | PETTA_ARITY(1)},
    {"sqrt", 0u},
    {"exp", PETTA_ARITY(1)},
    {"log", 0u},
    {"cos", 0u},
    {"sin", 0u},
    {"first-from-pair", PETTA_ARITY(1)},
    {"second-from-pair", PETTA_ARITY(1)},
    {"car-atom", PETTA_ARITY(1)},
    {"cdr-atom", PETTA_ARITY(1)},
    {"unique-atom", PETTA_ARITY(1)},
    {"alpha-unique-atom", PETTA_ARITY(1)},
    {"repr", PETTA_ARITY(1)},
    {"repra", PETTA_ARITY(1)},
    {"parse", PETTA_ARITY(1)},
    {"println!", PETTA_ARITY(1)},
    {"readln!", PETTA_ARITY(0)},
    {"test", PETTA_ARITY(2)},
    {"assert", PETTA_ARITY(0) | PETTA_ARITY(1)},
    {"mm2-exec", PETTA_ARITY(2)},
    {"atom_concat", PETTA_ARITY(2)},
    {"atom_chars", PETTA_ARITY(1)},
    {"copy_term", PETTA_ARITY(1) | PETTA_ARITY(2) | PETTA_ARITY(3)},
    {"term_hash", PETTA_ARITY(1) | PETTA_ARITY(3)},
    {"foldl", PETTA_ARITY(3) | PETTA_ARITY(4) | PETTA_ARITY(5) | PETTA_ARITY(6)},
    {"first", PETTA_ARITY(1)},
    {"last", PETTA_ARITY(1)},
    {"append", PETTA_ARITY(0) | PETTA_ARITY(1) | PETTA_ARITY(2)},
    {"length", PETTA_ARITY(1)},
    {"size-atom", PETTA_ARITY(1)},
    {"sort", PETTA_ARITY(1) | PETTA_ARITY(3)},
    {"msort", PETTA_ARITY(1)},
    {"member", PETTA_ARITY(1) | PETTA_ARITY(2)},
    {"is-member", PETTA_ARITY(2)},
    {"is-alpha-member", PETTA_ARITY(2)},
    {"exclude-item", PETTA_ARITY(2)},
    {"list_to_set", PETTA_ARITY(1)},
    {"maplist", PETTA_ARITY(1) | PETTA_ARITY(2) | PETTA_ARITY(3) | PETTA_ARITY(4)},
    {"eval", PETTA_ARITY(1) | PETTA_ARITY(2)},
    {"reduce", PETTA_ARITY(1)},
    {"import!", PETTA_ARITY(2)},
    {"add-atom", PETTA_ARITY(2)},
    {"remove-atom", PETTA_ARITY(2)},
    {"get-atoms", PETTA_ARITY(1)},
    {"match", PETTA_ARITY(3)},
    {"is-var", PETTA_ARITY(1)},
    {"is-ground", PETTA_ARITY(1)},
    {"is-expr", PETTA_ARITY(1)},
    {"is-space", PETTA_ARITY(1)},
    {"get-mettatype", 0u},
    {"decons", PETTA_ARITY(1)},
    {"decons-atom", PETTA_ARITY(1)},
    {"py-call", PETTA_ARITY(1) | PETTA_ARITY(2)},
    {"get-type", PETTA_ARITY(1)},
    {"get-metatype", PETTA_ARITY(1)},
    {"=alpha", PETTA_ARITY(2)},
    {"concat", 0u},
    {"sread", PETTA_ARITY(1)},
    {"cons", PETTA_ARITY(2)},
    {"reverse", PETTA_ARITY(1)},
    {"#+", PETTA_ARITY(2)},
    {"#-", PETTA_ARITY(2)},
    {"#*", PETTA_ARITY(2)},
    {"#div", PETTA_ARITY(2)},
    {"#//", PETTA_ARITY(2)},
    {"#mod", PETTA_ARITY(2)},
    {"#min", PETTA_ARITY(2)},
    {"#max", PETTA_ARITY(2)},
    {"#<", PETTA_ARITY(2)},
    {"#>", PETTA_ARITY(2)},
    {"#=", PETTA_ARITY(2)},
    {"#\\=", PETTA_ARITY(2)},
    {"set_hook", 0u},
    {"union-atom", PETTA_ARITY(2)},
    {"cons-atom", PETTA_ARITY(2)},
    {"intersection-atom", PETTA_ARITY(2)},
    {"subtraction-atom", PETTA_ARITY(2)},
    {"index-atom", PETTA_ARITY(2)},
    {"id", PETTA_ARITY(1)},
    {"pow-math", PETTA_ARITY(2)},
    {"sqrt-math", PETTA_ARITY(1)},
    {"sort-atom", PETTA_ARITY(1)},
    {"abs-math", PETTA_ARITY(1)},
    {"log-math", PETTA_ARITY(2)},
    {"trunc-math", PETTA_ARITY(1)},
    {"ceil-math", PETTA_ARITY(1)},
    {"floor-math", PETTA_ARITY(1)},
    {"round-math", PETTA_ARITY(1)},
    {"sin-math", PETTA_ARITY(1)},
    {"cos-math", PETTA_ARITY(1)},
    {"tan-math", PETTA_ARITY(1)},
    {"asin-math", PETTA_ARITY(1)},
    {"random-int", PETTA_ARITY(2) | PETTA_ARITY(3)},
    {"random-float", PETTA_ARITY(2) | PETTA_ARITY(3)},
    {"acos-math", PETTA_ARITY(1)},
    {"atan-math", PETTA_ARITY(1)},
    {"isnan-math", PETTA_ARITY(1)},
    {"isinf-math", PETTA_ARITY(1)},
    {"min-atom", PETTA_ARITY(1)},
    {"max-atom", PETTA_ARITY(1)},
    {"foldl-atom", PETTA_ARITY(3)},
    {"map-atom", PETTA_ARITY(2)},
    {"filter-atom", PETTA_ARITY(2)},
    {"current-time", PETTA_ARITY(0)},
    {"format-time", PETTA_ARITY(1)},
    {"library", PETTA_ARITY(1) | PETTA_ARITY(2)},
    {"exists_file", PETTA_ARITY(0)},
    {"import_prolog_function", PETTA_ARITY(1)},
    {"Predicate", PETTA_ARITY(1)},
    {"callPredicate", PETTA_ARITY(1)},
    {"assertaPredicate", PETTA_ARITY(1)},
    {"assertzPredicate", PETTA_ARITY(1)},
    {"retractPredicate", PETTA_ARITY(1)},
    {"add-translator-rule!", PETTA_ARITY(1)},
    {"remove-translator-rule!", PETTA_ARITY(1)},
    {"argv", PETTA_ARITY(1)},
};
#undef PETTA_ARITY

enum {
    PETTA_REGISTERED_BUILTIN_COUNT =
        sizeof(petta_registered_builtins) /
        sizeof(petta_registered_builtins[0]),
};

typedef struct {
    SymbolId id;
    uint16_t arities;
} PeTTaRegisteredBuiltin;

typedef struct {
    const SymbolTable *table;
    uint64_t table_instance_id;
    size_t len;
    PeTTaRegisteredBuiltin entries[PETTA_REGISTERED_BUILTIN_COUNT];
} PeTTaRegisteredBuiltins;

static _Thread_local PeTTaRegisteredBuiltins g_petta_registered_builtins;

static int petta_registered_builtin_compare(const void *left,
                                            const void *right) {
    SymbolId a = ((const PeTTaRegisteredBuiltin *)left)->id;
    SymbolId b = ((const PeTTaRegisteredBuiltin *)right)->id;
    return (a > b) - (a < b);
}

bool petta_semantics_registered_builtin_arities(SymbolId symbol,
                                                uint16_t *arities) {
    PeTTaRegisteredBuiltins *set = &g_petta_registered_builtins;
    uint64_t table_instance_id = symbol_table_instance_id(g_symbols);
    if (set->table != g_symbols ||
        set->table_instance_id != table_instance_id) {
        if (!g_symbols || symbol == SYMBOL_ID_NONE)
            return false;
        size_t len = 0u;
        for (size_t index = 0u; index < PETTA_REGISTERED_BUILTIN_COUNT;
             index++) {
            SymbolId id = symbol_intern_cstr(
                g_symbols, petta_registered_builtins[index].name);
            if (id != SYMBOL_ID_NONE)
                set->entries[len++] = (PeTTaRegisteredBuiltin){
                    .id = id,
                    .arities = petta_registered_builtins[index].arities,
                };
        }
        qsort(set->entries, len, sizeof(set->entries[0]),
              petta_registered_builtin_compare);
        set->len = len;
        set->table = g_symbols;
        set->table_instance_id = table_instance_id;
    }
    size_t low = 0u;
    size_t high = set->len;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (set->entries[middle].id < symbol)
            low = middle + 1u;
        else
            high = middle;
    }
    if (low >= set->len || set->entries[low].id != symbol)
        return false;
    if (arities)
        *arities = set->entries[low].arities;
    return true;
}

/* A registered name's answer about `supplied` arguments, read from the bits
 * of its recorded arities (RegisteredArity.answer_mask). */
PeTTaNamedArity petta_semantics_registered_named_arity(
    uint16_t arities, CettaExprLen supplied) {
    enum { WIDTH = 16u };
    PeTTaNamedArity info = {.known = true};
    if (supplied >= WIDTH) {
        info.smaller = arities != 0u;
        return info;
    }
    unsigned count = (unsigned)supplied;
    info.exact = ((arities >> count) & 1u) != 0u;
    info.larger = (arities >> (count + 1u)) != 0u;
    info.smaller = (arities & ((1u << count) - 1u)) != 0u;
    return info;
}

bool petta_semantics_registered_builtin(SymbolId symbol) {
    return petta_semantics_registered_builtin_arities(symbol, NULL);
}

bool petta_semantics_static_builtin_definition(
    SymbolId symbol, CettaExprLen input_arity) {
    /* get-type/2 is declared dynamic; the other registered prelude
     * predicates are static. Arity belongs to the predicate, not its name. */
    if (symbol == g_builtin_syms.get_type)
        return false;
    /* chain/3 is translator syntax. The one-input spelling instead
     * collides with the imported CLPFD chain/2 predicate. */
    if (symbol == g_builtin_syms.chain)
        return input_arity == 1u;
    uint16_t arities = 0u;
    return input_arity < 16u &&
        petta_semantics_registered_builtin_arities(symbol, &arities) &&
        (arities & (uint16_t)(1u << input_arity)) != 0u;
}

Atom *petta_semantics_builtin_definition_error(
    Arena *arena, SymbolId symbol, CettaExprLen input_arity) {
    if (!arena ||
        !petta_semantics_static_builtin_definition(symbol, input_arity))
        return NULL;
    return petta_static_procedure_error_with_context(
        arena, symbol == g_builtin_syms.chain ? "clpfd" : NULL,
        symbol, (int64_t)input_arity + 1, "assertz", 2);
}

/* SWI-PeTTa's get-metatype/2: a truth value is Grounded, and so is an atom
 * that names a registered function (fun/1); any other atom is a Symbol. */
Atom *petta_semantics_symbol_metatype(Arena *arena, SymbolId symbol,
                                      bool registered) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    bool grounded = registered ||
        symbol == ids->true_text || symbol == ids->false_text ||
        symbol == g_builtin_syms.true_text ||
        symbol == g_builtin_syms.false_text ||
        petta_semantics_registered_builtin(symbol);
    return grounded ? atom_grounded_type(arena) : atom_symbol_type(arena);
}

bool petta_semantics_truth_value(const Atom *atom, bool *value) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    if (!atom || !value)
        return false;
    if (atom->kind == ATOM_SYMBOL && atom->sym_id == ids->true_text) {
        *value = true;
        return true;
    }
    if (atom->kind == ATOM_SYMBOL && atom->sym_id == ids->false_text) {
        *value = false;
        return true;
    }
    if (atom->kind == ATOM_SYMBOL &&
        atom->sym_id == g_builtin_syms.true_text) {
        *value = true;
        return true;
    }
    if (atom->kind == ATOM_SYMBOL &&
        atom->sym_id == g_builtin_syms.false_text) {
        *value = false;
        return true;
    }
    if (atom->kind == ATOM_GROUNDED &&
        atom->ground.gkind == GV_BOOL) {
        *value = atom->ground.bval;
        return true;
    }
    return false;
}

Atom *petta_semantics_success_value(Arena *arena) {
    return petta_semantics_boolean_value(arena, true);
}

Atom *petta_semantics_boolean_value(Arena *arena, bool value) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    return arena && ids->table
        ? atom_symbol_id(
              arena, value ? ids->true_text : ids->false_text)
        : NULL;
}

static const char *petta_semantics_path_component(
    const Atom *atom) {
    if (!atom)
        return NULL;
    if (atom->kind == ATOM_SYMBOL)
        return symbol_bytes(g_symbols, atom->sym_id);
    if (atom->kind == ATOM_GROUNDED &&
        atom->ground.gkind == GV_STRING) {
        return atom->ground.sval;
    }
    return NULL;
}

bool petta_semantics_library_reference(
    const Atom *atom, PeTTaLibraryReference *reference) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    if (reference) {
        reference->kind = PETTA_LIBRARY_REFERENCE_NONE;
        reference->root = NULL;
        reference->member = NULL;
    }
    if (!atom || !reference || !ids->table ||
        atom->kind != ATOM_EXPR || atom->expr.len == 0u ||
        !atom_is_symbol_id(atom->expr.elems[0], ids->library)) {
        return false;
    }
    if (atom->expr.len == 2u) {
        reference->member = petta_semantics_path_component(
            atom->expr.elems[1]);
        if (!reference->member)
            return false;
        reference->kind = PETTA_LIBRARY_REFERENCE_STANDARD;
        return true;
    }
    if (atom->expr.len == 3u) {
        reference->root = petta_semantics_path_component(
            atom->expr.elems[1]);
        reference->member = petta_semantics_path_component(
            atom->expr.elems[2]);
        if (!reference->root || !reference->member)
            return false;
        reference->kind = PETTA_LIBRARY_REFERENCE_ROOTED;
        return true;
    }
    return false;
}

bool petta_semantics_library_path_effect(
    const Atom *atom, PeTTaLibraryPathEffect *effect) {
    if (effect) {
        effect->kind = PETTA_LIBRARY_PATH_EFFECT_NONE;
        effect->path = NULL;
    }
    if (!atom || !effect || atom->kind != ATOM_EXPR ||
        atom->expr.len != 2u || !atom->expr.elems[0] ||
        atom->expr.elems[0]->kind != ATOM_SYMBOL) {
        return false;
    }

    switch (petta_semantics_form(atom->expr.elems[0]->sym_id)) {
    case PETTA_FORM_ASSERTA_PREDICATE:
        effect->kind = PETTA_LIBRARY_PATH_EFFECT_PREPEND;
        break;
    case PETTA_FORM_ASSERTZ_PREDICATE:
        effect->kind = PETTA_LIBRARY_PATH_EFFECT_APPEND;
        break;
    case PETTA_FORM_RETRACT_PREDICATE:
        effect->kind = PETTA_LIBRARY_PATH_EFFECT_RETRACT_FIRST;
        break;
    default:
        return false;
    }

    /* The argument is a value, the compound library_path(Path) its
     * Predicate named. */
    Atom *body = NULL;
    if (!atom_petta_prolog_compound_body(atom->expr.elems[1], &body) ||
        !body || body->kind != ATOM_EXPR || body->expr.len != 2u ||
        !body->expr.elems[0] || body->expr.elems[0]->kind != ATOM_SYMBOL ||
        !symbol_eq_cstr(
            g_symbols, body->expr.elems[0]->sym_id, "library_path")) {
        effect->kind = PETTA_LIBRARY_PATH_EFFECT_NONE;
        return false;
    }
    effect->path = petta_semantics_path_component(body->expr.elems[1]);
    if (!effect->path) {
        effect->kind = PETTA_LIBRARY_PATH_EFFECT_NONE;
        return false;
    }
    return true;
}

PeTTaCountConsumer petta_semantics_count_consumer(SymbolId head) {
    if (head == SYMBOL_ID_NONE)
        return PETTA_COUNT_CONSUMER_NONE;
    if (petta_semantics_form(head) == PETTA_FORM_LENGTH)
        return PETTA_COUNT_CONSUMER_FORM;
    if (head == g_builtin_syms.size_atom || head == g_builtin_syms.size)
        return PETTA_COUNT_CONSUMER_BUILTIN;
    return PETTA_COUNT_CONSUMER_NONE;
}

bool petta_semantics_count_use_children_executable(SymbolId head) {
    PeTTaForm form = head == SYMBOL_ID_NONE
        ? PETTA_FORM_NONE : petta_semantics_form(head);
    return head != g_builtin_syms.quote &&
           head != g_builtin_syms.return_text &&
           form != PETTA_FORM_LAMBDA &&
           form != PETTA_FORM_PREDICATE;
}

bool petta_semantics_match_existence_observer_shape(
    const Atom *expression, SymbolId primary_reify) {
    if (!expression || expression->kind != ATOM_EXPR ||
        expression->expr.len != 3u ||
        !atom_is_symbol_id(expression->expr.elems[0],
                           g_builtin_syms.op_eq)) {
        return false;
    }
    const Atom *observed = NULL;
    if (expression->expr.elems[1] &&
        expression->expr.elems[1]->kind == ATOM_EXPR &&
        expression->expr.elems[1]->expr.len == 0u) {
        observed = expression->expr.elems[2];
    } else if (expression->expr.elems[2] &&
               expression->expr.elems[2]->kind == ATOM_EXPR &&
               expression->expr.elems[2]->expr.len == 0u) {
        observed = expression->expr.elems[1];
    }
    const Atom *reify = observed && observed->kind == ATOM_EXPR &&
            observed->expr.len == 2u
        ? observed->expr.elems[0] : NULL;
    if (!reify || reify->kind != ATOM_SYMBOL ||
        (reify->sym_id != primary_reify &&
         reify->sym_id != g_builtin_syms.collapse)) {
        return false;
    }
    const Atom *once = observed->expr.elems[1];
    const Atom *match = once && once->kind == ATOM_EXPR &&
            once->expr.len == 2u &&
            atom_is_symbol_id(once->expr.elems[0], g_builtin_syms.once)
        ? once->expr.elems[1] : NULL;
    return match && match->kind == ATOM_EXPR &&
        match->expr.len == 4u &&
        atom_is_symbol_id(match->expr.elems[0], g_builtin_syms.match);
}

bool petta_semantics_is_value_let(SymbolId head) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    return ids->table && head == ids->value_let;
}

bool petta_semantics_is_value_chain(SymbolId head) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    return ids->table && head == ids->value_chain;
}

static Atom *petta_semantics_lower_shared_atom_rec(
    Arena *arena, Atom *atom) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    if (!arena || !atom || !ids->table ||
        atom->kind != ATOM_EXPR || atom->expr.len == 0u) {
        return atom;
    }
    if (atom_is_symbol_id(
            atom->expr.elems[0], g_builtin_syms.quote)) {
        return atom;
    }

    Atom **rewritten = NULL;
    for (CettaExprIndex index = 0u;
         index < atom->expr.len; index++) {
        Atom *original = atom->expr.elems[index];
        Atom *next = original;
        if (index == 0u && original->kind == ATOM_SYMBOL) {
            if (original->sym_id == g_builtin_syms.let) {
                next = atom_symbol_id(arena, ids->value_let);
            } else if (original->sym_id == g_builtin_syms.chain) {
                next = atom_symbol_id(arena, ids->value_chain);
            }
        } else {
            next = petta_semantics_lower_shared_atom_rec(
                arena, original);
        }
        if (!rewritten && next != original) {
            rewritten = arena_alloc(
                arena, sizeof(*rewritten) * atom->expr.len);
            for (CettaExprIndex prefix = 0u;
                 prefix < index; prefix++) {
                rewritten[prefix] = atom->expr.elems[prefix];
            }
        }
        if (rewritten)
            rewritten[index] = next;
    }
    return rewritten
        ? atom_expr(arena, rewritten, atom->expr.len)
        : atom;
}

Atom *petta_semantics_lower_shared_atom(
    Arena *arena, Atom *atom) {
    return petta_semantics_lower_shared_atom_rec(arena, atom);
}

static Atom *petta_value_let_symbol(Arena *arena) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    return atom_symbol_id(arena, ids->value_let);
}

static Atom *petta_sequence_lower(
    Arena *arena, Atom *form, bool return_first) {
    CettaExprLen nargs = form->expr.len - 1u;
    if (nargs == 0u)
        return atom_petta_no_result(arena);
    if (nargs == 1u)
        return form->expr.elems[1];

    Atom *body;
    CettaExprIndex first_rest;
    if (return_first) {
        Atom *saved = atom_var_with_id(
            arena, "__petta_prog1_result", fresh_var_id());
        body = atom_expr2(
            arena, atom_symbol_id(arena, g_builtin_syms.quote), saved);
        first_rest = 1u;
        for (CettaExprIndex index = nargs; index-- > first_rest;) {
            Atom *ignored = atom_var_with_id(
                arena, "__petta_sequence", fresh_var_id());
            Atom *let_elems[4] = {
                petta_value_let_symbol(arena),
                ignored,
                form->expr.elems[index + 1u],
                body,
            };
            body = atom_expr(arena, let_elems, 4u);
        }
        Atom *let_elems[4] = {
            petta_value_let_symbol(arena),
            saved,
            form->expr.elems[1],
            body,
        };
        return atom_expr(arena, let_elems, 4u);
    }

    body = form->expr.elems[nargs];
    for (CettaExprIndex index = nargs - 1u; index-- > 0u;) {
        Atom *ignored = atom_var_with_id(
            arena, "__petta_sequence", fresh_var_id());
        Atom *let_elems[4] = {
            petta_value_let_symbol(arena),
            ignored,
            form->expr.elems[index + 1u],
            body,
        };
        body = atom_expr(arena, let_elems, 4u);
    }
    return body;
}

static Atom *petta_foldall_lower(Arena *arena, Atom *form) {
    if (form->expr.len != 4u)
        return NULL;
    Atom *acc = atom_var_with_id(
        arena, "__petta_fold_acc", fresh_var_id());
    Atom *item = atom_var_with_id(
        arena, "__petta_fold_item", fresh_var_id());
    Atom *raw_item = atom_var_with_id(
        arena, "__petta_fold_raw_item", fresh_var_id());
    /* PeTTa's aggregate goal calls reduce/2 for every yielded occurrence.
     * SWI foldall/4 calls the closure as call(Op, State0, State) after the
     * closure has already captured the yielded value, so the step is
     * (function item accumulator).  The chain normalizes that value before
     * the fold algebra observes it. */
    Atom *demanded_item = atom_expr2(
        arena,
        atom_symbol_id(arena, g_builtin_syms.eval),
        raw_item);
    Atom *stream_elems[4] = {
        atom_symbol_id(arena, g_builtin_syms.chain),
        form->expr.elems[2],
        raw_item,
        demanded_item,
    };
    Atom *stream = atom_expr(arena, stream_elems, 4u);
    Atom *step = atom_expr3(
        arena, form->expr.elems[1], item, acc);
    Atom *elems[6] = {
        atom_symbol_id(arena, g_builtin_syms.fold),
        stream,
        form->expr.elems[3],
        acc,
        item,
        step,
    };
    return atom_expr(arena, elems, 6u);
}

static Atom *petta_maplist_lower(Arena *arena, Atom *form) {
    if (form->expr.len != 3u)
        return NULL;
    Atom *item = atom_var_with_id(
        arena, "__petta_map_item", fresh_var_id());
    Atom *body = atom_expr2(
        arena, form->expr.elems[1], item);
    Atom *elements[4] = {
        atom_symbol_id(arena, g_builtin_syms.map_atom),
        form->expr.elems[2],
        item,
        body,
    };
    return atom_expr(arena, elements, 4u);
}

static Atom *petta_map_atom_lower(Arena *arena, Atom *form) {
    if (form->expr.len != 3u)
        return NULL;
    Atom *item = atom_var_with_id(
        arena, "__petta_map_item", fresh_var_id());
    Atom *body = atom_expr2(
        arena, form->expr.elems[2], item);
    Atom *elements[4] = {
        atom_symbol_id(arena, g_builtin_syms.map_atom),
        form->expr.elems[1],
        item,
        body,
    };
    return atom_expr(arena, elements, 4u);
}

static Atom *petta_eager_binary_lower(
    Arena *arena, Atom *form, SymbolId target) {
    if (form->expr.len != 3u)
        return NULL;
    Atom *left = atom_var_with_id(
        arena, "__petta_binary_left", fresh_var_id());
    Atom *right = atom_var_with_id(
        arena, "__petta_binary_right", fresh_var_id());
    Atom *call = atom_expr3(
        arena, atom_symbol_id(arena, target), left, right);
    Atom *right_let_elems[4] = {
        petta_value_let_symbol(arena),
        right,
        form->expr.elems[2],
        call,
    };
    Atom *right_let = atom_expr(arena, right_let_elems, 4u);
    Atom *left_let_elems[4] = {
        petta_value_let_symbol(arena),
        left,
        form->expr.elems[1],
        right_let,
    };
    return atom_expr(arena, left_let_elems, 4u);
}

static Atom *petta_eager_unary_lower(
    Arena *arena, Atom *form, SymbolId target) {
    if (form->expr.len != 2u)
        return NULL;
    Atom *value = atom_var_with_id(
        arena, "__petta_unary_value", fresh_var_id());
    Atom *call = atom_expr2(
        arena, atom_symbol_id(arena, target), value);
    Atom *let_elems[4] = {
        petta_value_let_symbol(arena),
        value,
        form->expr.elems[1],
        call,
    };
    return atom_expr(arena, let_elems, 4u);
}

static Atom *petta_stream_emit_value(
    Arena *arena, Atom *computation) {
    Atom *items = atom_var_with_id(
        arena, "__petta_stream_items", fresh_var_id());
    Atom *emit = atom_expr2(
        arena, atom_symbol_id(arena, g_builtin_syms.superpose), items);
    Atom *let_elems[4] = {
        petta_value_let_symbol(arena),
        items,
        computation,
        emit,
    };
    return atom_expr(arena, let_elems, 4u);
}

/* `(unique X)` and `(alpha-unique X)` answer the collected answers of X
 * with repeats removed, the first occurrence kept: by equality or by alpha
 * equivalence. */
static Atom *petta_stream_unary_lower(
    Arena *arena, Atom *form, SymbolId aggregate, SymbolId reify_head) {
    if (form->expr.len != 2u)
        return NULL;
    Atom *reified = atom_expr2(
        arena, atom_symbol_id(arena, reify_head),
        form->expr.elems[1]);
    Atom *unique = atom_expr2(
        arena, atom_symbol_id(arena, aggregate), reified);
    return petta_stream_emit_value(arena, unique);
}

static bool petta_is_superpose_form(const Atom *atom) {
    return atom && atom->kind == ATOM_EXPR &&
           atom->expr.len > 0u &&
           atom_is_symbol_id(
               atom->expr.elems[0], g_builtin_syms.superpose);
}

static Atom *petta_stream_binary_lower(
    Arena *arena, Atom *form, SymbolId aggregate,
    SymbolId reify_head) {
    if (form->expr.len != 3u ||
        !petta_is_superpose_form(form->expr.elems[1]) ||
        !petta_is_superpose_form(form->expr.elems[2])) {
        return NULL;
    }
    Atom *left_computation = atom_expr2(
        arena, atom_symbol_id(arena, reify_head),
        form->expr.elems[1]);
    Atom *right_computation = atom_expr2(
        arena, atom_symbol_id(arena, reify_head),
        form->expr.elems[2]);
    Atom *left = atom_var_with_id(
        arena, "__petta_stream_left", fresh_var_id());
    Atom *right = atom_var_with_id(
        arena, "__petta_stream_right", fresh_var_id());
    Atom *combined = atom_expr3(
        arena, atom_symbol_id(arena, aggregate), left, right);
    Atom *emit = petta_stream_emit_value(arena, combined);
    Atom *right_let_elems[4] = {
        petta_value_let_symbol(arena),
        right,
        right_computation,
        emit,
    };
    Atom *right_let = atom_expr(
        arena, right_let_elems, 4u);
    Atom *left_let_elems[4] = {
        petta_value_let_symbol(arena),
        left,
        left_computation,
        right_let,
    };
    return atom_expr(arena, left_let_elems, 4u);
}

static Atom *petta_second_from_pair_lower(
    Arena *arena, Atom *form) {
    if (form->expr.len != 2u)
        return NULL;
    Atom *pair = atom_var_with_id(
        arena, "__petta_pair", fresh_var_id());
    Atom *index = atom_expr3(
        arena, atom_symbol_id(arena, g_builtin_syms.index_atom),
        pair, atom_int(arena, 1));
    Atom *let_elems[4] = {
        petta_value_let_symbol(arena),
        pair,
        form->expr.elems[1],
        index,
    };
    return atom_expr(arena, let_elems, 4u);
}

Atom *petta_semantics_lower(
    Arena *arena, Atom *form, PeTTaForm kind, SymbolId reify_head) {
    if (!arena || !form || form->kind != ATOM_EXPR ||
        form->expr.len == 0u) {
        return NULL;
    }
    switch (kind) {
    case PETTA_FORM_TRACE: {
        if (form->expr.len != 3u)
            return NULL;
        /* The translator sequences printing before the body. Reuse the
         * same lowering as progn so failure, choices and caller bindings
         * remain owned by the existing continuation. */
        Atom *print = atom_expr2(
            arena, atom_symbol_id(arena, g_builtin_syms.println_bang),
            form->expr.elems[1]);
        Atom *sequence = atom_expr3(
            arena, atom_symbol_id(arena, petta_symbol_ids()->progn),
            print, form->expr.elems[2]);
        return petta_sequence_lower(arena, sequence, false);
    }
    case PETTA_FORM_PROGN:
        return petta_sequence_lower(arena, form, false);
    case PETTA_FORM_PROG1:
        return petta_sequence_lower(arena, form, true);
    case PETTA_FORM_FOLDALL:
        return petta_foldall_lower(arena, form);
    case PETTA_FORM_MAPLIST:
        return petta_maplist_lower(arena, form);
    case PETTA_FORM_MAP_ATOM:
        return petta_map_atom_lower(arena, form);
    case PETTA_FORM_FOLDL:
        return NULL;
    case PETTA_FORM_ID:
        return form->expr.len == 2u ? form->expr.elems[1] : NULL;
    case PETTA_FORM_APPEND:
        return petta_eager_binary_lower(
            arena, form, g_builtin_syms.union_atom);
    case PETTA_FORM_CONS:
        return petta_eager_binary_lower(
            arena, form, g_builtin_syms.cons_atom);
    case PETTA_FORM_STREAM_UNIQUE:
        return petta_stream_unary_lower(
            arena, form, g_builtin_syms.unique_atom, reify_head);
    case PETTA_FORM_STREAM_ALPHA_UNIQUE:
        return petta_stream_unary_lower(
            arena, form, petta_symbol_ids()->alpha_unique_atom, reify_head);
    case PETTA_FORM_STREAM_UNION:
        return petta_stream_binary_lower(
            arena, form, g_builtin_syms.union_atom, reify_head);
    case PETTA_FORM_STREAM_INTERSECTION:
        return petta_stream_binary_lower(
            arena, form, g_builtin_syms.intersection_atom, reify_head);
    case PETTA_FORM_STREAM_SUBTRACTION:
        return petta_stream_binary_lower(
            arena, form, g_builtin_syms.subtraction_atom, reify_head);
    case PETTA_FORM_LENGTH:
        return petta_eager_unary_lower(
            arena, form, g_builtin_syms.size_atom);
    case PETTA_FORM_FIRST_FROM_PAIR:
        return NULL;
    case PETTA_FORM_SECOND_FROM_PAIR:
        return petta_second_from_pair_lower(arena, form);
    case PETTA_FORM_CALL:
    case PETTA_FORM_EVAL:
    case PETTA_FORM_REDUCE:
    case PETTA_FORM_CATCH:
        return form->expr.len == 2u ? form->expr.elems[1] : NULL;
    case PETTA_FORM_NONE:
    case PETTA_FORM_TEST:
    case PETTA_FORM_IF:
    case PETTA_FORM_FORALL:
    case PETTA_FORM_PREDICATE:
    case PETTA_FORM_TRANSLATE_PREDICATE:
    case PETTA_FORM_IMPORT_PROLOG_FUNCTION:
    case PETTA_FORM_PROCESS_METTA_STRING:
    case PETTA_FORM_CALL_PREDICATE:
    case PETTA_FORM_ASSERTA_PREDICATE:
    case PETTA_FORM_ASSERTZ_PREDICATE:
    case PETTA_FORM_RETRACT_PREDICATE:
    case PETTA_FORM_TABLED:
    case PETTA_FORM_ADD_TRANSLATOR_RULE:
    case PETTA_FORM_REMOVE_TRANSLATOR_RULE:
    case PETTA_FORM_CUT:
    case PETTA_FORM_LAMBDA:
    case PETTA_FORM_LET:
    case PETTA_FORM_CHAIN:
    case PETTA_FORM_IS_VAR:
    case PETTA_FORM_IS_GROUND:
    case PETTA_FORM_IS_EXPR:
    case PETTA_FORM_IS_SPACE:
    case PETTA_FORM_IS_MEMBER:
    case PETTA_FORM_INT_ADD:
    case PETTA_FORM_IS_ALPHA_MEMBER:
    case PETTA_FORM_ALPHA_UNIQUE:
    case PETTA_FORM_LIST_TO_SET:
    case PETTA_FORM_EXCLUDE_ITEM:
    case PETTA_FORM_REPRA:
    case PETTA_FORM_SREAD:
    case PETTA_FORM_BIND_STATE:
    case PETTA_FORM_GET_STATE:
    case PETTA_FORM_CHANGE_STATE:
    case PETTA_FORM_NEW_STATE:
    case PETTA_FORM_MSORT:
        return NULL;
    }
    return NULL;
}

typedef enum {
    PETTA_TERM_VARIABLE = 0,
    PETTA_TERM_NUMBER = 1,
    PETTA_TERM_STRING = 2,
    PETTA_TERM_NIL = 3,
    PETTA_TERM_ATOM = 4,
    PETTA_TERM_COMPOUND = 5,
} PeTTaTermClass;

static PeTTaTermClass petta_term_class(const Atom *atom) {
    if (atom->kind == ATOM_VAR)
        return PETTA_TERM_VARIABLE;
    if (atom_is_number(atom))
        return PETTA_TERM_NUMBER;
    if (atom->kind == ATOM_GROUNDED &&
        atom->ground.gkind == GV_STRING) {
        return PETTA_TERM_STRING;
    }
    if (atom->kind == ATOM_EXPR)
        return atom->expr.len ? PETTA_TERM_COMPOUND : PETTA_TERM_NIL;
    return PETTA_TERM_ATOM;
}

static int petta_compare_u64(uint64_t left, uint64_t right) {
    return left < right ? -1 : left > right ? 1 : 0;
}

static int petta_compare_text(const char *left, size_t left_len,
                              const char *right, size_t right_len) {
    size_t shared = left_len < right_len ? left_len : right_len;
    int raw = memcmp(left, right, shared);
    if (!raw)
        raw = left_len < right_len ? -1 : left_len > right_len ? 1 : 0;
    return raw < 0 ? -1 : raw > 0 ? 1 : 0;
}

static const char *petta_known_atom_text(const Atom *atom, size_t *length) {
    if (atom->kind == ATOM_SYMBOL) {
        *length = symbol_len(g_symbols, atom->sym_id);
        return symbol_bytes(g_symbols, atom->sym_id);
    }
    if (atom->kind == ATOM_EXPR && atom->expr.len == 0u) {
        *length = 2u;
        return "[]";
    }
    if (atom->kind == ATOM_GROUNDED &&
        atom->ground.gkind == GV_BOOL) {
        *length = atom->ground.bval ? 4u : 5u;
        return atom->ground.bval ? "true" : "false";
    }
    return NULL;
}

#if CETTA_BUILD_WITH_GMP
static bool petta_integer_to_mpz(const Atom *atom, mpz_t value) {
    if (atom->ground.gkind == GV_BIGINT)
        return atom_bigint_get_mpz(atom, value);
    if (atom->ground.gkind != GV_INT)
        return false;
    uint64_t magnitude = atom->ground.ival < 0
        ? (uint64_t)(-(atom->ground.ival + 1)) + 1u
        : (uint64_t)atom->ground.ival;
    mpz_import(value, 1u, -1, sizeof(magnitude), 0, 0, &magnitude);
    if (atom->ground.ival < 0)
        mpz_neg(value, value);
    return true;
}

static bool petta_exact_number_to_mpq(const Atom *atom, mpq_t value) {
    if (atom->ground.gkind == GV_RATIONAL)
        return atom_rational_get_mpq(atom, value);
    mpz_t integer;
    mpz_init(integer);
    bool converted = petta_integer_to_mpz(atom, integer);
    if (converted)
        mpq_set_z(value, integer);
    mpz_clear(integer);
    return converted;
}
#endif

static long double petta_number_to_long_double(const Atom *atom) {
    if (atom->ground.gkind == GV_INT)
        return (long double)atom->ground.ival;
    if (atom->ground.gkind == GV_FLOAT)
        return (long double)atom->ground.fval;
    if (atom->ground.gkind == GV_BIGINT)
        return strtold(atom_bigint_cstr(atom), NULL);
    const char *text = atom_rational_cstr(atom);
    const char *slash = text ? strchr(text, '/') : NULL;
    if (!slash)
        return 0.0L;
    long double numerator = strtold(text, NULL);
    long double denominator = strtold(slash + 1u, NULL);
    return numerator / denominator;
}

static int petta_compare_float_values(double left, double right) {
    bool left_nan = isnan(left);
    bool right_nan = isnan(right);
    if (left_nan || right_nan) {
        if (left_nan != right_nan)
            return left_nan ? -1 : 1;
        return 0;
    }
    if (left < right)
        return -1;
    if (left > right)
        return 1;
    if (signbit(left) != signbit(right))
        return signbit(left) ? -1 : 1;
    return 0;
}

/* A machine integer against a finite double, exactly: an integer of at most
 * 53 bits converts to a double without rounding; a larger one is compared
 * with the double's whole part, which is exact below 2^63, and then with its
 * fraction. */
static int petta_compare_int_with_double(int64_t value, double floating) {
    const int64_t exact_limit = INT64_C(1) << 53;
    if (value >= -exact_limit && value <= exact_limit) {
        double exact = (double)value;
        return exact < floating ? -1 : exact > floating ? 1 : 0;
    }
    if (floating >= 9223372036854775808.0)
        return -1;
    if (floating < -9223372036854775808.0)
        return 1;
    double whole = trunc(floating);
    int64_t whole_value = (int64_t)whole;
    if (value != whole_value)
        return value < whole_value ? -1 : 1;
    double fraction = floating - whole;
    return fraction > 0.0 ? -1 : fraction < 0.0 ? 1 : 0;
}

static bool petta_compare_numbers(
    const Atom *left, const Atom *right, int *ordering) {
    bool left_float = left->ground.gkind == GV_FLOAT;
    bool right_float = right->ground.gkind == GV_FLOAT;
    if (left_float && right_float) {
        *ordering = petta_compare_float_values(
            left->ground.fval, right->ground.fval);
        return true;
    }
    /* Two machine integers, and a machine integer against a finite float,
     * compare without GMP; a float before an exact number of equal value,
     * as SWI orders them. */
    if (left->ground.gkind == GV_INT && right->ground.gkind == GV_INT) {
        *ordering = left->ground.ival < right->ground.ival ? -1
            : left->ground.ival > right->ground.ival ? 1 : 0;
        return true;
    }
    if ((left_float && right->ground.gkind == GV_INT) ||
        (right_float && left->ground.gkind == GV_INT)) {
        double floating =
            left_float ? left->ground.fval : right->ground.fval;
        if (!isnan(floating) && !isinf(floating)) {
            int64_t exact =
                left_float ? right->ground.ival : left->ground.ival;
            int exact_against_float =
                petta_compare_int_with_double(exact, floating);
            *ordering = exact_against_float == 0
                ? (left_float ? -1 : 1)
                : (left_float ? -exact_against_float
                              : exact_against_float);
            return true;
        }
    }

#if CETTA_BUILD_WITH_GMP
    if (!left_float && !right_float) {
        mpq_t left_value;
        mpq_t right_value;
        mpq_inits(left_value, right_value, NULL);
        bool converted =
            petta_exact_number_to_mpq(left, left_value) &&
            petta_exact_number_to_mpq(right, right_value);
        if (converted)
            *ordering = mpq_cmp(left_value, right_value);
        mpq_clears(left_value, right_value, NULL);
        if (converted) {
            *ordering = *ordering < 0 ? -1 : *ordering > 0 ? 1 : 0;
            return true;
        }
    } else {
        const Atom *exact = left_float ? right : left;
        double floating =
            left_float ? left->ground.fval : right->ground.fval;
        if (isnan(floating)) {
            *ordering = left_float ? -1 : 1;
            return true;
        }
        if (isinf(floating)) {
            int exact_against_float = signbit(floating) ? 1 : -1;
            *ordering = left_float
                ? -exact_against_float
                : exact_against_float;
            return true;
        }
        mpq_t exact_value;
        mpq_t floating_value;
        mpq_inits(exact_value, floating_value, NULL);
        bool converted =
            petta_exact_number_to_mpq(exact, exact_value);
        if (converted)
            mpq_set_d(floating_value, floating);
        int exact_against_float = converted
            ? mpq_cmp(exact_value, floating_value)
            : 0;
        mpq_clears(exact_value, floating_value, NULL);
        if (converted) {
            exact_against_float =
                exact_against_float < 0
                    ? -1
                    : exact_against_float > 0 ? 1 : 0;
            if (exact_against_float == 0) {
                /* SWI orders a float before an exact number of equal value. */
                *ordering = left_float ? -1 : 1;
            } else {
                *ordering = left_float
                    ? -exact_against_float
                    : exact_against_float;
            }
            return true;
        }
    }
#endif

    long double left_value = petta_number_to_long_double(left);
    long double right_value = petta_number_to_long_double(right);
    if (left_value < right_value)
        *ordering = -1;
    else if (left_value > right_value)
        *ordering = 1;
    else if (left_float != right_float)
        *ordering = left_float ? -1 : 1;
    else
        *ordering = 0;
    return true;
}

static int petta_compare_grounded_atoms(
    const Atom *left, const Atom *right) {
    size_t left_len = 0u, right_len = 0u;
    const char *left_text = petta_known_atom_text(left, &left_len);
    const char *right_text = petta_known_atom_text(right, &right_len);
    if (left_text && right_text)
        return petta_compare_text(left_text, left_len, right_text, right_len);
    if (left_text != NULL)
        return -1;
    if (right_text != NULL)
        return 1;
    if (left->ground.gkind != right->ground.gkind) {
        return left->ground.gkind < right->ground.gkind ? -1 : 1;
    }
    switch (left->ground.gkind) {
    case GV_SPACE:
    case GV_STATE:
    case GV_CAPTURE:
    case GV_BINDINGS:
    case GV_FOREIGN:
    case GV_TERM_GRAPH:
        return petta_compare_u64(
            (uint64_t)(uintptr_t)left->ground.ptr,
            (uint64_t)(uintptr_t)right->ground.ptr);
    case GV_PRIME_NEED_CAPABILITY:
        return petta_compare_u64(
            (uint64_t)(uintptr_t)left->ground.prime_need_capability,
            (uint64_t)(uintptr_t)right->ground.prime_need_capability);
    case GV_PRIME_CONTEXT:
        return petta_compare_u64(
            (uint64_t)(uintptr_t)left->ground.prime_context,
            (uint64_t)(uintptr_t)right->ground.prime_context);
    case GV_INTERNAL_TAG:
        return petta_compare_u64((uint64_t)left->ground.ival,
                                 (uint64_t)right->ground.ival);
    case GV_BOOL:
        return left->ground.bval == right->ground.bval
            ? 0
            : left->ground.bval ? 1 : -1;
    case GV_INT:
    case GV_FLOAT:
    case GV_BIGINT:
    case GV_RATIONAL:
    case GV_STRING:
        return 0;
    }
    return 0;
}

bool petta_semantics_term_compare_expression_boundary(
        bool nonempty, const Atom *leaf, int *ordering) {
    if (!leaf || !ordering || leaf->kind == ATOM_EXPR ||
        (leaf->kind != ATOM_VAR && leaf->kind != ATOM_SYMBOL &&
         leaf->kind != ATOM_GROUNDED))
        return false;
    PeTTaTermClass expression_class = nonempty
        ? PETTA_TERM_COMPOUND : PETTA_TERM_NIL;
    PeTTaTermClass leaf_class = petta_term_class(leaf);
    if (expression_class != leaf_class) {
        *ordering = expression_class < leaf_class ? -1 : 1;
        return true;
    }
    size_t length = 0u;
    const char *text = petta_known_atom_text(leaf, &length);
    *ordering = text ? petta_compare_text("[]", 2u, text, length) : -1;
    return true;
}

static bool petta_term_compare_leaf(
    const Atom *left, const Atom *right, int *ordering) {
    PeTTaTermClass left_class = petta_term_class(left);
    PeTTaTermClass right_class = petta_term_class(right);
    if (left_class != right_class) {
        *ordering = left_class < right_class ? -1 : 1;
        return true;
    }

    switch (left_class) {
    case PETTA_TERM_VARIABLE:
        *ordering = petta_compare_u64(left->var_id, right->var_id);
        return true;
    case PETTA_TERM_NUMBER:
        return petta_compare_numbers(left, right, ordering);
    case PETTA_TERM_STRING:
        *ordering = atom_string_compare(left, right);
        return true;
    case PETTA_TERM_NIL:
        *ordering = 0;
        return true;
    case PETTA_TERM_ATOM: {
        size_t left_len = 0u, right_len = 0u;
        const char *left_text = petta_known_atom_text(left, &left_len);
        const char *right_text = petta_known_atom_text(right, &right_len);
        if (left_text && right_text) {
            *ordering = petta_compare_text(left_text, left_len, right_text, right_len);
            return true;
        }
        if (left->kind == ATOM_GROUNDED &&
            right->kind == ATOM_GROUNDED) {
            *ordering = petta_compare_grounded_atoms(left, right);
            return true;
        }
        *ordering = left_text ? -1 : right_text ? 1 :
            left->kind < right->kind ? -1 :
            left->kind > right->kind ? 1 : 0;
        return true;
    }
    case PETTA_TERM_COMPOUND:
        return false;
    }
    return false;
}

typedef struct {
    const Atom *left;
    const Atom *right;
    /* A pending pair cannot justify skipping its descendants. */
    bool complete;
} PeTTaOrderPair;

typedef struct {
    const Atom *left;
    const Atom *right;
    CettaExprIndex next;
} PeTTaOrderFrame;

typedef struct {
    PeTTaOrderPair local[64];
    PeTTaOrderPair *slots;
    size_t capacity;
    size_t used;
} PeTTaOrderMemo;

static size_t petta_order_pair_hash(const Atom *left, const Atom *right) {
    uint64_t a = (uint64_t)(uintptr_t)left;
    uint64_t b = (uint64_t)(uintptr_t)right;
    a ^= a >> 30u;
    a *= UINT64_C(0xbf58476d1ce4e5b9);
    b ^= b >> 27u;
    b *= UINT64_C(0x94d049bb133111eb);
    return (size_t)(a ^ (b + (a << 6u) + (a >> 2u)));
}

static PeTTaOrderPair *petta_order_pair_find(
        PeTTaOrderMemo *memo, const Atom *left, const Atom *right) {
    size_t at = petta_order_pair_hash(left, right) & (memo->capacity - 1u);
    while (memo->slots[at].left &&
           (memo->slots[at].left != left || memo->slots[at].right != right))
        at = (at + 1u) & (memo->capacity - 1u);
    return &memo->slots[at];
}

static bool petta_order_memo_grow(PeTTaOrderMemo *memo) {
    if (memo->capacity > SIZE_MAX / (2u * sizeof(*memo->slots)))
        return false;
    size_t capacity = memo->capacity * 2u;
    PeTTaOrderPair *slots = cetta_malloc(capacity * sizeof(*slots));
    if (!slots)
        return false;
    memset(slots, 0, capacity * sizeof(*slots));
    PeTTaOrderPair *previous = memo->slots;
    size_t previous_capacity = memo->capacity;
    memo->slots = slots;
    memo->capacity = capacity;
    for (size_t i = 0u; i < previous_capacity; i++) {
        PeTTaOrderPair pair = previous[i];
        if (pair.left)
            *petta_order_pair_find(memo, pair.left, pair.right) = pair;
    }
    if (previous != memo->local)
        free(previous);
    return true;
}

/* Lexicographic depth-first traversal over pairs of immutable expression
 * nodes. Complete equal pairs can be reused; a pair on the active path is
 * reported as unsupported instead of assuming equality of cyclic terms.
 * The memo belongs to this comparison only: bindings and mutable resources
 * cannot change under it, and no address escapes as semantic authority. */
static bool petta_term_compare_graph(
        const Atom *left, const Atom *right, int *ordering) {
    PeTTaOrderFrame local_frames[32];
    PeTTaOrderFrame *frames = local_frames;
    size_t capacity = 32u, depth = 0u;
    PeTTaOrderMemo memo = {.capacity = 64u};
    memo.slots = memo.local;
    bool success = false;
    for (;;) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_ORDER_PAIR_VISIT);
        if (left == right) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_ORDER_EQUAL_REUSE);
        } else if (petta_term_compare_leaf(left, right, ordering)) {
            if (*ordering != 0) {
                success = true;
                break;
            }
        } else {
            if (left->kind != ATOM_EXPR || right->kind != ATOM_EXPR)
                break;
            PeTTaOrderPair *pair = petta_order_pair_find(&memo, left, right);
            if (pair->left) {
                if (!pair->complete)
                    break;
                cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_ORDER_EQUAL_REUSE);
            } else {
                if (memo.used >= memo.capacity / 2u) {
                    if (!petta_order_memo_grow(&memo))
                        break;
                    pair = petta_order_pair_find(&memo, left, right);
                }
                if (depth == capacity) {
                    if (capacity > SIZE_MAX / (2u * sizeof(*frames)))
                        break;
                    size_t next_capacity = capacity * 2u;
                    PeTTaOrderFrame *next = cetta_malloc(next_capacity * sizeof(*next));
                    if (!next)
                        break;
                    memcpy(next, frames, depth * sizeof(*next));
                    if (frames != local_frames)
                        free(frames);
                    frames = next;
                    capacity = next_capacity;
                }
                *pair = (PeTTaOrderPair){left, right, false};
                memo.used++;
                frames[depth++] = (PeTTaOrderFrame){left, right, 1u};
                cetta_runtime_stats_update_max(
                    CETTA_RUNTIME_COUNTER_TERM_ORDER_FRONTIER_PEAK, depth);
                left = left->expr.elems[0];
                right = right->expr.elems[0];
                continue;
            }
        }
        for (;;) {
            if (!depth) {
                *ordering = 0;
                success = true;
                goto done;
            }
            PeTTaOrderFrame *frame = &frames[depth - 1u];
            CettaExprLen shared = frame->left->expr.len < frame->right->expr.len
                ? frame->left->expr.len : frame->right->expr.len;
            if (frame->next < shared) {
                left = frame->left->expr.elems[frame->next];
                right = frame->right->expr.elems[frame->next++];
                break;
            }
            *ordering = frame->left->expr.len < frame->right->expr.len ? -1 :
                frame->left->expr.len > frame->right->expr.len ? 1 : 0;
            if (*ordering != 0) {
                success = true;
                goto done;
            }
            petta_order_pair_find(&memo, frame->left, frame->right)->complete = true;
            depth--;
        }
    }
done:
    if (frames != local_frames)
        free(frames);
    if (memo.slots != memo.local)
        free(memo.slots);
    return success;
}

static bool petta_term_compare_flat(
    const Atom *left, const Atom *right, int *ordering) {
    /* Flat values need neither a memo nor a heap worklist. Nested terms
     * enter the same graph traversal used by all deeper comparisons. */
    CettaExprLen shared = left->expr.len < right->expr.len
        ? left->expr.len
        : right->expr.len;
    for (CettaExprIndex index = 0u; index < shared; index++) {
        const Atom *a = left->expr.elems[index];
        const Atom *b = right->expr.elems[index];
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_ORDER_PAIR_VISIT);
        if (a == b)
            continue;
        int element_order = 0;
        if (!petta_term_compare_leaf(a, b, &element_order))
            return petta_term_compare_graph(left, right, ordering);
        if (element_order != 0) {
            *ordering = element_order;
            return true;
        }
    }
    *ordering = left->expr.len < right->expr.len
        ? -1
        : left->expr.len > right->expr.len ? 1 : 0;
    return true;
}

bool petta_semantics_term_compare(
    const Atom *left, const Atom *right, int *ordering) {
    if (!left || !right || !ordering)
        return false;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_ORDER_PAIR_VISIT);
    if (left == right) {
        *ordering = 0;
        return true;
    }
    if (left->kind == right->kind) {
        if (left->kind == ATOM_SYMBOL) {
            *ordering = symbol_compare(g_symbols, left->sym_id, right->sym_id);
            return true;
        }
        if (left->kind == ATOM_VAR) {
            *ordering = petta_compare_u64(left->var_id, right->var_id);
            return true;
        }
        if (left->kind == ATOM_GROUNDED &&
            left->ground.gkind == GV_INT && right->ground.gkind == GV_INT) {
            *ordering = left->ground.ival < right->ground.ival ? -1 :
                        left->ground.ival > right->ground.ival ? 1 : 0;
            return true;
        }
        if (left->kind == ATOM_EXPR && left->expr.len && right->expr.len)
            return petta_term_compare_flat(left, right, ordering);
    }
    return petta_term_compare_leaf(left, right, ordering);
}

Atom *petta_semantics_msort(Arena *arena, Atom *list) {
    if (!arena || !list || list->kind != ATOM_EXPR || atom_is_list_rest(list))
        return NULL;
    /* A list's elements sort into a list. */
    if (atom_is_list(list)) {
        Atom *elements = atom_sequence_like(
            arena, NULL, atom_list_elems(list), atom_list_len(list));
        Atom *sorted = elements ? petta_semantics_msort(arena, elements) : NULL;
        return sorted ? atom_list(arena, sorted->expr.elems, sorted->expr.len)
                      : NULL;
    }
    if (list->expr.len == 0u)
        return atom_expr(arena, NULL, 0u);
    if ((uint64_t)list->expr.len >
        (uint64_t)(SIZE_MAX / sizeof(Atom *))) {
        return NULL;
    }

    size_t length = (size_t)list->expr.len;
    Atom **items = arena_alloc(arena, sizeof(*items) * length);
    Atom **scratch = arena_alloc(arena, sizeof(*scratch) * length);
    memcpy(items, list->expr.elems, sizeof(*items) * length);

    Atom **source = items;
    Atom **target = scratch;
    for (size_t width = 1u; width < length;) {
        size_t start = 0u;
        while (start < length) {
            size_t middle =
                length - start < width ? length : start + width;
            size_t remaining = length - middle;
            size_t right_width = remaining < width ? remaining : width;
            size_t end = middle + right_width;
            size_t left_index = start;
            size_t right_index = middle;
            size_t output = start;
            while (left_index < middle && right_index < end) {
                int ordering = 0;
                if (!petta_semantics_term_compare(
                        source[left_index], source[right_index],
                        &ordering)) {
                    return NULL;
                }
                target[output++] = ordering <= 0
                    ? source[left_index++]
                    : source[right_index++];
            }
            while (left_index < middle)
                target[output++] = source[left_index++];
            while (right_index < end)
                target[output++] = source[right_index++];
            start = end;
        }
        Atom **swap = source;
        source = target;
        target = swap;
        if (width > length / 2u)
            width = length;
        else
            width *= 2u;
    }
    if (source != items)
        memcpy(items, source, sizeof(*items) * length);
    return atom_expr(arena, items, list->expr.len);
}

Atom *petta_semantics_apply(
    Arena *arena, Atom *callable, Atom *argument) {
    if (!arena || !callable || !argument)
        return NULL;
    return atom_expr2(arena, callable, argument);
}

/* Fresh names may be translated by different workers, then copied between
 * them. Like variable identities, their allocation cannot be thread-local. */
static _Atomic uint64_t g_petta_callable_identity;

Atom *petta_semantics_new_callable_identity(Arena *arena) {
    if (!arena || !petta_symbol_ids()->table)
        return NULL;
    for (;;) {
        uint64_t previous = atomic_load_explicit(
            &g_petta_callable_identity, memory_order_relaxed);
        do {
            if (previous == (uint64_t)INT64_MAX)
                return NULL;
        } while (!atomic_compare_exchange_weak_explicit(
            &g_petta_callable_identity, &previous, previous + 1u,
            memory_order_relaxed, memory_order_relaxed));
        uint64_t identity = previous + 1u;
        char spelling[64];
        int length = snprintf(spelling, sizeof(spelling), "lambda_%llu",
                              (unsigned long long)identity);
        if (length <= 0 || (size_t)length >= sizeof(spelling))
            return NULL;
        /* Avoid taking an authored name when the callable later crosses
         * the foreign atom boundary. Do not intern unused labels: repeated
         * translation must not retain one symbol per transient callable. */
        if (symbol_lookup_cstr(g_symbols, spelling) == SYMBOL_ID_NONE)
            return atom_int(arena, (int64_t)identity);
    }
}

Atom *petta_semantics_lambda_value(
    Arena *arena, Atom *identity, Atom *canonical_body) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    if (!arena || !identity || !canonical_body || !ids->table)
        return NULL;
    return atom_expr3(
        arena,
        atom_symbol_id(arena, ids->canonical_lam),
        atom_expr2(arena, atom_internal_tag(arena, CETTA_INTERNAL_TAG_PETTA_CALLABLE_IDENTITY),
                   identity),
        canonical_body);
}

bool petta_semantics_lambda_body(
    const Atom *atom, Atom **canonical_body) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    if (canonical_body)
        *canonical_body = NULL;
    if (!atom || !ids->table || atom->kind != ATOM_EXPR ||
        atom->expr.len != 3u ||
        !atom_is_symbol_id(atom->expr.elems[0], ids->canonical_lam) ||
        atom->expr.elems[1]->kind != ATOM_EXPR ||
        atom->expr.elems[1]->expr.len != 2u ||
        !atom_is_internal_tag(atom->expr.elems[1]->expr.elems[0],
                           CETTA_INTERNAL_TAG_PETTA_CALLABLE_IDENTITY)) {
        return false;
    }
    if (canonical_body)
        *canonical_body = atom->expr.elems[2];
    return true;
}

bool petta_semantics_is_mm2_exec(SymbolId head) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    return ids->table && head != SYMBOL_ID_NONE && head == ids->mm2_exec;
}

bool petta_semantics_is_mork_space_name(const Atom *atom) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    return ids->table && atom && atom->kind == ATOM_SYMBOL &&
        atom->sym_id == ids->mork_space;
}

bool petta_semantics_is_canonical_closure(const Atom *atom) {
    Atom *base = NULL;
    if (petta_semantics_partial_view(atom, &base, NULL))
        atom = base;
    return petta_semantics_lambda_body(atom, NULL) ||
           petta_semantics_nullary_lambda_body(atom, NULL);
}

bool petta_semantics_closure_remaining(const Atom *atom,
                                       CettaExprLen *remaining) {
    Atom *base = NULL;
    Atom *bound = NULL;
    if (!petta_semantics_partial_view(atom, &base, &bound) ||
        !petta_semantics_lambda_body(base, NULL))
        return false;
    CettaExprLen parameters = 0u;
    for (Atom *body = base; petta_semantics_lambda_body(body, &body);)
        parameters++;
    if (parameters < bound->expr.len)
        return false;
    if (remaining)
        *remaining = parameters - bound->expr.len;
    return true;
}

CettaExprIndex petta_semantics_output_child(const Atom *body) {
    if (!body || body->kind != ATOM_EXPR || body->expr.len < 2u ||
        body->expr.elems[0]->kind != ATOM_SYMBOL)
        return 0u;
    SymbolId head = body->expr.elems[0]->sym_id;
    PeTTaForm form = petta_semantics_form(head);
    CettaExprLen len = body->expr.len;
    if ((form == PETTA_FORM_LET || form == PETTA_FORM_CHAIN) && len == 4u)
        return 3u;
    if (head == g_builtin_syms.let_star && len == 3u)
        return 2u;
    if (form == PETTA_FORM_PROGN)
        return len - 1u;
    if (form == PETTA_FORM_TRACE && len == 3u)
        return 2u;
    if (form == PETTA_FORM_PROG1)
        return 1u;
    return 0u;
}

bool petta_semantics_runtime_callable_value(const Atom *atom) {
    return petta_semantics_lambda_body(atom, NULL) ||
           petta_semantics_nullary_lambda_body(atom, NULL) ||
           petta_semantics_partial_view(atom, NULL, NULL);
}

Atom *petta_semantics_nullary_lambda_value(
    Arena *arena, Atom *identity, Atom *body) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    if (!arena || !identity || !body || !ids->table)
        return NULL;
    return atom_expr3(
        arena, atom_internal_tag(arena, CETTA_INTERNAL_TAG_PETTA_NULLARY_CALLABLE),
        identity, body);
}

bool petta_semantics_nullary_lambda_body(
    const Atom *atom, Atom **body) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    if (body)
        *body = NULL;
    if (!atom || !ids->table || atom->kind != ATOM_EXPR ||
        atom->expr.len != 3u ||
        !atom_is_internal_tag(atom->expr.elems[0],
                              CETTA_INTERNAL_TAG_PETTA_NULLARY_CALLABLE)) {
        return false;
    }
    if (body)
        *body = atom->expr.elems[2];
    return true;
}

Atom *petta_semantics_partial_value(
    Arena *arena, Atom *base, Atom *const *arguments,
    CettaExprLen nargs) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    if (!arena || !base || !ids->table ||
        (nargs > 0u && !arguments) ||
        (uint64_t)nargs > (uint64_t)(SIZE_MAX / sizeof(Atom *))) {
        return NULL;
    }
    Atom **items = nargs
        ? arena_alloc(arena, sizeof(*items) * (size_t)nargs)
        : NULL;
    for (CettaExprIndex index = 0u; index < nargs; index++)
        items[index] = arguments[index];
    Atom *bound = atom_expr(arena, items, nargs);
    return atom_expr3(
        arena, atom_internal_tag(arena, CETTA_INTERNAL_TAG_PETTA_PARTIAL),
        base, bound);
}

bool petta_semantics_partial_head(const Atom *head) {
    return atom_is_internal_tag(head, CETTA_INTERNAL_TAG_PETTA_PARTIAL);
}

bool petta_semantics_partial_view(
    const Atom *atom, Atom **base, Atom **arguments) {
    if (base)
        *base = NULL;
    if (arguments)
        *arguments = NULL;
    if (!atom || atom->kind != ATOM_EXPR ||
        atom->expr.len != 3u ||
        !petta_semantics_partial_head(atom->expr.elems[0]) ||
        !atom->expr.elems[2] ||
        atom->expr.elems[2]->kind != ATOM_EXPR) {
        return false;
    }
    if (base)
        *base = atom->expr.elems[1];
    if (arguments)
        *arguments = atom->expr.elems[2];
    return true;
}

PeTTaValueRepresentation petta_semantics_value_representation(const Atom *value) {
    return atom_petta_value_representation(value);
}

bool petta_semantics_is_nonlist_carrier(const Atom *value) {
    return petta_semantics_value_representation(value) != PETTA_VALUE_ORDINARY;
}

bool petta_semantics_sequence_view(
    const Atom *value, Atom *const **elements, CettaExprLen *length) {
    return !petta_semantics_is_nonlist_carrier(value) &&
           atom_sequence_view(value, elements, length);
}
