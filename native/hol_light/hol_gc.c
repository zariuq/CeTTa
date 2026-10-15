/* hol_gc.c - ownership: references, owners, roots and collection.
 *
 * Objects are never moved.  A collection marks everything reachable from
 * live references, registered roots (replay engines, writers) and the
 * bootstrap, then frees the rest.  It runs only at the start of a public
 * call or between replay records, never while kernel code holds raw
 * pointers, so temporaries of a call stay valid for that call and are
 * reclaimed afterwards.  If the mark stack cannot grow, the collection is
 * abandoned without freeing anything.
 *
 * References encode (kernel tag:16 | kind:4 | generation:12 | slot+1:32).
 * A slot's generation advances on release; a slot whose generation reaches
 * 4095 is retired, so a (slot, generation) pair is issued at most once and
 * a released reference can never alias a later object. */
#include "hol_internal.h"

enum { MK_TYPE = 0, MK_TERM = 1, MK_THM = 2, MK_DECL = 3, MK_DEPS = 4 };

static bool mark_push(HolKernel *k, void *p, int tag) {
    if (k->nmark == k->capmark) {
        size_t ncap = k->capmark ? k->capmark * 2 : 4096;
        void **ns = (void **)realloc(k->markstack, ncap * sizeof(void *));
        if (!ns) return false;
        k->markstack = ns;
        k->capmark = ncap;
    }
    k->markstack[k->nmark++] = (void *)((uintptr_t)p | (uintptr_t)tag);
    return true;
}

static inline void mk(HolKernel *k, void *p, int tag) {
    if (!p) return;
    uint8_t *m;
    switch (tag) {
    case MK_TYPE: m = &((HolType *)p)->mark; break;
    case MK_TERM: m = &((HolTerm *)p)->mark; break;
    case MK_THM: m = &((HolThm *)p)->mark; break;
    case MK_DECL: m = &((HolDecl *)p)->mark; break;
    default: m = &((HolDeps *)p)->mark; break;
    }
    if (*m) return;
    *m = 1;
    if (!mark_push(k, p, tag)) k->mark_failed = true;
}

void hk_mark_type(HolKernel *k, HolType *t) { mk(k, t, MK_TYPE); }
void hk_mark_term(HolKernel *k, HolTerm *t) { mk(k, t, MK_TERM); }
void hk_mark_thm(HolKernel *k, HolThm *t) { mk(k, t, MK_THM); }
void hk_mark_decl(HolKernel *k, HolDecl *d) { mk(k, d, MK_DECL); }

static void mark_drain(HolKernel *k) {
    while (k->nmark && !k->mark_failed) {
        uintptr_t v = (uintptr_t)k->markstack[--k->nmark];
        int tag = (int)(v & 7u);
        void *p = (void *)(v & ~(uintptr_t)7u);
        switch (tag) {
        case MK_TYPE: {
            HolType *t = (HolType *)p;
            mk(k, t->op, MK_DECL);
            mk(k, t->anchor, MK_DECL);
            for (uint32_t i = 0; i < t->nargs; i++) mk(k, t->args[i], MK_TYPE);
            break;
        }
        case MK_TERM: {
            HolTerm *t = (HolTerm *)p;
            mk(k, t->type, MK_TYPE);
            mk(k, t->anchor, MK_DECL);
            if (t->kind == HOL_TM_VAR || t->kind == HOL_TM_CONST) mk(k, t->u.leaf.decl, MK_DECL);
            else {
                mk(k, t->u.node.l, MK_TERM);
                mk(k, t->u.node.r, MK_TERM);
            }
            break;
        }
        case MK_THM: {
            HolThm *t = (HolThm *)p;
            mk(k, t->concl, MK_TERM);
            for (uint32_t i = 0; i < t->nhyps; i++) mk(k, t->hyps[i], MK_TERM);
            mk(k, t->deps, MK_DEPS);
            mk(k, t->anchor, MK_DECL);
            break;
        }
        case MK_DEPS: {
            HolDeps *d = (HolDeps *)p;
            for (uint32_t i = 0; i < d->n; i++) mk(k, d->ax[i], MK_DECL);
            break;
        }
        default: {
            HolDecl *d = (HolDecl *)p;
            mk(k, d->parent, MK_DECL);
            mk(k, d->gtype, MK_TYPE);
            mk(k, d->body, MK_TERM);
            mk(k, d->thm, MK_THM);
            mk(k, d->thm2, MK_THM);
            mk(k, d->witness, MK_THM);
            break;
        }
        }
    }
}

static void clear_marks_table(HolTable *t, size_t off) {
    for (size_t i = 0; i < t->cap; i++)
        if (t->slot[i]) *((uint8_t *)t->slot[i] + off) = 0;
}

static void clear_all_marks(HolKernel *k) {
    clear_marks_table(&k->types, offsetof(HolType, mark));
    clear_marks_table(&k->terms, offsetof(HolTerm, mark));
    clear_marks_table(&k->thms, offsetof(HolThm, mark));
    clear_marks_table(&k->deps, offsetof(HolDeps, mark));
    for (size_t i = 0; i < k->ndecls; i++) k->decls[i]->mark = 0;
    k->nmark = 0;
}

static size_t type_size(const HolType *t) { return sizeof(HolType) + (size_t)t->nargs * sizeof(HolType *); }
static size_t thm_size(const HolThm *t) { return sizeof(HolThm) + (size_t)t->nhyps * sizeof(HolTerm *); }
static size_t deps_size(const HolDeps *d) { return sizeof(HolDeps) + (size_t)d->n * sizeof(HolDecl *); }

/* Sweeping happens in two phases so that it is all-or-nothing: every new
 * slot array is allocated before any object is freed.  A partial sweep
 * could leave a dead object in a table whose referents were freed, and a
 * later hash-cons lookup could then resurrect it. */
static void **sweep_prepare(HolTable *t, size_t mark_off, size_t *ncap_out, size_t *live_out) {
    size_t live = 0;
    for (size_t i = 0; i < t->cap; i++)
        if (t->slot[i] && *((uint8_t *)t->slot[i] + mark_off)) live++;
    size_t ncap = 1024;
    while (ncap < live * 2 + 2) ncap *= 2;
    void **ns = (void **)malloc(ncap * sizeof(void *));
    if (ns) memset(ns, 0, ncap * sizeof(void *));
    *ncap_out = ncap;
    *live_out = live;
    return ns;
}

static void sweep_commit(HolKernel *k, HolTable *t, size_t mark_off, int kind, void **ns,
                         size_t ncap, size_t live) {
    for (size_t i = 0; i < t->cap; i++) {
        void *o = t->slot[i];
        if (!o) continue;
        uint8_t *m = (uint8_t *)o + mark_off;
        if (*m) {
            *m = 0;
            size_t j = (*(uint32_t *)o) & (ncap - 1);
            while (ns[j]) j = (j + 1) & (ncap - 1);
            ns[j] = o;
        } else {
            size_t sz = kind == MK_TYPE ? type_size((HolType *)o)
                      : kind == MK_TERM ? sizeof(HolTerm)
                      : kind == MK_THM ? thm_size((HolThm *)o) : deps_size((HolDeps *)o);
            hk_dealloc(k, o, sz);
        }
    }
    k->bytes_live -= t->cap * sizeof(void *);
    free(t->slot);
    t->slot = ns;
    t->cap = ncap;
    t->count = live;
    k->bytes_live += ncap * sizeof(void *);
}

static void sweep_decls(HolKernel *k) {
    /* Live events are closed under ancestors, so the dead events of a
     * branch are a suffix of it: move each tip back to its last live event. */
    for (size_t i = 0; i < k->ndecls; i++) {
        HolDecl *d = k->decls[i];
        if (d->mark || !d->committed || d->branch->tip != d) continue;
        HolDecl *p = d->parent;
        while (p && p->branch == d->branch && !p->mark) p = p->parent;
        d->branch->tip = (p && p->branch == d->branch) ? p : NULL;
    }
    /* Remove dead events from the name chains (all still allocated). */
    for (size_t i = 0; i < k->ndecls; i++) {
        HolDecl *d = k->decls[i];
        if (d->mark || !d->committed || !d->name) continue;
        HolNameRec *nr = &k->names[d->name];
        HolDecl **heads[2] = {&nr->types, &nr->consts};
        for (int h = 0; h < 2; h++) {
            HolDecl **pp = heads[h];
            while (*pp) {
                if (!(*pp)->mark) *pp = (*pp)->next_same;
                else pp = &(*pp)->next_same;
            }
        }
    }
    size_t o = 0;
    for (size_t i = 0; i < k->ndecls; i++) {
        HolDecl *d = k->decls[i];
        if (d->mark) {
            d->mark = 0;
            d->index = (uint32_t)o;
            k->decls[o++] = d;
            continue;
        }
        if (--d->branch->refs == 0) hk_dealloc(k, d->branch, sizeof(HolBranch));
        hk_dealloc(k, d, sizeof(HolDecl));
    }
    k->ndecls = o;
}

void hol_gc(HolKernel *k) {
    if (!k) return;
    k->mark_failed = false;
    k->nmark = 0;
    mk(k, k->d_fun, MK_DECL);
    mk(k, k->d_bool, MK_DECL);
    mk(k, k->d_eq, MK_DECL);
    mk(k, k->bool_ty, MK_TYPE);
    mk(k, k->aty, MK_TYPE);
    for (uint32_t i = 0; i < k->nslots; i++) {
        HolSlot *s = &k->slots[i];
        switch (s->kind) {
        case HOL_KIND_TYPE: mk(k, s->obj, MK_TYPE); break;
        case HOL_KIND_TERM: mk(k, s->obj, MK_TERM); break;
        case HOL_KIND_THM: mk(k, s->obj, MK_THM); break;
        case HOL_KIND_ENV: mk(k, s->obj, MK_DECL); break;
        default: break;
        }
        mark_drain(k);
    }
    for (size_t i = 0; i < k->nroots; i++) {
        k->roots[i].fn(k, k->roots[i].ctx);
        mark_drain(k);
    }
    hk_journal_mark(k);
    mark_drain(k);
    mark_drain(k);
    if (k->mark_failed) {
        clear_all_marks(k);
        return;
    }
    HolTable *tabs[4] = {&k->thms, &k->terms, &k->types, &k->deps};
    size_t offs[4] = {offsetof(HolThm, mark), offsetof(HolTerm, mark), offsetof(HolType, mark),
                      offsetof(HolDeps, mark)};
    int kinds[4] = {MK_THM, MK_TERM, MK_TYPE, MK_DEPS};
    void **ns[4] = {0};
    size_t ncap[4], live[4];
    for (int i = 0; i < 4; i++) {
        ns[i] = sweep_prepare(tabs[i], offs[i], &ncap[i], &live[i]);
        if (!ns[i]) {
            for (int j = 0; j < i; j++) free(ns[j]);
            clear_all_marks(k);
            return;
        }
    }
    for (int i = 0; i < 4; i++) sweep_commit(k, tabs[i], offs[i], kinds[i], ns[i], ncap[i], live[i]);
    sweep_decls(k);
    k->gc_runs++;
    k->bytes_since_gc = 0;
    if (k->memo.cap) { /* entries may name freed objects; never consulted again */
        k->memo.gen++;
        k->memo.used = 0;
        if (k->memo.gen == 0) {
            memset(k->memo.e, 0, k->memo.cap * sizeof(HolMemoEntry));
            k->memo.gen = 1;
        }
    }
}

void hk_maybe_gc(HolKernel *k) {
    if (k->bytes_since_gc > k->gc_threshold && k->bytes_since_gc > k->bytes_live / 2) hol_gc(k);
}

int hk_root_add(HolKernel *k, HolRootFn fn, void *ctx) {
    if (k->nroots == k->caproots) {
        size_t ncap = k->caproots ? k->caproots * 2 : 8;
        HolRootRec *nr = (HolRootRec *)realloc(k->roots, ncap * sizeof(HolRootRec));
        if (!nr) { hk_fail(k, HOL_NOMEM, HOL_R_NOMEM); return -1; }
        k->roots = nr;
        k->caproots = ncap;
    }
    k->roots[k->nroots].fn = fn;
    k->roots[k->nroots].ctx = ctx;
    k->nroots++;
    return 0;
}

void hk_root_remove(HolKernel *k, HolRootFn fn, void *ctx) {
    for (size_t i = 0; i < k->nroots; i++)
        if (k->roots[i].fn == fn && k->roots[i].ctx == ctx) {
            k->roots[i] = k->roots[--k->nroots];
            return;
        }
}

/* ------------------------------------------------------------------------ */
/* References and owners                                                     */
/* ------------------------------------------------------------------------ */

#define REF_INDEX(r) ((uint32_t)((r) & 0xffffffffu))
#define REF_GEN(r) ((uint32_t)(((r) >> 32) & 0xfffu))
#define REF_KIND(r) ((uint32_t)(((r) >> 44) & 0xfu))
#define REF_TAG(r) ((uint16_t)((r) >> 48))
#define GEN_RETIRE 0xfffu

#define OWNER_MASK UINT32_C(0xffffff)
#define OWNER_INDEX(o) ((uint32_t)((o) & OWNER_MASK))
#define OWNER_GENERATION(o) ((uint32_t)(((o) >> 24) & OWNER_MASK))

static bool owner_index(HolKernel *k, HolOwner owner, uint32_t *out) {
    if (owner == HOL_OWNER_KERNEL) { *out = 0; return true; }
    if ((uint16_t)(owner >> 48) != k->tag) {
        hk_fail(k, HOL_FOREIGN, HOL_R_FOREIGN_KERNEL);
        return false;
    }
    uint32_t index = OWNER_INDEX(owner);
    if (!index || index >= k->nowners || !k->owners[index].live ||
        k->owners[index].generation != OWNER_GENERATION(owner)) {
        hk_fail(k, HOL_STALE, HOL_R_BAD_HANDLE);
        return false;
    }
    *out = index;
    return true;
}

static void owner_link(HolKernel *k, uint32_t idx, uint32_t o) {
    HolSlot *s = &k->slots[idx];
    s->owner = o;
    s->prev = 0;
    s->next = k->owners[o].head;
    if (s->next) k->slots[s->next - 1].prev = idx + 1;
    k->owners[o].head = idx + 1;
}

static void owner_unlink(HolKernel *k, uint32_t idx) {
    HolSlot *s = &k->slots[idx];
    if (s->prev) k->slots[s->prev - 1].next = s->next;
    else k->owners[s->owner].head = s->next;
    if (s->next) k->slots[s->next - 1].prev = s->prev;
    s->next = s->prev = 0;
}

HolRef hk_ref_new(HolKernel *k, HolKind kind, void *obj, uint64_t event) {
    uint32_t idx;
    if (k->free_slot) {
        idx = k->free_slot - 1;
        k->free_slot = k->slots[idx].next;
    } else {
        if (k->nslots == UINT32_MAX - 1) { hk_fail(k, HOL_LIMIT, HOL_R_BAD_HANDLE); return 0; }
        if (k->nslots == k->capslots) {
            uint32_t ncap = k->capslots ? k->capslots * 2 : 256;
            HolSlot *ns = (HolSlot *)hk_realloc(k, k->slots, k->capslots * sizeof(HolSlot),
                                                 (size_t)ncap * sizeof(HolSlot));
            if (!ns) return 0;
            k->slots = ns;
            k->capslots = ncap;
        }
        idx = k->nslots++;
        memset(&k->slots[idx], 0, sizeof(HolSlot));
    }
    HolSlot *s = &k->slots[idx];
    s->obj = obj;
    s->event = event;
    s->kind = (uint8_t)kind;
    s->next = s->prev = 0;
    uint32_t o = OWNER_INDEX(k->cur_owner);
    owner_link(k, idx, o);
    k->live_refs++;
    return ((uint64_t)k->tag << 48) | ((uint64_t)kind << 44) |
           ((uint64_t)(s->gen & 0xfffu) << 32) | (uint64_t)(idx + 1);
}

static HolSlot *ref_slot(HolKernel *k, HolRef r, uint32_t *idx_out) {
    if (!r) { hk_fail(k, HOL_MALFORMED, HOL_R_NULL_ARG); return NULL; }
    if (REF_TAG(r) != k->tag) { hk_fail(k, HOL_FOREIGN, HOL_R_FOREIGN_KERNEL); return NULL; }
    uint32_t idx = REF_INDEX(r);
    if (idx == 0 || idx > k->nslots) { hk_fail(k, HOL_STALE, HOL_R_FUTURE_HANDLE); return NULL; }
    idx--;
    HolSlot *s = &k->slots[idx];
    uint32_t g = REF_GEN(r), sg = s->gen & 0xfffu;
    if (s->kind == HOL_KIND_NONE || g != sg) {
        if (g < sg || (g == sg && s->kind == HOL_KIND_NONE))
            hk_fail(k, HOL_STALE, HOL_R_STALE_HANDLE);
        else hk_fail(k, HOL_STALE, HOL_R_FUTURE_HANDLE);
        return NULL;
    }
    if (REF_KIND(r) != s->kind) { hk_fail(k, HOL_MALFORMED, HOL_R_BAD_HANDLE); return NULL; }
    *idx_out = idx;
    return s;
}

void *hk_ref_get(HolKernel *k, HolRef r, HolKind kind, uint64_t *event) {
    uint32_t idx;
    HolSlot *s = ref_slot(k, r, &idx);
    if (!s) return NULL;
    if (s->kind != kind) return hk_fail(k, HOL_MALFORMED, HOL_R_WRONG_KIND);
    if (event) *event = s->event;
    return s->obj;
}

static void slot_release(HolKernel *k, uint32_t idx) {
    HolSlot *s = &k->slots[idx];
    owner_unlink(k, idx);
    s->kind = HOL_KIND_NONE;
    s->obj = NULL;
    s->gen++;
    k->live_refs--;
    if ((s->gen & 0xfffu) >= GEN_RETIRE) {
        s->retired = 1; /* never reissued */
        return;
    }
    s->next = k->free_slot;
    k->free_slot = idx + 1;
}

/* Internal: release without touching the call status (error paths). */
void hk_ref_drop(HolKernel *k, HolRef ref) {
    HolStatus saved = k->status;
    HolReason saved_r = k->reason;
    uint32_t idx;
    if (ref_slot(k, ref, &idx)) slot_release(k, idx);
    k->status = saved;
    k->reason = saved_r;
}

/* Internal: kind of a valid reference, NONE otherwise; status untouched. */
HolKind hk_ref_kind(HolKernel *k, HolRef ref) {
    HolStatus saved = k->status;
    HolReason saved_r = k->reason;
    uint32_t idx;
    HolSlot *s = ref_slot(k, ref, &idx);
    k->status = saved;
    k->reason = saved_r;
    return s ? (HolKind)s->kind : HOL_KIND_NONE;
}

HolStatus hol_ref_release(HolKernel *k, HolRef ref) {
    if (!k) return HOL_MALFORMED;
    hk_begin_call(k);
    uint32_t idx;
    if (!ref_slot(k, ref, &idx)) return k->status;
    slot_release(k, idx);
    return HOL_OK;
}

HolKind hol_ref_kind(HolKernel *k, HolRef ref) {
    if (!k) return HOL_KIND_NONE;
    hk_begin_call(k);
    uint32_t idx;
    HolSlot *s = ref_slot(k, ref, &idx);
    return s ? (HolKind)s->kind : HOL_KIND_NONE;
}

HolStatus hol_ref_dup(HolKernel *k, HolRef ref, HolRef *out) {
    if (!k || !out) return HOL_MALFORMED;
    hk_begin_call(k);
    uint32_t idx;
    HolSlot *s = ref_slot(k, ref, &idx);
    if (!s) return k->status;
    HolRef r = hk_ref_new(k, (HolKind)s->kind, s->obj, s->event);
    if (!r) return k->status;
    *out = r;
    return HOL_OK;
}

HolStatus hol_ref_move(HolKernel *k, HolRef ref, HolOwner owner) {
    if (!k) return HOL_MALFORMED;
    hk_begin_call(k);
    uint32_t idx;
    if (!ref_slot(k, ref, &idx)) return k->status;
    uint32_t owner_idx;
    if (!owner_index(k, owner, &owner_idx)) return k->status;
    owner_unlink(k, idx);
    owner_link(k, idx, owner_idx);
    return HOL_OK;
}

HolOwner hol_owner_new(HolKernel *k) {
    if (!k) return 0;
    hk_begin_call(k);
    uint32_t index;
    if (k->free_owner) {
        index = k->free_owner;
        k->free_owner = k->owners[index].next_free;
    } else {
      if (k->nowners > OWNER_MASK) {
        hk_fail(k, HOL_LIMIT, HOL_R_BAD_HANDLE);
        return 0;
      }
      if (k->nowners == k->capowners) {
        uint32_t ncap = k->capowners < (OWNER_MASK + 1u) / 2u
            ? k->capowners * 2u : OWNER_MASK + 1u;
        HolOwnerRec *no = (HolOwnerRec *)hk_realloc(k, k->owners, k->capowners * sizeof(HolOwnerRec),
                                                     (size_t)ncap * sizeof(HolOwnerRec));
        if (!no) return 0;
        k->owners = no;
        k->capowners = ncap;
      }
      index = k->nowners++;
      memset(&k->owners[index], 0, sizeof k->owners[index]);
    }
    k->owners[index].head = 0;
    k->owners[index].next_free = 0;
    k->owners[index].live = 1;
    return ((uint64_t)k->tag << 48) |
           ((uint64_t)k->owners[index].generation << 24) | index;
}

HolOwner hol_owner_set_current(HolKernel *k, HolOwner owner) {
    if (!k) return HOL_OWNER_KERNEL;
    hk_begin_call(k);
    HolOwner prev = k->cur_owner;
    uint32_t index;
    if (owner_index(k, owner, &index)) k->cur_owner = owner;
    return prev;
}

HolStatus hol_owner_release(HolKernel *k, HolOwner owner) {
    if (!k) return HOL_MALFORMED;
    hk_begin_call(k);
    if (owner == HOL_OWNER_KERNEL) {
        hk_fail(k, HOL_STALE, HOL_R_BAD_HANDLE);
        return k->status;
    }
    uint32_t index;
    if (!owner_index(k, owner, &index)) return k->status;
    HolOwnerRec *record = &k->owners[index];
    while (record->head) slot_release(k, record->head - 1);
    record->live = 0;
    if (record->generation < OWNER_MASK) {
        record->generation++;
        record->next_free = k->free_owner;
        k->free_owner = index;
    } /* Exhausted generations are retired, never wrapped. */
    if (k->cur_owner == owner) k->cur_owner = HOL_OWNER_KERNEL;
    return HOL_OK;
}

void hk_handles_free(HolKernel *k) {
    hk_dealloc(k, k->slots, k->capslots * sizeof(HolSlot));
    hk_dealloc(k, k->owners, k->capowners * sizeof(HolOwnerRec));
    k->slots = NULL;
    k->owners = NULL;
    k->nslots = k->capslots = k->nowners = k->capowners = k->free_owner = 0;
}
