#include "petta_semantics.h"

#include "space.h"
#include "symbol.h"
#include "term_universe.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PETTA_FORM_CACHE_CAP 128u

typedef struct {
    SymbolId symbol;
    PeTTaForm form;
} PeTTaFormCacheSlot;

typedef struct {
    const SymbolTable *table;
    uint64_t table_instance_id;
    PeTTaFormCacheSlot form_cache[PETTA_FORM_CACHE_CAP];
    bool form_cache_complete;
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
    SymbolId callable_marker;
    SymbolId nullary_callable;
    SymbolId partial;
    SymbolId library;
    SymbolId value_let;
    SymbolId value_chain;
    SymbolId open_cons;
    uint32_t open_cons_tag_arena_id;
    uint64_t open_cons_tag_arena_reset_epoch;
    Atom *open_cons_tag;
} PeTTaSymbolIds;

static _Thread_local PeTTaSymbolIds g_petta_symbol_ids;

static bool petta_form_cache_insert(
    PeTTaSymbolIds *ids, SymbolId symbol, PeTTaForm form) {
    if (!ids || symbol == SYMBOL_ID_NONE ||
        form == PETTA_FORM_NONE) {
        return true;
    }
    uint32_t slot =
        ((uint32_t)symbol * UINT32_C(2654435761)) &
        (PETTA_FORM_CACHE_CAP - 1u);
    for (uint32_t probe = 0u;
         probe < PETTA_FORM_CACHE_CAP; probe++) {
        PeTTaFormCacheSlot *entry = &ids->form_cache[slot];
        if (entry->form == PETTA_FORM_NONE) {
            entry->symbol = symbol;
            entry->form = form;
            return true;
        }
        /*
         * Preserve the first branch of the declarative dispatch below if
         * two surface spellings happen to intern to the same SymbolId.
         */
        if (entry->symbol == symbol)
            return true;
        slot = (slot + 1u) & (PETTA_FORM_CACHE_CAP - 1u);
    }
    return false;
}

static PeTTaForm petta_form_cache_lookup(
    const PeTTaSymbolIds *ids, SymbolId symbol) {
    uint32_t slot =
        ((uint32_t)symbol * UINT32_C(2654435761)) &
        (PETTA_FORM_CACHE_CAP - 1u);
    for (uint32_t probe = 0u;
         probe < PETTA_FORM_CACHE_CAP; probe++) {
        const PeTTaFormCacheSlot *entry =
            &ids->form_cache[slot];
        if (entry->form == PETTA_FORM_NONE)
            return PETTA_FORM_NONE;
        if (entry->symbol == symbol)
            return entry->form;
        slot = (slot + 1u) & (PETTA_FORM_CACHE_CAP - 1u);
    }
    return PETTA_FORM_NONE;
}

static bool petta_form_cache_build(PeTTaSymbolIds *ids) {
#define PETTA_CACHE_FORM(symbol, form)                         \
    do {                                                       \
        if (!petta_form_cache_insert(ids, (symbol), (form)))   \
            return false;                                      \
    } while (0)
    PETTA_CACHE_FORM(ids->test, PETTA_FORM_TEST);
    PETTA_CACHE_FORM(g_builtin_syms.if_text, PETTA_FORM_IF);
    PETTA_CACHE_FORM(ids->progn, PETTA_FORM_PROGN);
    PETTA_CACHE_FORM(ids->prog1, PETTA_FORM_PROG1);
    PETTA_CACHE_FORM(ids->foldall, PETTA_FORM_FOLDALL);
    PETTA_CACHE_FORM(ids->forall, PETTA_FORM_FORALL);
    PETTA_CACHE_FORM(ids->maplist, PETTA_FORM_MAPLIST);
    PETTA_CACHE_FORM(g_builtin_syms.map_atom, PETTA_FORM_MAP_ATOM);
    PETTA_CACHE_FORM(ids->foldl, PETTA_FORM_FOLDL);
    PETTA_CACHE_FORM(ids->id, PETTA_FORM_ID);
    PETTA_CACHE_FORM(ids->append, PETTA_FORM_APPEND);
    PETTA_CACHE_FORM(ids->cons, PETTA_FORM_CONS);
    PETTA_CACHE_FORM(ids->int_add, PETTA_FORM_INT_ADD);
    PETTA_CACHE_FORM(ids->stream_unique, PETTA_FORM_STREAM_UNIQUE);
    PETTA_CACHE_FORM(ids->stream_union, PETTA_FORM_STREAM_UNION);
    PETTA_CACHE_FORM(
        ids->stream_intersection,
        PETTA_FORM_STREAM_INTERSECTION);
    PETTA_CACHE_FORM(
        ids->stream_subtraction,
        PETTA_FORM_STREAM_SUBTRACTION);
    PETTA_CACHE_FORM(ids->length, PETTA_FORM_LENGTH);
    PETTA_CACHE_FORM(ids->msort, PETTA_FORM_MSORT);
    PETTA_CACHE_FORM(ids->sort_atom, PETTA_FORM_MSORT);
    PETTA_CACHE_FORM(
        ids->first_from_pair, PETTA_FORM_FIRST_FROM_PAIR);
    PETTA_CACHE_FORM(ids->first, PETTA_FORM_FIRST_FROM_PAIR);
    PETTA_CACHE_FORM(
        ids->second_from_pair, PETTA_FORM_SECOND_FROM_PAIR);
    PETTA_CACHE_FORM(ids->is_var, PETTA_FORM_IS_VAR);
    PETTA_CACHE_FORM(ids->is_ground, PETTA_FORM_IS_GROUND);
    PETTA_CACHE_FORM(ids->is_expr, PETTA_FORM_IS_EXPR);
    PETTA_CACHE_FORM(ids->is_space, PETTA_FORM_IS_SPACE);
    PETTA_CACHE_FORM(ids->is_member, PETTA_FORM_IS_MEMBER);
    PETTA_CACHE_FORM(
        ids->is_alpha_member, PETTA_FORM_IS_ALPHA_MEMBER);
    PETTA_CACHE_FORM(
        ids->alpha_unique_atom, PETTA_FORM_ALPHA_UNIQUE);
    PETTA_CACHE_FORM(ids->list_to_set, PETTA_FORM_LIST_TO_SET);
    PETTA_CACHE_FORM(ids->exclude_item, PETTA_FORM_EXCLUDE_ITEM);
    PETTA_CACHE_FORM(ids->repra, PETTA_FORM_REPRA);
    PETTA_CACHE_FORM(ids->sread, PETTA_FORM_SREAD);
    PETTA_CACHE_FORM(
        g_builtin_syms.bind_bang, PETTA_FORM_BIND_STATE);
    PETTA_CACHE_FORM(
        g_builtin_syms.get_state, PETTA_FORM_GET_STATE);
    PETTA_CACHE_FORM(
        g_builtin_syms.change_state_bang,
        PETTA_FORM_CHANGE_STATE);
    PETTA_CACHE_FORM(
        g_builtin_syms.new_state, PETTA_FORM_NEW_STATE);
    PETTA_CACHE_FORM(ids->call, PETTA_FORM_CALL);
    PETTA_CACHE_FORM(ids->eval, PETTA_FORM_EVAL);
    PETTA_CACHE_FORM(g_builtin_syms.reduce, PETTA_FORM_REDUCE);
    PETTA_CACHE_FORM(ids->predicate, PETTA_FORM_PREDICATE);
    PETTA_CACHE_FORM(
        ids->translate_predicate,
        PETTA_FORM_TRANSLATE_PREDICATE);
    PETTA_CACHE_FORM(
        ids->import_prolog_function,
        PETTA_FORM_IMPORT_PROLOG_FUNCTION);
    PETTA_CACHE_FORM(
        ids->process_metta_string,
        PETTA_FORM_PROCESS_METTA_STRING);
    PETTA_CACHE_FORM(
        ids->call_predicate, PETTA_FORM_CALL_PREDICATE);
    PETTA_CACHE_FORM(
        ids->asserta_predicate, PETTA_FORM_ASSERTA_PREDICATE);
    PETTA_CACHE_FORM(
        ids->assertz_predicate, PETTA_FORM_ASSERTZ_PREDICATE);
    PETTA_CACHE_FORM(
        ids->retract_predicate, PETTA_FORM_RETRACT_PREDICATE);
    PETTA_CACHE_FORM(ids->tabled, PETTA_FORM_TABLED);
    PETTA_CACHE_FORM(
        ids->add_translator_rule,
        PETTA_FORM_ADD_TRANSLATOR_RULE);
    PETTA_CACHE_FORM(
        ids->remove_translator_rule,
        PETTA_FORM_REMOVE_TRANSLATOR_RULE);
    PETTA_CACHE_FORM(ids->cut, PETTA_FORM_CUT);
    PETTA_CACHE_FORM(ids->catch_text, PETTA_FORM_CATCH);
    PETTA_CACHE_FORM(ids->lambda, PETTA_FORM_LAMBDA);
    PETTA_CACHE_FORM(g_builtin_syms.let, PETTA_FORM_LET);
    PETTA_CACHE_FORM(g_builtin_syms.chain, PETTA_FORM_CHAIN);
#undef PETTA_CACHE_FORM
    return true;
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
        ids.callable_marker =
            symbol_intern_cstr(g_symbols, "PeTTa.CallableV1");
        ids.nullary_callable =
            symbol_intern_cstr(g_symbols, "PeTTa.NullaryCallableV1");
        ids.partial = symbol_intern_cstr(g_symbols, "partial");
        ids.library = symbol_intern_cstr(g_symbols, "library");
        ids.value_let =
            symbol_intern_cstr(g_symbols, "PeTTa.ValueLetV1");
        ids.value_chain =
            symbol_intern_cstr(g_symbols, "PeTTa.ValueChainV1");
        ids.open_cons =
            symbol_intern_cstr(g_symbols, "PeTTa.OpenConsV1");
    }
    ids.form_cache_complete = petta_form_cache_build(&ids);
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

PeTTaForm petta_semantics_form(SymbolId head) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    if (head == SYMBOL_ID_NONE || !ids->table)
        return PETTA_FORM_NONE;
    if (ids->form_cache_complete)
        return petta_form_cache_lookup(ids, head);
    if (head == ids->test)
        return PETTA_FORM_TEST;
    if (head == g_builtin_syms.if_text)
        return PETTA_FORM_IF;
    if (head == ids->progn)
        return PETTA_FORM_PROGN;
    if (head == ids->prog1)
        return PETTA_FORM_PROG1;
    if (head == ids->foldall)
        return PETTA_FORM_FOLDALL;
    if (head == ids->forall)
        return PETTA_FORM_FORALL;
    if (head == ids->maplist)
        return PETTA_FORM_MAPLIST;
    if (head == g_builtin_syms.map_atom)
        return PETTA_FORM_MAP_ATOM;
    if (head == ids->foldl)
        return PETTA_FORM_FOLDL;
    if (head == ids->id)
        return PETTA_FORM_ID;
    if (head == ids->append)
        return PETTA_FORM_APPEND;
    if (head == ids->cons)
        return PETTA_FORM_CONS;
    if (head == ids->int_add)
        return PETTA_FORM_INT_ADD;
    if (head == ids->stream_unique)
        return PETTA_FORM_STREAM_UNIQUE;
    if (head == ids->stream_union)
        return PETTA_FORM_STREAM_UNION;
    if (head == ids->stream_intersection)
        return PETTA_FORM_STREAM_INTERSECTION;
    if (head == ids->stream_subtraction)
        return PETTA_FORM_STREAM_SUBTRACTION;
    if (head == ids->length)
        return PETTA_FORM_LENGTH;
    if (head == ids->msort || head == ids->sort_atom)
        return PETTA_FORM_MSORT;
    if (head == ids->first_from_pair || head == ids->first)
        return PETTA_FORM_FIRST_FROM_PAIR;
    if (head == ids->second_from_pair)
        return PETTA_FORM_SECOND_FROM_PAIR;
    if (head == ids->is_var)
        return PETTA_FORM_IS_VAR;
    if (head == ids->is_ground)
        return PETTA_FORM_IS_GROUND;
    if (head == ids->is_expr)
        return PETTA_FORM_IS_EXPR;
    if (head == ids->is_space)
        return PETTA_FORM_IS_SPACE;
    if (head == ids->is_member)
        return PETTA_FORM_IS_MEMBER;
    if (head == ids->is_alpha_member)
        return PETTA_FORM_IS_ALPHA_MEMBER;
    if (head == ids->alpha_unique_atom)
        return PETTA_FORM_ALPHA_UNIQUE;
    if (head == ids->list_to_set)
        return PETTA_FORM_LIST_TO_SET;
    if (head == ids->exclude_item)
        return PETTA_FORM_EXCLUDE_ITEM;
    if (head == ids->repra)
        return PETTA_FORM_REPRA;
    if (head == ids->sread)
        return PETTA_FORM_SREAD;
    if (head == g_builtin_syms.bind_bang)
        return PETTA_FORM_BIND_STATE;
    if (head == g_builtin_syms.get_state)
        return PETTA_FORM_GET_STATE;
    if (head == g_builtin_syms.change_state_bang)
        return PETTA_FORM_CHANGE_STATE;
    if (head == g_builtin_syms.new_state)
        return PETTA_FORM_NEW_STATE;
    if (head == ids->call)
        return PETTA_FORM_CALL;
    if (head == ids->eval)
        return PETTA_FORM_EVAL;
    if (head == g_builtin_syms.reduce)
        return PETTA_FORM_REDUCE;
    if (head == ids->predicate)
        return PETTA_FORM_PREDICATE;
    if (head == ids->translate_predicate)
        return PETTA_FORM_TRANSLATE_PREDICATE;
    if (head == ids->import_prolog_function)
        return PETTA_FORM_IMPORT_PROLOG_FUNCTION;
    if (head == ids->process_metta_string)
        return PETTA_FORM_PROCESS_METTA_STRING;
    if (head == ids->call_predicate)
        return PETTA_FORM_CALL_PREDICATE;
    if (head == ids->asserta_predicate)
        return PETTA_FORM_ASSERTA_PREDICATE;
    if (head == ids->assertz_predicate)
        return PETTA_FORM_ASSERTZ_PREDICATE;
    if (head == ids->retract_predicate)
        return PETTA_FORM_RETRACT_PREDICATE;
    if (head == ids->tabled)
        return PETTA_FORM_TABLED;
    if (head == ids->add_translator_rule)
        return PETTA_FORM_ADD_TRANSLATOR_RULE;
    if (head == ids->remove_translator_rule)
        return PETTA_FORM_REMOVE_TRANSLATOR_RULE;
    if (head == ids->cut)
        return PETTA_FORM_CUT;
    if (head == ids->catch_text)
        return PETTA_FORM_CATCH;
    if (head == ids->lambda)
        return PETTA_FORM_LAMBDA;
    if (head == g_builtin_syms.let)
        return PETTA_FORM_LET;
    if (head == g_builtin_syms.chain)
        return PETTA_FORM_CHAIN;
    return PETTA_FORM_NONE;
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

    CettaExprLen intrinsic = 0u;
    if (petta_semantics_intrinsic_partial_arity(
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
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    return atom && atom->kind == ATOM_EXPR &&
           atom->expr.len == 3u &&
           atom->expr.elems[0]->kind == ATOM_SYMBOL &&
           atom->expr.elems[0]->sym_id == ids->open_cons;
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
        if (cursor->flat_index < cursor->rest->expr.len) {
            *item = cursor->rest->expr.elems[cursor->flat_index++];
            return PETTA_LOGICAL_LIST_ITEM;
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
    if (cursor->rest->kind != ATOM_EXPR) {
        cursor->invalid = true;
        return PETTA_LOGICAL_LIST_INVALID;
    }

    cursor->in_flat_tail = true;
    cursor->flat_index = 0u;
    return petta_semantics_logical_list_cursor_next(cursor, item);
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

Atom *petta_semantics_materialize_closed_logical_list(
    Arena *arena, Atom *list) {
    if (!arena || !list)
        return NULL;
    if (!petta_semantics_is_open_cons_value(list))
        return list->kind == ATOM_EXPR ? list : NULL;

    CettaExprLen length = 0u;
    if (!petta_semantics_logical_list_length(list, &length) ||
        !cetta_expr_len_fits_size(length) ||
        !cetta_expr_len_mul_fits_size(length, sizeof(Atom *))) {
        return NULL;
    }
    Atom **items = length
        ? cetta_malloc((size_t)length * sizeof(*items))
        : NULL;
    PeTTaLogicalListCursor cursor;
    petta_semantics_logical_list_cursor_init(&cursor, list);
    for (CettaExprIndex index = 0u; index < length; index++) {
        Atom *item = NULL;
        if (petta_semantics_logical_list_cursor_next(
                &cursor, &item) != PETTA_LOGICAL_LIST_ITEM) {
            free(items);
            return NULL;
        }
        items[index] = item;
    }
    Atom *extra = NULL;
    if (petta_semantics_logical_list_cursor_next(
            &cursor, &extra) != PETTA_LOGICAL_LIST_END) {
        free(items);
        return NULL;
    }
    Atom *result = atom_expr(arena, items, length);
    free(items);
    return result;
}

Atom *petta_semantics_materialize_logical_list(
    Arena *arena, Atom *list) {
    if (!arena || !list)
        return NULL;
    if (!petta_semantics_is_open_cons_value(list))
        return list->kind == ATOM_EXPR ? list : NULL;

    Atom **heads = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
    Atom *cursor = list;
    while (petta_semantics_is_open_cons_value(cursor)) {
        if (length == capacity) {
            size_t next = capacity ? capacity * 2u : 64u;
            if (next <= capacity ||
                next > SIZE_MAX / sizeof(*heads)) {
                free(heads);
                return NULL;
            }
            void *grown = realloc(heads, sizeof(*heads) * next);
            if (!grown) {
                free(heads);
                return NULL;
            }
            heads = grown;
            capacity = next;
        }
        heads[length++] = cursor->expr.elems[1];
        cursor = cursor->expr.elems[2];
        if (!cursor) {
            free(heads);
            return NULL;
        }
    }

    if (cursor->kind == ATOM_EXPR) {
        if ((uint64_t)cursor->expr.len >
                (uint64_t)(SIZE_MAX - length)) {
            free(heads);
            return NULL;
        }
        size_t total = length + (size_t)cursor->expr.len;
        if (!cetta_expr_len_fits_size((CettaExprLen)total) ||
            total > SIZE_MAX / sizeof(*heads)) {
            free(heads);
            return NULL;
        }
        if (total > capacity) {
            void *grown = realloc(heads, sizeof(*heads) * total);
            if (!grown && total > 0u) {
                free(heads);
                return NULL;
            }
            heads = grown;
        }
        if (cursor->expr.len > 0u) {
            memcpy(heads + length, cursor->expr.elems,
                   sizeof(*heads) * (size_t)cursor->expr.len);
        }
        Atom *result = atom_expr(
            arena, heads, (CettaExprLen)total);
        free(heads);
        return result;
    }

    const PeTTaSymbolIds *ids = petta_symbol_ids();
    Atom *cons = ids->cons != SYMBOL_ID_NONE
        ? atom_symbol_id(arena, ids->cons) : NULL;
    Atom *result = cursor;
    if (!cons) {
        free(heads);
        return NULL;
    }
    for (size_t index = length; index > 0u; index--) {
        result = atom_expr3(
            arena, cons, heads[index - 1u], result);
        if (!result) {
            free(heads);
            return NULL;
        }
    }
    free(heads);
    return result;
}

Atom *petta_semantics_flatten_closed_open_cons(Arena *arena, Atom *atom) {
    if (!arena || !atom || atom->kind != ATOM_EXPR)
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
            atom_symbol_id(arena, ids->open_cons);
        if (!g_petta_symbol_ids.open_cons_tag)
            return NULL;
        g_petta_symbol_ids.open_cons_tag_arena_id = arena->identity;
        g_petta_symbol_ids.open_cons_tag_arena_reset_epoch =
            arena->reset_epoch;
    }
    return atom_expr3(
        arena, g_petta_symbol_ids.open_cons_tag,
        head, tail);
}

/*
 * PeTTa exposes lists as flat expressions, but recursive relational
 * decomposition needs shared tails.  Convert a closed flat list once to the
 * internal open-cons carrier; each subsequent tail is then a pointer into
 * the same persistent spine rather than a copied expression suffix.
 */
Atom *petta_semantics_flat_list_spine(
    Arena *arena, Atom *flat_list) {
    if (!arena || !flat_list ||
        flat_list->kind != ATOM_EXPR) {
        return NULL;
    }
    if (flat_list->expr.len == 0u)
        return flat_list;

    Atom *tail = atom_unit(arena);
    for (CettaExprIndex index = flat_list->expr.len;
         index > 0u; index--) {
        tail = petta_semantics_open_cons_value(
            arena, flat_list->expr.elems[index - 1u], tail);
        if (!tail)
            return NULL;
    }
    return tail;
}

Atom *petta_semantics_construct_value(
    Arena *arena, Atom **elements, CettaExprLen length) {
    if (!arena || (length > 0u && !elements))
        return NULL;
    if (length == 3u && elements[0] &&
        elements[0]->kind == ATOM_SYMBOL &&
        petta_semantics_form(elements[0]->sym_id) == PETTA_FORM_CONS) {
        return petta_semantics_open_cons_value(
            arena, elements[1], elements[2]);
    }
    return atom_expr(arena, elements, length);
}

typedef struct {
    Atom *source;
    Atom **source_children;
    Atom **result_children;
    CettaExprLen length;
    CettaExprIndex next;
    Atom **result_slot;
} PeTTaMaterializeFrame;

static bool petta_materialize_frame_reserve(
    PeTTaMaterializeFrame **frames,
    size_t *capacity, size_t required) {
    if (required <= *capacity)
        return true;
    size_t next = *capacity ? *capacity * 2u : 32u;
    while (next < required) {
        if (next > SIZE_MAX / 2u)
            return false;
        next *= 2u;
    }
    if (next > SIZE_MAX / sizeof(**frames))
        return false;
    void *grown = realloc(*frames, sizeof(**frames) * next);
    if (!grown)
        return false;
    *frames = grown;
    *capacity = next;
    return true;
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

Atom *petta_semantics_materialize_value(
    Arena *arena, Atom *value) {
    if (!arena || !value)
        return NULL;
    Atom *result = NULL;
    PeTTaMaterializeFrame *frames = NULL;
    size_t length = 0u;
    size_t capacity = 0u;

#define PETTA_MATERIALIZE_PUSH(source_atom, destination_slot) do { \
    Atom *petta_source__ = (source_atom); \
    Atom **petta_slot__ = (destination_slot); \
    if (!petta_source__ || !petta_slot__) \
        goto fail; \
    if (petta_source__->kind != ATOM_EXPR || \
        petta_materialize_opaque_value(petta_source__)) { \
        *petta_slot__ = atom_deep_copy(arena, petta_source__); \
        if (!*petta_slot__) \
            goto fail; \
        break; \
    } \
    if (petta_semantics_is_open_cons_value(petta_source__)) { \
        petta_source__ = petta_semantics_materialize_logical_list( \
            arena, petta_source__); \
        if (!petta_source__) \
            goto fail; \
    } \
    Atom **petta_sources__ = petta_source__->expr.elems; \
    CettaExprLen petta_count__ = petta_source__->expr.len; \
    if (!cetta_expr_len_mul_fits_size( \
            petta_count__, sizeof(Atom *)) || \
        !petta_materialize_frame_reserve( \
            &frames, &capacity, length + 1u)) { \
        goto fail; \
    } \
    Atom **petta_results__ = petta_count__ \
        ? arena_alloc( \
              arena, sizeof(*petta_results__) * (size_t)petta_count__) \
        : NULL; \
    if (petta_count__ > 0u && !petta_results__) { \
        goto fail; \
    } \
    frames[length++] = (PeTTaMaterializeFrame){ \
        .source = petta_source__, \
        .source_children = petta_sources__, \
        .result_children = petta_results__, \
        .length = petta_count__, \
        .result_slot = petta_slot__, \
    }; \
} while (0)

    PETTA_MATERIALIZE_PUSH(value, &result);
    while (length > 0u) {
        PeTTaMaterializeFrame *frame = &frames[length - 1u];
        if (frame->next < frame->length) {
            CettaExprIndex index = frame->next++;
            Atom *child = frame->source_children[index];
            Atom **slot = &frame->result_children[index];
            PETTA_MATERIALIZE_PUSH(child, slot);
            continue;
        }
        Atom *built = atom_expr(
            arena, frame->result_children, frame->length);
        if (!built)
            goto fail;
        *frame->result_slot = built;
        length--;
    }
    free(frames);
#undef PETTA_MATERIALIZE_PUSH
    return result;

fail:
    free(frames);
#undef PETTA_MATERIALIZE_PUSH
    return NULL;
}

typedef struct {
    Atom *left;
    Atom *right;
} PeTTaConsMatchPair;

typedef struct {
    const Atom *pattern;
    const Atom *value;
} PeTTaConsShapePair;

static bool petta_cons_match_pair_push(
    PeTTaConsMatchPair **pairs, size_t *length, size_t *capacity,
    Atom *left, Atom *right) {
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
        (PeTTaConsMatchPair){.left = left, .right = right};
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

        if (petta_semantics_is_cons_constraint(pattern)) {
            if (petta_semantics_is_cons_constraint(value))
                continue;
            if (value->kind == ATOM_EXPR &&
                value->expr.len > 0u) {
                continue;
            }
            free(pairs);
            return false;
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

bool petta_semantics_match_cons_constraint(
    Arena *arena, Atom *constraint, Atom *value,
    BindingsBuilder *builder) {
    if (!arena || !constraint || !value || !builder)
        return false;

    uint32_t entry_mark = bindings_builder_save(builder);
    PeTTaConsMatchPair *pairs = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
    if (!petta_cons_match_pair_push(
            &pairs, &length, &capacity, constraint, value)) {
        return false;
    }

    while (length > 0u) {
        PeTTaConsMatchPair pair = pairs[--length];
        const Bindings *current =
            bindings_builder_bindings(builder);
        Atom *left = bindings_apply_if_vars(
            current, arena, pair.left);
        Atom *right = bindings_apply_if_vars(
            current, arena, pair.right);
        if (!left || !right)
            goto fail;

        bool left_cons =
            petta_semantics_is_cons_constraint(left);
        bool right_cons =
            petta_semantics_is_cons_constraint(right);
        /*
         * An open PeTTa list is represented by a variable whose binding is a
         * cons spine.  Bind that variable to the spine before attempting
         * structural decomposition; otherwise the first relational
         * `member/2` clause could inspect closed lists but could never
         * construct an open one.
         */
        if (left->kind == ATOM_VAR || right->kind == ATOM_VAR) {
            if (!match_atoms_builder(left, right, builder))
                goto fail;
            continue;
        }
        if (!left_cons && !right_cons &&
            left->kind == ATOM_EXPR &&
            right->kind == ATOM_EXPR &&
            left->expr.len == right->expr.len) {
            for (CettaExprIndex index = left->expr.len;
                 index > 0u; index--) {
                CettaExprIndex child = index - 1u;
                if (!petta_cons_match_pair_push(
                        &pairs, &length, &capacity,
                        left->expr.elems[child],
                        right->expr.elems[child])) {
                    goto fail;
                }
            }
            continue;
        }
        if (!left_cons && !right_cons) {
            if (!match_atoms_builder(left, right, builder))
                goto fail;
            continue;
        }

        /*
         * A cons constraint against a closed flat list is the one boundary
         * where the observable tuple carrier enters relational traversal.
         * Reify the flat side as a shared spine once.  Binding a tail
         * variable then retains a suffix pointer, so recursive clauses do
         * not allocate and retain N, N-1, ... element arrays.
         */
        if (left_cons && !right_cons &&
            right->kind == ATOM_EXPR &&
            right->expr.len > 0u) {
            right = petta_semantics_flat_list_spine(arena, right);
            if (!right)
                goto fail;
            right_cons = true;
        } else if (right_cons && !left_cons &&
                   left->kind == ATOM_EXPR &&
                   left->expr.len > 0u) {
            left = petta_semantics_flat_list_spine(arena, left);
            if (!left)
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
        } else {
            goto fail;
        }
        if (right_cons) {
            right_head = right->expr.elems[1];
            right_tail = right->expr.elems[2];
        } else {
            goto fail;
        }
        if (!petta_cons_match_pair_push(
                &pairs, &length, &capacity,
                left_tail, right_tail) ||
            !petta_cons_match_pair_push(
                &pairs, &length, &capacity,
                left_head, right_head)) {
            goto fail;
        }
    }

    free(pairs);
    return true;

fail:
    free(pairs);
    bindings_builder_rollback(builder, entry_mark);
    return false;
}

typedef struct {
    Atom *atom;
} PeTTaConsWalkItem;

static bool petta_semantics_contains_cons_walk(
    const Atom *root, bool observable_open_only) {
    if (!root)
        return false;
    PeTTaConsWalkItem *stack = NULL;
    size_t length = 0u;
    size_t capacity = 0u;
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
    for (CettaExprIndex index = root->expr.len;
         index > 0u; index--) {
        if (length == capacity) {
            size_t next = capacity ? capacity * 2u : 32u;
            if (next <= capacity ||
                next > SIZE_MAX / sizeof(*stack)) {
                free(stack);
                /* Unknown must take the conservative cons-capable route. */
                return true;
            }
            stack = stack
                ? cetta_realloc(stack, sizeof(*stack) * next)
                : cetta_malloc(sizeof(*stack) * next);
            capacity = next;
        }
        stack[length++] = (PeTTaConsWalkItem){
            .atom = root->expr.elems[index - 1u],
        };
    }
    while (length > 0u) {
        PeTTaConsWalkItem item = stack[--length];
        Atom *atom = item.atom;
        bool matches = observable_open_only
            ? petta_semantics_is_open_cons_value(atom)
            : petta_semantics_is_cons_constraint(atom);
        if (matches) {
            free(stack);
            return true;
        }
        if (!atom || atom->kind != ATOM_EXPR ||
            (observable_open_only &&
             petta_materialize_opaque_value(atom))) {
            continue;
        }
        for (CettaExprIndex index = atom->expr.len;
             index > 0u; index--) {
            if (length == capacity) {
                size_t next = capacity ? capacity * 2u : 32u;
                if (next <= capacity ||
                    next > SIZE_MAX / sizeof(*stack)) {
                    free(stack);
                    /* Unknown must take the conservative cons-capable route. */
                    return true;
                }
                stack = cetta_realloc(
                    stack, sizeof(*stack) * next);
                capacity = next;
            }
            stack[length++] = (PeTTaConsWalkItem){
                .atom = atom->expr.elems[index - 1u],
            };
        }
    }
    free(stack);
    return false;
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
    if (head == g_builtin_syms.let ||
        head == g_builtin_syms.chain) {
        if (arity)
            *arity = 3u;
        return true;
    }
    return false;
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

bool petta_semantics_library_descriptor(
    const Atom *atom, const char **member) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    if (member)
        *member = NULL;
    if (!atom || !member || !ids->table ||
        atom->kind != ATOM_EXPR || atom->expr.len != 2u ||
        !atom_is_symbol_id(atom->expr.elems[0], ids->library)) {
        return false;
    }
    Atom *name = atom->expr.elems[1];
    if (name->kind == ATOM_SYMBOL) {
        *member = atom_name_cstr(name);
        return *member != NULL;
    }
    if (name->kind == ATOM_GROUNDED &&
        name->ground.gkind == GV_STRING) {
        *member = name->ground.sval;
        return *member != NULL;
    }
    return false;
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

bool petta_semantics_library_file_descriptor(
    const Atom *atom, const char **root,
    const char **member) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    if (root)
        *root = NULL;
    if (member)
        *member = NULL;
    if (!atom || !root || !member || !ids->table ||
        atom->kind != ATOM_EXPR || atom->expr.len != 3u ||
        !atom_is_symbol_id(atom->expr.elems[0], ids->library)) {
        return false;
    }
    *root = petta_semantics_path_component(
        atom->expr.elems[1]);
    *member = petta_semantics_path_component(
        atom->expr.elems[2]);
    return *root != NULL && *member != NULL;
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
        return atom_empty(arena);
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
    Atom *step = atom_expr3(
        arena, form->expr.elems[1], acc, item);
    Atom *elems[6] = {
        atom_symbol_id(arena, g_builtin_syms.fold),
        form->expr.elems[2],
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

static Atom *petta_stream_unique_lower(
    Arena *arena, Atom *form, SymbolId reify_head) {
    if (form->expr.len != 2u)
        return NULL;
    Atom *reified = atom_expr2(
        arena, atom_symbol_id(arena, reify_head),
        form->expr.elems[1]);
    Atom *unique = atom_expr2(
        arena, atom_symbol_id(arena, g_builtin_syms.unique_atom),
        reified);
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
        return petta_stream_unique_lower(arena, form, reify_head);
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
    PETTA_TERM_ATOM = 3,
    PETTA_TERM_COMPOUND = 4,
} PeTTaTermClass;

static bool petta_grounded_is_number(const Atom *atom) {
    if (!atom || atom->kind != ATOM_GROUNDED)
        return false;
    return atom->ground.gkind == GV_INT ||
           atom->ground.gkind == GV_FLOAT ||
           atom->ground.gkind == GV_BIGINT ||
           atom->ground.gkind == GV_RATIONAL;
}

static PeTTaTermClass petta_term_class(const Atom *atom) {
    if (atom->kind == ATOM_VAR)
        return PETTA_TERM_VARIABLE;
    if (petta_grounded_is_number(atom))
        return PETTA_TERM_NUMBER;
    if (atom->kind == ATOM_GROUNDED &&
        atom->ground.gkind == GV_STRING) {
        return PETTA_TERM_STRING;
    }
    if (atom->kind == ATOM_EXPR && atom->expr.len > 0u)
        return PETTA_TERM_COMPOUND;
    return PETTA_TERM_ATOM;
}

static int petta_compare_u64(uint64_t left, uint64_t right) {
    return left < right ? -1 : left > right ? 1 : 0;
}

static int petta_compare_text(const char *left, const char *right) {
    int raw = strcmp(left ? left : "", right ? right : "");
    return raw < 0 ? -1 : raw > 0 ? 1 : 0;
}

static const char *petta_known_atom_text(const Atom *atom) {
    if (atom->kind == ATOM_SYMBOL)
        return symbol_bytes(g_symbols, atom->sym_id);
    if (atom->kind == ATOM_EXPR && atom->expr.len == 0u)
        return "[]";
    if (atom->kind == ATOM_GROUNDED &&
        atom->ground.gkind == GV_BOOL) {
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

static bool petta_compare_numbers(
    const Atom *left, const Atom *right, int *ordering) {
    bool left_float = left->ground.gkind == GV_FLOAT;
    bool right_float = right->ground.gkind == GV_FLOAT;
    if (left_float && right_float) {
        *ordering = petta_compare_float_values(
            left->ground.fval, right->ground.fval);
        return true;
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
    const char *left_text = petta_known_atom_text(left);
    const char *right_text = petta_known_atom_text(right);
    if (left_text && right_text)
        return petta_compare_text(left_text, right_text);
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
    case GV_FOREIGN:
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

bool petta_semantics_term_compare(
    const Atom *left, const Atom *right, int *ordering) {
    if (!left || !right || !ordering)
        return false;
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
        *ordering = petta_compare_text(
            left->ground.sval, right->ground.sval);
        return true;
    case PETTA_TERM_ATOM: {
        const char *left_text = petta_known_atom_text(left);
        const char *right_text = petta_known_atom_text(right);
        if (left_text && right_text) {
            *ordering = petta_compare_text(left_text, right_text);
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
        break;
    }

    CettaExprLen shared = left->expr.len < right->expr.len
        ? left->expr.len
        : right->expr.len;
    for (CettaExprIndex index = 0u; index < shared; index++) {
        int element_order = 0;
        if (!petta_semantics_term_compare(
                left->expr.elems[index],
                right->expr.elems[index], &element_order)) {
            return false;
        }
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

Atom *petta_semantics_msort(Arena *arena, Atom *list) {
    if (!arena || !list || list->kind != ATOM_EXPR)
        return NULL;
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

Atom *petta_semantics_lambda_value(
    Arena *arena, Atom *canonical_body) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    if (!arena || !canonical_body || !ids->table)
        return NULL;
    return atom_expr3(
        arena,
        atom_symbol_id(arena, ids->canonical_lam),
        atom_symbol_id(arena, ids->callable_marker),
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
        !atom_is_symbol_id(atom->expr.elems[1], ids->callable_marker)) {
        return false;
    }
    if (canonical_body)
        *canonical_body = atom->expr.elems[2];
    return true;
}

Atom *petta_semantics_nullary_lambda_value(
    Arena *arena, Atom *body) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    if (!arena || !body || !ids->table)
        return NULL;
    return atom_expr2(
        arena, atom_symbol_id(arena, ids->nullary_callable), body);
}

bool petta_semantics_nullary_lambda_body(
    const Atom *atom, Atom **body) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    if (body)
        *body = NULL;
    if (!atom || !ids->table || atom->kind != ATOM_EXPR ||
        atom->expr.len != 2u ||
        !atom_is_symbol_id(atom->expr.elems[0], ids->nullary_callable)) {
        return false;
    }
    if (body)
        *body = atom->expr.elems[1];
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
        arena, atom_symbol_id(arena, ids->partial), base, bound);
}

bool petta_semantics_partial_view(
    const Atom *atom, Atom **base, Atom **arguments) {
    const PeTTaSymbolIds *ids = petta_symbol_ids();
    if (base)
        *base = NULL;
    if (arguments)
        *arguments = NULL;
    if (!atom || !ids->table || atom->kind != ATOM_EXPR ||
        atom->expr.len != 3u ||
        !atom_is_symbol_id(atom->expr.elems[0], ids->partial) ||
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
