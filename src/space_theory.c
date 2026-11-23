#include "space_theory.h"
#include "rule_binding.h"

#include <stdlib.h>
#include <string.h>

/* ── Atomic Program-Clock Guard ─────────────────────────────────────────── */

bool space_theory_atomic_clock_guard_recheck(
        SpaceTheoryAtomicClockGuard *guard) {
    if (!guard)
        return false;
    uint64_t clock = space_global_program_epoch();
    if (!space_theory_token_is_current(guard->token))
        return false;
    atomic_store_explicit(&guard->program_clock, clock, memory_order_relaxed);
    return true;
}

/* ── Atom Classification ────────────────────────────────────────────────── */

SpaceTheoryAtomKind space_theory_classify_atom(
        const Atom *atom, SymbolId *head_out, bool *compound_subject_out) {
    if (head_out)
        *head_out = SYMBOL_ID_NONE;
    if (compound_subject_out)
        *compound_subject_out = false;
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len < 3u ||
        !atom->expr.elems[0] || atom->expr.elems[0]->kind != ATOM_SYMBOL)
        return SPACE_THEORY_ATOM_DATA;

    SymbolId form = atom->expr.elems[0]->sym_id;
    SpaceTheoryAtomKind kind =
        space_theory_classify_head_arity(form, atom->expr.len);
    switch (kind) {
    case SPACE_THEORY_ATOM_DATA:
    case SPACE_THEORY_ATOM_EQUATION:
    case SPACE_THEORY_ATOM_SUBTYPE_ASSIGNMENT:
        return kind;
    case SPACE_THEORY_ATOM_DEFINITION:
    case SPACE_THEORY_ATOM_DIRECTIONAL_DEFINITION: {
        const Atom *subject = atom->expr.elems[1];
        if (head_out && subject && subject->kind == ATOM_EXPR &&
            subject->expr.len > 0u && subject->expr.elems[0] &&
            subject->expr.elems[0]->kind == ATOM_SYMBOL) {
            *head_out = subject->expr.elems[0]->sym_id;
        }
        return kind;
    }
    case SPACE_THEORY_ATOM_TYPE_ASSIGNMENT: {
        const Atom *subject = atom->expr.elems[1];
        if (subject) {
            if (subject->kind == ATOM_SYMBOL) {
                if (head_out)
                    *head_out = subject->sym_id;
            } else if (subject->kind == ATOM_EXPR) {
                if (compound_subject_out)
                    *compound_subject_out = true;
                if (head_out && subject->expr.len > 0u &&
                    subject->expr.elems[0] &&
                    subject->expr.elems[0]->kind == ATOM_SYMBOL) {
                    *head_out = subject->expr.elems[0]->sym_id;
                }
            }
        }
        return kind;
    }
    }
    return SPACE_THEORY_ATOM_DATA;
}

SpaceTheoryAtomKind space_theory_classify_header(
        const TermUniverse *u, AtomId id, const CettaTermHdr *hdr,
        SymbolId *target_head_out, bool *is_open_out) {
    if (target_head_out)
        *target_head_out = SYMBOL_ID_NONE;
    if (is_open_out)
        *is_open_out = false;
    if (!u || !hdr) {
        if (is_open_out)
            *is_open_out = true;
        return SPACE_THEORY_ATOM_DATA;
    }
    if (hdr->tag == ATOM_VAR) {
        if (is_open_out)
            *is_open_out = true;
        return SPACE_THEORY_ATOM_DATA;
    }
    if (hdr->tag == ATOM_SYMBOL) {
        if (target_head_out)
            *target_head_out = hdr->sym_or_head;
        return SPACE_THEORY_ATOM_DATA;
    }
    if (hdr->tag != ATOM_EXPR) {
        return SPACE_THEORY_ATOM_DATA;
    }
    CettaExprLen arity = hdr->aux32 & CETTA_TERM_HDR_DATA_MASK;
    if (arity == 0u)
        return SPACE_THEORY_ATOM_DATA;
    SymbolId head = hdr->sym_or_head;
    if (head == SYMBOL_ID_NONE) {
        AtomId first_id = tu_child_from_header(u, hdr, 0u);
        const CettaTermHdr *first_hdr = tu_hdr(u, first_id);
        if (!first_hdr || first_hdr->tag == ATOM_VAR) {
            if (is_open_out)
                *is_open_out = true;
        }
        return SPACE_THEORY_ATOM_DATA;
    }
    SpaceTheoryAtomKind kind = space_theory_classify_head_arity(head, arity);
    switch (kind) {
    case SPACE_THEORY_ATOM_DATA:
        if (target_head_out)
            *target_head_out = head;
        return SPACE_THEORY_ATOM_DATA;
    case SPACE_THEORY_ATOM_EQUATION:
        return SPACE_THEORY_ATOM_EQUATION;
    case SPACE_THEORY_ATOM_SUBTYPE_ASSIGNMENT:
        if (is_open_out)
            *is_open_out = true;
        return SPACE_THEORY_ATOM_SUBTYPE_ASSIGNMENT;
    case SPACE_THEORY_ATOM_DEFINITION:
    case SPACE_THEORY_ATOM_DIRECTIONAL_DEFINITION: {
        AtomId lhs_id = tu_child_from_header(u, hdr, 1u);
        const CettaTermHdr *lhs = tu_hdr(u, lhs_id);
        if (!lhs) {
            if (is_open_out)
                *is_open_out = true;
        } else if (lhs->tag == ATOM_EXPR) {
            CettaExprLen lhs_arity = lhs->aux32 & CETTA_TERM_HDR_DATA_MASK;
            if (lhs_arity > 0u && lhs->sym_or_head != SYMBOL_ID_NONE) {
                if (target_head_out)
                    *target_head_out = lhs->sym_or_head;
            } else {
                if (is_open_out)
                    *is_open_out = true;
            }
        } else {
            /* Bare symbol, variable, or grounded LHS can rewrite non-call
             * occurrences globally; treat as open for definition counters. */
            if (is_open_out)
                *is_open_out = true;
        }
        (void)id;
        return kind;
    }
    case SPACE_THEORY_ATOM_TYPE_ASSIGNMENT: {
        AtomId subj_id = tu_child_from_header(u, hdr, 1u);
        const CettaTermHdr *subj = tu_hdr(u, subj_id);
        if (!subj) {
            if (is_open_out)
                *is_open_out = true;
        } else if (subj->tag == ATOM_SYMBOL) {
            if (target_head_out)
                *target_head_out = subj->sym_or_head;
        } else if (subj->tag == ATOM_EXPR) {
            CettaExprLen subj_arity = subj->aux32 & CETTA_TERM_HDR_DATA_MASK;
            if (subj_arity > 0u && subj->sym_or_head != SYMBOL_ID_NONE) {
                if (target_head_out)
                    *target_head_out = subj->sym_or_head;
            } else {
                if (is_open_out)
                    *is_open_out = true;
            }
        } else {
            if (is_open_out)
                *is_open_out = true;
        }
        return kind;
    }
    }
    return SPACE_THEORY_ATOM_DATA;
}

bool space_theory_overlay_chain_tokens_current(
        const Space *space, const SpaceTheoryToken *views, size_t count) {
    if (!views || count == 0u)
        return false;
    for (size_t i = 0u; i < count; i++) {
        if (!space)
            return false;
        SpaceTheoryToken old = views[i];
        if (old.space != space ||
            old.instance_id != space_instance_id(space) ||
            old.equation_revision != space_equation_revision(space) ||
            old.declaration_revision != space_declaration_revision(space)) {
            return false;
        }
        space = space->overlay_base;
    }
    return !space;
}

/* ── Live SpaceTheoryView Readers ───────────────────────────────────────── */

bool space_theory_view_head_has_alias_rules_slow(
        const SpaceTheoryView *view, SymbolId head, unsigned admitted_forms) {
    if (!view || !view->space)
        return false;
    return (admitted_forms & CETTA_RULE_SYNTAX_EQUATIONAL_ALIAS) != 0u &&
           space_alias_head_in_carrier_slow(view->space, head) &&
           space_alias_head_has_rules(view->space, head, admitted_forms);
}

bool space_theory_view_collect_head_rules(
        const SpaceTheoryView *view, SymbolId head, unsigned admitted_forms,
        Atom ***out_equations, size_t *out_count, bool *out_via_alias) {
    if (out_equations)
        *out_equations = NULL;
    if (out_count)
        *out_count = 0u;
    if (out_via_alias)
        *out_via_alias = false;
    if (!view || !view->space || !out_equations || !out_count)
        return false;
    Space *s = view->space;
    if ((admitted_forms & CETTA_RULE_SYNTAX_EQUATIONAL_ALIAS) != 0u &&
        space_alias_head_in_carrier(s, head)) {
        if (out_via_alias)
            *out_via_alias = true;
        return space_alias_collect_head_rules(
            s, head, admitted_forms, out_equations, out_count);
    }
    SpaceEquationCursor cursor;
    if (!space_rule_cursor_init(s, head, admitted_forms, &cursor))
        return false;
    Atom **items = NULL;
    size_t len = 0u, cap = 0u;
    for (;;) {
        SpaceEquationOccurrence occ;
        SpaceEquationCursorStep step = space_rule_cursor_read(&cursor, &occ);
        if (step == SPACE_EQUATION_CURSOR_END)
            break;
        if (step == SPACE_EQUATION_CURSOR_INVALIDATED) {
            free(items);
            return false;
        }
        if (len == cap) {
            size_t next = cap ? cap * 2u : 8u;
            Atom **grown = realloc(items, next * sizeof(*items));
            if (!grown) {
                free(items);
                return false;
            }
            items = grown;
            cap = next;
        }
        items[len++] = occ.equation;
    }
    *out_equations = items;
    *out_count = len;
    return true;
}

void space_theory_equation_cache_free(SpaceTheoryEquationCache *cache) {
    if (!cache)
        return;
    free(cache->own_alias_ids);
    memset(cache, 0, sizeof(*cache));
}

static bool space_theory_supports_native_alias_scan(const Space *s) {
    if (!s || s->overlay_base || s->native.start != 0u || !s->native.universe ||
        (s->kind != SPACE_KIND_ATOM && s->kind != SPACE_KIND_SET) ||
        (s->match_backend.kind != SPACE_ENGINE_NATIVE &&
         s->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT))
        return false;
    if (s->native.len == 0u)
        return true;
    return s->native.atom_ids != NULL &&
           cetta_atom_id_storage_width_bytes_from_bits(
               s->native.atom_id_width_bits) != 0u;
}

static bool space_theory_scan_alias_range(const Space *s, CettaIndex from,
                                          CettaIndex to, AtomId **ids,
                                          size_t *len, size_t *cap) {
    if (from >= to)
        return true;
    const TermUniverse *u = s->native.universe;
    size_t width =
        cetta_atom_id_storage_width_bytes_from_bits(s->native.atom_id_width_bits);
    const uint64_t *marks = s->native.marked ? s->native.marks : NULL;
    for (CettaIndex idx = from; idx < to; idx++) {
        if (marks && idx < s->native.mark_rows &&
            ((marks[idx >> 6] >> (idx & 63u)) & 1u) != 0u)
            continue;
        AtomId id = cetta_atom_id_storage_load_bits(
            s->native.atom_ids + (size_t)idx * width,
            s->native.atom_id_width_bits);
        const CettaTermHdr *hdr = tu_hdr(u, id);
        if (hdr) {
            if (hdr->tag != ATOM_EXPR)
                continue;
            CettaExprLen arity = hdr->aux32 & CETTA_TERM_HDR_DATA_MASK;
            if (space_theory_classify_head_arity(hdr->sym_or_head, arity) !=
                SPACE_THEORY_ATOM_EQUATION)
                continue;
        } else {
            SymbolId head = term_universe_head_sym_unrecorded(u, id);
            if (!space_theory_is_alias_head(head))
                continue;
        }
        if (*len == *cap) {
            size_t next = *cap ? *cap * 2u : 8u;
            AtomId *grown = realloc(*ids, sizeof(**ids) * next);
            if (!grown)
                return false;
            *ids = grown;
            *cap = next;
        }
        (*ids)[(*len)++] = id;
    }
    return true;
}

static bool space_theory_append_atom(Atom *atom, Atom ***atoms, size_t *len,
                                     size_t *cap) {
    if (!atom)
        return true;
    if (*len == *cap) {
        size_t next = *cap ? *cap * 2u : 8u;
        Atom **grown = realloc(*atoms, sizeof(**atoms) * next);
        if (!grown)
            return false;
        *atoms = grown;
        *cap = next;
    }
    (*atoms)[(*len)++] = atom;
    return true;
}

bool space_theory_view_collect_equations(
        const SpaceTheoryView *view, SpaceTheoryEquationCache *cache,
        Atom ***out_atoms, size_t *out_count) {
    if (out_atoms)
        *out_atoms = NULL;
    if (out_count)
        *out_count = 0u;
    if (!view || !view->space || !out_atoms || !out_count)
        return false;
    Space *s = view->space;
    if (!s->native.universe)
        return true;

    Atom **atoms = NULL;
    size_t atom_len = 0u, atom_cap = 0u;

    if (space_theory_supports_native_alias_scan(s)) {
        uint64_t prefix = space_prefix_epoch(s);
        CettaIndex marked = s->native.marked;
        CettaCount cur_len = s->native.len;
        uint64_t u_inst = s->native.universe->instance_id;
        uint64_t u_epoch = s->native.universe->storage_epoch;
        if (cache) {
            if (cache->own_alias_valid &&
                cache->own_alias_prefix_epoch == prefix &&
                cache->own_alias_marked == marked &&
                cache->own_alias_universe_instance == u_inst &&
                cache->own_alias_universe_epoch == u_epoch &&
                cache->own_alias_synced_len <= cur_len) {
                if (!space_theory_scan_alias_range(
                        s, cache->own_alias_synced_len, cur_len,
                        &cache->own_alias_ids, &cache->own_alias_len,
                        &cache->own_alias_cap)) {
                    free(atoms);
                    return false;
                }
            } else {
                cache->own_alias_len = 0u;
                if (!space_theory_scan_alias_range(
                        s, 0u, cur_len,
                        &cache->own_alias_ids, &cache->own_alias_len,
                        &cache->own_alias_cap)) {
                    free(atoms);
                    return false;
                }
            }
            cache->own_alias_synced_len = cur_len;
            cache->own_alias_prefix_epoch = prefix;
            cache->own_alias_marked = marked;
            cache->own_alias_universe_instance = u_inst;
            cache->own_alias_universe_epoch = u_epoch;
            cache->own_alias_valid = true;

            for (size_t i = 0u; i < cache->own_alias_len; i++) {
                Atom *atom = term_universe_get_atom(s->native.universe,
                                                    cache->own_alias_ids[i]);
                if (atom &&
                    space_theory_classify_atom(atom, NULL, NULL) ==
                        SPACE_THEORY_ATOM_EQUATION &&
                    !space_theory_append_atom(atom, &atoms, &atom_len, &atom_cap)) {
                    free(atoms);
                    return false;
                }
            }
        } else {
            AtomId *own_ids = NULL;
            size_t own_len = 0u, own_cap = 0u;
            bool ok = space_theory_scan_alias_range(
                s, 0u, cur_len, &own_ids, &own_len, &own_cap);
            for (size_t i = 0u; ok && i < own_len; i++) {
                Atom *atom = term_universe_get_atom(s->native.universe, own_ids[i]);
                if (atom &&
                    space_theory_classify_atom(atom, NULL, NULL) ==
                        SPACE_THEORY_ATOM_EQUATION)
                    ok = space_theory_append_atom(atom, &atoms, &atom_len, &atom_cap);
            }
            free(own_ids);
            if (!ok) {
                free(atoms);
                return false;
            }
        }

        uint32_t dep_count = space_dependency_count(s);
        for (uint32_t d = 0u; d < dep_count; d++) {
            const Space *dep = space_dependency_at(s, d);
            if (!dep)
                continue;
            if (space_equational_revision(dep) == 0u &&
                !dep->overlay_base && dep->dep_count == 0u)
                continue;
            if (space_theory_supports_native_alias_scan(dep) &&
                dep->native.marked == 0u &&
                dep->imported_len <= dep->native.len) {
                AtomId *dep_ids = NULL;
                size_t dep_len = 0u, dep_cap = 0u;
                bool ok = space_theory_scan_alias_range(
                    dep, dep->imported_len, dep->native.len,
                    &dep_ids, &dep_len, &dep_cap);
                for (size_t i = 0u; ok && i < dep_len; i++) {
                    Atom *atom = term_universe_get_atom(dep->native.universe,
                                                        dep_ids[i]);
                    if (atom &&
                        space_theory_classify_atom(atom, NULL, NULL) ==
                            SPACE_THEORY_ATOM_EQUATION)
                        ok = space_theory_append_atom(atom, &atoms, &atom_len,
                                                      &atom_cap);
                }
                free(dep_ids);
                if (!ok) {
                    free(atoms);
                    return false;
                }
            } else {
                CettaIndex begin = space_imported_length(dep);
                CettaCount dep_total = space_length64(dep);
                for (CettaIndex i = begin; i < dep_total; i++) {
                    Atom *atom = space_get_at64(dep, i);
                    if (atom &&
                        space_theory_classify_atom(atom, NULL, NULL) ==
                            SPACE_THEORY_ATOM_EQUATION &&
                        !space_theory_append_atom(atom, &atoms, &atom_len,
                                                  &atom_cap)) {
                        free(atoms);
                        return false;
                    }
                }
            }
        }
    } else {
        if (cache)
            cache->own_alias_valid = false;
        CettaCount total = space_view_length64(s);
        for (CettaIndex i = 0u; i < total; i++) {
            Atom *atom = space_view_get_at64(s, i);
            if (atom &&
                space_theory_classify_atom(atom, NULL, NULL) ==
                    SPACE_THEORY_ATOM_EQUATION &&
                !space_theory_append_atom(atom, &atoms, &atom_len, &atom_cap)) {
                free(atoms);
                return false;
            }
        }
    }

    *out_atoms = atoms;
    *out_count = atom_len;
    return true;
}

/* ── Lazy Per-Head Clocks, Sealing & Protecting Imports ─────────────────── */

typedef struct SpaceHeadClockEntry {
    SymbolId head;
    uint8_t sealed_mask;
    uint64_t rule_rev;
    uint64_t decl_rev;
    uint64_t data_rev;
} SpaceHeadClockEntry;

struct SpaceHeadClocks {
    uint64_t table_id;
    uint64_t open_rule_rev;
    uint64_t open_decl_rev;
    uint64_t open_data_rev;
    uint64_t opaque_rev;
    uint64_t seal_rev;
    SpaceHeadClockEntry *slots;
    uint32_t cap;
    uint32_t count;
};

static _Atomic uint64_t g_space_head_clocks_next_id = 1u;
_Atomic uint64_t cetta_space_global_head_clocks_active = 0u;

void space_head_clocks_free(struct SpaceHeadClocks *clocks) {
    if (!clocks)
        return;
    free(clocks->slots);
    free(clocks);
}

static SpaceHeadClockEntry *head_clocks_find(struct SpaceHeadClocks *clocks,
                                             SymbolId head) {
    if (!clocks || !clocks->slots || clocks->cap == 0u ||
        head == SYMBOL_ID_NONE)
        return NULL;
    uint32_t mask = clocks->cap - 1u;
    for (uint32_t slot = space_theory_symbol_id_mix(head) & mask;;
         slot = (slot + 1u) & mask) {
        SpaceHeadClockEntry *entry = &clocks->slots[slot];
        if (entry->head == head)
            return entry;
        if (entry->head == SYMBOL_ID_NONE)
            return NULL;
    }
}

static bool head_clocks_grow(struct SpaceHeadClocks *clocks, uint32_t new_cap) {
    SpaceHeadClockEntry *slots = calloc(new_cap, sizeof(*slots));
    if (!slots)
        return false;
    for (uint32_t i = 0u; i < new_cap; i++)
        slots[i].head = SYMBOL_ID_NONE;
    uint32_t mask = new_cap - 1u;
    for (uint32_t i = 0u; i < clocks->cap; i++) {
        if (clocks->slots[i].head == SYMBOL_ID_NONE)
            continue;
        for (uint32_t slot = space_theory_symbol_id_mix(clocks->slots[i].head) & mask;;
             slot = (slot + 1u) & mask) {
            if (slots[slot].head == SYMBOL_ID_NONE) {
                slots[slot] = clocks->slots[i];
                break;
            }
        }
    }
    free(clocks->slots);
    clocks->slots = slots;
    clocks->cap = new_cap;
    return true;
}

static struct SpaceHeadClocks *head_clocks_ensure_locked(Space *s) {
    if (s->head_clocks)
        return s->head_clocks;
    struct SpaceHeadClocks *clocks = calloc(1u, sizeof(*clocks));
    if (!clocks)
        return NULL;
    clocks->table_id =
        atomic_fetch_add_explicit(&g_space_head_clocks_next_id, 1u,
                                  memory_order_relaxed);
    if (!head_clocks_grow(clocks, 16u)) {
        free(clocks);
        return NULL;
    }
    atomic_store_explicit(&cetta_space_global_head_clocks_active, 1u,
                          memory_order_release);
    s->head_clocks = clocks;
    return clocks;
}

static SpaceHeadClockEntry *head_clocks_ensure_entry_locked(
        struct SpaceHeadClocks *clocks, SymbolId head) {
    if (!clocks || head == SYMBOL_ID_NONE)
        return NULL;
    SpaceHeadClockEntry *existing = head_clocks_find(clocks, head);
    if (existing)
        return existing;
    if ((clocks->count + 1u) * 2u > clocks->cap) {
        if (!head_clocks_grow(clocks, clocks->cap ? clocks->cap * 2u : 16u))
            return NULL;
    }
    uint32_t mask = clocks->cap - 1u;
    for (uint32_t slot = space_theory_symbol_id_mix(head) & mask;;
         slot = (slot + 1u) & mask) {
        SpaceHeadClockEntry *entry = &clocks->slots[slot];
        if (entry->head == SYMBOL_ID_NONE) {
            *entry = (SpaceHeadClockEntry){.head = head};
            clocks->count++;
            return entry;
        }
    }
}

static void head_clocks_clear_seals_all(struct SpaceHeadClocks *clocks,
                                        uint8_t roles) {
    if (!clocks || !clocks->slots || roles == 0u)
        return;
    bool cleared = false;
    for (uint32_t i = 0u; i < clocks->cap; i++) {
        if (clocks->slots[i].head != SYMBOL_ID_NONE &&
            (clocks->slots[i].sealed_mask & roles) != 0u) {
            clocks->slots[i].sealed_mask =
                (uint8_t)(clocks->slots[i].sealed_mask & ~roles);
            cleared = true;
        }
    }
    if (cleared)
        clocks->seal_rev++;
}

static void head_clocks_apply_classified(struct SpaceHeadClocks *clocks,
                                         SpaceTheoryAtomKind kind,
                                         SymbolId target_head, bool is_open) {
    switch (kind) {
    case SPACE_THEORY_ATOM_DATA:
        if (is_open) {
            clocks->open_data_rev++;
            clocks->open_rule_rev++;
            clocks->open_decl_rev++;
            head_clocks_clear_seals_all(
                clocks,
                SPACE_THEORY_ROLE_DATA | SPACE_THEORY_ROLE_RULES |
                    SPACE_THEORY_ROLE_DECLS);
        } else if (target_head != SYMBOL_ID_NONE) {
            SpaceHeadClockEntry *entry = head_clocks_find(clocks, target_head);
            if (entry) {
                entry->data_rev++;
                if (entry->sealed_mask & SPACE_THEORY_ROLE_DATA) {
                    entry->sealed_mask =
                        (uint8_t)(entry->sealed_mask & ~SPACE_THEORY_ROLE_DATA);
                    clocks->seal_rev++;
                }
            }
        }
        return;
    case SPACE_THEORY_ATOM_DEFINITION:
    case SPACE_THEORY_ATOM_DIRECTIONAL_DEFINITION:
        if (is_open || target_head == SYMBOL_ID_NONE) {
            clocks->open_rule_rev++;
            head_clocks_clear_seals_all(clocks, SPACE_THEORY_ROLE_RULES);
        } else {
            SpaceHeadClockEntry *entry = head_clocks_find(clocks, target_head);
            if (entry) {
                entry->rule_rev++;
                if (entry->sealed_mask & SPACE_THEORY_ROLE_RULES) {
                    entry->sealed_mask =
                        (uint8_t)(entry->sealed_mask & ~SPACE_THEORY_ROLE_RULES);
                    clocks->seal_rev++;
                }
            }
        }
        return;
    case SPACE_THEORY_ATOM_TYPE_ASSIGNMENT:
        if (is_open || target_head == SYMBOL_ID_NONE) {
            clocks->open_decl_rev++;
            head_clocks_clear_seals_all(clocks, SPACE_THEORY_ROLE_DECLS);
        } else {
            SpaceHeadClockEntry *entry = head_clocks_find(clocks, target_head);
            if (entry) {
                entry->decl_rev++;
                if (entry->sealed_mask & SPACE_THEORY_ROLE_DECLS) {
                    entry->sealed_mask =
                        (uint8_t)(entry->sealed_mask & ~SPACE_THEORY_ROLE_DECLS);
                    clocks->seal_rev++;
                }
            }
        }
        return;
    case SPACE_THEORY_ATOM_SUBTYPE_ASSIGNMENT:
        clocks->open_decl_rev++;
        head_clocks_clear_seals_all(clocks, SPACE_THEORY_ROLE_DECLS);
        return;
    case SPACE_THEORY_ATOM_EQUATION:
        /* Equations affect modulo lookups space-wide; equational_revision
         * advances on the Space, and any SPACE_THEORY_ROLE_ALIAS seal clears. */
        head_clocks_clear_seals_all(clocks, SPACE_THEORY_ROLE_ALIAS);
        return;
    }
}

static SymbolId space_theory_data_atom_head(const Atom *atom) {
    if (!atom)
        return SYMBOL_ID_NONE;
    if (atom->kind == ATOM_SYMBOL)
        return atom->sym_id;
    if (atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
        atom->expr.elems[0] && atom->expr.elems[0]->kind == ATOM_SYMBOL)
        return atom->expr.elems[0]->sym_id;
    return SYMBOL_ID_NONE;
}

static bool atom_is_outer_open(const Atom *atom) {
    if (!atom)
        return true;
    if (atom->kind == ATOM_VAR)
        return true;
    return atom->kind == ATOM_EXPR && atom->expr.len > 0u &&
           atom->expr.elems[0] && atom->expr.elems[0]->kind == ATOM_VAR;
}

static void head_clocks_note_atom_id(struct SpaceHeadClocks *clocks,
                                     const Space *source, AtomId id,
                                     const Atom *atom) {
    const TermUniverse *u = source ? source->native.universe : NULL;
    if (u && id != CETTA_ATOM_ID_NONE) {
        const CettaTermHdr *hdr = tu_hdr(u, id);
        if (hdr) {
            SymbolId target_head = SYMBOL_ID_NONE;
            bool is_open = false;
            SpaceTheoryAtomKind kind = space_theory_classify_header(
                u, id, hdr, &target_head, &is_open);
            head_clocks_apply_classified(clocks, kind, target_head, is_open);
            return;
        }
        if (!atom)
            atom = term_universe_peek_atom(u, id);
    }
    if (atom) {
        SymbolId target_head = SYMBOL_ID_NONE;
        SpaceTheoryAtomKind kind =
            space_theory_classify_atom(atom, &target_head, NULL);
        bool is_open = false;
        if (kind == SPACE_THEORY_ATOM_DATA) {
            is_open = atom_is_outer_open(atom);
            if (!is_open)
                target_head = space_theory_data_atom_head(atom);
        } else if (kind == SPACE_THEORY_ATOM_SUBTYPE_ASSIGNMENT) {
            is_open = true;
        } else if (kind != SPACE_THEORY_ATOM_EQUATION) {
            is_open = target_head == SYMBOL_ID_NONE;
        }
        head_clocks_apply_classified(clocks, kind, target_head, is_open);
        return;
    }
    clocks->opaque_rev++;
    head_clocks_clear_seals_all(clocks, SPACE_THEORY_ROLE_ALL);
}

void space_theory_note_mutation_slow(
        Space *s, const SpaceHeadMutationNote *note, uint8_t fallback_roles) {
    if (!s || !s->head_clocks)
        return;
    struct SpaceHeadClocks *clocks = s->head_clocks;
    if (note && note->already_noted)
        return;
    if (!note || note->is_opaque ||
        (fallback_roles & SPACE_THEORY_ROLE_OPAQUE) != 0u) {
        clocks->opaque_rev++;
        head_clocks_clear_seals_all(clocks, SPACE_THEORY_ROLE_ALL);
        return;
    }
    if (note->has_head) {
        uint8_t roles = note->roles & SPACE_THEORY_ROLE_ALL;
        if (note->head == SYMBOL_ID_NONE) {
            if (roles & SPACE_THEORY_ROLE_DATA)
                clocks->open_data_rev++;
            if (roles & SPACE_THEORY_ROLE_RULES)
                clocks->open_rule_rev++;
            if (roles & SPACE_THEORY_ROLE_DECLS)
                clocks->open_decl_rev++;
            head_clocks_clear_seals_all(clocks, roles);
        } else {
            SpaceHeadClockEntry *entry = head_clocks_find(clocks, note->head);
            if (entry) {
                if (roles & SPACE_THEORY_ROLE_DATA)
                    entry->data_rev++;
                if (roles & SPACE_THEORY_ROLE_RULES)
                    entry->rule_rev++;
                if (roles & SPACE_THEORY_ROLE_DECLS)
                    entry->decl_rev++;
                if (entry->sealed_mask & roles) {
                    entry->sealed_mask =
                        (uint8_t)(entry->sealed_mask & ~roles);
                    clocks->seal_rev++;
                }
            }
            if (roles & SPACE_THEORY_ROLE_ALIAS)
                head_clocks_clear_seals_all(clocks, SPACE_THEORY_ROLE_ALIAS);
        }
        return;
    }
    if (note->has_atom) {
        const Space *source = note->source_space ? note->source_space : s;
        head_clocks_note_atom_id(clocks, source, note->atom_id, note->atom);
        if (note->secondary_atom_id != CETTA_ATOM_ID_NONE) {
            head_clocks_note_atom_id(clocks, source, note->secondary_atom_id,
                                     NULL);
        }
        return;
    }
    clocks->opaque_rev++;
    head_clocks_clear_seals_all(clocks, SPACE_THEORY_ROLE_ALL);
}

bool space_theory_watch_head(Space *s, SymbolId head) {
    if (!s || head == SYMBOL_ID_NONE)
        return false;
    CETTA_SCOPED_SPACE_OBSERVE(hc_obs, s);
    SPACE_MAINTAIN(hc_maint, s);
    struct SpaceHeadClocks *clocks = head_clocks_ensure_locked(s);
    if (!clocks)
        return false;
    return head_clocks_ensure_entry_locked(clocks, head) != NULL;
}

bool space_theory_seal_head(Space *s, SymbolId head, uint8_t seal_mask) {
    if (!s || head == SYMBOL_ID_NONE)
        return false;
    seal_mask &= SPACE_THEORY_ROLE_ALL;
    CETTA_SCOPED_SPACE_OBSERVE(hc_obs, s);
    SPACE_MAINTAIN(hc_maint, s);
    struct SpaceHeadClocks *clocks = head_clocks_ensure_locked(s);
    if (!clocks)
        return false;
    SpaceHeadClockEntry *entry = head_clocks_ensure_entry_locked(clocks, head);
    if (!entry)
        return false;
    uint8_t next = (uint8_t)(entry->sealed_mask | seal_mask);
    if (next != entry->sealed_mask) {
        entry->sealed_mask = next;
        clocks->seal_rev++;
    }
    return true;
}

uint8_t space_theory_head_sealed_mask(const Space *s, SymbolId head) {
    if (!s || head == SYMBOL_ID_NONE)
        return 0u;
    CETTA_SCOPED_SPACE_OBSERVE(hc_obs, s);
    SPACE_MAINTAIN(hc_maint, s);
    SpaceHeadClockEntry *entry = head_clocks_find(s->head_clocks, head);
    return entry ? entry->sealed_mask : 0u;
}

bool space_theory_head_clock_capture(
        Space *s, SymbolId head, uint8_t roles, SpaceHeadClockStamp *out) {
    if (out)
        *out = (SpaceHeadClockStamp){0};
    if (!s || head == SYMBOL_ID_NONE || !out)
        return false;
    CETTA_SCOPED_SPACE_OBSERVE(hc_obs, s);
    SPACE_MAINTAIN(hc_maint, s);
    uint64_t instance_id = space_instance_id(s);
    if (instance_id == 0u)
        return false;
    struct SpaceHeadClocks *clocks = head_clocks_ensure_locked(s);
    if (!clocks)
        return false;
    SpaceHeadClockEntry *entry = head_clocks_ensure_entry_locked(clocks, head);
    if (!entry)
        return false;
    uint64_t base_dep_epoch =
        s->overlay_base ? space_overlay_dependency_epoch(s) : 0u;
    if (base_dep_epoch == UINT64_MAX)
        return false;
    *out = (SpaceHeadClockStamp){
        .space = s,
        .overlay_base = s->overlay_base,
        .instance_id = instance_id,
        .table_id = clocks->table_id,
        .head = head,
        .roles = (uint8_t)(roles & SPACE_THEORY_ROLE_ALL),
        .rule_rev = entry->rule_rev,
        .decl_rev = entry->decl_rev,
        .data_rev = entry->data_rev,
        .open_rule_rev = clocks->open_rule_rev,
        .open_decl_rev = clocks->open_decl_rev,
        .open_data_rev = clocks->open_data_rev,
        .opaque_rev = clocks->opaque_rev,
        .equational_revision = space_equational_revision(s),
        .base_dependency_epoch = base_dep_epoch,
        .base_prefix_epoch =
            s->overlay_base ? space_prefix_epoch(s->overlay_base) : 0u,
    };
    return true;
}

bool space_theory_head_clock_current(
        const Space *s, const SpaceHeadClockStamp *stamp) {
    if (!s || !stamp || stamp->space != s || stamp->instance_id == 0u ||
        stamp->table_id == 0u || stamp->head == SYMBOL_ID_NONE)
        return false;
    CETTA_SCOPED_SPACE_OBSERVE(hc_obs, s);
    SPACE_MAINTAIN(hc_maint, s);
    if (space_instance_id(s) != stamp->instance_id ||
        s->overlay_base != stamp->overlay_base)
        return false;
    if (s->overlay_base) {
        if (space_overlay_dependency_epoch(s) != stamp->base_dependency_epoch ||
            space_prefix_epoch(s->overlay_base) != stamp->base_prefix_epoch)
            return false;
    }
    struct SpaceHeadClocks *clocks = s->head_clocks;
    if (!clocks || clocks->table_id != stamp->table_id ||
        clocks->opaque_rev != stamp->opaque_rev)
        return false;
    if ((stamp->roles & SPACE_THEORY_ROLE_ALIAS) != 0u &&
        space_equational_revision(s) != stamp->equational_revision)
        return false;
    SpaceHeadClockEntry *entry = head_clocks_find(clocks, stamp->head);
    if (!entry)
        return false;
    if ((stamp->roles & SPACE_THEORY_ROLE_RULES) != 0u &&
        (entry->rule_rev != stamp->rule_rev ||
         clocks->open_rule_rev != stamp->open_rule_rev))
        return false;
    if ((stamp->roles & SPACE_THEORY_ROLE_DECLS) != 0u &&
        (entry->decl_rev != stamp->decl_rev ||
         clocks->open_decl_rev != stamp->open_decl_rev))
        return false;
    if ((stamp->roles & SPACE_THEORY_ROLE_DATA) != 0u &&
        (entry->data_rev != stamp->data_rev ||
         clocks->open_data_rev != stamp->open_data_rev))
        return false;
    return true;
}

bool space_theory_head_set_clock_capture(
        Space *s, const SymbolId *heads, uint32_t head_count, uint8_t roles,
        SpaceHeadSetStamp *out) {
    if (out)
        *out = (SpaceHeadSetStamp){0};
    roles &= SPACE_THEORY_ROLE_ALL;
    if (!s || !heads || head_count == 0u || roles == 0u || !out ||
        s->overlay_base != NULL)
        return false;
    CETTA_SCOPED_SPACE_OBSERVE(hc_obs, s);
    SPACE_MAINTAIN(hc_maint, s);
    if (space_instance_id(s) == 0u || s->overlay_base != NULL)
        return false;
    struct SpaceHeadClocks *clocks = head_clocks_ensure_locked(s);
    if (!clocks)
        return false;
    uint64_t combined = clocks->opaque_rev;
    if ((roles & SPACE_THEORY_ROLE_RULES) != 0u)
        combined += clocks->open_rule_rev;
    if ((roles & SPACE_THEORY_ROLE_DECLS) != 0u)
        combined += clocks->open_decl_rev;
    if ((roles & SPACE_THEORY_ROLE_DATA) != 0u)
        combined += clocks->open_data_rev;
    if ((roles & SPACE_THEORY_ROLE_ALIAS) != 0u)
        combined += space_equational_revision(s);
    for (uint32_t i = 0u; i < head_count; i++) {
        if (heads[i] == SYMBOL_ID_NONE)
            return false;
        SpaceHeadClockEntry *entry =
            head_clocks_ensure_entry_locked(clocks, heads[i]);
        if (!entry)
            return false;
        if ((roles & SPACE_THEORY_ROLE_RULES) != 0u)
            combined += entry->rule_rev;
        if ((roles & SPACE_THEORY_ROLE_DECLS) != 0u)
            combined += entry->decl_rev;
        if ((roles & SPACE_THEORY_ROLE_DATA) != 0u)
            combined += entry->data_rev;
    }
    *out = (SpaceHeadSetStamp){
        .table_id = clocks->table_id,
        .combined_rev = combined,
        .head_count = head_count,
        .roles = roles,
        .active = 1u,
    };
    return true;
}

bool space_theory_head_set_clock_current(
        const Space *s, const SymbolId *heads, uint32_t head_count,
        const SpaceHeadSetStamp *stamp) {
    if (!s || !heads || !stamp || !stamp->active || stamp->table_id == 0u ||
        head_count == 0u || stamp->head_count != head_count ||
        s->overlay_base != NULL)
        return false;
    CETTA_SCOPED_SPACE_OBSERVE(hc_obs, s);
    SPACE_MAINTAIN(hc_maint, s);
    if (s->overlay_base != NULL)
        return false;
    struct SpaceHeadClocks *clocks = s->head_clocks;
    if (!clocks || clocks->table_id != stamp->table_id)
        return false;
    uint8_t roles = stamp->roles;
    uint64_t combined = clocks->opaque_rev;
    if ((roles & SPACE_THEORY_ROLE_RULES) != 0u)
        combined += clocks->open_rule_rev;
    if ((roles & SPACE_THEORY_ROLE_DECLS) != 0u)
        combined += clocks->open_decl_rev;
    if ((roles & SPACE_THEORY_ROLE_DATA) != 0u)
        combined += clocks->open_data_rev;
    if ((roles & SPACE_THEORY_ROLE_ALIAS) != 0u)
        combined += space_equational_revision(s);
    for (uint32_t i = 0u; i < head_count; i++) {
        SpaceHeadClockEntry *entry = head_clocks_find(clocks, heads[i]);
        if (!entry)
            return false;
        if ((roles & SPACE_THEORY_ROLE_RULES) != 0u)
            combined += entry->rule_rev;
        if ((roles & SPACE_THEORY_ROLE_DECLS) != 0u)
            combined += entry->decl_rev;
        if ((roles & SPACE_THEORY_ROLE_DATA) != 0u)
            combined += entry->data_rev;
    }
    return combined == stamp->combined_rev;
}

static bool space_own_atoms_touch_head(const Space *s, SymbolId head,
                                       uint8_t roles) {
    if (!s || head == SYMBOL_ID_NONE || roles == 0u)
        return false;
    CettaIndex begin = space_imported_length(s);
    CettaCount len = space_length64(s);
    for (CettaIndex i = begin; i < len; i++) {
        Atom *atom = space_get_at64(s, i);
        if (!atom)
            return true;
        if (atom_is_outer_open(atom)) {
            if ((roles & (SPACE_THEORY_ROLE_RULES | SPACE_THEORY_ROLE_DECLS |
                          SPACE_THEORY_ROLE_DATA)) != 0u)
                return true;
            continue;
        }
        SymbolId atom_head = SYMBOL_ID_NONE;
        SpaceTheoryAtomKind kind =
            space_theory_classify_atom(atom, &atom_head, NULL);
        switch (kind) {
        case SPACE_THEORY_ATOM_DATA:
            if ((roles & SPACE_THEORY_ROLE_DATA) != 0u &&
                space_theory_data_atom_head(atom) == head)
                return true;
            break;
        case SPACE_THEORY_ATOM_DEFINITION:
        case SPACE_THEORY_ATOM_DIRECTIONAL_DEFINITION:
            if ((roles & SPACE_THEORY_ROLE_RULES) != 0u &&
                (atom_head == head || atom_head == SYMBOL_ID_NONE))
                return true;
            break;
        case SPACE_THEORY_ATOM_TYPE_ASSIGNMENT:
            if ((roles & SPACE_THEORY_ROLE_DECLS) != 0u &&
                (atom_head == head || atom_head == SYMBOL_ID_NONE))
                return true;
            break;
        case SPACE_THEORY_ATOM_SUBTYPE_ASSIGNMENT:
            if ((roles & SPACE_THEORY_ROLE_DECLS) != 0u)
                return true;
            break;
        case SPACE_THEORY_ATOM_EQUATION:
            if ((roles & SPACE_THEORY_ROLE_ALIAS) != 0u)
                return true;
            break;
        }
    }
    return false;
}

bool space_theory_import_protects_head(
        const Space *importer, const Space *module, SymbolId head,
        uint8_t roles) {
    roles &= SPACE_THEORY_ROLE_ALL;
    if (!importer || !module || head == SYMBOL_ID_NONE || roles == 0u)
        return false;
    if (importer == module)
        return (space_theory_head_sealed_mask(module, head) & roles) == roles;
    CETTA_SCOPED_SPACES2(protect_guard,
                         importer, space_observation_mode(importer),
                         module, space_observation_mode(module));
    if (importer->overlay_base != NULL)
        return false;
    bool imports_module = false;
    uint32_t dep_count = space_dependency_count(importer);
    for (uint32_t d = 0u; d < dep_count; d++) {
        if (space_dependency_at(importer, d) == module) {
            imports_module = true;
            break;
        }
    }
    if (!imports_module)
        return false;
    uint8_t sealed = 0u;
    {
        SPACE_MAINTAIN(mod_maint, module);
        SpaceHeadClockEntry *entry = head_clocks_find(module->head_clocks, head);
        sealed = entry ? entry->sealed_mask : 0u;
    }
    if ((sealed & roles) != roles)
        return false;
    if (space_own_atoms_touch_head(importer, head, roles))
        return false;
    for (uint32_t d = 0u; d < dep_count; d++) {
        const Space *dep = space_dependency_at(importer, d);
        if (dep && dep != module && space_own_atoms_touch_head(dep, head, roles))
            return false;
    }
    return true;
}

static bool space_head_set_contains(const SymbolId *heads, uint32_t head_count,
                                    SymbolId target) {
    if (!heads || head_count == 0u || target == SYMBOL_ID_NONE)
        return false;
    uint32_t lo = 0u, hi = head_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (heads[mid] < target)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo < head_count && heads[lo] == target;
}

static bool space_own_atoms_touch_head_set(const Space *s,
                                           const SymbolId *sorted_heads,
                                           uint32_t head_count,
                                           uint8_t roles) {
    if (!s || !sorted_heads || head_count == 0u || roles == 0u)
        return false;
    CettaIndex begin = space_imported_length(s);
    CettaCount len = space_length64(s);
    for (CettaIndex i = begin; i < len; i++) {
        Atom *atom = space_get_at64(s, i);
        if (!atom)
            return true;
        if (atom_is_outer_open(atom)) {
            if ((roles & (SPACE_THEORY_ROLE_RULES | SPACE_THEORY_ROLE_DECLS |
                          SPACE_THEORY_ROLE_DATA)) != 0u)
                return true;
            continue;
        }
        SymbolId atom_head = SYMBOL_ID_NONE;
        SpaceTheoryAtomKind kind =
            space_theory_classify_atom(atom, &atom_head, NULL);
        switch (kind) {
        case SPACE_THEORY_ATOM_DATA:
            if ((roles & SPACE_THEORY_ROLE_DATA) != 0u &&
                space_head_set_contains(sorted_heads, head_count,
                                        space_theory_data_atom_head(atom)))
                return true;
            break;
        case SPACE_THEORY_ATOM_DEFINITION:
        case SPACE_THEORY_ATOM_DIRECTIONAL_DEFINITION:
            if ((roles & SPACE_THEORY_ROLE_RULES) != 0u &&
                (atom_head == SYMBOL_ID_NONE ||
                 space_head_set_contains(sorted_heads, head_count, atom_head)))
                return true;
            break;
        case SPACE_THEORY_ATOM_TYPE_ASSIGNMENT:
            if ((roles & SPACE_THEORY_ROLE_DECLS) != 0u &&
                (atom_head == SYMBOL_ID_NONE ||
                 space_head_set_contains(sorted_heads, head_count, atom_head)))
                return true;
            break;
        case SPACE_THEORY_ATOM_SUBTYPE_ASSIGNMENT:
            if ((roles & SPACE_THEORY_ROLE_DECLS) != 0u)
                return true;
            break;
        case SPACE_THEORY_ATOM_EQUATION:
            if ((roles & SPACE_THEORY_ROLE_ALIAS) != 0u)
                return true;
            break;
        }
    }
    return false;
}

bool space_theory_import_protects_head_set(
        const Space *importer, const Space *module,
        const SymbolId *heads, uint32_t head_count, uint8_t roles) {
    roles &= SPACE_THEORY_ROLE_ALL;
    if (!importer || !module || !heads || head_count == 0u || roles == 0u)
        return false;
    if (importer == module)
        return true;
    CETTA_SCOPED_SPACES2(protect_guard,
                         importer, space_observation_mode(importer),
                         module, space_observation_mode(module));
    if (importer->overlay_base != NULL)
        return false;
    bool imports_module = false;
    uint32_t dep_count = space_dependency_count(importer);
    for (uint32_t d = 0u; d < dep_count; d++) {
        if (space_dependency_at(importer, d) == module) {
            imports_module = true;
            break;
        }
    }
    if (!imports_module)
        return false;
    if (space_own_atoms_touch_head_set(importer, heads, head_count, roles))
        return false;
    for (uint32_t d = 0u; d < dep_count; d++) {
        const Space *dep = space_dependency_at(importer, d);
        if (dep && dep != module &&
            space_own_atoms_touch_head_set(dep, heads, head_count, roles))
            return false;
    }
    return true;
}

/* ── Per-Head Validity Guard Constructor ────────────────────────────────── */

SpaceRegionGuard space_region_guard_for_head(
        SpaceObserverKind proved_observer,
        Space *theory_space, uint8_t theory_roles,
        Space *data_space, uint8_t data_roles,
        SymbolId head, bool positive_witness_present) {
    SpaceRegionGuard guard = {
        .proved_observer = proved_observer,
        .footprint = SPACE_FOOTPRINT_HEAD_SCOPED,
        .positive_witness_present = positive_witness_present,
        .data_dependency_free =
            data_space != NULL && space_dependency_count(data_space) == 0u,
        .has_discovery = false,
        .has_theory_head = false,
        .has_data_head = false,
        .theory = space_theory_token(theory_space),
        .data = data_space ? space_read_token(data_space) : (SpaceReadToken){0},
    };
    if (theory_space && theory_roles != 0u && head != SYMBOL_ID_NONE) {
        guard.has_theory_head = space_theory_head_clock_capture(
            theory_space, head,
            (uint8_t)((theory_roles | SPACE_THEORY_ROLE_ALIAS) &
                      SPACE_THEORY_ROLE_ALL),
            &guard.theory_head);
        if (!guard.has_theory_head) {
            guard.theory.instance_id = 0u;
            return guard;
        }
    }
    if (data_space && data_roles != 0u && head != SYMBOL_ID_NONE) {
        guard.has_data_head = space_theory_head_clock_capture(
            data_space, head,
            (uint8_t)((data_roles | SPACE_THEORY_ROLE_ALIAS) &
                      SPACE_THEORY_ROLE_ALL),
            &guard.data_head);
        if (!guard.has_data_head) {
            guard.has_theory_head = false;
            guard.theory.instance_id = 0u;
            return guard;
        }
    }
    return guard;
}
