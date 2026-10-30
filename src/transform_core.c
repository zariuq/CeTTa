#include "transform_core.h"

#include "equation_presentation.h"
#include "match.h"
#include "parser.h"
#include "petta_semantics.h"
#include "symbol.h"
#include "term_universe.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CETTA_TRANSFORM_REFERENCE=1 runs the authored firing, so the two can be
 * compared. */
static bool transform_reference(void) {
    static int reference = -1;
    if (reference < 0) {
        const char *setting = getenv("CETTA_TRANSFORM_REFERENCE");
        reference = setting && strcmp(setting, "1") == 0;
    }
    return reference != 0;
}

/* CETTA_TRANSFORM_TRACE=1 reports the presentation verdict and each firing
 * the realization answers or declines on stderr. */
static bool transform_trace(void) {
    static int trace = -1;
    if (trace < 0) {
        const char *setting = getenv("CETTA_TRANSFORM_TRACE");
        trace = setting && strcmp(setting, "1") == 0;
    }
    return trace != 0;
}

/* ── The library presentation ──────────────────────────────────────────── */

static _Thread_local struct {
    SpaceProgramToken program;
    char path[1024];
    bool admitted;
} g_transform_presentation;

static bool transform_equation_head(Atom *atom, SymbolId *head_out) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 3u ||
        !atom_is_symbol(atom->expr.elems[0], "="))
        return false;
    SymbolId head = atom_head_symbol_id(atom->expr.elems[1]);
    if (head == SYMBOL_ID_NONE)
        return false;
    *head_out = head;
    return true;
}

static bool transform_declaration_subject(Atom *atom, SymbolId *subject_out) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 3u ||
        !atom_is_symbol(atom->expr.elems[0], ":") ||
        atom->expr.elems[1]->kind != ATOM_SYMBOL)
        return false;
    *subject_out = atom_symbol_current_id(atom->expr.elems[1]);
    return true;
}

/* The live declarations of `subject` are exactly the library's, in order. */
static bool transform_declarations_exact(Arena *scratch, Space *program,
                                         SymbolId subject, Atom **library,
                                         size_t library_len) {
    Atom **types = NULL;
    uint32_t ntypes = space_get_declared_types(
        program, scratch, atom_symbol_id(scratch, subject), &types);
    bool exact = ntypes == library_len;
    for (size_t i = 0u; exact && i < library_len; i++)
        exact = atom_alpha_eq(types[i], library[i]->expr.elems[2]);
    free(types);
    return exact;
}

static bool transform_presentation_check(Space *program, const char *path) {
    Arena scratch;
    arena_init(&scratch);
    /* Read with the lane's own reader, as an import of the file reads it
     * into the program's universe. */
    TermUniverse *universe = program->native.universe;
    AtomId *ids = NULL;
    int count = universe ? parse_metta_file_ids(path, universe, &ids) : -1;
    Atom **atoms = count > 0 ? cetta_malloc(sizeof(Atom *) * (size_t)count) : NULL;
    bool admitted = count > 0 && atoms;
    for (int i = 0; admitted && i < count; i++) {
        atoms[i] = term_universe_copy_atom(universe, &scratch, ids[i]);
        admitted = atoms[i] != NULL;
    }
    /* Each head the library defines, with its equations and declarations in
     * file order. */
    for (int i = 0; admitted && i < count; i++) {
        SymbolId head;
        bool equation = transform_equation_head(atoms[i], &head);
        if (!equation && !transform_declaration_subject(atoms[i], &head))
            continue;
        bool first = true;
        for (int j = 0; j < i && first; j++) {
            SymbolId earlier;
            if ((transform_equation_head(atoms[j], &earlier) ||
                 transform_declaration_subject(atoms[j], &earlier)) &&
                earlier == head)
                first = false;
        }
        if (!first)
            continue;
        size_t equations = 0u, declarations = 0u;
        Atom **eq = cetta_malloc(sizeof(Atom *) * (size_t)count);
        Atom **decl = cetta_malloc(sizeof(Atom *) * (size_t)count);
        for (int j = i; j < count; j++) {
            SymbolId other;
            if (transform_equation_head(atoms[j], &other) && other == head)
                eq[equations++] = atoms[j];
            else if (transform_declaration_subject(atoms[j], &other) &&
                     other == head)
                decl[declarations++] = atoms[j];
        }
        if (equations > 0u)
            admitted = cetta_equation_presentation_exact_atoms(
                &scratch, program, head, eq, equations);
        admitted = admitted && transform_declarations_exact(
            &scratch, program, head, decl, declarations);
        if (!admitted && transform_trace())
            fprintf(stderr, "[transform] presentation differs at %s\n",
                    symbol_bytes(g_symbols, head));
        free(eq);
        free(decl);
    }
    /* The library's data constructors stay data: no equation defines one. */
    static const char *const constructors[] = {
        "tf:fired", "tf:cand", "tf:row", "tf:at", "tf:ok",
        "tf:caller", "tf:kernel", "tf:extension", "tf:plain",
    };
    for (size_t c = 0u; admitted && c < sizeof(constructors) / sizeof(constructors[0]); c++) {
        SymbolId symbol = symbol_intern_cstr(g_symbols, constructors[c]);
        SpaceEquationCursor cursor;
        if (!space_equation_cursor_init(program, symbol, &cursor))
            continue;
        for (;;) {
            SpaceEquationOccurrenceId id;
            SpaceEquationCursorStep step = space_equation_cursor_next(&cursor, &id);
            if (step == SPACE_EQUATION_CURSOR_END)
                break;
            SpaceEquationOccurrence occurrence;
            if (step != SPACE_EQUATION_CURSOR_ITEM ||
                !space_equation_occurrence_resolve(id, &occurrence) ||
                atom_head_symbol_id(occurrence.lhs) == symbol ||
                (occurrence.lhs && occurrence.lhs->kind == ATOM_SYMBOL &&
                 atom_symbol_current_id(occurrence.lhs) == symbol)) {
                admitted = false;
                break;
            }
        }
    }
    free(atoms);
    free(ids);
    arena_free(&scratch);
    return admitted;
}

bool cetta_transform_presentation_admitted(Space *program,
                                           const char *library_path) {
    if (!program || !library_path || !*library_path || transform_reference())
        return false;
    SpaceProgramToken token = space_program_token(program);
    if (space_program_token_eq(g_transform_presentation.program, token) &&
        strcmp(g_transform_presentation.path, library_path) == 0)
        return g_transform_presentation.admitted;
    bool admitted = strlen(library_path) < sizeof(g_transform_presentation.path) &&
        transform_presentation_check(program, library_path);
    g_transform_presentation.program = token;
    snprintf(g_transform_presentation.path, sizeof(g_transform_presentation.path),
             "%s", library_path);
    g_transform_presentation.admitted = admitted;
    if (transform_trace())
        fprintf(stderr, "[transform] presentation %s: %s\n",
                admitted ? "admitted" : "refused", library_path);
    return admitted;
}

/* ── The plan ──────────────────────────────────────────────────────────── */

typedef enum {
    TF_READ = 0,
    TF_TAKE,
    TF_ABSENT,
    TF_UNIQUE,
    TF_ALL,
    TF_LET,
} TfKind;

typedef struct {
    TfKind kind;
    TfKind access;     /* TF_READ or TF_TAKE: how a matched atom is used */
    Atom *space_ref;   /* as the conjunct names it */
    Space *space;
    Atom *pattern;     /* the matched pattern, or a let's pattern */
    Atom *collection;  /* all: the collection pattern */
    Atom *expression;  /* let: the code */
} TfConjunct;

typedef struct {
    TfConjunct *items;
    size_t len, cap;
    size_t kernel_len; /* conjuncts before the first `every` */
    bool has_every;
    bool has_let;
} TfPlan;

typedef enum {
    TF_MODE_PLAIN = 0,
    TF_MODE_READ,
    TF_MODE_TAKE,
    TF_MODE_ABSENT,
    TF_MODE_UNIQUE,
    TF_MODE_ALL,
    TF_MODE_EVERY,
    TF_MODE_LET,
} TfMode;

/* tf:mode: an expression headed by a mode word; anything else is a pattern. */
static TfMode tf_mode(Atom *c) {
    if (!c || c->kind != ATOM_EXPR || c->expr.len == 0u)
        return TF_MODE_PLAIN;
    Atom *head = c->expr.elems[0];
    if (head->kind != ATOM_SYMBOL)
        return TF_MODE_PLAIN;
    static const struct { const char *name; TfMode mode; } words[] = {
        {"read", TF_MODE_READ}, {"take", TF_MODE_TAKE},
        {"absent", TF_MODE_ABSENT}, {"unique", TF_MODE_UNIQUE},
        {"all", TF_MODE_ALL}, {"every", TF_MODE_EVERY}, {"let", TF_MODE_LET},
    };
    for (size_t i = 0u; i < sizeof(words) / sizeof(words[0]); i++)
        if (atom_is_symbol(head, words[i].name))
            return words[i].mode;
    return TF_MODE_PLAIN;
}

static bool tf_is_comma(Atom *x) {
    return x && x->kind == ATOM_EXPR && x->expr.len > 0u &&
           atom_is_symbol(x->expr.elems[0], ",");
}

typedef struct {
    const CettaTransformHost *host;
    Atom *default_space;
} TfPlanner;

static bool tf_plan_push(TfPlan *plan, TfConjunct conjunct) {
    if (plan->len == plan->cap) {
        size_t cap = plan->cap ? plan->cap * 2u : 8u;
        TfConjunct *items = cetta_realloc(plan->items, cap * sizeof(*items));
        if (!items)
            return false;
        plan->items = items;
        plan->cap = cap;
    }
    plan->items[plan->len++] = conjunct;
    return true;
}

/* tf:locate for a read, take or plain conjunct: its access, space and pattern. */
static bool tf_locate(const TfPlanner *planner, Atom *c, TfKind *access,
                      Atom **space_ref, Atom **pattern) {
    TfMode mode = tf_mode(c);
    if (mode == TF_MODE_PLAIN) {
        *access = TF_READ;
        *space_ref = planner->default_space;
        *pattern = c;
        return true;
    }
    if (mode != TF_MODE_READ && mode != TF_MODE_TAKE && mode != TF_MODE_ABSENT)
        return false;
    *access = mode == TF_MODE_TAKE ? TF_TAKE : TF_READ;
    if (c->expr.len == 3u) {
        *space_ref = c->expr.elems[1];
        *pattern = c->expr.elems[2];
        return true;
    }
    if (c->expr.len == 2u) {
        *space_ref = planner->default_space;
        *pattern = c->expr.elems[1];
        return true;
    }
    return false;
}

static bool tf_resolve(const TfPlanner *planner, TfConjunct *conjunct) {
    conjunct->space = NULL;
    return planner->host->resolve_space(planner->host->context,
                                        conjunct->space_ref, &conjunct->space);
}

/* The library's walk, flattened: an `every` splices its inner conjuncts, and
 * the first one ends the kernel. */
static bool tf_plan_list(const TfPlanner *planner, TfPlan *plan,
                         Atom **conjuncts, size_t len) {
    for (size_t i = 0u; i < len; i++) {
        Atom *c = conjuncts[i];
        TfConjunct conjunct = {0};
        switch (tf_mode(c)) {
        case TF_MODE_EVERY: {
            if (c->expr.len != 2u)
                return false;
            if (!plan->has_every) {
                plan->has_every = true;
                plan->kernel_len = plan->len;
            }
            /* tf:conjuncts of the inner sources */
            Atom *inner = c->expr.elems[1];
            bool spliced;
            if (inner->kind == ATOM_EXPR && inner->expr.len == 0u)
                spliced = true;
            else if (tf_is_comma(inner))
                spliced = tf_plan_list(planner, plan, inner->expr.elems + 1,
                                       inner->expr.len - 1u);
            else
                spliced = tf_plan_list(planner, plan, &inner, 1u);
            if (!spliced)
                return false;
            continue;
        }
        case TF_MODE_LET:
            if (c->expr.len != 3u)
                return false;
            conjunct.kind = TF_LET;
            conjunct.pattern = c->expr.elems[1];
            conjunct.expression = c->expr.elems[2];
            plan->has_let = true;
            break;
        case TF_MODE_UNIQUE:
        case TF_MODE_ALL: {
            bool all = tf_mode(c) == TF_MODE_ALL;
            if (c->expr.len != (all ? 3u : 2u))
                return false;
            Atom *inner = c->expr.elems[1];
            TfMode inner_mode = tf_mode(inner);
            if (inner_mode != TF_MODE_PLAIN && inner_mode != TF_MODE_READ &&
                inner_mode != TF_MODE_TAKE)
                return false;
            conjunct.kind = all ? TF_ALL : TF_UNIQUE;
            conjunct.collection = all ? c->expr.elems[2] : NULL;
            if (!tf_locate(planner, inner, &conjunct.access,
                           &conjunct.space_ref, &conjunct.pattern) ||
                !tf_resolve(planner, &conjunct))
                return false;
            break;
        }
        case TF_MODE_ABSENT:
            conjunct.kind = TF_ABSENT;
            if (!tf_locate(planner, c, &conjunct.access, &conjunct.space_ref,
                           &conjunct.pattern) ||
                !tf_resolve(planner, &conjunct))
                return false;
            conjunct.access = TF_READ;
            break;
        case TF_MODE_PLAIN:
        case TF_MODE_READ:
        case TF_MODE_TAKE:
            if (!tf_locate(planner, c, &conjunct.access, &conjunct.space_ref,
                           &conjunct.pattern) ||
                !tf_resolve(planner, &conjunct))
                return false;
            conjunct.kind = conjunct.access;
            break;
        }
        if (!tf_plan_push(plan, conjunct))
            return false;
    }
    return true;
}

/* ── Proposal ──────────────────────────────────────────────────────────── */

typedef struct {
    TfKind access;
    Atom *space_ref;
    Space *space;
    Atom *atom;
} TfEntry;

typedef struct {
    TfEntry *items;
    size_t len, cap;
} TfEntries;

typedef struct {
    TfEntries entries;
    Atom *instance;
} TfRow;

typedef struct {
    TfEntries kernel;
    TfRow *rows;
    size_t row_len;
} TfCandidate;

typedef enum {
    TF_CONTINUE = 0,
    TF_STOP,      /* a candidate validated, or committed */
    TF_FAIL,      /* capacity refusal before anything ran: decline */
    TF_ERROR,     /* an error after something ran: answered */
} TfStatus;

typedef struct {
    const CettaTransformHost *host;
    Arena *scratch;
    Arena *answer_arena;
    TfPlan plan;
    Atom *template_atom;
    BindingsBuilder builder;
    CettaFrameIdentityScope *scope;
    TfEntries entries;      /* the footprint of the row being proposed */
    bool extension;         /* proposing the rows of an `every` */
    size_t extension_start; /* entries before the extension */
    TfRow *rows;            /* the extension rows of the current kernel match */
    size_t row_len, row_cap;
    TfCandidate *candidates;/* kept when a let may run code: validated after;
                             * otherwise the first that validates, committed
                             * after the proposal has released its readers */
    size_t candidate_len, candidate_cap;
    bool effects;           /* code ran: a decline is no longer possible */
    Atom *answer;
    Atom *error;
} TfFire;

static bool tf_entries_push(TfEntries *entries, TfEntry entry) {
    if (entries->len == entries->cap) {
        size_t cap = entries->cap ? entries->cap * 2u : 8u;
        TfEntry *items = cetta_realloc(entries->items, cap * sizeof(*items));
        if (!items)
            return false;
        entries->items = items;
        entries->cap = cap;
    }
    entries->items[entries->len++] = entry;
    return true;
}

static Bindings *tf_bindings(TfFire *f) {
    return (Bindings *)bindings_builder_bindings(&f->builder);
}

static Atom *tf_apply(TfFire *f, Atom *atom) {
    return bindings_apply(tf_bindings(f), f->scratch, atom);
}

typedef TfStatus (*TfVisit)(TfFire *f, void *data);

/* Each stored atom of `space` matching `pattern` under the current bindings,
 * in declaration order, as `match` reads them; `visit` sees the bindings the
 * match adds, which are undone after it. */
static TfStatus tf_each_match(TfFire *f, Space *space, Atom *pattern,
                              TfVisit visit, void *data) {
    if (!space)
        return TF_CONTINUE; /* an empty location */
    Atom *resolved = atom_has_vars(pattern) ? tf_apply(f, pattern) : pattern;
    Atom *index = resolved
        ? petta_semantics_match_index_pattern(f->scratch, resolved, NULL)
        : NULL;
    if (!index)
        return f->effects ? TF_ERROR : TF_FAIL;
    SpaceOccurrenceCursor cursor;
    space_occurrence_cursor_init_empty(&cursor);
    if (!space_occurrence_cursor_init(space, index, &cursor))
        return f->effects ? TF_ERROR : TF_FAIL;
    TfStatus status = TF_CONTINUE;
    for (;;) {
        Atom *candidate = NULL;
        CettaIndex index = 0u;
        SpaceOccurrenceCursorStep step =
            space_occurrence_cursor_next(&cursor, &index, &candidate);
        if (step == SPACE_OCCURRENCE_CURSOR_END)
            break;
        if (step != SPACE_OCCURRENCE_CURSOR_ITEM) {
            status = f->effects ? TF_ERROR : TF_FAIL;
            break;
        }
        ArenaMark candidate_mark = arena_mark(f->scratch);
        if (cursor.source_ops) {
            candidate = space_occurrence_cursor_receive_atom(&cursor, index, f->scratch);
            if (!candidate) {
                status = f->effects ? TF_ERROR : TF_FAIL;
                break;
            }
        }
        if (!candidate)
            continue;
        if (atom_has_vars(candidate)) {
            CettaFrameIdentity epoch = CETTA_FRAME_IDENTITY_NONE;
            if (!cetta_frame_identity_scope_try(f->scope, &epoch)) {
                status = f->effects ? TF_ERROR : TF_FAIL;
                break;
            }
            candidate = atom_freshen_epoch(f->scratch, candidate, epoch);
            if (!candidate) {
                status = f->effects ? TF_ERROR : TF_FAIL;
                break;
            }
        }
        uint32_t mark = bindings_builder_save(&f->builder);
        bool matched = match_atoms_builder(pattern, candidate, &f->builder, f->scratch);
        if (matched)
            status = visit(f, data);
        bindings_builder_rollback(&f->builder, mark);
        if (!matched && cursor.source_ops)
            arena_reset(f->scratch, candidate_mark);
        if (status != TF_CONTINUE)
            break;
    }
    space_occurrence_cursor_release(&cursor);
    return status;
}

static TfStatus tf_walk(TfFire *f, size_t at);
static TfStatus tf_candidate(TfFire *f, TfEntries *kernel, TfRow *rows,
                             size_t row_len);

typedef struct {
    size_t at;
    const TfConjunct *conjunct;
} TfStep;

static TfStatus tf_after_match(TfFire *f, void *data) {
    TfStep *step = data;
    Atom *matched = tf_apply(f, step->conjunct->pattern);
    if (!matched ||
        !tf_entries_push(&f->entries, (TfEntry){
            step->conjunct->access, step->conjunct->space_ref,
            step->conjunct->space, matched}))
        return f->effects ? TF_ERROR : TF_FAIL;
    TfStatus status = tf_walk(f, step->at + 1u);
    f->entries.len--;
    return status;
}

static TfStatus tf_found_any(TfFire *f, void *data) {
    (void)f;
    *(bool *)data = true;
    return TF_STOP;
}

typedef struct {
    Atom **items;
    size_t len, cap;
    size_t limit;
    const TfConjunct *conjunct;
} TfFound;

static TfStatus tf_collect(TfFire *f, void *data) {
    TfFound *found = data;
    if (found->len == found->cap) {
        size_t cap = found->cap ? found->cap * 2u : 8u;
        Atom **items = cetta_realloc(found->items, cap * sizeof(*items));
        if (!items)
            return f->effects ? TF_ERROR : TF_FAIL;
        found->items = items;
        found->cap = cap;
    }
    Atom *instance = tf_apply(f, found->conjunct->pattern);
    if (!instance)
        return f->effects ? TF_ERROR : TF_FAIL;
    found->items[found->len++] = instance;
    return found->limit && found->len >= found->limit ? TF_STOP : TF_CONTINUE;
}

static TfStatus tf_found_status(TfStatus status) {
    return status == TF_STOP ? TF_CONTINUE : status;
}

static TfStatus tf_finish(TfFire *f) {
    Atom *instance = tf_apply(f, f->template_atom);
    if (!instance)
        return f->effects ? TF_ERROR : TF_FAIL;
    if (!f->extension) {
        TfEntries kernel = f->entries;
        TfRow row = {{NULL, 0u, 0u}, instance};
        return tf_candidate(f, &kernel, &row, 1u);
    }
    if (f->row_len == f->row_cap) {
        size_t cap = f->row_cap ? f->row_cap * 2u : 8u;
        TfRow *rows = cetta_realloc(f->rows, cap * sizeof(*rows));
        if (!rows)
            return f->effects ? TF_ERROR : TF_FAIL;
        f->rows = rows;
        f->row_cap = cap;
    }
    size_t own = f->entries.len - f->extension_start;
    TfRow row = {{NULL, 0u, 0u}, instance};
    for (size_t i = 0u; i < own; i++)
        if (!tf_entries_push(&row.entries,
                             f->entries.items[f->extension_start + i]))
            return f->effects ? TF_ERROR : TF_FAIL;
    f->rows[f->row_len++] = row;
    return TF_CONTINUE;
}

static void tf_rows_free(TfRow *rows, size_t len) {
    for (size_t i = 0u; i < len; i++)
        free(rows[i].entries.items);
}

/* The kernel match is complete: every extension of it forms one candidate. */
static TfStatus tf_extend(TfFire *f, size_t at) {
    TfRow *saved_rows = f->rows;
    size_t saved_len = f->row_len, saved_cap = f->row_cap;
    f->rows = NULL;
    f->row_len = f->row_cap = 0u;
    f->extension = true;
    f->extension_start = f->entries.len;
    TfStatus status = tf_walk(f, at);
    f->extension = false;
    if (status == TF_CONTINUE) {
        TfEntries kernel = f->entries;
        status = tf_candidate(f, &kernel, f->rows, f->row_len);
    }
    tf_rows_free(f->rows, f->row_len);
    free(f->rows);
    f->rows = saved_rows;
    f->row_len = saved_len;
    f->row_cap = saved_cap;
    return status;
}

static TfStatus tf_walk(TfFire *f, size_t at) {
    if (f->plan.has_every && !f->extension && at == f->plan.kernel_len)
        return tf_extend(f, at);
    if (at == f->plan.len)
        return tf_finish(f);
    const TfConjunct *c = &f->plan.items[at];
    TfStep step = {at, c};
    switch (c->kind) {
    case TF_READ:
    case TF_TAKE:
        return tf_each_match(f, c->space, c->pattern, tf_after_match, &step);
    case TF_ABSENT: {
        bool any = false;
        TfStatus status = tf_found_status(
            tf_each_match(f, c->space, c->pattern, tf_found_any, &any));
        if (status != TF_CONTINUE || any)
            return status;
        return tf_walk(f, at + 1u);
    }
    case TF_UNIQUE: {
        TfFound found = {.limit = 2u, .conjunct = c};
        TfStatus status = tf_found_status(
            tf_each_match(f, c->space, c->pattern, tf_collect, &found));
        if (status == TF_CONTINUE && found.len == 1u) {
            Atom *one = found.items[0];
            uint32_t mark = bindings_builder_save(&f->builder);
            if (match_atoms_builder(c->pattern, one, &f->builder, f->scratch)) {
                if (!tf_entries_push(&f->entries,
                                     (TfEntry){c->access, c->space_ref, c->space, one}))
                    status = f->effects ? TF_ERROR : TF_FAIL;
                else {
                    status = tf_walk(f, at + 1u);
                    f->entries.len--;
                }
            }
            bindings_builder_rollback(&f->builder, mark);
        }
        free(found.items);
        return status;
    }
    case TF_ALL: {
        TfFound found = {.limit = 0u, .conjunct = c};
        TfStatus status = tf_each_match(f, c->space, c->pattern, tf_collect, &found);
        if (status != TF_CONTINUE) {
            free(found.items);
            return status;
        }
        Atom *collection = atom_expr(f->scratch, found.items,
                                     (CettaExprLen)found.len);
        size_t entries_before = f->entries.len;
        uint32_t mark = bindings_builder_save(&f->builder);
        if (collection &&
            match_atoms_builder(c->collection, collection, &f->builder,
                                f->scratch)) {
            for (size_t i = 0u; c->access == TF_TAKE && i < found.len &&
                                status == TF_CONTINUE; i++)
                if (!tf_entries_push(&f->entries, (TfEntry){
                        TF_TAKE, c->space_ref, c->space, found.items[i]}))
                    status = f->effects ? TF_ERROR : TF_FAIL;
            if (status == TF_CONTINUE)
                status = tf_walk(f, at + 1u);
        }
        bindings_builder_rollback(&f->builder, mark);
        f->entries.len = entries_before;
        free(found.items);
        return status;
    }
    case TF_LET: {
        Atom *code = tf_apply(f, c->expression);
        if (!code)
            return f->effects ? TF_ERROR : TF_FAIL;
        f->effects = true;
        Atom **results = NULL;
        size_t result_len = 0u;
        Atom *error = NULL;
        if (!f->host->evaluate(f->host->context, f->scratch, code, &results,
                               &result_len, &error)) {
            free(results);
            f->error = error;
            return TF_ERROR;
        }
        TfStatus status = TF_CONTINUE;
        for (size_t i = 0u; i < result_len && status == TF_CONTINUE; i++) {
            uint32_t mark = bindings_builder_save(&f->builder);
            if (match_atoms_builder(c->pattern, results[i], &f->builder,
                                    f->scratch))
                status = tf_walk(f, at + 1u);
            bindings_builder_rollback(&f->builder, mark);
        }
        free(results);
        return status;
    }
    }
    return f->effects ? TF_ERROR : TF_FAIL;
}

/* ── Validation and the commit ─────────────────────────────────────────── */

static bool tf_entry_eq(const TfEntry *left, TfKind access, const TfEntry *right) {
    return left->access == access && atom_alpha_eq(left->space_ref, right->space_ref) &&
           atom_alpha_eq(left->atom, right->atom);
}

typedef struct {
    const TfEntries *kernel;
    const TfRow *rows;
    size_t row_len;
} TfFootprint;

static size_t tf_footprint_len(const TfFootprint *fp) {
    size_t len = fp->kernel->len;
    for (size_t r = 0u; r < fp->row_len; r++)
        len += fp->rows[r].entries.len;
    return len;
}

/* A hash of an atom that alpha-equivalent atoms share: variables hash alike. */
static uint64_t tf_hash(const Atom *a) {
    uint64_t h;
    switch (a->kind) {
    case ATOM_SYMBOL:
        return 0x9e3779b97f4a7c15ull ^ atom_symbol_spelling_hash(a);
    case ATOM_VAR:
        return 0xc4ceb9fe1a85ec53ull;
    case ATOM_EXPR:
        h = 0x2545f4914f6cdd1dull ^ a->expr.len;
        for (CettaExprLen i = 0u; i < a->expr.len; i++)
            h = (h ^ tf_hash(a->expr.elems[i])) * 0x100000001b3ull;
        return h;
    case ATOM_GROUNDED:
        switch (a->ground.gkind) {
        case GV_INT:
            return 0x6a09e667f3bcc909ull ^ ((uint64_t)a->ground.ival * 0x9e3779b97f4a7c15ull);
        case GV_BOOL:
            return 0xbb67ae8584caa73bull ^ (uint64_t)a->ground.bval;
        case GV_STRING: {
            h = 0x3c6ef372fe94f82bull;
            for (const char *c = a->ground.sval; c && *c; c++)
                h = (h ^ (unsigned char)*c) * 0x100000001b3ull;
            return h;
        }
        default:
            return 0xa54ff53a5f1d36f1ull ^ (uint64_t)a->ground.gkind;
        }
    default:
        return 0x510e527fade682d1ull;
    }
}

/* The footprint grouped by equal entries, in the order each first occurs:
 * tf:count is a group's size, and tf:apply-takes visits the take groups in
 * this order. */
typedef struct {
    const TfEntry *entry; /* the first occurrence */
    uint64_t hash;
    size_t count;
    size_t reads;         /* take groups: read entries of the same atom */
    size_t stored;
} TfGroup;

typedef struct {
    TfGroup *groups;
    size_t len;
    size_t *slots;        /* open addressing over group indices, +1 */
    size_t mask;
} TfGroups;

static uint64_t tf_entry_hash(TfKind access, const TfEntry *e) {
    return (tf_hash(e->space_ref) * 31u + tf_hash(e->atom)) * 2u + (uint64_t)access;
}

static TfGroup *tf_group_find(TfGroups *g, TfKind access, const TfEntry *e,
                              uint64_t hash, size_t **slot_out) {
    size_t at = (size_t)hash & g->mask;
    for (;;) {
        size_t index = g->slots[at];
        if (index == 0u) {
            *slot_out = &g->slots[at];
            return NULL;
        }
        TfGroup *group = &g->groups[index - 1u];
        if (group->hash == hash && tf_entry_eq(group->entry, access, e))
            return group;
        at = (at + 1u) & g->mask;
    }
}

static bool tf_groups_build(const TfFootprint *fp, TfGroups *g) {
    size_t len = tf_footprint_len(fp);
    size_t cap = 16u;
    while (cap < 2u * len + 2u)
        cap <<= 1u;
    *g = (TfGroups){0};
    g->groups = cetta_malloc(sizeof(TfGroup) * (len ? len : 1u));
    g->slots = calloc(cap, sizeof(size_t));
    g->mask = cap - 1u;
    if (!g->groups || !g->slots)
        return false;
    const TfEntries *parts_kernel = fp->kernel;
    for (size_t part = 0u; part <= fp->row_len; part++) {
        const TfEntries *entries = part == 0u ? parts_kernel : &fp->rows[part - 1u].entries;
        for (size_t i = 0u; i < entries->len; i++) {
            const TfEntry *e = &entries->items[i];
            uint64_t hash = tf_entry_hash(e->access, e);
            size_t *slot = NULL;
            TfGroup *group = tf_group_find(g, e->access, e, hash, &slot);
            if (group) {
                group->count++;
                continue;
            }
            g->groups[g->len] = (TfGroup){e, hash, 1u, 0u, 0u};
            *slot = ++g->len;
        }
    }
    for (size_t i = 0u; i < g->len; i++) {
        TfGroup *take = &g->groups[i];
        if (take->entry->access != TF_TAKE)
            continue;
        size_t *slot = NULL;
        TfGroup *read = tf_group_find(g, TF_READ, take->entry,
                                      tf_entry_hash(TF_READ, take->entry), &slot);
        take->reads = read ? read->count : 0u;
    }
    return true;
}

static void tf_groups_free(TfGroups *g) {
    free(g->groups);
    free(g->slots);
}

static TfStatus tf_count_one(TfFire *f, void *data) {
    (void)f;
    (*(size_t *)data)++;
    return TF_CONTINUE;
}

/* tf:stored: how many stored atoms `match` finds for the entry's atom. */
static bool tf_stored(TfFire *f, const TfEntry *entry, size_t *out) {
    *out = 0u;
    return tf_each_match(f, entry->space, entry->atom, tf_count_one, out) ==
           TF_CONTINUE;
}

/* tf:valid: each taken atom is stored with the multiplicity the firing takes,
 * plus one more copy when the firing also reads it. */
static TfStatus tf_valid(TfFire *f, TfGroups *g, bool *valid) {
    *valid = true;
    for (size_t i = 0u; i < g->len && *valid; i++) {
        TfGroup *take = &g->groups[i];
        if (take->entry->access != TF_TAKE)
            continue;
        if (!tf_stored(f, take->entry, &take->stored))
            return f->effects ? TF_ERROR : TF_FAIL;
        *valid = take->stored >= take->count + (take->reads > 0u ? 1u : 0u);
    }
    return TF_CONTINUE;
}

/* tf:apply-takes: each distinct taken atom, in footprint order: remove every
 * copy, then store back the copies the firing does not take. */
static TfStatus tf_commit(TfFire *f, const TfFootprint *fp, TfGroups *g) {
    Atom **instances = arena_alloc(f->answer_arena,
                                   sizeof(Atom *) * (fp->row_len ? fp->row_len : 1u));
    if (!instances)
        return f->effects ? TF_ERROR : TF_FAIL;
    /* Everything the commit answers or stores back is copied before any
     * removal can release the stored syntax it borrows.  A removal can change
     * another taken atom's count only when one of them has variables. */
    bool ground = true;
    for (size_t r = 0u; r < fp->row_len; r++)
        instances[r] = atom_deep_copy(f->answer_arena, fp->rows[r].instance);
    Atom **taken = arena_alloc(f->answer_arena, sizeof(Atom *) * (g->len ? g->len : 1u));
    if (!taken)
        return f->effects ? TF_ERROR : TF_FAIL;
    for (size_t i = 0u; i < g->len; i++) {
        if (g->groups[i].entry->access != TF_TAKE)
            continue;
        taken[i] = atom_deep_copy(f->answer_arena, g->groups[i].entry->atom);
        ground = ground && !atom_has_vars(taken[i]);
    }
    f->effects = true;
    for (size_t i = 0u; i < g->len; i++) {
        TfGroup *take = &g->groups[i];
        if (take->entry->access != TF_TAKE)
            continue;
        size_t have = take->stored;
        TfEntry now = *take->entry;
        now.atom = taken[i];
        if (!ground && !tf_stored(f, &now, &have))
            return TF_ERROR;
        Atom *error = f->host->remove_atom(f->host->context, now.space_ref, now.atom);
        for (size_t k = take->count; !error && k < have; k++)
            error = f->host->add_atom(f->host->context, now.space_ref, now.atom);
        if (error) {
            f->error = error;
            return TF_ERROR;
        }
    }
    Atom *fired = atom_expr(f->answer_arena, instances, (CettaExprLen)fp->row_len);
    Atom *parts[2] = {atom_symbol(f->answer_arena, "tf:fired"), fired};
    f->answer = atom_expr(f->answer_arena, parts, 2u);
    return TF_STOP;
}

/* Validate a candidate and, when it validates, commit it. */
static TfStatus tf_try_commit(TfFire *f, const TfFootprint *fp) {
    TfGroups g;
    if (!tf_groups_build(fp, &g)) {
        tf_groups_free(&g);
        return f->effects ? TF_ERROR : TF_FAIL;
    }
    bool valid = false;
    TfStatus status = tf_valid(f, &g, &valid);
    if (status == TF_CONTINUE && valid)
        status = tf_commit(f, fp, &g);
    tf_groups_free(&g);
    return status;
}

static bool tf_entries_copy(Arena *arena, const TfEntries *src, TfEntries *dst) {
    *dst = (TfEntries){NULL, 0u, 0u};
    for (size_t i = 0u; i < src->len; i++) {
        TfEntry e = src->items[i];
        e.atom = atom_deep_copy(arena, e.atom);
        if (!tf_entries_push(dst, e))
            return false;
    }
    return true;
}

/* Keep a candidate past its proposal, its syntax copied out of the rows the
 * proposal reads. */
static TfStatus tf_keep_candidate(TfFire *f, TfEntries *kernel, TfRow *rows,
                                  size_t row_len) {
    if (f->candidate_len == f->candidate_cap) {
        size_t cap = f->candidate_cap ? f->candidate_cap * 2u : 8u;
        TfCandidate *items = cetta_realloc(f->candidates, cap * sizeof(*items));
        if (!items)
            return TF_ERROR;
        f->candidates = items;
        f->candidate_cap = cap;
    }
    TfCandidate kept = {0};
    if (!tf_entries_copy(f->scratch, kernel, &kept.kernel))
        return TF_ERROR;
    kept.rows = cetta_malloc(sizeof(TfRow) * (row_len ? row_len : 1u));
    if (!kept.rows)
        return TF_ERROR;
    for (size_t r = 0u; r < row_len; r++) {
        kept.rows[r].instance = atom_deep_copy(f->scratch, rows[r].instance);
        if (!tf_entries_copy(f->scratch, &rows[r].entries, &kept.rows[r].entries))
            return TF_ERROR;
    }
    kept.row_len = row_len;
    f->candidates[f->candidate_len++] = kept;
    return TF_CONTINUE;
}

static TfStatus tf_candidate(TfFire *f, TfEntries *kernel, TfRow *rows,
                             size_t row_len) {
    TfFootprint fp = {kernel, rows, row_len};
    /* Code may have run between proposals: keep the candidate and validate
     * every one after the proposal, as the library does. */
    if (f->plan.has_let)
        return tf_keep_candidate(f, kernel, rows, row_len);
    /* The first candidate that validates stops the proposal; it commits
     * once the proposal has unwound, as the library commits after its
     * search, so no removal meets a reader the proposal still holds. */
    TfGroups g;
    if (!tf_groups_build(&fp, &g)) {
        tf_groups_free(&g);
        return f->effects ? TF_ERROR : TF_FAIL;
    }
    bool valid = false;
    TfStatus status = tf_valid(f, &g, &valid);
    tf_groups_free(&g);
    if (status != TF_CONTINUE || !valid)
        return status;
    status = tf_keep_candidate(f, kernel, rows, row_len);
    return status == TF_CONTINUE ? TF_STOP : status;
}

/* ── The firing ────────────────────────────────────────────────────────── */

CettaTransformOutcome cetta_transform_fire(
    Space *program, Arena *a, Atom *call, const CettaTransformHost *host,
    Atom **answer_out) {
    (void)program;
    *answer_out = NULL;
    /* (tf:fire-into SPACE CONJUNCTS TEMPLATE DESTINATION) adds the instances
     * to DESTINATION after the commit and answers how many. */
    bool into = call && call->kind == ATOM_EXPR && call->expr.len == 5u;
    if (!call || call->kind != ATOM_EXPR ||
        (call->expr.len != 4u && !into) || !host ||
        !host->resolve_space || !host->remove_atom || !host->add_atom ||
        !host->evaluate)
        return CETTA_TRANSFORM_DECLINED;
    Arena scratch;
    arena_init(&scratch);
    CETTA_FRAME_IDENTITY_SCOPE(scope);
    TfFire f = {
        .host = host,
        .scratch = &scratch,
        .answer_arena = a,
        .template_atom = call->expr.elems[3],
        .scope = &scope,
    };
    TfPlanner planner = {host, call->expr.elems[1]};
    CettaTransformOutcome outcome = CETTA_TRANSFORM_DECLINED;
    Atom *conjuncts = call->expr.elems[2];
    if (conjuncts->kind != ATOM_EXPR ||
        !tf_plan_list(&planner, &f.plan, conjuncts->expr.elems,
                      conjuncts->expr.len) ||
        !bindings_builder_init(&f.builder, NULL)) {
        if (transform_trace()) {
            fprintf(stderr, "[transform] declined ");
            atom_print(call, stderr);
            fprintf(stderr, "\n");
        }
        free(f.plan.items);
        arena_free(&scratch);
        return CETTA_TRANSFORM_DECLINED;
    }
    TfStatus status = tf_walk(&f, 0u);
    /* With a let, every kept candidate in turn; without one, the candidate
     * that stopped the proposal, which validates again over the same store. */
    if (f.plan.has_let ? status == TF_CONTINUE : status == TF_STOP) {
        status = TF_CONTINUE;
        for (size_t i = 0u; i < f.candidate_len && status == TF_CONTINUE; i++) {
            TfFootprint fp = {&f.candidates[i].kernel, f.candidates[i].rows,
                              f.candidates[i].row_len};
            status = tf_try_commit(&f, &fp);
        }
    }
    if (status == TF_STOP && into) {
        /* tf:split, then tf:add-all: a comma template gives its parts. */
        Atom *fired = f.answer->expr.elems[1];
        Atom *destination = call->expr.elems[4];
        int64_t added = 0;
        for (CettaExprLen r = 0u; r < fired->expr.len && status == TF_STOP; r++) {
            Atom *instance = fired->expr.elems[r];
            bool comma = tf_is_comma(instance);
            CettaExprLen first = comma ? 1u : 0u;
            CettaExprLen last = comma ? instance->expr.len : 1u;
            for (CettaExprLen k = first; k < last && status == TF_STOP; k++) {
                Atom *part = comma ? instance->expr.elems[k] : instance;
                Atom *error = host->add_atom(host->context, destination, part);
                if (error) {
                    f.error = error;
                    status = TF_ERROR;
                } else {
                    added++;
                }
            }
        }
        if (status == TF_STOP)
            f.answer = atom_int(a, added);
    }
    switch (status) {
    case TF_STOP:
        *answer_out = f.answer;
        outcome = CETTA_TRANSFORM_FIRED;
        break;
    case TF_CONTINUE:
        outcome = CETTA_TRANSFORM_NONE;
        break;
    case TF_FAIL:
        outcome = f.effects ? CETTA_TRANSFORM_ERROR : CETTA_TRANSFORM_DECLINED;
        break;
    case TF_ERROR:
        outcome = CETTA_TRANSFORM_ERROR;
        break;
    }
    if (outcome == CETTA_TRANSFORM_ERROR) {
        *answer_out = f.error ? atom_deep_copy(a, f.error)
            : atom_symbol(a, "TransformRealizationFailed");
    }
    for (size_t i = 0u; i < f.candidate_len; i++) {
        free(f.candidates[i].kernel.items);
        tf_rows_free(f.candidates[i].rows, f.candidates[i].row_len);
        free(f.candidates[i].rows);
    }
    free(f.candidates);
    free(f.entries.items);
    free(f.plan.items);
    bindings_builder_free(&f.builder);
    arena_free(&scratch);
    if (transform_trace()) {
        fprintf(stderr, "[transform] %s ",
                outcome == CETTA_TRANSFORM_FIRED ? "fired" :
                outcome == CETTA_TRANSFORM_NONE ? "none" :
                outcome == CETTA_TRANSFORM_ERROR ? "error" : "declined");
        atom_print(call, stderr);
        fprintf(stderr, "\n");
    }
    return outcome;
}
