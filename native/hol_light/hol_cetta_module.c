#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
/* Owned HOL values and checked operations for CeTTa. Kernel structures are
 * borrowed read-only during observation while their root references are held.
 * All input construction goes through the checked kernel API. */
#include "hol_cetta_module.h"
#include "hol_internal.h"
#include "hol_replay.h"
#include "hol_cli.h"
#include "seal.h"
#include <pthread.h>
#include <stdatomic.h>
#include "eval.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KIND_SESSION "hol-kernel"
#define KIND_THM "hol-thm"
#define KIND_ENV "hol-env"
#define KIND_TERM "hol-term"
#define KIND_TYPE "hol-type"

typedef struct {
    HolKernel *kernel;
    HolRef env;
    atomic_size_t refs;
    pthread_mutex_t gate;
    HolAxiomPolicy policy;
    uint64_t budget;
    char **labels;
    HolRef *theorems;
    size_t nlabels;
} Session;

typedef struct { atomic_size_t refs; Session *session; HolRef ref; } Held;
typedef struct {
    const void *key;
    unsigned kind;
    bool pending;
    HolRef ref;
    Atom *atom;
} MemoEntry;
typedef struct {
    Session *session;
    struct CettaLibraryContext *host;
    Arena *arena;
    HolStatus status;
    const char *reason;
    uint64_t remaining;
    MemoEntry *memo;
    size_t capacity, count;
} Call;

enum { READ_TYPE = 1, READ_TERM, WRITE_TYPE, WRITE_TERM };

static bool interrupted(void *unused) {
    (void)unused;
    return eval_native_interrupted();
}

static void labels_free(Session *s) {
    for (size_t i = 0; i < s->nlabels; ++i) {
        free(s->labels[i]);
        hol_ref_release(s->kernel, s->theorems[i]);
    }
    free(s->labels);
    free(s->theorems);
    s->labels = NULL;
    s->theorems = NULL;
    s->nlabels = 0;
}

/* The kernel brand and native payload kinds never enter MeTTa data. */
static CettaSealBrandCell g_brand = CETTA_SEAL_BRAND_CELL_INIT("hol");
static const char g_payload_kinds[5] = {0};

#ifdef CETTA_TEST_HOOKS
static atomic_size_t g_live_values;
#define TRACK_VALUE() ((void)atomic_fetch_add(&g_live_values, 1u))
#define UNTRACK_VALUE() ((void)atomic_fetch_sub(&g_live_values, 1u))
size_t cetta_hol_test_live_values(void) { return atomic_load(&g_live_values); }
#else
#define TRACK_VALUE() ((void)0)
#define UNTRACK_VALUE() ((void)0)
#endif

static const void *payload_kind(const char *kind) {
    const char *kinds[] = {KIND_SESSION, KIND_THM, KIND_ENV, KIND_TERM, KIND_TYPE};
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; ++i)
        if (!strcmp(kind, kinds[i])) return &g_payload_kinds[i];
    return NULL;
}
static void session_ref(void *value) {
    Session *s = value;
    if (atomic_fetch_add(&s->refs, 1u) == SIZE_MAX) abort();
}
static void session_unref(Session *s) {
    if (atomic_fetch_sub(&s->refs, 1u) != 1u) return;
    labels_free(s);
    hol_kernel_free(s->kernel);
    pthread_mutex_destroy(&s->gate);
    UNTRACK_VALUE();
    free(s);
}
static void free_session(void *p) { session_unref(p); }
static void held_ref(void *value) {
    Held *h = value;
    if (atomic_fetch_add(&h->refs, 1u) == SIZE_MAX) abort();
}
static void free_held(void *p) {
    Held *h = p;
    if (atomic_fetch_sub(&h->refs, 1u) != 1u) return;
    Session *s = h->session;
    pthread_mutex_lock(&s->gate);
    if (h->ref) { hol_ref_release(s->kernel, h->ref); UNTRACK_VALUE(); }
    pthread_mutex_unlock(&s->gate);
    free(h);
    session_unref(s);
}
static Atom *sealed_value(Arena *arena, const char *kind, void *value,
                          void (*retain)(void *), void (*release)(void *)) {
    CettaSealObject *brand = cetta_seal_brand_get(&g_brand, NULL);
    if (!brand) return NULL;
    CettaSealNativePayload payload = {payload_kind(kind), value, retain, release};
    CettaSealObject *box = NULL;
    if (cetta_seal_native(brand, &payload, &box) != CETTA_SEAL_OK) return NULL;
    Atom *atom = cetta_seal_object_atom(arena, box);
    cetta_seal_release(box);
    return atom;
}

static Atom *symbol(Arena *a, const char *name) { return atom_symbol(a, name); }
static Atom *expression(Arena *a, Atom **items, size_t n) {
    return atom_expr(a, items, (CettaExprLen)n);
}
static Atom *error_atom(Arena *a, HolStatus status, const char *reason) {
    Atom *items[3] = {symbol(a, status == HOL_REFUSED || status == HOL_POLICY || status == HOL_FOREIGN
                                  ? "hol:refused" : "hol:error"),
                      atom_string(a, hol_status_name(status)), atom_string(a, reason)};
    return expression(a, items, 3);
}
static bool fail(Call *c, HolStatus status, const char *reason) {
    if (c->status == HOL_OK) { c->status = status; c->reason = reason; }
    return false;
}
static bool check(Call *c, HolStatus status) {
    return status == HOL_OK || fail(c, status, hol_reason_name(hol_last_reason(c->session->kernel)));
}
static bool visit(Call *c) {
    if (c->status != HOL_OK) return false;
    if (!c->remaining) return fail(c, HOL_LIMIT, "term conversion budget");
    if (c->remaining != UINT64_MAX) --c->remaining;
    if (interrupted(NULL)) return fail(c, HOL_INTERRUPTED, "interrupted");
    return true;
}
static const char *name_arg(Atom *x) {
    if (!x) return NULL;
    if (x->kind == ATOM_SYMBOL) return atom_name_cstr(x);
    if (x->kind == ATOM_GROUNDED && x->ground.gkind == GV_STRING &&
        !memchr(x->ground.sval, 0, atom_string_len(x))) return x->ground.sval;
    return NULL;
}
static bool integer_arg(Atom *x, uint64_t *out) {
    if (!x || x->kind != ATOM_GROUNDED || x->ground.gkind != GV_INT || x->ground.ival < 0) return false;
    *out = (uint64_t)x->ground.ival;
    return true;
}
static bool policy_arg(Atom *x, HolAxiomPolicy *out) {
    const char *name = name_arg(x);
    if (!name) return false;
    if (!strcmp(name, "open")) *out = HOL_AXIOMS_OPEN;
    else if (!strcmp(name, "standard")) *out = HOL_AXIOMS_STANDARD;
    else if (!strcmp(name, "sealed")) *out = HOL_AXIOMS_SEALED;
    else return false;
    return true;
}
static void *handle_arg(struct CettaLibraryContext *host, Atom *x, const char *kind) {
    (void)host;
    CettaSealObject *brand = cetta_seal_brand_get(&g_brand, NULL);
    void *value = NULL;
    if (!brand || cetta_seal_unseal_native(brand, cetta_seal_atom_object(x),
                                            payload_kind(kind), &value) != CETTA_SEAL_OK)
        return NULL;
    return value;
}
#ifdef CETTA_TEST_HOOKS
bool cetta_hol_test_is_value(Atom *atom, const char *kind) {
    return handle_arg(NULL, atom, kind) != NULL;
}
#endif
static Held *held(Call *c, Atom *x, const char *kind) {
    Held *h = handle_arg(c->host, x, kind);
    if (!h) { fail(c, HOL_MALFORMED, "expected a live HOL handle"); return NULL; }
    if (h->session != c->session) { fail(c, HOL_FOREIGN, "other HOL session"); return NULL; }
    return h;
}

static size_t memo_index(const void *key, unsigned kind, size_t capacity) {
    return (size_t)hk_mix64((uint64_t)(uintptr_t)key ^ ((uint64_t)kind << 56)) & (capacity - 1);
}
static MemoEntry *memo_slot(Call *c, const void *key, unsigned kind) {
    size_t i = memo_index(key, kind, c->capacity);
    while (c->memo[i].key && (c->memo[i].key != key || c->memo[i].kind != kind))
        i = (i + 1) & (c->capacity - 1);
    return &c->memo[i];
}
static MemoEntry *memo(Call *c, const void *key, unsigned kind) {
    if (!c->capacity || (c->count + 1) * 2 >= c->capacity) {
        size_t capacity = c->capacity ? c->capacity * 2 : 64;
        if (capacity < c->capacity || capacity > SIZE_MAX / sizeof(MemoEntry)) {
            fail(c, HOL_LIMIT, "conversion table size"); return NULL;
        }
        MemoEntry *table = calloc(capacity, sizeof *table);
        if (!table) { fail(c, HOL_NOMEM, "conversion table allocation"); return NULL; }
        for (size_t i = 0; i < c->capacity; ++i) if (c->memo[i].key) {
            size_t j = memo_index(c->memo[i].key, c->memo[i].kind, capacity);
            while (table[j].key) j = (j + 1) & (capacity - 1);
            table[j] = c->memo[i];
        }
        free(c->memo); c->memo = table; c->capacity = capacity;
    }
    MemoEntry *entry = memo_slot(c, key, kind);
    if (!entry->key) { entry->key = key; entry->kind = kind; ++c->count; }
    return entry;
}

/* Conversion walks use explicit postorder frames. Their inline capacity is
 * allocation granularity, not a limit on the admitted syntax. */
static void *grow_frames(Call *c, void *frames, const void *local,
                         size_t *capacity, size_t length, size_t item_size) {
    if (length < *capacity) return frames;
    if (*capacity > SIZE_MAX / 2 / item_size) {
        fail(c, HOL_LIMIT, "conversion stack size"); return NULL;
    }
    size_t next = *capacity * 2;
    void *grown;
    if (frames == local) {
        grown = malloc(next * item_size);
        if (grown) memcpy(grown, local, length * item_size);
    } else grown = realloc(frames, next * item_size);
    if (!grown) { fail(c, HOL_NOMEM, "conversion stack allocation"); return NULL; }
    *capacity = next;
    return grown;
}

enum ReadOp { R_QUOTE, R_TYVAR, R_TYPE, R_VAR, R_CONST, R_COMB, R_ABS };
typedef struct {
    Atom *node;
    const char *name;
    unsigned kind;
    enum ReadOp op;
    size_t count, next;
    HolRef small[2], *wide;
} ReadFrame;

static Atom *read_child(const ReadFrame *f, bool *type) {
    *type = f->op == R_TYPE || f->op == R_VAR || f->op == R_CONST ||
            (f->op == R_QUOTE && f->kind == READ_TYPE);
    size_t index = f->op == R_TYPE ? f->next + 2 :
                   f->op == R_VAR || f->op == R_CONST ? 2 : f->next + 1;
    return f->node->expr.elems[index];
}
static bool read_finish(Call *c, ReadFrame *f, HolRef *out) {
    HolKernel *k = c->session->kernel;
    HolRef *args = f->wide ? f->wide : f->small;
    HolStatus status;
    switch (f->op) {
    case R_QUOTE: *out = args[0]; return true;
    case R_TYVAR: status = hol_mk_vartype(k, f->name, out); break;
    case R_TYPE: status = hol_mk_type(k, c->session->env, f->name, args, f->count, out); break;
    case R_VAR: status = hol_mk_var(k, f->name, args[0], out); break;
    case R_CONST: status = hol_mk_const_at(k, c->session->env, f->name, args[0], out); break;
    case R_COMB: status = hol_mk_comb(k, args[0], args[1], out); break;
    case R_ABS: status = hol_mk_abs(k, args[0], args[1], out); break;
    default: return fail(c, HOL_MALFORMED, "term constructor");
    }
    return check(c, status);
}
static bool read_value(Call *c, Atom *x, bool type, HolRef *out) {
    ReadFrame local[32], *frames = local;
    size_t capacity = sizeof local / sizeof local[0], length = 0;
    HolRef value = HOL_NULL;
    bool entering = true, valid = false;
    for (;;) {
        if (entering) {
            if (!x) { fail(c, HOL_MALFORMED, "missing term data"); break; }
            if (!visit(c)) break;
            unsigned kind = type ? READ_TYPE : READ_TERM;
            MemoEntry *entry = memo(c, x, kind);
            if (!entry) break;
            if (entry->pending) { fail(c, HOL_MALFORMED, "cyclic term data"); break; }
            if (entry->ref) value = entry->ref;
            else {
                entry->pending = true;
                Held *h = handle_arg(c->host, x, type ? KIND_TYPE : KIND_TERM);
                if (h) {
                    if (h->session != c->session) { fail(c, HOL_FOREIGN, "other HOL session"); break; }
                    if (!check(c, hol_ref_dup(c->session->kernel, h->ref, &value))) break;
                } else {
                    if (x->kind != ATOM_EXPR || x->expr.len < 2) {
                        fail(c, HOL_MALFORMED, "expected HOL term data"); break;
                    }
                    size_t n = x->expr.len;
                    Atom **it = x->expr.elems;
                    const char *tag = it[0] && it[0]->kind == ATOM_SYMBOL ? atom_name_cstr(it[0]) : NULL;
                    ReadFrame f = {.node = x, .kind = kind, .name = name_arg(it[1])};
                    /* Prime retains this explicit literal envelope as data. */
                    if (tag && !strcmp(tag, "quote") && n == 2) {
                        f.op = R_QUOTE; f.count = 1;
                    } else if (type && tag && !strcmp(tag, "V") && n == 2 && f.name) f.op = R_TYVAR;
                    else if (type && tag && !strcmp(tag, "T") && f.name) {
                        f.op = R_TYPE; f.count = n - 2;
                    } else if (!type && n == 3 && tag &&
                               (!strcmp(tag, "v") || !strcmp(tag, "c")) && f.name) {
                        f.op = tag[0] == 'v' ? R_VAR : R_CONST; f.count = 1;
                    } else if (!type && n == 3 && tag && (!strcmp(tag, "A") || !strcmp(tag, "L"))) {
                        f.op = tag[0] == 'A' ? R_COMB : R_ABS; f.count = 2;
                    } else { fail(c, HOL_MALFORMED, type ? "expected HOL type data" : "term constructor"); break; }
                    if (f.count > c->remaining || f.count > SIZE_MAX / sizeof(HolRef)) {
                        fail(c, HOL_LIMIT, "conversion child count"); break;
                    }
                    if (f.count) {
                        ReadFrame *grown = grow_frames(c, frames, local, &capacity, length, sizeof *frames);
                        if (!grown) break;
                        frames = grown;
                        if (f.count > sizeof f.small / sizeof f.small[0]) {
                            f.wide = calloc(f.count, sizeof *f.wide);
                            if (!f.wide) { fail(c, HOL_NOMEM, "type arguments"); break; }
                        }
                        frames[length++] = f;
                        x = read_child(&frames[length - 1], &type);
                        continue;
                    }
                    if (!read_finish(c, &f, &value)) break;
                }
                entry = memo_slot(c, x, kind);
                entry->pending = false; entry->ref = value;
            }
        }
        if (!length) { *out = value; valid = true; break; }
        ReadFrame *f = &frames[length - 1];
        (f->wide ? f->wide : f->small)[f->next++] = value;
        if (f->next < f->count) {
            x = read_child(f, &type); entering = true; continue;
        }
        if (!read_finish(c, f, &value)) break;
        MemoEntry *entry = memo_slot(c, f->node, f->kind);
        entry->pending = false; entry->ref = value;
        free(f->wide);
        --length;
        entering = false;
    }
    for (size_t i = 0; i < length; ++i) free(frames[i].wide);
    if (frames != local) free(frames);
    return valid;
}

typedef struct {
    const void *node;
    Atom *draft;
    size_t count, offset, next;
    unsigned kind;
} WriteFrame;

static const void *write_child(const WriteFrame *f, bool *type) {
    *type = f->kind == WRITE_TYPE;
    if (*type) return ((const HolType *)f->node)->args[f->next];
    const HolTerm *term = f->node;
    if (term->kind == HOL_TM_VAR || term->kind == HOL_TM_CONST) { *type = true; return term->type; }
    return f->next ? term->u.node.r : term->u.node.l;
}
static Atom *write_value(Call *c, const void *node, bool type) {
    WriteFrame local[32], *frames = local;
    size_t capacity = sizeof local / sizeof local[0], length = 0;
    Atom *value = NULL;
    bool entering = true, valid = false;
    for (;;) {
        if (entering) {
            if (!visit(c)) break;
            unsigned kind = type ? WRITE_TYPE : WRITE_TERM;
            MemoEntry *entry = memo(c, node, kind);
            if (!entry) break;
            if (entry->pending) { fail(c, HOL_MALFORMED, "cyclic kernel data"); break; }
            if (entry->atom) value = entry->atom;
            else {
                WriteFrame f = {.node = node, .kind = kind};
                const char *name = NULL, *tag;
                if (type) {
                    const HolType *t = node;
                    name = hk_name(c->session->kernel, t->name)->bytes;
                    tag = t->kind == HOL_TY_VAR ? "V" : "T";
                    f.offset = 2; f.count = t->kind == HOL_TY_VAR ? 0 : t->nargs;
                } else {
                    const HolTerm *t = node;
                    if (t->kind == HOL_TM_VAR || t->kind == HOL_TM_CONST) {
                        tag = t->kind == HOL_TM_VAR ? "v" : "c";
                        name = hk_name(c->session->kernel, t->u.leaf.name)->bytes;
                        f.offset = 2; f.count = 1;
                    } else {
                        tag = t->kind == HOL_TM_COMB ? "A" : "L";
                        f.offset = 1; f.count = 2;
                    }
                }
                if (f.count > c->remaining || f.count > UINT32_MAX - f.offset ||
                    f.count + f.offset > SIZE_MAX / sizeof(Atom *)) {
                    fail(c, HOL_LIMIT, "conversion output arity"); break;
                }
                entry->pending = true;
                f.draft = atom_expr_builder_begin(c->arena, (CettaExprLen)(f.count + f.offset));
                f.draft->expr.elems[0] = symbol(c->arena, tag);
                if (name) f.draft->expr.elems[1] = atom_string(c->arena, name);
                if (f.count) {
                    WriteFrame *grown = grow_frames(c, frames, local, &capacity, length, sizeof *frames);
                    if (!grown) break;
                    frames = grown;
                    frames[length++] = f;
                    node = write_child(&frames[length - 1], &type);
                    continue;
                }
                value = atom_expr_builder_finish(c->arena, f.draft);
                entry = memo_slot(c, node, kind);
                entry->pending = false; entry->atom = value;
            }
        }
        if (!length) { valid = true; break; }
        WriteFrame *f = &frames[length - 1];
        f->draft->expr.elems[f->offset + f->next++] = value;
        if (f->next < f->count) {
            node = write_child(f, &type); entering = true; continue;
        }
        value = atom_expr_builder_finish(c->arena, f->draft);
        MemoEntry *entry = memo_slot(c, f->node, f->kind);
        entry->pending = false; entry->atom = value;
        --length;
        entering = false;
    }
    if (frames != local) free(frames);
    return valid ? value : NULL;
}

static Atom *write_type(Call *c, const HolType *type) { return write_value(c, type, true); }
static Atom *write_term(Call *c, const HolTerm *term) { return write_value(c, term, false); }
static Atom *value_data(Call *c, HolRef ref, bool type) {
    void *object = hk_ref_get(c->session->kernel, ref, type ? HOL_KIND_TYPE : HOL_KIND_TERM, NULL);
    if (!object) { check(c, hol_last_status(c->session->kernel)); return NULL; }
    return type ? write_type(c, object) : write_term(c, object);
}

/* Publication succeeds before any change to the session's current environment. */
static Atom *publish(Call *c, const char *kind, HolRef ref) {
    Session *s = c->session;
    Held *h = calloc(1, sizeof *h);
    if (!h) { fail(c, HOL_NOMEM, "HOL value allocation"); return NULL; }
    atomic_init(&h->refs, 1u);
    h->session = s; session_ref(s);
    if (!check(c, hol_ref_move(s->kernel, ref, HOL_OWNER_KERNEL))) {
        free_held(h); return NULL;
    }
    h->ref = ref; TRACK_VALUE();
    Atom *out = sealed_value(c->arena, kind, h, held_ref, free_held);
    free_held(h); /* the capsule now owns the retained resource */
    if (!out) fail(c, HOL_NOMEM, "sealed HOL value");
    return out;
}
static void abandon_publication(Call *c, Atom *value, const char *kind) {
    Held *h = handle_arg(c->host, value, kind);
    if (h && h->ref) {
        hol_ref_release(h->session->kernel, h->ref);
        h->ref = HOL_NULL;
        UNTRACK_VALUE();
    }
}

static bool set_env(Call *c, HolRef env) {
    Session *s = c->session;
    if (!check(c, hol_ref_move(s->kernel, env, HOL_OWNER_KERNEL))) return false;
    hol_ref_release(s->kernel, s->env); s->env = env;
    return true;
}
static Atom *new_session(struct CettaLibraryContext *host, Arena *arena,
                         HolAxiomPolicy policy, bool recording) {
    Session *s = calloc(1, sizeof *s);
    if (!s) return error_atom(arena, HOL_NOMEM, "session allocation");
    atomic_init(&s->refs, 1u);
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr)) { free(s); return error_atom(arena, HOL_NOMEM, "session lock"); }
    int locked = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    if (!locked) locked = pthread_mutex_init(&s->gate, &attr);
    pthread_mutexattr_destroy(&attr);
    if (locked) { free(s); return error_atom(arena, HOL_NOMEM, "session lock"); }
    TRACK_VALUE();
    s->policy = policy; s->budget = UINT64_MAX;
    s->kernel = hol_kernel_new(NULL);
    if (!s->kernel) { session_unref(s); return error_atom(arena, HOL_NOMEM, "kernel allocation"); }
    HolRef bare = HOL_NULL;
    HolStatus status = hol_env_bare(s->kernel, &bare);
    if (status == HOL_OK) status = hol_env_set_axiom_policy(s->kernel, bare, policy, &s->env);
    if (bare) hol_ref_release(s->kernel, bare);
    if (status != HOL_OK) { session_unref(s); return error_atom(arena, status, "initial environment"); }
    hol_set_fuel(s->kernel, 0);
    hol_set_interrupt_poll(s->kernel, interrupted, NULL);
    hol_journal_enable(s->kernel, recording);
    (void)host;
    Atom *result = sealed_value(arena, KIND_SESSION, s, session_ref, free_session);
    session_unref(s);
    return result ? result : error_atom(arena, HOL_NOMEM, "sealed HOL session");
}

static Atom *replay_file(Call *c, const char *path, HolAxiomPolicy policy) {
    Session *s = c->session;
    if (policy < s->policy) { fail(c, HOL_POLICY, "session policy cannot loosen"); return NULL; }
    FILE *file = fopen(path, "rb");
    if (!file) { fail(c, HOL_MALFORMED, "cannot open HOL replay"); return NULL; }
    HolStatus read_status;
    HolReplay *replay = cetta_hol_read_stream(s->kernel, file, policy, &read_status);
    fclose(file);
    if (read_status != HOL_OK) fail(c, read_status, "replay input or interruption");
    if (!replay) return NULL;
    HolReplayState state = hol_replay_state(replay);
    HolReplayInfo checked; hol_replay_info(replay, &checked);
    if (state == HOL_REPLAY_ACCEPTED && checked.failed)
        fail(c, HOL_REFUSED, "probe stream contains refused records");
    Atom *result = NULL;
    Session pending = {.kernel = s->kernel};
    HolRef env = HOL_NULL;
    HolAxiomPolicy reached = policy;
    if (c->status == HOL_OK && state != HOL_REPLAY_ACCEPTED)
        fail(c, hol_replay_status(replay), hol_reason_name(hol_replay_reason(replay)));
    if (c->status == HOL_OK && check(c, hol_replay_env(replay, &env)) &&
        check(c, hol_env_policy(s->kernel, env, &reached))) {
        size_t count = hol_replay_nlabels(replay);
        pending.labels = calloc(count ? count : 1, sizeof *pending.labels);
        pending.theorems = calloc(count ? count : 1, sizeof *pending.theorems);
        if (!pending.labels || !pending.theorems) fail(c, HOL_NOMEM, "replay labels");
        for (size_t i = 0; c->status == HOL_OK && i < count; ++i) {
            uint64_t theorem;
            const char *name;
            size_t length;
            if (!hol_replay_label(replay, i, &theorem, &name, &length) || memchr(name, 0, length)) {
                fail(c, HOL_MALFORMED, "replay label"); break;
            }
            pending.labels[i] = malloc(length + 1);
            if (!pending.labels[i]) { fail(c, HOL_NOMEM, "replay label name"); break; }
            memcpy(pending.labels[i], name, length); pending.labels[i][length] = 0;
            pending.nlabels = i + 1;
            if (!check(c, hol_replay_thm(replay, theorem, &pending.theorems[i]))) break;
        }
        if (c->status == HOL_OK) {
            for (size_t i = 0; i < count; ++i)
                if (!check(c, hol_ref_move(s->kernel, pending.theorems[i], HOL_OWNER_KERNEL))) break;
        }
        if (c->status == HOL_OK && set_env(c, env)) {
            labels_free(s);
            s->labels = pending.labels; s->theorems = pending.theorems; s->nlabels = pending.nlabels;
            memset(&pending, 0, sizeof pending);
            s->policy = reached;
            HolReplayInfo info; hol_replay_info(replay, &info);
            Atom *items[] = {symbol(c->arena, "hol:accepted"), atom_int(c->arena, (int64_t)info.records),
                             atom_int(c->arena, (int64_t)count)};
            result = expression(c->arena, items, 3);
        }
    }
    if (pending.kernel) labels_free(&pending);
    hol_replay_free(replay);
    return result;
}

static bool pairs(Call *c, Atom *x, bool type, HolRef **out, size_t *count) {
    if (!x || x->kind != ATOM_EXPR) return fail(c, HOL_MALFORMED, "substitution pairs");
    size_t n = x->expr.len;
    if (n > c->remaining || n > (SIZE_MAX / sizeof(HolRef) - 1) / 2) return fail(c, HOL_LIMIT, "substitution size");
    HolRef *refs = calloc(n * 2 + 1, sizeof *refs);
    if (!refs) return fail(c, HOL_NOMEM, "substitution allocation");
    for (size_t i = 0; i < n; ++i) {
        Atom *pair = x->expr.elems[i];
        if (!pair || pair->kind != ATOM_EXPR || pair->expr.len != 2) {
            free(refs); return fail(c, HOL_MALFORMED, "substitution pair");
        }
        if (!read_value(c, pair->expr.elems[0], type, &refs[i * 2]) ||
            !read_value(c, pair->expr.elems[1], type, &refs[i * 2 + 1])) { free(refs); return false; }
    }
    *out = refs; *count = n; return true;
}

/* The source rows remain borrowed for this synchronous call. Only checked
 * references and the exact source-ID index are retained during traversal;
 * caller-supplied sequents are claims, never theorem capabilities. */
typedef struct { uint64_t id; HolRef ref; } DagEntry;
typedef struct { DagEntry *entries; size_t capacity, count; } DagIndex;

static DagEntry *dag_slot(DagIndex *index, uint64_t id) {
    size_t i = (size_t)hk_mix64(id) & (index->capacity - 1);
    while (index->entries[i].ref && index->entries[i].id != id)
        i = (i + 1) & (index->capacity - 1);
    return &index->entries[i];
}
static bool dag_reserve(Call *c, DagIndex *index) {
    if (index->capacity && index->count + 1 < index->capacity / 2) return true;
    size_t capacity = index->capacity ? index->capacity * 2 : 64;
    if (capacity < index->capacity || capacity > SIZE_MAX / sizeof(DagEntry))
        return fail(c, HOL_LIMIT, "proof index size");
    DagIndex next = {.capacity = capacity, .entries = calloc(capacity, sizeof(DagEntry))};
    if (!next.entries) return fail(c, HOL_NOMEM, "proof index allocation");
    for (size_t i = 0; i < index->capacity; ++i) if (index->entries[i].ref)
        *dag_slot(&next, index->entries[i].id) = index->entries[i];
    free(index->entries);
    index->entries = next.entries; index->capacity = capacity;
    return true;
}

typedef enum {
    DAG_REFL, DAG_TRANS, DAG_MK_COMB, DAG_ABS, DAG_BETA, DAG_ASSUME,
    DAG_EQ_MP, DAG_DEDUCT_ANTISYM, DAG_INST_TYPE, DAG_INST
} DagRule;
static bool dag_infer(Call *c, Atom *request, Atom *parents, DagIndex *index, HolRef *out) {
    static const struct { const char *name; DagRule rule; unsigned args, parents; } rules[] = {
        {"REFL", DAG_REFL, 1, 0}, {"TRANS", DAG_TRANS, 0, 2},
        {"MK_COMB", DAG_MK_COMB, 0, 2}, {"ABS", DAG_ABS, 1, 1},
        {"BETA", DAG_BETA, 1, 0}, {"ASSUME", DAG_ASSUME, 1, 0},
        {"EQ_MP", DAG_EQ_MP, 0, 2}, {"DEDUCT_ANTISYM_RULE", DAG_DEDUCT_ANTISYM, 0, 2},
        {"INST_TYPE", DAG_INST_TYPE, 1, 1}, {"INST", DAG_INST, 1, 1}
    };
    if (!request || request->kind != ATOM_EXPR || !request->expr.len ||
        !parents || parents->kind != ATOM_EXPR)
        return fail(c, HOL_MALFORMED, "proof request or parents");
    size_t rule;
    for (rule = 0; rule < sizeof rules / sizeof rules[0]; ++rule)
        if (atom_is_symbol(request->expr.elems[0], rules[rule].name)) break;
    if (rule == sizeof rules / sizeof rules[0]) return fail(c, HOL_MALFORMED, "proof rule");
    if (request->expr.len != rules[rule].args + 1 || parents->expr.len != rules[rule].parents)
        return fail(c, HOL_MALFORMED, "proof rule arity");
    HolRef premise[2] = {HOL_NULL, HOL_NULL};
    for (unsigned i = 0; i < rules[rule].parents; ++i) {
        uint64_t id;
        if (!visit(c) || !integer_arg(parents->expr.elems[i], &id))
            return fail(c, HOL_MALFORMED, "proof parent ID");
        premise[i] = dag_slot(index, id)->ref;
        if (!premise[i]) return fail(c, HOL_REFUSED, "proof parent not checked earlier");
    }
    HolKernel *k = c->session->kernel;
    HolRef argument = HOL_NULL;
    if (rules[rule].args && rules[rule].rule != DAG_INST_TYPE && rules[rule].rule != DAG_INST &&
        !read_value(c, request->expr.elems[1], false, &argument)) return false;
    HolStatus status;
    switch (rules[rule].rule) {
    case DAG_REFL: status = hol_REFL(k, argument, out); break;
    case DAG_TRANS: status = hol_TRANS(k, premise[0], premise[1], out); break;
    case DAG_MK_COMB: status = hol_MK_COMB(k, premise[0], premise[1], out); break;
    case DAG_ABS: status = hol_ABS(k, argument, premise[0], out); break;
    case DAG_BETA: status = hol_BETA(k, argument, out); break;
    case DAG_ASSUME: status = hol_ASSUME(k, argument, out); break;
    case DAG_EQ_MP: status = hol_EQ_MP(k, premise[0], premise[1], out); break;
    case DAG_DEDUCT_ANTISYM: status = hol_DEDUCT_ANTISYM_RULE(k, premise[0], premise[1], out); break;
    case DAG_INST_TYPE: case DAG_INST: {
        HolRef *substitution; size_t count;
        bool types = rules[rule].rule == DAG_INST_TYPE;
        if (!pairs(c, request->expr.elems[1], types, &substitution, &count)) return false;
        status = types ? hol_INST_TYPE(k, substitution, count, premise[0], out)
                       : hol_INST(k, substitution, count, premise[0], out);
        free(substitution);
        break;
    }
    default: return fail(c, HOL_MALFORMED, "proof rule");
    }
    return check(c, status);
}

static bool dag_claim_term(Call *c, HolRef actual, Atom *claimed) {
    HolRef term; bool equal = false;
    return read_value(c, claimed, false, &term) &&
           check(c, hol_aconv(c->session->kernel, actual, term, &equal)) &&
           (equal || fail(c, HOL_REFUSED, "proof sequent differs from checked inference"));
}

/* HOL hypotheses are sets modulo alpha-equivalence. Ordered producers use
 * the direct comparison; only a mismatch needs the membership index. */
static bool dag_claim_hypotheses(Call *c, HolRef ref, Atom *hyps, size_t n) {
    HolKernel *k = c->session->kernel;
    size_t m = hyps->expr.len;
    if (m < n || (!n && m)) return fail(c, HOL_REFUSED, "proof hypothesis set");
    bool ordered = m == n;
    for (size_t i = 0; ordered && i < n; ++i) {
        HolRef actual, claimed; bool equal = false;
        if (!check(c, hol_thm_hyp(k, ref, i, &actual))) return false;
        bool valid = read_value(c, hyps->expr.elems[i], false, &claimed) &&
                     check(c, hol_aconv(k, actual, claimed, &equal));
        hol_ref_release(k, actual);
        if (!valid) return false;
        ordered = equal;
    }
    if (ordered) return true;
    if (n > SIZE_MAX / sizeof(HolRef)) return fail(c, HOL_LIMIT, "proof hypothesis index");
    HolRef *actual = calloc(n, sizeof(*actual));
    bool *seen = calloc(n, sizeof(*seen));
    if (!actual || !seen) { free(actual); free(seen); return fail(c, HOL_NOMEM, "proof hypothesis index"); }
    bool valid = true;
    size_t found = 0;
    for (size_t i = 0; valid && i < n; ++i) valid = check(c, hol_thm_hyp(k, ref, i, &actual[i]));
    for (size_t i = 0; valid && i < m; ++i) {
        HolRef claimed;
        valid = read_value(c, hyps->expr.elems[i], false, &claimed);
        size_t lo = 0, hi = n;
        bool matched = false;
        while (valid && lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            int order;
            valid = check(c, hol_alphaorder(k, actual[mid], claimed, &order));
            if (!valid) break;
            if (!order) {
                if (!seen[mid]) { seen[mid] = true; ++found; }
                matched = true;
                break;
            }
            if (order < 0) lo = mid + 1;
            else hi = mid;
        }
        if (valid && !matched) valid = fail(c, HOL_REFUSED, "proof hypothesis set");
    }
    for (size_t i = 0; i < n; ++i) if (actual[i]) hol_ref_release(k, actual[i]);
    free(actual); free(seen);
    return valid && (found == n || fail(c, HOL_REFUSED, "proof hypothesis set"));
}

static bool dag_claim(Call *c, HolRef ref, Atom *claim) {
    if (!claim || claim->kind != ATOM_EXPR || claim->expr.len != 3 ||
        !atom_is_symbol(claim->expr.elems[0], "thm") ||
        !claim->expr.elems[1] || claim->expr.elems[1]->kind != ATOM_EXPR)
        return fail(c, HOL_MALFORMED, "proof sequent claim");
    HolKernel *k = c->session->kernel;
    Atom *hyps = claim->expr.elems[1];
    size_t n;
    if (!check(c, hol_thm_nhyps(k, ref, &n))) return false;
    if (hyps->expr.len > c->remaining) return fail(c, HOL_LIMIT, "proof hypothesis budget");
    HolRef conclusion;
    if (!check(c, hol_thm_concl(k, ref, &conclusion))) return false;
    bool valid = dag_claim_term(c, conclusion, claim->expr.elems[2]);
    hol_ref_release(k, conclusion);
    return valid && dag_claim_hypotheses(c, ref, hyps, n);
}

static Atom *check_dag(Call *c, Atom *rows, Atom *root_arg) {
    uint64_t root;
    if (rows && rows->kind == ATOM_EXPR && rows->expr.len == 2 &&
        atom_is_symbol(rows->expr.elems[0], "quote")) rows = rows->expr.elems[1];
    if (!rows || rows->kind != ATOM_EXPR || !integer_arg(root_arg, &root)) {
        fail(c, HOL_MALFORMED, "proof rows or root ID"); return NULL;
    }
    if (rows->expr.len > c->remaining) { fail(c, HOL_LIMIT, "proof row budget"); return NULL; }
    DagIndex index = {0};
    Atom *result = NULL;
    for (size_t i = 0; i < rows->expr.len; ++i) {
        if (!visit(c) || !dag_reserve(c, &index)) goto done;
        Atom *row = rows->expr.elems[i]; uint64_t id;
        if (!row || row->kind != ATOM_EXPR || (row->expr.len != 4 && row->expr.len != 5) ||
            !atom_is_symbol(row->expr.elems[0], "HolNode") || !integer_arg(row->expr.elems[1], &id)) {
            fail(c, HOL_MALFORMED, "proof node"); goto done;
        }
        if (dag_slot(&index, id)->ref) { fail(c, HOL_REFUSED, "duplicate proof node ID"); goto done; }
        HolRef ref;
        if (!dag_infer(c, row->expr.elems[2], row->expr.elems[3], &index, &ref) ||
            (row->expr.len == 5 && !dag_claim(c, ref, row->expr.elems[4]))) goto done;
        *dag_slot(&index, id) = (DagEntry){id, ref};
        ++index.count;
    }
    if (!index.count || !dag_slot(&index, root)->ref) fail(c, HOL_REFUSED, "proof root not checked");
    else result = publish(c, KIND_THM, dag_slot(&index, root)->ref);
done:
    free(index.entries);
    return result;
}

typedef HolStatus (*BinaryRule)(HolKernel *, HolRef, HolRef, HolRef *);
static BinaryRule binary_rule(const char *op) {
    if (!strcmp(op, "trans")) return hol_TRANS;
    if (!strcmp(op, "mk-comb")) return hol_MK_COMB;
    if (!strcmp(op, "eq-mp")) return hol_EQ_MP;
    if (!strcmp(op, "deduct-antisym")) return hol_DEDUCT_ANTISYM_RULE;
    return NULL;
}
static Atom *theorem_call(Call *c, const char *op, Held *h, Atom **args, uint32_t n) {
    Session *s = c->session; HolKernel *k = s->kernel;
    HolRef r = HOL_NULL;
    if (n == 1 && (!strcmp(op, "sequent") || !strcmp(op, "axioms"))) {
        if (!strcmp(op, "sequent")) {
            const HolThm *th = hk_ref_get(k, h->ref, HOL_KIND_THM, NULL);
            if (!th) { check(c, hol_last_status(k)); return NULL; }
            if (th->nhyps > c->remaining) { fail(c, HOL_LIMIT, "hypotheses"); return NULL; }
            Atom **hyps = arena_alloc(c->arena, (th->nhyps + 1u) * sizeof *hyps);
            for (uint32_t i = 0; i < th->nhyps; ++i) if (!(hyps[i] = write_term(c, th->hyps[i]))) return NULL;
            Atom *conclusion = write_term(c, th->concl);
            if (!conclusion) return NULL;
            Atom *items[] = {symbol(c->arena, "thm"), expression(c->arena, hyps, th->nhyps), conclusion};
            return expression(c->arena, items, 3);
        }
        size_t count = 0;
        if (!check(c, hol_thm_axioms(k, h->ref, NULL, 0, &count))) return NULL;
        if (count > c->remaining) { fail(c, HOL_LIMIT, "axiom count"); return NULL; }
        HolRef *refs = calloc(count ? count : 1, sizeof *refs);
        if (!refs) { fail(c, HOL_NOMEM, "axiom references"); return NULL; }
        bool valid = check(c, hol_thm_axioms(k, h->ref, refs, count, &count));
        Atom **items = arena_alloc(c->arena, (count + 1) * sizeof *items);
        for (size_t i = 0; valid && i < count; ++i) {
            valid = check(c, hol_thm_concl(k, refs[i], &r));
            if (valid) valid = (items[i] = value_data(c, r, false)) != NULL;
        }
        free(refs);
        return valid ? expression(c->arena, items, count) : NULL;
    }
    if (!strcmp(op, "conclusion") && n == 1)
        return check(c, hol_thm_concl(k, h->ref, &r)) ? publish(c, KIND_TERM, r) : NULL;
    if (!strcmp(op, "standard") && n == 1) {
        bool yes;
        return check(c, hol_thm_standard(k, h->ref, &yes)) ? atom_bool(c->arena, yes) : NULL;
    }
    if (!strcmp(op, "event") && n == 1) {
        uint64_t event;
        return check(c, hol_thm_event(k, h->ref, &event)) ? atom_int(c->arena, (int64_t)event) : NULL;
    }
    if (!strcmp(op, "text") && n == 1) {
        size_t size;
        if (!check(c, hol_print(k, h->ref, NULL, 0, &size))) return NULL;
        if (size == SIZE_MAX) { fail(c, HOL_LIMIT, "theorem text"); return NULL; }
        char *text = arena_alloc(c->arena, size + 1);
        return check(c, hol_print(k, h->ref, text, size + 1, &size)) ? atom_string_n(c->arena, text, size) : NULL;
    }
    if (n == 2 && (binary_rule(op) || !strcmp(op, "same-payload"))) {
        Held *other = held(c, args[1], KIND_THM);
        if (!other) return NULL;
        if (!strcmp(op, "same-payload")) {
            bool yes;
            return check(c, hol_thm_same_payload(k, h->ref, other->ref, &yes)) ? atom_bool(c->arena, yes) : NULL;
        }
        return check(c, binary_rule(op)(k, h->ref, other->ref, &r)) ? publish(c, KIND_THM, r) : NULL;
    }
    if (n == 2 && (!strcmp(op, "inst") || !strcmp(op, "inst-type"))) {
        bool types = !strcmp(op, "inst-type"); HolRef *refs; size_t count;
        if (!pairs(c, args[1], types, &refs, &count)) return NULL;
        bool valid = check(c, types ? hol_INST_TYPE(k, refs, count, h->ref, &r) : hol_INST(k, refs, count, h->ref, &r));
        free(refs);
        return valid ? publish(c, KIND_THM, r) : NULL;
    }
    fail(c, HOL_MALFORMED, "theorem operation or arity"); return NULL;
}

static Atom *session_call(Call *c, const char *op, Atom **args, uint32_t n) {
    Session *s = c->session; HolKernel *k = s->kernel;
    HolRef x = HOL_NULL, y = HOL_NULL, r = HOL_NULL, env = HOL_NULL;
    if (!strcmp(op, "check-dag") && n == 3) return check_dag(c, args[1], args[2]);
    if (!strcmp(op, "stats") && n == 1) {
        HolStats stats;
        hol_stats(k, &stats);
        Atom *items[] = {symbol(c->arena, "HolStats"),
            atom_int(c->arena, (int64_t)stats.bytes_live),
            atom_int(c->arena, (int64_t)stats.journal_bytes),
            atom_int(c->arena, (int64_t)stats.journal_events),
            atom_int(c->arena, (int64_t)stats.terms),
            atom_int(c->arena, (int64_t)stats.thms),
            atom_int(c->arena, (int64_t)stats.refs)};
        return expression(c->arena, items, sizeof items / sizeof items[0]);
    }
    if (!strcmp(op, "budget") && n == 2) {
        uint64_t budget;
        if (args[1] && args[1]->kind == ATOM_GROUNDED && args[1]->ground.gkind == GV_INT &&
            args[1]->ground.ival == -1) budget = UINT64_MAX;
        else if (!integer_arg(args[1], &budget) || !budget) {
            fail(c, HOL_MALFORMED, "positive budget or -1 required"); return NULL;
        }
        s->budget = budget;
        hol_set_fuel(k, budget == UINT64_MAX ? 0 : budget);
        return symbol(c->arena, "hol:ok");
    }
    if (!strcmp(op, "replay") && (n == 2 || n == 3)) {
        HolAxiomPolicy policy = s->policy; const char *path = name_arg(args[1]);
        if (!path || (n == 3 && !policy_arg(args[2], &policy))) { fail(c, HOL_MALFORMED, "replay arguments"); return NULL; }
        return replay_file(c, path, policy);
    }
    if (!strcmp(op, "theorem") && n == 2) {
        const char *name = name_arg(args[1]);
        for (size_t i = name ? s->nlabels : 0; i-- > 0;)
            if (!strcmp(name, s->labels[i]))
                return check(c, hol_ref_dup(k, s->theorems[i], &r)) ? publish(c, KIND_THM, r) : NULL;
        fail(c, HOL_MALFORMED, "unknown theorem label"); return NULL;
    }
    if (!strcmp(op, "checkpoint") && n == 1)
        return check(c, hol_ref_dup(k, s->env, &r)) ? publish(c, KIND_ENV, r) : NULL;
    if (!strcmp(op, "restore") && n == 2) {
        Held *h = held(c, args[1], KIND_ENV); if (!h) return NULL;
        HolAxiomPolicy old;
        if (!check(c, hol_env_policy(k, h->ref, &old)) ||
            !check(c, hol_env_set_axiom_policy(k, h->ref, old > s->policy ? old : s->policy, &env))) return NULL;
        if (!set_env(c, env)) return NULL;
        if (old > s->policy) s->policy = old;
        return symbol(c->arena, "hol:ok");
    }
    if (!strcmp(op, "admits") && n == 2) {
        Held *h = held(c, args[1], KIND_THM); if (!h) return NULL;
        bool yes;
        return check(c, hol_env_admits(k, s->env, h->ref, &yes)) ? atom_bool(c->arena, yes) : NULL;
    }
    if ((!strcmp(op, "term") || !strcmp(op, "type")) && n == 2) {
        bool type = !strcmp(op, "type");
        return read_value(c, args[1], type, &r) ? publish(c, type ? KIND_TYPE : KIND_TERM, r) : NULL;
    }
    if (!strcmp(op, "aconv") && n == 3) {
        bool yes;
        return read_value(c, args[1], false, &x) && read_value(c, args[2], false, &y) &&
               check(c, hol_aconv(k, x, y, &yes)) ? atom_bool(c->arena, yes) : NULL;
    }
    if (!strcmp(op, "type-of") && n == 2)
        return read_value(c, args[1], false, &x) && check(c, hol_type_of(k, x, &r)) ? value_data(c, r, true) : NULL;
    if (!strcmp(op, "policy") && n == 2) {
        HolAxiomPolicy policy;
        if (!policy_arg(args[1], &policy)) { fail(c, HOL_MALFORMED, "axiom policy"); return NULL; }
        if (policy < s->policy) { fail(c, HOL_POLICY, "session policy cannot loosen"); return NULL; }
        if (!check(c, hol_env_set_axiom_policy(k, s->env, policy, &env)) || !set_env(c, env)) return NULL;
        s->policy = policy; return symbol(c->arena, "hol:ok");
    }
    if (!strcmp(op, "new-type") && n == 3) {
        const char *name = name_arg(args[1]); uint64_t arity;
        if (!name || !integer_arg(args[2], &arity) || arity > UINT32_MAX) { fail(c, HOL_MALFORMED, "type declaration"); return NULL; }
        if (!check(c, hol_new_type(k, s->env, name, (uint32_t)arity, &env)) || !set_env(c, env)) return NULL;
        return symbol(c->arena, "hol:ok");
    }
    if (!strcmp(op, "new-constant") && n == 3) {
        const char *name = name_arg(args[1]);
        if (!name) { fail(c, HOL_MALFORMED, "constant name"); return NULL; }
        if (!read_value(c, args[2], true, &x) || !check(c, hol_new_constant(k, s->env, name, x, &env)) || !set_env(c, env)) return NULL;
        return symbol(c->arena, "hol:ok");
    }
    if (!strcmp(op, "new-type-definition") && n == 5) {
        const char *name = name_arg(args[1]), *abs = name_arg(args[2]), *rep = name_arg(args[3]);
        Held *witness = held(c, args[4], KIND_THM);
        if (!name || !abs || !rep || !witness) { fail(c, HOL_MALFORMED, "type definition"); return NULL; }
        if (!check(c, hol_new_basic_type_definition(k, s->env, name, abs, rep, witness->ref, &env, &x, &y))) return NULL;
        Atom *items[2];
        items[0] = publish(c, KIND_THM, x);
        if (!items[0]) return NULL;
        items[1] = publish(c, KIND_THM, y);
        if (!items[1] || !set_env(c, env)) {
            abandon_publication(c, items[0], KIND_THM);
            abandon_publication(c, items[1], KIND_THM);
            return NULL;
        }
        return expression(c->arena, items, 2);
    }
    if (!strcmp(op, "abs") && n == 3) {
        Held *h = held(c, args[2], KIND_THM);
        return h && read_value(c, args[1], false, &x) && check(c, hol_ABS(k, x, h->ref, &r)) ? publish(c, KIND_THM, r) : NULL;
    }
    if (n == 2 && (!strcmp(op, "refl") || !strcmp(op, "assume") || !strcmp(op, "beta") ||
                   !strcmp(op, "new-axiom") || !strcmp(op, "new-definition"))) {
        if (!read_value(c, args[1], false, &x)) return NULL;
        HolStatus status = !strcmp(op, "refl") ? hol_REFL(k, x, &r) :
            !strcmp(op, "assume") ? hol_ASSUME(k, x, &r) :
            !strcmp(op, "beta") ? hol_BETA(k, x, &r) :
            !strcmp(op, "new-axiom") ? hol_new_axiom(k, s->env, x, &env, &r) :
            hol_new_basic_definition(k, s->env, x, &env, &r);
        if (!check(c, status)) return NULL;
        Atom *result = publish(c, KIND_THM, r);
        if (!result) return NULL;
        if (env && !set_env(c, env)) {
            abandon_publication(c, result, KIND_THM);
            return NULL;
        }
        return result;
    }
    fail(c, HOL_MALFORMED, "session operation or arity"); return NULL;
}

Atom *cetta_hol_dispatch(struct CettaLibraryContext *host, Space *space, Arena *arena,
                         Atom *head, Atom **args, uint32_t n) {
    (void)space;
    static const char prefix[] = "__cetta_lib_hol_";
    if (!head || head->kind != ATOM_SYMBOL || strncmp(atom_name_cstr(head), prefix, sizeof prefix - 1)) return NULL;
    const char *op = atom_name_cstr(head) + sizeof prefix - 1;
    if (!strcmp(op, "kernel") && n <= 2) {
        HolAxiomPolicy policy = HOL_AXIOMS_STANDARD;
        if (n && !policy_arg(args[0], &policy)) return error_atom(arena, HOL_MALFORMED, "axiom policy");
        const char *mode = n == 2 ? name_arg(args[1]) : "check";
        if (!mode || (strcmp(mode, "check") && strcmp(mode, "record")))
            return error_atom(arena, HOL_MALFORMED, "expected check or record mode");
        return new_session(host, arena, policy, !strcmp(mode, "record"));
    }
    Session *session = n ? handle_arg(host, args[0], KIND_SESSION) : NULL;
    Held *h = NULL; const char *kind = NULL;
    const char *kinds[] = {KIND_THM, KIND_TERM, KIND_TYPE};
    for (size_t i = 0; !session && i < sizeof kinds / sizeof kinds[0]; ++i) {
        h = n ? handle_arg(host, args[0], kinds[i]) : NULL;
        if (h) { session = h->session; kind = kinds[i]; }
    }
    if (!session) return error_atom(arena, HOL_MALFORMED, "expected a live HOL session or value");
    if (interrupted(NULL)) return error_atom(arena, HOL_INTERRUPTED, "interrupted");
    pthread_mutex_lock(&session->gate);
    Call c = {.session = session, .host = host, .arena = arena, .remaining = session->budget};
    HolOwner owner = hol_owner_new(session->kernel);
    if (!owner) {
        Atom *error = error_atom(arena, hol_last_status(session->kernel), hol_reason_name(hol_last_reason(session->kernel)));
        pthread_mutex_unlock(&session->gate);
        return error;
    }
    HolOwner previous = hol_owner_set_current(session->kernel, owner);
    Atom *result = NULL;
    if (!h) result = session_call(&c, op, args, n);
    else if (!strcmp(kind, KIND_THM)) result = theorem_call(&c, op, h, args, n);
    else if (n == 1 && ((!strcmp(kind, KIND_TERM) && !strcmp(op, "term-data")) ||
                        (!strcmp(kind, KIND_TYPE) && !strcmp(op, "type-data"))))
        result = value_data(&c, h->ref, !strcmp(kind, KIND_TYPE));
    else fail(&c, HOL_MALFORMED, "value operation or arity");
    hol_owner_set_current(session->kernel, previous);
    hol_owner_release(session->kernel, owner);
    free(c.memo);
    pthread_mutex_unlock(&session->gate);
    if (c.status != HOL_OK) return error_atom(arena, c.status, c.reason);
    return result ? result : error_atom(arena, HOL_MALFORMED, "missing result");
}
