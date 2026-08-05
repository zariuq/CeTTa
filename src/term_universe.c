#include "term_universe.h"
#include "match.h"
#include "stats.h"
#include "term_canon.h"

#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CETTA_TERM_ENTRY_ALIGN 8u
#define CETTA_TERM_HDR_HAS_VARS 0x80000000u
#define CETTA_TERM_HDR_DATA_MASK 0x7fffffffu
#define CETTA_TERM_UNIVERSE_COPY_MEMO_INLINE_CAP 64u
#define CETTA_TERM_UNIVERSE_SOURCE_MEMO_CAP (1u << 16)

static _Atomic uint64_t g_term_universe_next_instance_id = 1u;

static uint64_t term_universe_fresh_instance_id(void) {
    uint64_t id = atomic_fetch_add_explicit(
        &g_term_universe_next_instance_id, 1u, memory_order_relaxed);
    if (id == 0u || id == UINT64_MAX) {
        fputs("CeTTa: exhausted process-local TermUniverse identities\n",
              stderr);
        abort();
    }
    return id;
}

static bool term_universe_intern_reserve(TermUniverse *universe,
                                         size_t min_slots);
static bool term_universe_ptr_reserve(TermUniverse *universe,
                                      size_t min_slots);
static bool term_universe_insert_stable_id(TermUniverse *universe, AtomId id);
static bool term_universe_insert_ptr_id(TermUniverse *universe, AtomId id);
static AtomId term_universe_lookup_stable_id(const TermUniverse *universe,
                                             Atom *src);
static AtomId term_universe_stable_atom_id_iterative(
    TermUniverse *universe, Atom *src, bool insert, bool *out_stable);
static AtomId term_universe_lookup_ptr_id(const TermUniverse *universe,
                                          Atom *src);
static const TermEntry *term_universe_entry(const TermUniverse *universe,
                                            AtomId id);
static bool term_universe_entry_has_blob(const TermEntry *entry);
static uint64_t term_universe_logical_atom_id_base(
    const TermUniverse *universe);
static AtomId term_universe_physical_index_to_atom_id(
    const TermUniverse *universe, size_t index);
static bool term_universe_atom_id_to_physical_index(
    const TermUniverse *universe, AtomId id, size_t *out_index);
static size_t term_universe_atom_id_storage_width_bytes(
    const TermUniverse *universe);
static size_t term_universe_slot_storage_width_bytes(
    const TermUniverse *universe);
static AtomId term_universe_load_stored_atom_id(const TermUniverse *universe,
                                                const uint8_t *src);
static uint64_t term_universe_load_slot_value(const TermUniverse *universe,
                                              const uint8_t *slots,
                                              size_t idx);
static bool term_universe_store_slot_value(TermUniverseStoreFormat format,
                                           uint8_t *slots,
                                           size_t idx,
                                           uint64_t value);
static bool term_universe_encode_expr_payload(const TermUniverse *universe,
                                              const AtomId *child_ids,
                                              uint32_t arity,
                                              uint8_t **out_payload,
                                              size_t *out_payload_len);
static bool term_universe_record_payload_len(const TermUniverse *universe,
                                             const CettaTermHdr *hdr,
                                             size_t *out_len);
static bool term_universe_append_raw_record(TermUniverse *universe,
                                            const CettaTermHdr *hdr,
                                            const uint8_t *payload,
                                            size_t payload_len,
                                            TermEntry *out_entry);
static AtomId term_universe_insert_new_record(TermUniverse *universe,
                                              const CettaTermHdr *hdr,
                                              const uint8_t *payload,
                                              size_t payload_len);
static bool term_universe_notify_store_format_observers(
    TermUniverse *universe,
    TermUniverseStoreFormat old_format,
    TermUniverseStoreFormat new_format);

typedef struct {
    AtomId id;
    Atom *atom;
} TermUniverseCopyMemoSlot;

typedef struct {
    TermUniverseCopyMemoSlot inline_slots[CETTA_TERM_UNIVERSE_COPY_MEMO_INLINE_CAP];
    TermUniverseCopyMemoSlot *slots;
    size_t cap;
    size_t used;
} TermUniverseCopyMemo;

typedef struct {
    Atom *atom;
    AtomId id;
    uint64_t generation;
} TermUniverseSourceMemoSlot;

typedef struct {
    TermUniverse *universe;
    uint64_t universe_instance_id;
    uint64_t universe_storage_epoch;
    const Arena *source_arena;
    uint32_t source_arena_identity;
    uint64_t source_arena_reset_epoch;
    uint64_t generation;
    TermUniverseSourceMemoSlot *slots;
} TermUniverseSourceMemo;

typedef struct TermUniverseSourceMemoRun {
    TermUniverseSourceMemo *memo;
    uint64_t lookups;
    uint64_t hits;
    uint64_t stores;
} TermUniverseSourceMemoRun;

static _Thread_local TermUniverseSourceMemo *g_term_universe_source_memo = NULL;
static pthread_key_t g_term_universe_source_memo_key;
static pthread_once_t g_term_universe_source_memo_key_once = PTHREAD_ONCE_INIT;
static bool g_term_universe_source_memo_key_ready = false;

static void term_universe_source_memo_destroy(void *raw) {
    TermUniverseSourceMemo *memo = raw;
    if (!memo)
        return;
    free(memo->slots);
    free(memo);
}

static void term_universe_source_memo_make_key(void) {
    g_term_universe_source_memo_key_ready =
        pthread_key_create(&g_term_universe_source_memo_key,
                           term_universe_source_memo_destroy) == 0;
}

bool term_universe_source_id_memo_enabled(void) {
    static _Atomic int cached = -1;
    int state = atomic_load_explicit(&cached, memory_order_acquire);
    if (state >= 0)
        return state != 0;
    const char *value = getenv("CETTA_TERM_UNIVERSE_SOURCE_ID_MEMO");
    int computed = value && *value && strcmp(value, "0") != 0 &&
                           strcmp(value, "off") != 0 &&
                           strcmp(value, "false") != 0;
    int expected = -1;
    (void)atomic_compare_exchange_strong_explicit(
        &cached, &expected, computed, memory_order_release,
        memory_order_acquire);
    return atomic_load_explicit(&cached, memory_order_acquire) != 0;
}

static TermUniverseSourceMemo *term_universe_source_memo_get(void) {
    if (g_term_universe_source_memo)
        return g_term_universe_source_memo;
    pthread_once(&g_term_universe_source_memo_key_once,
                 term_universe_source_memo_make_key);
    if (!g_term_universe_source_memo_key_ready)
        return NULL;
    TermUniverseSourceMemo *memo = calloc(1u, sizeof(*memo));
    if (!memo)
        return NULL;
    memo->slots = calloc(CETTA_TERM_UNIVERSE_SOURCE_MEMO_CAP,
                         sizeof(*memo->slots));
    if (!memo->slots ||
        pthread_setspecific(g_term_universe_source_memo_key, memo) != 0) {
        term_universe_source_memo_destroy(memo);
        return NULL;
    }
    g_term_universe_source_memo = memo;
    return memo;
}

static TermUniverseSourceMemoRun term_universe_source_memo_begin(
    TermUniverse *universe, const Arena *source_arena) {
    TermUniverseSourceMemoRun run = {0};
    if (!universe || !source_arena || source_arena->identity == 0u ||
        !term_universe_source_id_memo_enabled()) {
        return run;
    }
    TermUniverseSourceMemo *memo = term_universe_source_memo_get();
    if (!memo)
        return run;
    if (memo->universe != universe ||
        memo->universe_instance_id != universe->instance_id ||
        memo->universe_storage_epoch != universe->storage_epoch ||
        memo->source_arena != source_arena ||
        memo->source_arena_identity != source_arena->identity ||
        memo->source_arena_reset_epoch != source_arena->reset_epoch) {
        memo->generation++;
        if (memo->generation == 0u) {
            memset(memo->slots, 0,
                   CETTA_TERM_UNIVERSE_SOURCE_MEMO_CAP *
                       sizeof(*memo->slots));
            memo->generation = 1u;
        }
        memo->universe = universe;
        memo->universe_instance_id = universe->instance_id;
        memo->universe_storage_epoch = universe->storage_epoch;
        memo->source_arena = source_arena;
        memo->source_arena_identity = source_arena->identity;
        memo->source_arena_reset_epoch = source_arena->reset_epoch;
    }
    run.memo = memo;
    return run;
}

static size_t term_universe_source_memo_slot_index(const Atom *atom) {
    uint64_t key = (uint64_t)(uintptr_t)atom;
    key >>= 4u;
    key ^= key >> 33u;
    key *= UINT64_C(0xff51afd7ed558ccd);
    key ^= key >> 33u;
    return (size_t)key & (CETTA_TERM_UNIVERSE_SOURCE_MEMO_CAP - 1u);
}

static bool term_universe_source_memo_lookup(
    TermUniverseSourceMemoRun *run, Atom *atom, AtomId *out_id) {
    if (!run || !run->memo || !atom || !out_id ||
        atom->arena_id != run->memo->source_arena_identity) {
        return false;
    }
    run->lookups++;
    TermUniverseSourceMemoSlot *slot =
        &run->memo->slots[term_universe_source_memo_slot_index(atom)];
    if (slot->generation != run->memo->generation || slot->atom != atom)
        return false;
    run->hits++;
    *out_id = slot->id;
    return true;
}

static void term_universe_source_memo_store(
    TermUniverseSourceMemoRun *run, Atom *atom, AtomId id) {
    if (!run || !run->memo || !atom || id == CETTA_ATOM_ID_NONE ||
        atom->arena_id != run->memo->source_arena_identity) {
        return;
    }
    TermUniverseSourceMemoSlot *slot =
        &run->memo->slots[term_universe_source_memo_slot_index(atom)];
    slot->atom = atom;
    slot->id = id;
    slot->generation = run->memo->generation;
    run->stores++;
}

static void term_universe_source_memo_finish(
    const TermUniverseSourceMemoRun *run) {
    if (!run || !run->memo)
        return;
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_SOURCE_MEMO_LOOKUP,
        run->lookups);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_SOURCE_MEMO_HIT, run->hits);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_SOURCE_MEMO_STORE, run->stores);
}

#if CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS
#define TU_DIAG_INC(universe, field)                                             \
    do {                                                                         \
        if ((universe))                                                          \
            (universe)->diagnostics.field++;                                     \
    } while (0)
#define TU_DIAG_LOOKUP_PROBES(universe, count)                                   \
    do {                                                                         \
        if ((universe)) {                                                        \
            uint64_t probe_count__ = (uint64_t)(count);                          \
            (universe)->diagnostics.direct_lookup_probes += probe_count__;       \
            if (probe_count__ >                                                  \
                (universe)->diagnostics.direct_lookup_max_probe) {               \
                (universe)->diagnostics.direct_lookup_max_probe = probe_count__; \
            }                                                                    \
        }                                                                        \
    } while (0)
#else
#define TU_DIAG_INC(universe, field)                                             \
    do {                                                                         \
        (void)(universe);                                                        \
    } while (0)
#define TU_DIAG_LOOKUP_PROBES(universe, count)                                   \
    do {                                                                         \
        (void)(universe);                                                        \
        (void)(count);                                                           \
    } while (0)
#endif

static inline uint32_t term_universe_aux_make(uint32_t data, bool has_vars) {
    return (data & CETTA_TERM_HDR_DATA_MASK) |
           (has_vars ? CETTA_TERM_HDR_HAS_VARS : 0u);
}

static inline uint32_t term_universe_aux_data(const CettaTermHdr *hdr) {
    return hdr ? (hdr->aux32 & CETTA_TERM_HDR_DATA_MASK) : 0u;
}

static inline bool term_universe_hdr_has_vars(const CettaTermHdr *hdr) {
    return hdr && (hdr->aux32 & CETTA_TERM_HDR_HAS_VARS) != 0;
}

static void term_universe_set_error(TermUniverse *universe,
                                    TermUniverseError error) {
    if (universe && error != TERM_UNIVERSE_ERROR_NONE)
        universe->last_error = error;
}

static bool term_universe_expr_arity_checked(TermUniverse *universe,
                                             CettaExprLen arity,
                                             uint32_t *out_arity) {
    if (!out_arity)
        return false;
    if (!cetta_expr_len_fits_u32(arity)) {
        term_universe_set_error(universe, TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE);
        return false;
    }
    if (arity > (CettaExprLen)CETTA_TERM_HDR_DATA_MASK) {
        term_universe_set_error(universe, TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE);
        return false;
    }
    *out_arity = (uint32_t)arity;
    return true;
}

void term_universe_clear_error(TermUniverse *universe) {
    if (universe)
        universe->last_error = TERM_UNIVERSE_ERROR_NONE;
}

TermUniverseError term_universe_last_error_code(const TermUniverse *universe) {
    return universe ? universe->last_error : TERM_UNIVERSE_ERROR_NONE;
}

const char *term_universe_error_name(TermUniverseError error) {
    switch (error) {
    case TERM_UNIVERSE_ERROR_NONE:
        return "None";
    case TERM_UNIVERSE_ERROR_ATOM_ID_EXHAUSTED:
        return "TermUniverseAtomIdExhausted";
    case TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE:
        return "TermUniverseStorageTooLarge";
    case TERM_UNIVERSE_ERROR_ALLOCATION_FAILED:
        return "TermUniverseAllocationFailed";
    case TERM_UNIVERSE_ERROR_UNSUPPORTED_STORE_FORMAT:
        return "TermUniverseStoreFormatUnsupported";
    case TERM_UNIVERSE_ERROR_STORE_FORMAT_MIGRATION_FAILED:
        return "TermUniverseStoreFormatMigrationFailed";
    }
    return "TermUniverseUnknownError";
}

TermUniverseStoreFormat
term_universe_store_format(const TermUniverse *universe) {
    return universe ? universe->store_format
                    : TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1;
}

const char *term_universe_store_format_name(TermUniverseStoreFormat format) {
    switch (format) {
    case TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1:
        return "Compact32V1";
    case TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1:
        return "Wide64V1";
    }
    return "TermUniverseFormatUnknown";
}

uint32_t term_universe_store_format_version(TermUniverseStoreFormat format) {
    switch (format) {
    case TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1:
    case TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1:
        return 1u;
    }
    return 0;
}

uint32_t term_universe_store_format_atom_id_width_bits(TermUniverseStoreFormat format) {
    switch (format) {
    case TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1:
        return 32u;
    case TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1:
        return 64u;
    }
    return 0;
}

bool term_universe_add_store_format_observer(
    TermUniverse *universe,
    TermUniverseStoreFormatObserver fn,
    void *ctx) {
    size_t next_cap = 0;
    TermUniverseStoreFormatObserverEntry *next = NULL;
    if (!universe || !fn)
        return false;
    for (size_t i = 0; i < universe->observer_len; i++) {
        if (universe->observers[i].fn == fn &&
            universe->observers[i].ctx == ctx) {
            return true;
        }
    }
    if (universe->observer_len < universe->observer_cap) {
        universe->observers[universe->observer_len++] =
            (TermUniverseStoreFormatObserverEntry){.fn = fn, .ctx = ctx};
        return true;
    }
    next_cap = universe->observer_cap ? universe->observer_cap * 2u : 8u;
    next = cetta_realloc(universe->observers, sizeof(*next) * next_cap);
    if (!next) {
        term_universe_set_error(universe,
                                TERM_UNIVERSE_ERROR_ALLOCATION_FAILED);
        return false;
    }
    universe->observers = next;
    universe->observer_cap = next_cap;
    universe->observers[universe->observer_len++] =
        (TermUniverseStoreFormatObserverEntry){.fn = fn, .ctx = ctx};
    return true;
}

void term_universe_remove_store_format_observer(
    TermUniverse *universe,
    TermUniverseStoreFormatObserver fn,
    void *ctx) {
    if (!universe || !fn)
        return;
    for (size_t i = 0; i < universe->observer_len; i++) {
        if (universe->observers[i].fn != fn ||
            universe->observers[i].ctx != ctx) {
            continue;
        }
        universe->observer_len--;
        if (i != universe->observer_len)
            universe->observers[i] = universe->observers[universe->observer_len];
        return;
    }
}

static size_t
term_universe_store_format_atom_id_width_bytes(TermUniverseStoreFormat format) {
    return cetta_atom_id_storage_width_bytes_from_bits(
        term_universe_store_format_atom_id_width_bits(format));
}

static bool term_universe_store_format_supported(TermUniverseStoreFormat format) {
    switch (format) {
    case TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1:
    case TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1:
        return true;
    }
    return false;
}

static bool term_universe_notify_store_format_observers(
    TermUniverse *universe,
    TermUniverseStoreFormat old_format,
    TermUniverseStoreFormat new_format) {
    if (!universe)
        return false;
    for (size_t i = 0; i < universe->observer_len; i++) {
        TermUniverseStoreFormatObserverEntry observer = universe->observers[i];
        if (!observer.fn)
            continue;
        if (!observer.fn(universe, old_format, new_format, observer.ctx)) {
            term_universe_set_error(
                universe, TERM_UNIVERSE_ERROR_STORE_FORMAT_MIGRATION_FAILED);
            return false;
        }
    }
    return true;
}

static uint64_t
term_universe_logical_atom_id_base(const TermUniverse *universe) {
#if CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS
    if (universe &&
        term_universe_store_format(universe) ==
            TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1 &&
        universe->diag_logical_atom_id_base_override != 0) {
        return universe->diag_logical_atom_id_base_override;
    }
#else
    (void)universe;
#endif
    return 0;
}

static AtomId term_universe_physical_index_to_atom_id(
    const TermUniverse *universe, size_t index) {
    uint64_t base = term_universe_logical_atom_id_base(universe);
    if ((uint64_t)index > UINT64_MAX - base - 1u)
        return CETTA_ATOM_ID_NONE;
    return (AtomId)(base + (uint64_t)index);
}

static bool term_universe_atom_id_to_physical_index(
    const TermUniverse *universe, AtomId id, size_t *out_index) {
    if (!universe || !out_index || id == CETTA_ATOM_ID_NONE)
        return false;
    uint64_t base = term_universe_logical_atom_id_base(universe);
    if (id < base)
        return false;
    uint64_t physical = id - base;
    if (physical >= universe->len || physical > SIZE_MAX)
        return false;
    *out_index = (size_t)physical;
    return true;
}

uint64_t term_universe_atom_id_capacity(const TermUniverse *universe) {
#if CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS
    if (universe &&
        term_universe_store_format(universe) ==
            TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1 &&
        universe->diag_atom_id_capacity_override != 0) {
        return universe->diag_atom_id_capacity_override;
    }
#endif
    switch (term_universe_store_format(universe)) {
    case TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1:
        return (uint64_t)UINT32_MAX;
    case TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1:
        return UINT64_MAX;
    }
    return 0;
}

static uint64_t
term_universe_compact32_auto_migration_threshold(const TermUniverse *universe) {
    uint64_t capacity = term_universe_atom_id_capacity(universe);
    if (capacity == 0)
        return 0;
    uint64_t headroom = capacity / 64u;
    if (headroom == 0)
        headroom = 1u;
    if (headroom >= capacity)
        return capacity;
    return capacity - headroom;
}

static bool term_universe_atom_id_capacity_available(TermUniverse *universe) {
    if (!universe)
        return false;
    if (term_universe_store_format(universe) ==
        TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1) {
        uint64_t threshold =
            term_universe_compact32_auto_migration_threshold(universe);
        if ((uint64_t)universe->len < threshold)
            return true;
        if (term_universe_migrate_store_format(
                universe, TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1) &&
            (uint64_t)universe->len < term_universe_atom_id_capacity(universe)) {
            return true;
        }
        if (term_universe_last_error_code(universe) ==
            TERM_UNIVERSE_ERROR_NONE) {
            term_universe_set_error(
                universe, TERM_UNIVERSE_ERROR_STORE_FORMAT_MIGRATION_FAILED);
        }
        return false;
    }
    if ((uint64_t)universe->len < term_universe_atom_id_capacity(universe))
        return true;
    term_universe_set_error(universe,
                            TERM_UNIVERSE_ERROR_ATOM_ID_EXHAUSTED);
    return false;
}

static bool term_universe_double_request(TermUniverse *universe,
                                         size_t current,
                                         size_t initial,
                                         size_t *out) {
    if (!out)
        return false;
    if (current == 0) {
        *out = initial;
        return true;
    }
    if (current > SIZE_MAX / 2u) {
        term_universe_set_error(universe,
                                TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE);
        return false;
    }
    *out = current * 2u;
    return true;
}

static bool term_universe_next_capacity(TermUniverse *universe,
                                        size_t current,
                                        size_t needed,
                                        size_t initial,
                                        size_t elem_size,
                                        size_t *out) {
    if (!out || needed == 0) {
        term_universe_set_error(universe,
                                TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE);
        return false;
    }
    size_t next = current ? current : initial;
    while (next < needed) {
        if (next > SIZE_MAX / 2u) {
            term_universe_set_error(universe,
                                    TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE);
            return false;
        }
        next *= 2u;
    }
    if (elem_size != 0 && (size_t)next > SIZE_MAX / elem_size) {
        term_universe_set_error(universe,
                                TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE);
        return false;
    }
    *out = next;
    return true;
}

static inline size_t term_universe_align_up(size_t n) {
    return (n + (CETTA_TERM_ENTRY_ALIGN - 1u)) &
           ~(size_t)(CETTA_TERM_ENTRY_ALIGN - 1u);
}

static bool term_universe_atom_contains_epoch_var(Atom *atom) {
    Atom **stack = NULL;
    uint32_t len = 0;
    uint32_t cap = 0;
    bool found = false;

#define PUSH_ATOM(candidate) do { \
    if (len == cap) { \
        uint32_t next_cap = cap ? cap * 2u : 64u; \
        Atom **next_stack = cetta_realloc(stack, sizeof(Atom *) * next_cap); \
        if (!next_stack) goto done; \
        stack = next_stack; \
        cap = next_cap; \
    } \
    stack[len++] = (candidate); \
} while (0)

    if (!atom)
        goto done;
    PUSH_ATOM(atom);
    while (len > 0) {
        Atom *cur = stack[--len];
        if (!cur)
            continue;
        if (cur->kind == ATOM_VAR) {
            if (var_epoch_suffix(cur->var_id) != 0) {
                found = true;
                goto done;
            }
            continue;
        }
        if (cur->kind == ATOM_EXPR) {
            for (CettaExprIndex i = 0; i < cur->expr.len; i++)
                PUSH_ATOM(cur->expr.elems[i]);
        }
    }
done:
    free(stack);
#undef PUSH_ATOM
    return found;
}

bool term_universe_atom_is_stable(Atom *atom) {
    Atom **stack = NULL;
    uint32_t len = 0;
    uint32_t cap = 0;
    bool stable = false;

#define PUSH_STABLE_ATOM(candidate) do { \
    if (len == cap) { \
        uint32_t next_cap = cap ? cap * 2u : 64u; \
        Atom **next_stack = cetta_realloc(stack, sizeof(Atom *) * next_cap); \
        if (!next_stack) goto done; \
        stack = next_stack; \
        cap = next_cap; \
    } \
    stack[len++] = (candidate); \
} while (0)

    if (!atom)
        goto done;
    PUSH_STABLE_ATOM(atom);
    while (len > 0) {
        Atom *cur = stack[--len];
        if (!cur)
            goto done;
        switch (cur->kind) {
        case ATOM_SYMBOL:
        case ATOM_VAR:
            break;
        case ATOM_GROUNDED:
            switch (cur->ground.gkind) {
            case GV_INT:
            case GV_FLOAT:
            case GV_BOOL:
            case GV_STRING:
            case GV_BIGINT:
            case GV_RATIONAL:
                break;
            case GV_SPACE:
            case GV_STATE:
            case GV_CAPTURE:
            case GV_FOREIGN:
            case GV_PRIME_NEED_CAPABILITY:
            case GV_PRIME_CONTEXT:
            case GV_INTERNAL_TAG:
                goto done;
            }
            break;
        case ATOM_EXPR:
            for (CettaExprIndex i = 0; i < cur->expr.len; i++)
                PUSH_STABLE_ATOM(cur->expr.elems[i]);
            break;
        }
    }
    stable = true;

done:
    free(stack);
#undef PUSH_STABLE_ATOM
    return stable;
}

static void term_universe_clear_storage(TermUniverse *universe) {
    if (!universe)
        return;
    TermUniverseStoreFormat format = term_universe_store_format_supported(
                                         universe->store_format)
                                         ? universe->store_format
                                         : TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1;
    free(universe->blob_pool);
    free(universe->entries);
    free(universe->intern_slots);
    free(universe->ptr_slots);
    universe->blob_pool = NULL;
    universe->blob_len = 0;
    universe->blob_cap = 0;
    universe->entries = NULL;
    universe->len = 0;
    universe->cap = 0;
    universe->intern_slots = NULL;
    universe->intern_mask = 0;
    universe->intern_used = 0;
    universe->ptr_slots = NULL;
    universe->ptr_mask = 0;
    universe->ptr_used = 0;
    universe->last_error = TERM_UNIVERSE_ERROR_NONE;
    universe->store_format = format;
    if (universe->storage_epoch == UINT64_MAX) {
        fputs("CeTTa: exhausted TermUniverse storage generations\n", stderr);
        abort();
    }
    universe->storage_epoch++;
}

bool term_universe_init_with_store_format(TermUniverse *universe,
                                          TermUniverseStoreFormat format) {
    if (!universe)
        return false;
    if (!term_universe_store_format_supported(format)) {
        memset(universe, 0, sizeof(*universe));
        universe->instance_id = term_universe_fresh_instance_id();
        universe->storage_epoch = 1u;
        universe->last_error = TERM_UNIVERSE_ERROR_UNSUPPORTED_STORE_FORMAT;
        universe->store_format = TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1;
        return false;
    }
    universe->instance_id = term_universe_fresh_instance_id();
    universe->storage_epoch = 1u;
    universe->persistent_arena = NULL;
    universe->blob_pool = NULL;
    universe->blob_len = 0;
    universe->blob_cap = 0;
    universe->entries = NULL;
    universe->len = 0;
    universe->cap = 0;
    universe->intern_slots = NULL;
    universe->intern_mask = 0;
    universe->intern_used = 0;
    universe->ptr_slots = NULL;
    universe->ptr_mask = 0;
    universe->ptr_used = 0;
    universe->observers = NULL;
    universe->observer_len = 0;
    universe->observer_cap = 0;
    universe->last_error = TERM_UNIVERSE_ERROR_NONE;
    universe->store_format = format;
    term_universe_diag_reset(universe);
    return true;
}

void term_universe_init(TermUniverse *universe) {
    (void)term_universe_init_with_store_format(
        universe, TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1);
}

void term_universe_free(TermUniverse *universe) {
    if (!universe)
        return;
    term_universe_clear_storage(universe);
    free(universe->observers);
    universe->observers = NULL;
    universe->observer_len = 0;
    universe->observer_cap = 0;
    universe->persistent_arena = NULL;
}

void term_universe_set_persistent_arena(TermUniverse *universe,
                                        Arena *persistent_arena) {
    if (!universe)
        return;
    /* Some evaluator fallbacks are zero-initialized thread locals rather than
       explicitly initialized objects.  Give those lifetimes the same identity
       discipline before a pointer-derived accelerator can observe them. */
    if (universe->instance_id == 0u) {
        universe->instance_id = term_universe_fresh_instance_id();
        if (universe->storage_epoch == 0u)
            universe->storage_epoch = 1u;
    }
    if (universe->persistent_arena == persistent_arena)
        return;
    term_universe_clear_storage(universe);
    universe->persistent_arena = persistent_arena;
    term_universe_diag_reset(universe);
}

bool term_universe_migrate_store_format(TermUniverse *universe,
                                        TermUniverseStoreFormat format) {
    TermUniverseStoreFormat old_format;
    if (!universe)
        return false;
    if (!term_universe_store_format_supported(format)) {
        term_universe_set_error(universe,
                                TERM_UNIVERSE_ERROR_UNSUPPORTED_STORE_FORMAT);
        return false;
    }
    old_format = universe->store_format;
    if (universe->store_format == format)
        return true;
    if (format == TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1 &&
        universe->len > (size_t)UINT32_MAX) {
        term_universe_set_error(universe,
                                TERM_UNIVERSE_ERROR_ATOM_ID_EXHAUSTED);
        return false;
    }

    TermEntry *rewritten =
        universe->len ? cetta_malloc(sizeof(*rewritten) * universe->len) : NULL;
    if (universe->len != 0 && !rewritten) {
        term_universe_set_error(universe,
                                TERM_UNIVERSE_ERROR_ALLOCATION_FAILED);
        return false;
    }

    TermUniverse staging = {0};
    staging.store_format = format;
    staging.last_error = TERM_UNIVERSE_ERROR_NONE;

    for (size_t i = 0; i < universe->len; i++) {
        TermEntry src_entry = universe->entries[i];
        rewritten[i] = (TermEntry){
            .byte_off = CETTA_TERM_ENTRY_BLOB_NONE,
            .byte_len = 0,
            .decoded_cache = src_entry.decoded_cache,
        };
        if (!term_universe_entry_has_blob(&src_entry))
            continue;

        const CettaTermHdr *hdr =
            (const CettaTermHdr *)(universe->blob_pool + src_entry.byte_off);
        const uint8_t *payload =
            universe->blob_pool + src_entry.byte_off + sizeof(CettaTermHdr);
        size_t payload_len = 0;
        if (!term_universe_record_payload_len(universe, hdr, &payload_len)) {
            term_universe_set_error(universe,
                                    TERM_UNIVERSE_ERROR_STORE_FORMAT_MIGRATION_FAILED);
            free(rewritten);
            free(staging.blob_pool);
            return false;
        }

        const uint8_t *payload_src = payload;
        uint8_t *expr_payload = NULL;
        if (hdr->tag == ATOM_EXPR && payload_len != 0) {
            uint32_t arity = term_universe_aux_data(hdr);
            AtomId *child_ids =
                arity ? cetta_malloc(sizeof(*child_ids) * arity) : NULL;
            if (arity != 0 && !child_ids) {
                term_universe_set_error(universe,
                                        TERM_UNIVERSE_ERROR_ALLOCATION_FAILED);
                free(rewritten);
                free(staging.blob_pool);
                return false;
            }
            size_t old_width = term_universe_atom_id_storage_width_bytes(universe);
            for (uint32_t j = 0; j < arity; j++) {
                child_ids[j] = term_universe_load_stored_atom_id(
                    universe, payload + ((size_t)j * old_width));
            }
            TermUniverse shadow = *universe;
            shadow.store_format = format;
            size_t new_payload_len = 0;
            if (!term_universe_encode_expr_payload(&shadow, child_ids, arity,
                                                   &expr_payload,
                                                   &new_payload_len)) {
                free(child_ids);
                term_universe_set_error(universe, shadow.last_error);
                free(rewritten);
                free(staging.blob_pool);
                return false;
            }
            free(child_ids);
            payload_src = expr_payload;
            payload_len = new_payload_len;
        }

        if (!term_universe_append_raw_record(&staging, hdr, payload_src,
                                             payload_len, &rewritten[i])) {
            free(expr_payload);
            term_universe_set_error(universe, staging.last_error);
            free(rewritten);
            free(staging.blob_pool);
            return false;
        }
        rewritten[i].decoded_cache = src_entry.decoded_cache;
        free(expr_payload);
    }

    TermUniverse shadow = *universe;
    shadow.store_format = format;
    shadow.blob_pool = staging.blob_pool;
    shadow.blob_len = staging.blob_len;
    shadow.blob_cap = staging.blob_cap;
    shadow.entries = rewritten;
    shadow.intern_slots = NULL;
    shadow.intern_mask = 0;
    shadow.intern_used = 0;
    shadow.ptr_slots = NULL;
    shadow.ptr_mask = 0;
    shadow.ptr_used = 0;

    if (universe->len != 0) {
        size_t min_intern_slots =
            universe->intern_slots ? (universe->intern_mask + 1u) : 1024u;
        if (!term_universe_intern_reserve(&shadow, min_intern_slots)) {
            term_universe_set_error(universe, shadow.last_error);
            free(rewritten);
            free(staging.blob_pool);
            return false;
        }
        for (size_t i = 0; i < universe->len; i++) {
            AtomId id = term_universe_physical_index_to_atom_id(&shadow, i);
            if (id == CETTA_ATOM_ID_NONE)
                continue;
            if (term_universe_entry_has_blob(&rewritten[i]) &&
                !term_universe_insert_stable_id(&shadow, id)) {
                term_universe_set_error(
                    universe, TERM_UNIVERSE_ERROR_STORE_FORMAT_MIGRATION_FAILED);
                free(rewritten);
                free(staging.blob_pool);
                free(shadow.intern_slots);
                free(shadow.ptr_slots);
                return false;
            }
        }

        size_t min_ptr_slots =
            universe->ptr_slots ? (universe->ptr_mask + 1u) : 1024u;
        if (!term_universe_ptr_reserve(&shadow, min_ptr_slots)) {
            term_universe_set_error(universe, shadow.last_error);
            free(rewritten);
            free(staging.blob_pool);
            free(shadow.intern_slots);
            free(shadow.ptr_slots);
            return false;
        }
        for (size_t i = 0; i < universe->len; i++) {
            AtomId id = term_universe_physical_index_to_atom_id(&shadow, i);
            const TermEntry *entry = term_universe_entry(&shadow, id);
            if (!entry || !entry->decoded_cache)
                continue;
            if (!term_universe_insert_ptr_id(&shadow, id)) {
                term_universe_set_error(
                    universe, TERM_UNIVERSE_ERROR_STORE_FORMAT_MIGRATION_FAILED);
                free(rewritten);
                free(staging.blob_pool);
                free(shadow.intern_slots);
                free(shadow.ptr_slots);
                return false;
            }
        }
    }

    if (!term_universe_notify_store_format_observers(universe, old_format,
                                                     format)) {
        free(rewritten);
        free(staging.blob_pool);
        free(shadow.intern_slots);
        free(shadow.ptr_slots);
        return false;
    }

    free(universe->blob_pool);
    free(universe->intern_slots);
    free(universe->ptr_slots);
    universe->blob_pool = staging.blob_pool;
    universe->blob_len = staging.blob_len;
    universe->blob_cap = staging.blob_cap;
    for (size_t i = 0; i < universe->len; i++)
        universe->entries[i] = rewritten[i];
    universe->intern_slots = shadow.intern_slots;
    universe->intern_mask = shadow.intern_mask;
    universe->intern_used = shadow.intern_used;
    universe->ptr_slots = shadow.ptr_slots;
    universe->ptr_mask = shadow.ptr_mask;
    universe->ptr_used = shadow.ptr_used;
    universe->store_format = format;
    universe->last_error = TERM_UNIVERSE_ERROR_NONE;
    TU_DIAG_INC(universe, store_format_migrations);
    free(rewritten);
    return true;
}

void term_universe_diag_reset(TermUniverse *universe) {
    if (!universe)
        return;
#if CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS
    memset(&universe->diagnostics, 0, sizeof(universe->diagnostics));
    universe->diag_atom_id_capacity_override = 0;
    universe->diag_logical_atom_id_base_override = 0;
#endif
}

void term_universe_diag_snapshot(const TermUniverse *universe,
                                 CettaTermUniverseDiagnostics *out) {
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
#if CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS
    if (universe)
        *out = universe->diagnostics;
#else
    (void)universe;
#endif
}

#if CETTA_BUILD_WITH_TERM_UNIVERSE_DIAGNOSTICS
void term_universe_diag_set_atom_id_capacity_override(TermUniverse *universe,
                                                      uint64_t capacity) {
    if (!universe)
        return;
    universe->diag_atom_id_capacity_override = capacity;
}

void term_universe_diag_set_logical_atom_id_base_override(
    TermUniverse *universe, uint64_t base) {
    if (!universe)
        return;
    universe->diag_logical_atom_id_base_override = base;
}
#endif

typedef struct {
    CettaVarMap *src_to_canonical;
} TermUniverseCanonicalizeCtx;

static Atom *term_universe_create_canonical_var(Arena *dst, Atom *src_var,
                                                uint32_t ordinal, void *ctx) {
    (void)ctx;
    VarId canonical_id = (VarId)ordinal;
    if (canonical_id == VAR_ID_NONE)
        canonical_id = 1u;
    return atom_var_like(dst, src_var, canonical_id);
}

static Atom *term_universe_create_alpha_key_var(Arena *dst, Atom *src_var,
                                                uint32_t ordinal, void *ctx) {
    (void)src_var;
    (void)ctx;
    VarId canonical_id = (VarId)ordinal;
    if (canonical_id == VAR_ID_NONE)
        canonical_id = 1u;
    /* Alpha keys intentionally erase both symbol and structural name-key
       presentation.  Only the equality partition induced by repeated VarIds
       belongs in the key. */
    return atom_var_with_spelling(dst, SYMBOL_ID_NONE, canonical_id);
}

static Atom *term_universe_rewrite_epochless_var(Arena *dst, Atom *src_var,
                                                 void *ctx) {
    TermUniverseCanonicalizeCtx *canon = ctx;
    if (!canon || !canon->src_to_canonical)
        return NULL;
    return cetta_var_map_get_or_add(canon->src_to_canonical, dst, src_var,
                                    term_universe_create_canonical_var, NULL);
}

static Atom *term_universe_rewrite_alpha_key_var(Arena *dst, Atom *src_var,
                                                 void *ctx) {
    TermUniverseCanonicalizeCtx *canon = ctx;
    if (!canon || !canon->src_to_canonical)
        return NULL;
    return cetta_var_map_get_or_add(canon->src_to_canonical, dst, src_var,
                                    term_universe_create_alpha_key_var, NULL);
}

Atom *term_universe_canonicalize_atom(Arena *dst, Atom *src) {
    if (!term_universe_atom_contains_epoch_var(src))
        return atom_deep_copy(dst, src);
    CettaVarMap src_to_canonical;
    cetta_var_map_init(&src_to_canonical);
    TermUniverseCanonicalizeCtx canon = {
        .src_to_canonical = &src_to_canonical,
    };
    Atom *canonical =
        cetta_atom_rewrite_vars(dst, src,
                                term_universe_rewrite_epochless_var,
                                &canon, true);
    cetta_var_map_free(&src_to_canonical);
    return canonical;
}

Atom *term_universe_alpha_canonicalize_atom(Arena *dst, Atom *src) {
    if (!dst || !src)
        return NULL;
    if (!atom_has_vars(src))
        return atom_deep_copy(dst, src);
    CettaVarMap src_to_canonical;
    cetta_var_map_init(&src_to_canonical);
    TermUniverseCanonicalizeCtx key_ctx = {
        .src_to_canonical = &src_to_canonical,
    };
    Atom *canonical = cetta_atom_rewrite_vars(
        dst, src, term_universe_rewrite_alpha_key_var, &key_ctx, true);
    cetta_var_map_free(&src_to_canonical);
    return canonical;
}

static bool term_universe_reserve_entries(TermUniverse *universe,
                                          size_t needed) {
    if (!universe)
        return false;
    if (needed <= universe->cap)
        return true;
    size_t old_cap = universe->cap;
    size_t next_cap = 0;
    if (!term_universe_next_capacity(universe, universe->cap, needed, 64,
                                     sizeof(TermEntry), &next_cap)) {
        return false;
    }
    TermEntry *next =
        cetta_realloc(universe->entries, sizeof(TermEntry) * next_cap);
    if (!next) {
        term_universe_set_error(universe,
                                TERM_UNIVERSE_ERROR_ALLOCATION_FAILED);
        return false;
    }
    universe->entries = next;
    for (size_t i = old_cap; i < next_cap; i++) {
        universe->entries[i].byte_off = CETTA_TERM_ENTRY_BLOB_NONE;
        universe->entries[i].byte_len = 0;
        universe->entries[i].decoded_cache = NULL;
    }
    universe->cap = next_cap;
    return true;
}

static bool term_universe_blob_reserve(TermUniverse *universe,
                                       size_t needed) {
    if (!universe)
        return false;
    if (needed <= universe->blob_cap)
        return true;
    size_t next_cap = 0;
    if (!term_universe_next_capacity(universe, universe->blob_cap, needed,
                                     1024, sizeof(uint8_t), &next_cap)) {
        return false;
    }
    uint8_t *next = cetta_realloc(universe->blob_pool, next_cap);
    if (!next) {
        term_universe_set_error(universe,
                                TERM_UNIVERSE_ERROR_ALLOCATION_FAILED);
        return false;
    }
    universe->blob_pool = next;
    universe->blob_cap = next_cap;
    return true;
}

static const TermEntry *term_universe_entry(const TermUniverse *universe,
                                            AtomId id) {
    size_t physical_index = 0;
    if (!term_universe_atom_id_to_physical_index(universe, id, &physical_index))
        return NULL;
    return &universe->entries[physical_index];
}

static bool term_universe_entry_has_blob(const TermEntry *entry) {
    return entry && entry->byte_off != CETTA_TERM_ENTRY_BLOB_NONE &&
           entry->byte_len >= sizeof(CettaTermHdr);
}

static const uint8_t *term_universe_entry_bytes(const TermUniverse *universe,
                                                AtomId id) {
    const TermEntry *entry = term_universe_entry(universe, id);
    if (!term_universe_entry_has_blob(entry))
        return NULL;
    if (!universe->blob_pool || entry->byte_off > universe->blob_len ||
        entry->byte_len > universe->blob_len - entry->byte_off) {
        return NULL;
    }
    return universe->blob_pool + entry->byte_off;
}

const CettaTermHdr *tu_hdr(const TermUniverse *universe, AtomId id) {
    const uint8_t *bytes = term_universe_entry_bytes(universe, id);
    return bytes ? (const CettaTermHdr *)bytes : NULL;
}

static const uint8_t *term_universe_payload(const TermUniverse *universe,
                                            AtomId id) {
    const uint8_t *bytes = term_universe_entry_bytes(universe, id);
    return bytes ? bytes + sizeof(CettaTermHdr) : NULL;
}

static size_t term_universe_atom_id_storage_width_bytes(
    const TermUniverse *universe) {
    return term_universe_store_format_atom_id_width_bytes(
        term_universe_store_format(universe));
}

static size_t term_universe_slot_storage_width_bytes(
    const TermUniverse *universe) {
    return term_universe_atom_id_storage_width_bytes(universe);
}

static bool term_universe_expr_payload_len_for_format(
    TermUniverseStoreFormat format, uint32_t arity, size_t *out_len) {
    if (!out_len)
        return false;
    size_t atom_id_width = term_universe_store_format_atom_id_width_bytes(format);
    if (atom_id_width == 0 || (size_t)arity > SIZE_MAX / atom_id_width)
        return false;
    *out_len = (size_t)arity * atom_id_width;
    return true;
}

static uint64_t term_universe_load_u64(const uint8_t *src) {
    uint64_t value = 0;
    memcpy(&value, src, sizeof(value));
    return value;
}

static int64_t term_universe_load_i64(const uint8_t *src) {
    int64_t value = 0;
    memcpy(&value, src, sizeof(value));
    return value;
}

static double term_universe_load_double(const uint8_t *src) {
    double value = 0.0;
    memcpy(&value, src, sizeof(value));
    return value;
}

static AtomId term_universe_load_stored_atom_id(const TermUniverse *universe,
                                                const uint8_t *src) {
    return cetta_atom_id_storage_load_bits(
        src,
        term_universe_store_format_atom_id_width_bits(
            term_universe_store_format(universe)));
}

static uint64_t term_universe_load_slot_value(const TermUniverse *universe,
                                              const uint8_t *slots,
                                              size_t idx) {
    size_t width = term_universe_slot_storage_width_bytes(universe);
    const uint8_t *src = NULL;
    if (!slots || width == 0)
        return 0;
    src = slots + (idx * width);
    switch (term_universe_store_format(universe)) {
    case TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1: {
        uint32_t value = 0;
        memcpy(&value, src, sizeof(value));
        return value;
    }
    case TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1:
        return term_universe_load_u64(src);
    }
    return 0;
}

static void term_universe_store_u64(uint8_t *dst, uint64_t value) {
    memcpy(dst, &value, sizeof(value));
}

static void term_universe_store_i64(uint8_t *dst, int64_t value) {
    memcpy(dst, &value, sizeof(value));
}

static void term_universe_store_double(uint8_t *dst, double value) {
    memcpy(dst, &value, sizeof(value));
}

static bool term_universe_store_stored_atom_id(TermUniverseStoreFormat format,
                                               uint8_t *dst, AtomId value) {
    return cetta_atom_id_storage_store_bits(
        dst, term_universe_store_format_atom_id_width_bits(format), value);
}

static bool term_universe_store_slot_value(TermUniverseStoreFormat format,
                                           uint8_t *slots,
                                           size_t idx,
                                           uint64_t value) {
    size_t width = term_universe_store_format_atom_id_width_bytes(format);
    uint8_t *dst = NULL;
    if (!slots || width == 0)
        return false;
    if (format == TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1 &&
        value > UINT32_MAX) {
        return false;
    }
    dst = slots + (idx * width);
    switch (format) {
    case TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1: {
        uint32_t narrowed = (uint32_t)value;
        memcpy(dst, &narrowed, sizeof(narrowed));
        return true;
    }
    case TERM_UNIVERSE_STORE_FORMAT_WIDE64_V1:
        memcpy(dst, &value, sizeof(value));
        return true;
    }
    return false;
}

static bool term_universe_encode_expr_payload(const TermUniverse *universe,
                                              const AtomId *child_ids,
                                              uint32_t arity,
                                              uint8_t **out_payload,
                                              size_t *out_payload_len) {
    if (!out_payload || !out_payload_len)
        return false;
    *out_payload = NULL;
    *out_payload_len = 0;
    if (arity == 0)
        return true;
    if (!universe || !child_ids)
        return false;
    size_t atom_id_width = term_universe_atom_id_storage_width_bytes(universe);
    if (atom_id_width == 0 || arity > SIZE_MAX / atom_id_width) {
        term_universe_set_error((TermUniverse *)universe,
                                TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE);
        return false;
    }
    size_t payload_len = (size_t)arity * atom_id_width;
    uint8_t *payload = cetta_malloc(payload_len);
    if (!payload) {
        term_universe_set_error((TermUniverse *)universe,
                                TERM_UNIVERSE_ERROR_ALLOCATION_FAILED);
        return false;
    }
    for (uint32_t i = 0; i < arity; i++) {
        if (!term_universe_store_stored_atom_id(term_universe_store_format(universe),
                                                payload + ((size_t)i * atom_id_width),
                                                child_ids[i])) {
            free(payload);
            term_universe_set_error((TermUniverse *)universe,
                                    TERM_UNIVERSE_ERROR_ATOM_ID_EXHAUSTED);
            return false;
        }
    }
    *out_payload = payload;
    *out_payload_len = payload_len;
    return true;
}

static bool term_universe_record_payload_len(const TermUniverse *universe,
                                             const CettaTermHdr *hdr,
                                             size_t *out_len) {
    if (!out_len || !hdr)
        return false;
    switch ((AtomKind)hdr->tag) {
    case ATOM_SYMBOL:
        *out_len = 0;
        return true;
    case ATOM_VAR:
        *out_len = hdr->subtag == CETTA_VAR_SPELLING_NAME_KEY
                       ? 2u * sizeof(uint64_t)
                       : sizeof(uint64_t);
        return true;
    case ATOM_GROUNDED:
        switch ((GroundedKind)hdr->subtag) {
        case GV_INT:
        case GV_FLOAT:
            *out_len = sizeof(uint64_t);
            return true;
        case GV_BOOL:
            *out_len = 0;
            return true;
        case GV_STRING:
        case GV_BIGINT:
        case GV_RATIONAL:
            *out_len = (size_t)term_universe_aux_data(hdr) + 1u;
            return true;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            return false;
        }
        return false;
    case ATOM_EXPR:
        return term_universe_expr_payload_len_for_format(
            term_universe_store_format(universe), term_universe_aux_data(hdr),
            out_len);
    }
    return false;
}

/*
 * These helpers must stay exactly compatible with atom_hash_compute() in
 * atom.c so direct constructors and legacy Atom*-based interning converge on
 * the same canonical ids.
 */
static inline uint32_t term_universe_hash_mix(uint32_t h, uint32_t piece) {
    return ((h << 5) + h) ^ piece;
}

/*
 * The stored hash32 remains the cross-module structural-hash contract and the
 * cheap equality pre-filter.  It is deliberately not the intern-table hash:
 * its incremental xor recurrence admits large equal-hash term families, and
 * no finalizer can separate keys that have already collapsed to the same
 * 32-bit value.
 *
 * The intern index therefore derives a second, 64-bit structural hash from
 * the same canonical fields.  Expressions mix their canonical child ids in
 * position order.  Atom-based expression lookup first resolves those same
 * child ids through the non-inserting bottom-up path; leaf Atom lookup hashes
 * its canonical scalar payload directly.  Exact record/Atom equality remains
 * authoritative, so this hash is only an accelerator.  Lookup, insertion,
 * and rehashing must all use these helpers.
 */
static inline uint64_t term_universe_index_mix(uint64_t h, uint64_t value) {
    value += UINT64_C(0x9e3779b97f4a7c15);
    value = (value ^ (value >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    value = (value ^ (value >> 27)) * UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    h ^= value;
    h = ((h << 27) | (h >> 37)) * UINT64_C(5) +
        UINT64_C(0x52dce729);
    return h;
}

static inline uint64_t term_universe_index_finalize(uint64_t h) {
    h ^= h >> 33;
    h *= UINT64_C(0xff51afd7ed558ccd);
    h ^= h >> 33;
    h *= UINT64_C(0xc4ceb9fe1a85ec53);
    h ^= h >> 33;
    return h;
}

static uint64_t term_universe_index_mix_span(
    uint64_t h, const uint8_t *bytes, size_t len) {
    uint64_t span = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < len; i++) {
        span ^= bytes[i];
        span *= UINT64_C(1099511628211);
    }
    h = term_universe_index_mix(h, len);
    return term_universe_index_mix(h, span);
}

static uint64_t term_universe_record_slot_hash(
    const TermUniverse *universe, const CettaTermHdr *hdr,
    const uint8_t *payload, size_t payload_len) {
    if (!universe || !hdr)
        return 0;

    uint64_t h = UINT64_C(0x243f6a8885a308d3);
    h = term_universe_index_mix(h, hdr->tag);
    h = term_universe_index_mix(h, hdr->subtag);
    h = term_universe_index_mix(h, hdr->sym_or_head);
    h = term_universe_index_mix(h, term_universe_aux_data(hdr));
    h = term_universe_index_mix(h, hdr->hash32);

    switch ((AtomKind)hdr->tag) {
    case ATOM_SYMBOL:
        break;
    case ATOM_VAR:
        if (payload_len >= sizeof(uint64_t)) {
            h = term_universe_index_mix(
                h, term_universe_load_u64(payload));
        }
        if (hdr->subtag == CETTA_VAR_SPELLING_NAME_KEY &&
            payload_len >= 2u * sizeof(uint64_t)) {
            AtomId key_id = (AtomId)term_universe_load_u64(
                payload + sizeof(uint64_t));
            h = term_universe_index_mix(h, tu_hash32(universe, key_id));
        }
        break;
    case ATOM_GROUNDED:
        switch ((GroundedKind)hdr->subtag) {
        case GV_INT:
            if (payload_len >= sizeof(int64_t)) {
                h = term_universe_index_mix(
                    h, (uint64_t)term_universe_load_i64(payload));
            }
            break;
        case GV_FLOAT:
            if (payload_len >= sizeof(double)) {
                double value = term_universe_load_double(payload);
                uint64_t bits = 0;
                if (value != 0.0)
                    memcpy(&bits, &value, sizeof(bits));
                h = term_universe_index_mix(h, bits);
            }
            break;
        case GV_BOOL:
            h = term_universe_index_mix(
                h, term_universe_aux_data(hdr) != 0u);
            break;
        case GV_STRING:
        case GV_BIGINT:
        case GV_RATIONAL: {
            size_t logical_len = term_universe_aux_data(hdr);
            if (logical_len > payload_len)
                logical_len = payload_len;
            h = term_universe_index_mix_span(h, payload, logical_len);
            break;
        }
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            break;
        }
        break;
    case ATOM_EXPR: {
        uint32_t arity = term_universe_aux_data(hdr);
        size_t width = term_universe_atom_id_storage_width_bytes(universe);
        size_t available = width == 0 ? 0 : payload_len / width;
        if ((size_t)arity > available)
            arity = (uint32_t)available;
        for (uint32_t i = 0; i < arity; i++) {
            AtomId child_id = term_universe_load_stored_atom_id(
                universe, payload + ((size_t)i * width));
            h = term_universe_index_mix(
                h, child_id);
        }
        break;
    }
    }
    return term_universe_index_finalize(h);
}

static uint64_t term_universe_atom_slot_hash(Atom *atom) {
    if (!atom)
        return 0;

    uint8_t subtag = 0;
    uint32_t sym_or_head = SYMBOL_ID_NONE;
    uint32_t aux_data = 0;
    if (atom->kind == ATOM_SYMBOL) {
        sym_or_head = atom->sym_id;
    } else if (atom->kind == ATOM_VAR) {
        subtag = atom->name_key
                     ? (uint8_t)CETTA_VAR_SPELLING_NAME_KEY
                     : 0u;
        sym_or_head = atom->name_key ? SYMBOL_ID_NONE : atom->sym_id;
    } else if (atom->kind == ATOM_GROUNDED) {
        subtag = (uint8_t)atom->ground.gkind;
        if (atom->ground.gkind == GV_BOOL)
            aux_data = atom->ground.bval ? 1u : 0u;
        else if (atom->ground.gkind == GV_STRING)
            aux_data = (uint32_t)strlen(atom->ground.sval);
        else if (atom->ground.gkind == GV_BIGINT)
            aux_data = (uint32_t)strlen(atom_bigint_cstr(atom));
        else if (atom->ground.gkind == GV_RATIONAL)
            aux_data = (uint32_t)strlen(atom_rational_cstr(atom));
    } else if (atom->kind == ATOM_EXPR) {
        sym_or_head = atom_head_symbol_id(atom);
        aux_data = (uint32_t)atom->expr.len;
    }

    uint64_t h = UINT64_C(0x243f6a8885a308d3);
    h = term_universe_index_mix(h, atom->kind);
    h = term_universe_index_mix(h, subtag);
    h = term_universe_index_mix(h, sym_or_head);
    h = term_universe_index_mix(h, aux_data);
    h = term_universe_index_mix(h, atom_hash(atom));

    switch (atom->kind) {
    case ATOM_SYMBOL:
        break;
    case ATOM_VAR:
        h = term_universe_index_mix(h, atom->var_id);
        if (atom->name_key) {
            h = term_universe_index_mix(
                h, atom_hash(atom->name_key));
        }
        break;
    case ATOM_GROUNDED:
        switch (atom->ground.gkind) {
        case GV_INT:
            h = term_universe_index_mix(
                h, (uint64_t)atom->ground.ival);
            break;
        case GV_FLOAT: {
            uint64_t bits = 0;
            if (atom->ground.fval != 0.0)
                memcpy(&bits, &atom->ground.fval, sizeof(bits));
            h = term_universe_index_mix(h, bits);
            break;
        }
        case GV_BOOL:
            h = term_universe_index_mix(
                h, atom->ground.bval ? 1u : 0u);
            break;
        case GV_STRING:
            h = term_universe_index_mix_span(
                h, (const uint8_t *)atom->ground.sval,
                strlen(atom->ground.sval));
            break;
        case GV_BIGINT: {
            const char *text = atom_bigint_cstr(atom);
            h = term_universe_index_mix_span(
                h, (const uint8_t *)text, strlen(text));
            break;
        }
        case GV_RATIONAL: {
            const char *text = atom_rational_cstr(atom);
            h = term_universe_index_mix_span(
                h, (const uint8_t *)text, strlen(text));
            break;
        }
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            break;
        }
        break;
    case ATOM_EXPR:
        for (CettaExprIndex i = 0; i < atom->expr.len; i++) {
            h = term_universe_index_mix(
                h, atom_hash(atom->expr.elems[i]));
        }
        break;
    }
    return term_universe_index_finalize(h);
}

static uint64_t term_universe_entry_slot_hash(
    const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    size_t payload_len = 0;
    if (!hdr ||
        !term_universe_record_payload_len(universe, hdr, &payload_len)) {
        return 0;
    }
    return term_universe_record_slot_hash(
        universe, hdr, term_universe_payload(universe, id), payload_len);
}

static uint32_t term_universe_hash_symbol_id(SymbolId sym_id) {
    uint32_t h = 5381u;
    uint64_t sh = symbol_hash_value(g_symbols, sym_id);
    h = term_universe_hash_mix(h, (uint32_t)ATOM_SYMBOL);
    h = term_universe_hash_mix(h, (uint32_t)(sh & 0xffffffffu));
    h = term_universe_hash_mix(h, (uint32_t)(sh >> 32));
    return h;
}

static uint32_t term_universe_hash_var_id(VarId var_id) {
    uint32_t h = 5381u;
    h = term_universe_hash_mix(h, (uint32_t)ATOM_VAR);
    h = term_universe_hash_mix(h, (uint32_t)(var_id & 0xffffffffu));
    h = term_universe_hash_mix(h, (uint32_t)(var_id >> 32));
    return h;
}

static uint32_t term_universe_hash_int_value(int64_t value) {
    uint32_t h = 5381u;
    h = term_universe_hash_mix(h, (uint32_t)ATOM_GROUNDED);
    h = term_universe_hash_mix(h, (uint32_t)GV_INT);
    h = term_universe_hash_mix(h, (uint32_t)(value & 0xffffffffu));
    return h;
}

static uint32_t term_universe_hash_float_value(double value) {
    uint32_t h = 5381u;
    union {
        double d;
        uint64_t u;
    } conv;
    conv.d = value;
    h = term_universe_hash_mix(h, (uint32_t)ATOM_GROUNDED);
    h = term_universe_hash_mix(h, (uint32_t)GV_FLOAT);
    h = term_universe_hash_mix(h, (uint32_t)(conv.u & 0xffffffffu));
    return h;
}

static uint32_t term_universe_hash_bool_value(bool value) {
    uint32_t h = 5381u;
    h = term_universe_hash_mix(h, (uint32_t)ATOM_GROUNDED);
    h = term_universe_hash_mix(h, (uint32_t)GV_BOOL);
    h = term_universe_hash_mix(h, value ? 1u : 0u);
    return h;
}

static uint32_t term_universe_hash_text_value(GroundedKind kind,
                                              const char *value) {
    uint32_t h = 5381u;
    h = term_universe_hash_mix(h, (uint32_t)ATOM_GROUNDED);
    h = term_universe_hash_mix(h, (uint32_t)kind);
    for (const char *p = value; p && *p; p++)
        h = term_universe_hash_mix(h, (uint32_t)*p);
    return h;
}

static uint32_t term_universe_hash_string_value(const char *value) {
    return term_universe_hash_text_value(GV_STRING, value);
}

static uint32_t term_universe_hash_bigint_value(const char *value) {
    return term_universe_hash_text_value(GV_BIGINT, value);
}

static uint32_t term_universe_hash_rational_value(const char *value) {
    return term_universe_hash_text_value(GV_RATIONAL, value);
}

static uint32_t term_universe_hash_expr_ids(const TermUniverse *universe,
                                            const AtomId *child_ids,
                                            uint32_t arity) {
    uint32_t h = 5381u;
    h = term_universe_hash_mix(h, (uint32_t)ATOM_EXPR);
    h = term_universe_hash_mix(h, arity);
    h = term_universe_hash_mix(h, 0u);
    for (uint32_t i = 0; i < arity; i++)
        h = term_universe_hash_mix(h, tu_hash32(universe, child_ids[i]));
    return h;
}

static bool term_universe_entry_eq_record(const TermUniverse *universe, AtomId id,
                                          const CettaTermHdr *want_hdr,
                                          const uint8_t *want_payload,
                                          size_t want_payload_len) {
    const CettaTermHdr *have_hdr = tu_hdr(universe, id);
    const uint8_t *have_payload = term_universe_payload(universe, id);
    if (!have_hdr || !want_hdr)
        return false;
    if (have_hdr->tag != want_hdr->tag ||
        have_hdr->subtag != want_hdr->subtag ||
        have_hdr->arity_or_len != want_hdr->arity_or_len ||
        have_hdr->aux32 != want_hdr->aux32) {
        return false;
    }

    switch ((AtomKind)want_hdr->tag) {
    case ATOM_SYMBOL:
        return have_hdr->sym_or_head == want_hdr->sym_or_head;
    case ATOM_VAR:
        if (!have_payload || !want_payload ||
            term_universe_load_u64(have_payload) !=
                term_universe_load_u64(want_payload)) {
            return false;
        }
        return want_hdr->subtag != CETTA_VAR_SPELLING_NAME_KEY ||
               term_universe_load_u64(have_payload + sizeof(uint64_t)) ==
                   term_universe_load_u64(want_payload + sizeof(uint64_t));
    case ATOM_GROUNDED:
        switch ((GroundedKind)want_hdr->subtag) {
        case GV_INT:
            return have_payload && want_payload &&
                   term_universe_load_i64(have_payload) ==
                       term_universe_load_i64(want_payload);
        case GV_FLOAT:
            return have_payload && want_payload &&
                   term_universe_load_double(have_payload) ==
                       term_universe_load_double(want_payload);
        case GV_BOOL:
            return term_universe_aux_data(have_hdr) ==
                   term_universe_aux_data(want_hdr);
        case GV_STRING:
            if (want_payload_len == 0)
                return true;
            return have_payload && want_payload &&
                   memcmp(have_payload, want_payload, want_payload_len) == 0;
        case GV_BIGINT:
            if (want_payload_len == 0)
                return true;
            return have_payload && want_payload &&
                   memcmp(have_payload, want_payload, want_payload_len) == 0;
        case GV_RATIONAL:
            if (want_payload_len == 0)
                return true;
            return have_payload && want_payload &&
                   memcmp(have_payload, want_payload, want_payload_len) == 0;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            return false;
        }
        return false;
    case ATOM_EXPR:
        if (have_hdr->sym_or_head != want_hdr->sym_or_head)
            return false;
        if (want_payload_len == 0)
            return true;
        return have_payload && want_payload &&
               memcmp(have_payload, want_payload, want_payload_len) == 0;
    }
    return false;
}

static AtomId term_universe_lookup_record_id(const TermUniverse *universe,
                                             const CettaTermHdr *hdr,
                                             const uint8_t *payload,
                                             size_t payload_len) {
    if (!universe || !universe->intern_slots || !hdr)
        return CETTA_ATOM_ID_NONE;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LOOKUP);
    uint32_t h = hdr->hash32;
    uint64_t hslot = term_universe_record_slot_hash(
        universe, hdr, payload, payload_len);
    for (size_t probe = 0; probe <= universe->intern_mask; probe++) {
        size_t idx = ((size_t)hslot + probe) & universe->intern_mask;
        uint64_t slot = term_universe_load_slot_value(
            universe, universe->intern_slots, idx);
        if (slot == 0) {
            TU_DIAG_LOOKUP_PROBES(
                (TermUniverse *)universe, probe + 1u);
            TU_DIAG_INC((TermUniverse *)universe, direct_lookup_misses);
            return CETTA_ATOM_ID_NONE;
        }
        AtomId id = slot - 1;
        const CettaTermHdr *have_hdr = tu_hdr(universe, id);
        if (term_universe_entry(universe, id) && have_hdr &&
            have_hdr->hash32 == h &&
            term_universe_entry_eq_record(universe, id, hdr, payload,
                                          payload_len)) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_HIT);
            TU_DIAG_LOOKUP_PROBES(
                (TermUniverse *)universe, probe + 1u);
            TU_DIAG_INC((TermUniverse *)universe, direct_lookup_hits);
            return id;
        }
    }
    TU_DIAG_LOOKUP_PROBES(
        (TermUniverse *)universe, universe->intern_mask + 1u);
    TU_DIAG_INC((TermUniverse *)universe, direct_lookup_misses);
    return CETTA_ATOM_ID_NONE;
}

static bool term_universe_append_raw_record(TermUniverse *universe,
                                            const CettaTermHdr *hdr,
                                            const uint8_t *payload,
                                            size_t payload_len,
                                            TermEntry *out_entry) {
    if (!universe || !hdr || !out_entry)
        return false;
    if (payload_len > SIZE_MAX - sizeof(CettaTermHdr))
        return false;
    size_t raw_len = sizeof(CettaTermHdr) + payload_len;
    if (raw_len > SIZE_MAX - (CETTA_TERM_ENTRY_ALIGN - 1u))
        return false;
    size_t total_len = term_universe_align_up(raw_len);
    size_t off = universe->blob_len;
    if (off > SIZE_MAX - total_len)
        return false;
    if (!term_universe_blob_reserve(universe, off + total_len))
        return false;
    memset(universe->blob_pool + off, 0, total_len);
    memcpy(universe->blob_pool + off, hdr, sizeof(*hdr));
    if (payload_len != 0 && payload) {
        memcpy(universe->blob_pool + off + sizeof(CettaTermHdr), payload,
               payload_len);
    }

    universe->blob_len += total_len;
    out_entry->byte_off = off;
    out_entry->byte_len = total_len;
    out_entry->decoded_cache = NULL;
    return true;
}

static AtomId term_universe_intern_record(TermUniverse *universe,
                                          const CettaTermHdr *hdr,
                                          const uint8_t *payload,
                                          size_t payload_len) {
    if (!universe || !universe->persistent_arena || !hdr)
        return CETTA_ATOM_ID_NONE;

    AtomId existing =
        term_universe_lookup_record_id(universe, hdr, payload, payload_len);
    if (existing != CETTA_ATOM_ID_NONE)
        return existing;

    if (!term_universe_atom_id_capacity_available(universe))
        return CETTA_ATOM_ID_NONE;
    return term_universe_insert_new_record(universe, hdr, payload, payload_len);
}

static AtomId term_universe_insert_new_record(TermUniverse *universe,
                                              const CettaTermHdr *hdr,
                                              const uint8_t *payload,
                                              size_t payload_len) {
    if (!universe || !universe->persistent_arena || !hdr)
        return CETTA_ATOM_ID_NONE;
    if (!term_universe_reserve_entries(universe, universe->len + 1))
        return CETTA_ATOM_ID_NONE;

    size_t needed = universe->intern_slots ? (universe->intern_mask + 1) : 0;
    if (needed == 0 || (universe->intern_used + 1) * 10 > needed * 7) {
        size_t min_slots = 0;
        if (!term_universe_double_request(universe, needed, 1024,
                                          &min_slots) ||
            !term_universe_intern_reserve(universe, min_slots)) {
            return CETTA_ATOM_ID_NONE;
        }
    }

    TermEntry entry = {
        .byte_off = CETTA_TERM_ENTRY_BLOB_NONE,
        .byte_len = 0,
        .decoded_cache = NULL,
    };
    if (!term_universe_append_raw_record(universe, hdr, payload, payload_len,
                                         &entry)) {
        return CETTA_ATOM_ID_NONE;
    }

    size_t physical_index = universe->len;
    AtomId id = term_universe_physical_index_to_atom_id(universe, physical_index);
    if (id == CETTA_ATOM_ID_NONE)
        return CETTA_ATOM_ID_NONE;
    universe->len++;
    universe->entries[physical_index] = entry;
    if (!term_universe_insert_stable_id(universe, id)) {
        universe->len--;
        universe->entries[physical_index].byte_off = CETTA_TERM_ENTRY_BLOB_NONE;
        universe->entries[physical_index].byte_len = 0;
        universe->entries[physical_index].decoded_cache = NULL;
        universe->blob_len = entry.byte_off;
        return CETTA_ATOM_ID_NONE;
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_INSERT);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_BYTE_ENTRY);
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_BLOB_BYTES,
                            entry.byte_len);
    return id;
}

AtomKind tu_kind(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr)
        return (AtomKind)hdr->tag;
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache) ? entry->decoded_cache->kind
                                           : ATOM_SYMBOL;
}

CettaExprLen tu_arity(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr)
        return hdr->tag == ATOM_EXPR ? (CettaExprLen)term_universe_aux_data(hdr)
                                     : 0u;
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache &&
            entry->decoded_cache->kind == ATOM_EXPR)
               ? entry->decoded_cache->expr.len
               : 0u;
}

uint32_t tu_hash32(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr)
        return hdr->hash32;
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache) ? atom_hash(entry->decoded_cache) : 0u;
}

SymbolId tu_sym(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr) {
        if (hdr->tag == ATOM_SYMBOL ||
            (hdr->tag == ATOM_VAR &&
             hdr->subtag == CETTA_VAR_SPELLING_SYMBOL))
            return hdr->sym_or_head;
        return SYMBOL_ID_NONE;
    }
    const TermEntry *entry = term_universe_entry(universe, id);
    if (!entry || !entry->decoded_cache)
        return SYMBOL_ID_NONE;
    return (entry->decoded_cache->kind == ATOM_SYMBOL ||
            entry->decoded_cache->kind == ATOM_VAR)
               ? entry->decoded_cache->sym_id
               : SYMBOL_ID_NONE;
}

VarId tu_var_id(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr) {
        if (hdr->tag == ATOM_VAR)
            return (VarId)term_universe_load_u64(term_universe_payload(universe, id));
        return VAR_ID_NONE;
    }
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache &&
            entry->decoded_cache->kind == ATOM_VAR)
               ? entry->decoded_cache->var_id
               : VAR_ID_NONE;
}

AtomId tu_var_name_key_id(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr && hdr->tag == ATOM_VAR &&
        hdr->subtag == CETTA_VAR_SPELLING_NAME_KEY) {
        const uint8_t *payload = term_universe_payload(universe, id);
        return payload
                   ? (AtomId)term_universe_load_u64(payload + sizeof(uint64_t))
                   : CETTA_ATOM_ID_NONE;
    }
    const TermEntry *entry = term_universe_entry(universe, id);
    if (entry && entry->decoded_cache &&
        entry->decoded_cache->kind == ATOM_VAR &&
        entry->decoded_cache->name_key) {
        return term_universe_lookup_stable_id(
            universe, entry->decoded_cache->name_key);
    }
    return CETTA_ATOM_ID_NONE;
}

SymbolId tu_head_sym(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr) {
        if (hdr->tag == ATOM_SYMBOL || hdr->tag == ATOM_EXPR)
            return hdr->sym_or_head;
        return SYMBOL_ID_NONE;
    }
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache)
               ? atom_head_symbol_id(entry->decoded_cache)
               : SYMBOL_ID_NONE;
}

GroundedKind tu_ground_kind(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr)
        return (GroundedKind)hdr->subtag;
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache &&
            entry->decoded_cache->kind == ATOM_GROUNDED)
               ? entry->decoded_cache->ground.gkind
               : GV_FOREIGN;
}

int64_t tu_int(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr) {
        if (hdr->tag == ATOM_GROUNDED &&
            (GroundedKind)hdr->subtag == GV_INT) {
            return term_universe_load_i64(term_universe_payload(universe, id));
        }
        return 0;
    }
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache &&
            entry->decoded_cache->kind == ATOM_GROUNDED &&
            entry->decoded_cache->ground.gkind == GV_INT)
               ? entry->decoded_cache->ground.ival
               : 0;
}

double tu_float(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr) {
        if (hdr->tag == ATOM_GROUNDED &&
            (GroundedKind)hdr->subtag == GV_FLOAT) {
            return term_universe_load_double(term_universe_payload(universe, id));
        }
        return 0.0;
    }
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache &&
            entry->decoded_cache->kind == ATOM_GROUNDED &&
            entry->decoded_cache->ground.gkind == GV_FLOAT)
               ? entry->decoded_cache->ground.fval
               : 0.0;
}

bool tu_bool(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr) {
        if (hdr->tag == ATOM_GROUNDED &&
            (GroundedKind)hdr->subtag == GV_BOOL) {
            return term_universe_aux_data(hdr) != 0;
        }
        return false;
    }
    const TermEntry *entry = term_universe_entry(universe, id);
    return entry && entry->decoded_cache &&
           entry->decoded_cache->kind == ATOM_GROUNDED &&
           entry->decoded_cache->ground.gkind == GV_BOOL &&
           entry->decoded_cache->ground.bval;
}

const char *tu_string_cstr(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr) {
        if (hdr->tag == ATOM_GROUNDED &&
            (GroundedKind)hdr->subtag == GV_STRING) {
            return (const char *)term_universe_payload(universe, id);
        }
        return NULL;
    }
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache &&
            entry->decoded_cache->kind == ATOM_GROUNDED &&
            entry->decoded_cache->ground.gkind == GV_STRING)
               ? entry->decoded_cache->ground.sval
               : NULL;
}

const char *tu_bigint_cstr(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr) {
        if (hdr->tag == ATOM_GROUNDED &&
            (GroundedKind)hdr->subtag == GV_BIGINT) {
            return (const char *)term_universe_payload(universe, id);
        }
        return NULL;
    }
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache &&
            entry->decoded_cache->kind == ATOM_GROUNDED &&
            entry->decoded_cache->ground.gkind == GV_BIGINT)
               ? atom_bigint_cstr(entry->decoded_cache)
               : NULL;
}

const char *tu_rational_cstr(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr) {
        if (hdr->tag == ATOM_GROUNDED &&
            (GroundedKind)hdr->subtag == GV_RATIONAL) {
            return (const char *)term_universe_payload(universe, id);
        }
        return NULL;
    }
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache &&
            entry->decoded_cache->kind == ATOM_GROUNDED &&
            entry->decoded_cache->ground.gkind == GV_RATIONAL)
               ? atom_rational_cstr(entry->decoded_cache)
               : NULL;
}

AtomId tu_child(const TermUniverse *universe, AtomId id, CettaExprIndex idx) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!hdr || hdr->tag != ATOM_EXPR)
        return CETTA_ATOM_ID_NONE;
    uint32_t len = term_universe_aux_data(hdr);
    if (idx >= (CettaExprIndex)len)
        return CETTA_ATOM_ID_NONE;
    size_t atom_id_width = term_universe_atom_id_storage_width_bytes(universe);
    return term_universe_load_stored_atom_id(
        universe, term_universe_payload(universe, id) +
                      ((size_t)idx * atom_id_width));
}

bool tu_has_vars(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr)
        return term_universe_hdr_has_vars(hdr);
    const TermEntry *entry = term_universe_entry(universe, id);
    return entry && atom_has_vars(entry->decoded_cache);
}

AtomId tu_intern_symbol(TermUniverse *universe, SymbolId sym_id) {
    CettaTermHdr hdr = {0};
    hdr.tag = (uint8_t)ATOM_SYMBOL;
    hdr.sym_or_head = sym_id;
    hdr.aux32 = term_universe_aux_make(0u, false);
    hdr.hash32 = term_universe_hash_symbol_id(sym_id);
    TU_DIAG_INC(universe, direct_constructor_leaf_hits);
    return term_universe_intern_record(universe, &hdr, NULL, 0u);
}

AtomId tu_intern_var(TermUniverse *universe, SymbolId sym_id, VarId var_id) {
    CettaTermHdr hdr = {0};
    uint8_t payload[sizeof(uint64_t)] = {0};
    hdr.tag = (uint8_t)ATOM_VAR;
    hdr.sym_or_head = sym_id;
    hdr.aux32 = term_universe_aux_make(0u, true);
    hdr.hash32 = term_universe_hash_var_id(var_id);
    term_universe_store_u64(payload, var_id);
    TU_DIAG_INC(universe, direct_constructor_leaf_hits);
    return term_universe_intern_record(universe, &hdr, payload,
                                       sizeof(payload));
}

AtomId tu_intern_named_var(TermUniverse *universe, AtomId name_key_id,
                           VarId var_id) {
    if (!universe || name_key_id == CETTA_ATOM_ID_NONE ||
        !term_universe_entry(universe, name_key_id) ||
        tu_has_vars(universe, name_key_id)) {
        return CETTA_ATOM_ID_NONE;
    }
    CettaTermHdr hdr = {0};
    uint8_t payload[2u * sizeof(uint64_t)] = {0};
    hdr.tag = (uint8_t)ATOM_VAR;
    hdr.subtag = (uint8_t)CETTA_VAR_SPELLING_NAME_KEY;
    hdr.sym_or_head = SYMBOL_ID_NONE;
    hdr.aux32 = term_universe_aux_make(0u, true);
    hdr.hash32 = term_universe_hash_var_id(var_id);
    term_universe_store_u64(payload, var_id);
    term_universe_store_u64(payload + sizeof(uint64_t), name_key_id);
    TU_DIAG_INC(universe, direct_constructor_leaf_hits);
    return term_universe_intern_record(universe, &hdr, payload,
                                       sizeof(payload));
}

AtomId tu_intern_int(TermUniverse *universe, int64_t value) {
    CettaTermHdr hdr = {0};
    uint8_t payload[sizeof(int64_t)] = {0};
    hdr.tag = (uint8_t)ATOM_GROUNDED;
    hdr.subtag = (uint8_t)GV_INT;
    hdr.aux32 = term_universe_aux_make(0u, false);
    hdr.hash32 = term_universe_hash_int_value(value);
    term_universe_store_i64(payload, value);
    TU_DIAG_INC(universe, direct_constructor_leaf_hits);
    return term_universe_intern_record(universe, &hdr, payload,
                                       sizeof(payload));
}

AtomId tu_intern_float(TermUniverse *universe, double value) {
    CettaTermHdr hdr = {0};
    uint8_t payload[sizeof(double)] = {0};
    hdr.tag = (uint8_t)ATOM_GROUNDED;
    hdr.subtag = (uint8_t)GV_FLOAT;
    hdr.aux32 = term_universe_aux_make(0u, false);
    hdr.hash32 = term_universe_hash_float_value(value);
    term_universe_store_double(payload, value);
    TU_DIAG_INC(universe, direct_constructor_leaf_hits);
    return term_universe_intern_record(universe, &hdr, payload,
                                       sizeof(payload));
}

AtomId tu_intern_bool(TermUniverse *universe, bool value) {
    CettaTermHdr hdr = {0};
    hdr.tag = (uint8_t)ATOM_GROUNDED;
    hdr.subtag = (uint8_t)GV_BOOL;
    hdr.aux32 = term_universe_aux_make(value ? 1u : 0u, false);
    hdr.hash32 = term_universe_hash_bool_value(value);
    TU_DIAG_INC(universe, direct_constructor_leaf_hits);
    return term_universe_intern_record(universe, &hdr, NULL, 0u);
}

AtomId tu_intern_string(TermUniverse *universe, const char *value) {
    if (!value)
        return CETTA_ATOM_ID_NONE;
    size_t len_sz = strlen(value);
    if (len_sz > (size_t)UINT32_MAX - 1u)
        return CETTA_ATOM_ID_NONE;
    uint32_t len = (uint32_t)len_sz;
    CettaTermHdr hdr = {0};
    hdr.tag = (uint8_t)ATOM_GROUNDED;
    hdr.subtag = (uint8_t)GV_STRING;
    hdr.arity_or_len = len > UINT16_MAX ? UINT16_MAX : (uint16_t)len;
    hdr.aux32 = term_universe_aux_make(len, false);
    hdr.hash32 = term_universe_hash_string_value(value);
    TU_DIAG_INC(universe, direct_constructor_leaf_hits);
    return term_universe_intern_record(universe, &hdr,
                                       (const uint8_t *)value, len + 1u);
}

AtomId tu_intern_bigint(TermUniverse *universe, const char *value) {
    if (!value)
        return CETTA_ATOM_ID_NONE;
    char *canonical = cetta_bigint_canonicalize_owned(value);
    if (!canonical)
        return CETTA_ATOM_ID_NONE;
    int64_t small = 0;
    if (cetta_bigint_text_fits_i64(canonical, &small)) {
        free(canonical);
        return tu_intern_int(universe, small);
    }
    size_t len_sz = strlen(canonical);
    if (len_sz > (size_t)UINT32_MAX - 1u) {
        free(canonical);
        return CETTA_ATOM_ID_NONE;
    }
    uint32_t len = (uint32_t)len_sz;
    CettaTermHdr hdr = {0};
    hdr.tag = (uint8_t)ATOM_GROUNDED;
    hdr.subtag = (uint8_t)GV_BIGINT;
    hdr.arity_or_len = len > UINT16_MAX ? UINT16_MAX : (uint16_t)len;
    hdr.aux32 = term_universe_aux_make(len, false);
    hdr.hash32 = term_universe_hash_bigint_value(canonical);
    TU_DIAG_INC(universe, direct_constructor_leaf_hits);
    AtomId id = term_universe_intern_record(universe, &hdr,
                                            (const uint8_t *)canonical,
                                            len + 1u);
    free(canonical);
    return id;
}

AtomId tu_intern_rational(TermUniverse *universe, const char *value) {
    if (!value)
        return CETTA_ATOM_ID_NONE;
    char *canonical = cetta_rational_canonicalize_owned(value);
    if (!canonical)
        return CETTA_ATOM_ID_NONE;
    char *slash = strchr(canonical, '/');
    if (slash && strcmp(slash + 1, "1") == 0) {
        *slash = '\0';
        AtomId id = tu_intern_bigint(universe, canonical);
        free(canonical);
        return id;
    }
    size_t len_sz = strlen(canonical);
    if (len_sz > (size_t)UINT32_MAX - 1u) {
        free(canonical);
        return CETTA_ATOM_ID_NONE;
    }
    uint32_t len = (uint32_t)len_sz;
    CettaTermHdr hdr = {0};
    hdr.tag = (uint8_t)ATOM_GROUNDED;
    hdr.subtag = (uint8_t)GV_RATIONAL;
    hdr.arity_or_len = len > UINT16_MAX ? UINT16_MAX : (uint16_t)len;
    hdr.aux32 = term_universe_aux_make(len, false);
    hdr.hash32 = term_universe_hash_rational_value(canonical);
    TU_DIAG_INC(universe, direct_constructor_leaf_hits);
    AtomId id = term_universe_intern_record(universe, &hdr,
                                            (const uint8_t *)canonical,
                                            len + 1u);
    free(canonical);
    return id;
}

AtomId tu_expr_from_ids(TermUniverse *universe, const AtomId *child_ids,
                        CettaExprLen arity) {
    uint32_t arity32 = 0;
    if (!universe)
        return CETTA_ATOM_ID_NONE;
    if (!term_universe_expr_arity_checked(universe, arity, &arity32))
        return CETTA_ATOM_ID_NONE;
    if (arity > 0 && !child_ids)
        return CETTA_ATOM_ID_NONE;

    bool has_vars = false;
    SymbolId head_sym = SYMBOL_ID_NONE;
    for (uint32_t i = 0; i < arity32; i++) {
        const CettaTermHdr *child_hdr;
        if (child_ids[i] == CETTA_ATOM_ID_NONE)
            return CETTA_ATOM_ID_NONE;
        child_hdr = tu_hdr(universe, child_ids[i]);
        if (!child_hdr)
            return CETTA_ATOM_ID_NONE;
        if (term_universe_hdr_has_vars(child_hdr))
            has_vars = true;
    }
    if (arity > 0 && tu_kind(universe, child_ids[0]) == ATOM_SYMBOL)
        head_sym = tu_sym(universe, child_ids[0]);

    CettaTermHdr hdr = {0};
    hdr.tag = (uint8_t)ATOM_EXPR;
    hdr.arity_or_len = arity32 > UINT16_MAX ? UINT16_MAX : (uint16_t)arity32;
    hdr.sym_or_head = head_sym;
    hdr.aux32 = term_universe_aux_make(arity32, has_vars);
    hdr.hash32 = term_universe_hash_expr_ids(universe, child_ids, arity32);
    TU_DIAG_INC(universe, direct_constructor_expr_hits);
    TermUniverseStoreFormat encoded_format = term_universe_store_format(universe);
    uint8_t *payload = NULL;
    size_t payload_len = 0;
    AtomId id = CETTA_ATOM_ID_NONE;
    if (!term_universe_encode_expr_payload(universe, child_ids, arity32,
                                           &payload, &payload_len))
        return CETTA_ATOM_ID_NONE;
    id = term_universe_lookup_record_id(universe, &hdr, payload, payload_len);
    if (id != CETTA_ATOM_ID_NONE) {
        free(payload);
        return id;
    }
    if (!term_universe_atom_id_capacity_available(universe)) {
        free(payload);
        return CETTA_ATOM_ID_NONE;
    }
    if (encoded_format != term_universe_store_format(universe)) {
        free(payload);
        payload = NULL;
        payload_len = 0;
        if (!term_universe_encode_expr_payload(universe, child_ids, arity32,
                                               &payload, &payload_len))
            return CETTA_ATOM_ID_NONE;
    }
    id = term_universe_insert_new_record(universe, &hdr, payload, payload_len);
    free(payload);
    return id;
}

typedef struct {
    Atom *atom;
    AtomId *child_ids;
    uint32_t next_child;
    uint32_t parent_index;
} TermUniverseStoreFrame;

static bool term_universe_frame_push(TermUniverseStoreFrame **frames,
                                     uint32_t *len, uint32_t *cap,
                                     Atom *atom, uint32_t parent_index) {
    if (*len == *cap) {
        uint32_t next_cap = *cap ? (*cap * 2u) : 64u;
        TermUniverseStoreFrame *next =
            cetta_realloc(*frames, sizeof(TermUniverseStoreFrame) * next_cap);
        if (!next)
            return false;
        *frames = next;
        *cap = next_cap;
    }
    (*frames)[(*len)++] = (TermUniverseStoreFrame){
        .atom = atom,
        .child_ids = NULL,
        .next_child = 0,
        .parent_index = parent_index,
    };
    return true;
}

static AtomId term_universe_leaf_id(TermUniverse *universe, Atom *src,
                                    bool insert) {
    if (!universe || !src)
        return CETTA_ATOM_ID_NONE;
    if (!insert)
        return term_universe_lookup_stable_id(universe, src);
    switch (src->kind) {
    case ATOM_SYMBOL:
        return tu_intern_symbol(universe, src->sym_id);
    case ATOM_VAR:
        if (src->name_key) {
            AtomId key_id = term_universe_store_atom_id(
                universe, universe->persistent_arena, src->name_key);
            return key_id == CETTA_ATOM_ID_NONE
                       ? CETTA_ATOM_ID_NONE
                       : tu_intern_named_var(universe, key_id, src->var_id);
        }
        return tu_intern_var(universe, src->sym_id, src->var_id);
    case ATOM_GROUNDED:
        switch (src->ground.gkind) {
        case GV_INT:
            return tu_intern_int(universe, src->ground.ival);
        case GV_FLOAT:
            return tu_intern_float(universe, src->ground.fval);
        case GV_BOOL:
            return tu_intern_bool(universe, src->ground.bval);
        case GV_STRING:
            return tu_intern_string(universe, src->ground.sval);
        case GV_BIGINT:
            return tu_intern_bigint(universe, atom_bigint_cstr(src));
        case GV_RATIONAL:
            return tu_intern_rational(universe, atom_rational_cstr(src));
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            return CETTA_ATOM_ID_NONE;
        }
        return CETTA_ATOM_ID_NONE;
    case ATOM_EXPR:
        return CETTA_ATOM_ID_NONE;
    }
    return CETTA_ATOM_ID_NONE;
}

static AtomId term_universe_expr_id_from_ids(TermUniverse *universe,
                                             const AtomId *child_ids,
                                             CettaExprLen arity, bool insert) {
    uint32_t arity32 = 0;
    if (insert)
        return tu_expr_from_ids(universe, child_ids, arity);
    if (!term_universe_expr_arity_checked(universe, arity, &arity32))
        return CETTA_ATOM_ID_NONE;
    if (!universe || (arity > 0 && !child_ids)) {
        return CETTA_ATOM_ID_NONE;
    }

    bool has_vars = false;
    SymbolId head_sym = SYMBOL_ID_NONE;
    for (uint32_t i = 0; i < arity32; i++) {
        const CettaTermHdr *child_hdr;
        if (child_ids[i] == CETTA_ATOM_ID_NONE)
            return CETTA_ATOM_ID_NONE;
        child_hdr = tu_hdr(universe, child_ids[i]);
        if (!child_hdr)
            return CETTA_ATOM_ID_NONE;
        if (term_universe_hdr_has_vars(child_hdr))
            has_vars = true;
    }
    if (arity > 0 && tu_kind(universe, child_ids[0]) == ATOM_SYMBOL)
        head_sym = tu_sym(universe, child_ids[0]);

    CettaTermHdr hdr = {0};
    hdr.tag = (uint8_t)ATOM_EXPR;
    hdr.arity_or_len = arity32 > UINT16_MAX ? UINT16_MAX : (uint16_t)arity32;
    hdr.sym_or_head = head_sym;
    hdr.aux32 = term_universe_aux_make(arity32, has_vars);
    hdr.hash32 = term_universe_hash_expr_ids(universe, child_ids, arity32);
    uint8_t *payload = NULL;
    size_t payload_len = 0;
    AtomId id = CETTA_ATOM_ID_NONE;
    if (!term_universe_encode_expr_payload(universe, child_ids, arity32,
                                           &payload, &payload_len))
        return CETTA_ATOM_ID_NONE;
    id = term_universe_lookup_record_id(universe, &hdr, payload, payload_len);
    free(payload);
    return id;
}

static AtomId term_universe_stable_atom_id_iterative(TermUniverse *universe,
                                                     Atom *src, bool insert,
                                                     bool *out_stable) {
    TermUniverseStoreFrame *frames = NULL;
    uint32_t len = 0;
    uint32_t cap = 0;
    AtomId result = CETTA_ATOM_ID_NONE;
    bool stable = false;

    if (out_stable)
        *out_stable = false;
    if (!universe || !src)
        goto done;

    AtomId existing_ptr = term_universe_lookup_ptr_id(universe, src);
    if (existing_ptr != CETTA_ATOM_ID_NONE) {
        stable = true;
        result = existing_ptr;
        goto done;
    }

    if (!term_universe_frame_push(&frames, &len, &cap, src, UINT32_MAX))
        goto done;

    while (len > 0) {
        TermUniverseStoreFrame *frame = &frames[len - 1];
        Atom *atom = frame->atom;
        AtomId id = CETTA_ATOM_ID_NONE;

        if (!atom)
            goto done;

        if (atom->kind == ATOM_EXPR &&
            !cetta_expr_len_fits_u32(atom->expr.len)) {
            term_universe_set_error(universe,
                                    TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE);
            goto done;
        }

        if (atom->kind != ATOM_EXPR) {
            id = term_universe_leaf_id(universe, atom, insert);
            if (id == CETTA_ATOM_ID_NONE)
                goto done;
            uint32_t parent_index = frame->parent_index;
            free(frame->child_ids);
            len--;
            if (len == 0) {
                result = id;
                stable = true;
                goto done;
            }
            frames[len - 1].child_ids[parent_index] = id;
            continue;
        }

        if (!frame->child_ids && atom->expr.len > 0) {
            frame->child_ids =
                cetta_malloc(sizeof(AtomId) * (size_t)atom->expr.len);
            if (!frame->child_ids)
                goto done;
        }

        if (frame->next_child < atom->expr.len) {
            uint32_t child_index = frame->next_child++;
            if (!term_universe_frame_push(&frames, &len, &cap,
                                          atom->expr.elems[child_index],
                                          child_index)) {
                goto done;
            }
            continue;
        }

        id = term_universe_expr_id_from_ids(universe, frame->child_ids,
                                            atom->expr.len, insert);
        if (id == CETTA_ATOM_ID_NONE)
            goto done;
        uint32_t parent_index = frame->parent_index;
        free(frame->child_ids);
        len--;
        if (len == 0) {
            result = id;
            stable = true;
            goto done;
        }
        frames[len - 1].child_ids[parent_index] = id;
    }

done:
    for (uint32_t i = 0; i < len; i++)
        free(frames[i].child_ids);
    free(frames);
    if (out_stable)
        *out_stable = stable;
    return result;
}

static AtomId term_universe_stable_atom_id_iterative_source_memo(
    TermUniverse *universe, Atom *src, bool insert, bool *out_stable,
    TermUniverseSourceMemoRun *source_memo) {
    TermUniverseStoreFrame *frames = NULL;
    uint32_t len = 0;
    uint32_t cap = 0;
    AtomId result = CETTA_ATOM_ID_NONE;
    bool stable = false;

    if (out_stable)
        *out_stable = false;
    if (!universe || !src)
        goto done;

    AtomId existing_ptr = term_universe_lookup_ptr_id(universe, src);
    if (existing_ptr != CETTA_ATOM_ID_NONE) {
        stable = true;
        result = existing_ptr;
        goto done;
    }
    if (term_universe_source_memo_lookup(source_memo, src, &result)) {
        stable = true;
        goto done;
    }

    if (!term_universe_frame_push(&frames, &len, &cap, src, UINT32_MAX))
        goto done;

    while (len > 0) {
        TermUniverseStoreFrame *frame = &frames[len - 1];
        Atom *atom = frame->atom;
        AtomId id = CETTA_ATOM_ID_NONE;

        if (!atom)
            goto done;

        if (atom->kind == ATOM_EXPR &&
            !cetta_expr_len_fits_u32(atom->expr.len)) {
            term_universe_set_error(universe,
                                    TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE);
            goto done;
        }

        if (atom->kind != ATOM_EXPR) {
            id = term_universe_leaf_id(universe, atom, insert);
            if (id == CETTA_ATOM_ID_NONE)
                goto done;
            term_universe_source_memo_store(source_memo, atom, id);
            uint32_t parent_index = frame->parent_index;
            free(frame->child_ids);
            len--;
            if (len == 0) {
                result = id;
                stable = true;
                goto done;
            }
            frames[len - 1].child_ids[parent_index] = id;
            continue;
        }

        if (!frame->child_ids && atom->expr.len > 0) {
            frame->child_ids =
                cetta_malloc(sizeof(AtomId) * (size_t)atom->expr.len);
            if (!frame->child_ids)
                goto done;
        }

        if (frame->next_child < atom->expr.len) {
            uint32_t child_index = frame->next_child++;
            Atom *child = atom->expr.elems[child_index];
            AtomId child_id = CETTA_ATOM_ID_NONE;
            bool source_hit = false;
            if (source_memo && source_memo->memo) {
                child_id = term_universe_lookup_ptr_id(universe, child);
                if (child_id == CETTA_ATOM_ID_NONE) {
                    source_hit = term_universe_source_memo_lookup(
                        source_memo, child, &child_id);
                }
            }
            if (child_id != CETTA_ATOM_ID_NONE) {
                if (!source_hit)
                    term_universe_source_memo_store(
                        source_memo, child, child_id);
                frame->child_ids[child_index] = child_id;
                continue;
            }
            if (!term_universe_frame_push(&frames, &len, &cap,
                                          child,
                                          child_index)) {
                goto done;
            }
            continue;
        }

        id = term_universe_expr_id_from_ids(universe, frame->child_ids,
                                            atom->expr.len, insert);
        if (id == CETTA_ATOM_ID_NONE)
            goto done;
        term_universe_source_memo_store(source_memo, atom, id);
        uint32_t parent_index = frame->parent_index;
        free(frame->child_ids);
        len--;
        if (len == 0) {
            result = id;
            stable = true;
            goto done;
        }
        frames[len - 1].child_ids[parent_index] = id;
    }

done:
    for (uint32_t i = 0; i < len; i++)
        free(frames[i].child_ids);
    free(frames);
    if (out_stable)
        *out_stable = stable;
    return result;
}

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} TermUniverseStringBuilder;

static void term_universe_sb_reserve(TermUniverseStringBuilder *sb,
                                     size_t needed) {
    if (needed <= sb->cap)
        return;
    size_t next_cap = sb->cap ? sb->cap : 64u;
    while (next_cap < needed)
        next_cap *= 2u;
    sb->buf = cetta_realloc(sb->buf, next_cap);
    sb->cap = next_cap;
}

static void term_universe_sb_append_span(TermUniverseStringBuilder *sb,
                                         const char *text, size_t len) {
    if (!text || len == 0)
        return;
    term_universe_sb_reserve(sb, sb->len + len + 1u);
    memcpy(sb->buf + sb->len, text, len);
    sb->len += len;
    sb->buf[sb->len] = '\0';
}

static void term_universe_sb_append_cstr(TermUniverseStringBuilder *sb,
                                         const char *text) {
    term_universe_sb_append_span(sb, text, text ? strlen(text) : 0u);
}

static void term_universe_sb_append_char(TermUniverseStringBuilder *sb,
                                         char ch) {
    term_universe_sb_reserve(sb, sb->len + 2u);
    sb->buf[sb->len++] = ch;
    sb->buf[sb->len] = '\0';
}

static char *term_universe_sb_finish(TermUniverseStringBuilder *sb, Arena *dst) {
    char *out = arena_strdup(dst, sb->buf ? sb->buf : "");
    free(sb->buf);
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
    return out;
}

static void term_universe_sb_append_escaped_string(TermUniverseStringBuilder *sb,
                                                   const char *text) {
    term_universe_sb_append_char(sb, '"');
    for (const char *p = text; p && *p; p++) {
        switch (*p) {
        case '\n':
            term_universe_sb_append_cstr(sb, "\\n");
            break;
        case '"':
            term_universe_sb_append_cstr(sb, "\\\"");
            break;
        case '\\':
            term_universe_sb_append_cstr(sb, "\\\\");
            break;
        default:
            term_universe_sb_append_char(sb, *p);
            break;
        }
    }
    term_universe_sb_append_char(sb, '"');
}

static void term_universe_sb_append_atom_text(TermUniverseStringBuilder *sb,
                                              const TermUniverse *universe,
                                              AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!hdr) {
        Atom *atom = term_universe_get_atom(universe, id);
        Arena scratch;
        arena_init(&scratch);
        char *text = atom_to_parseable_string(&scratch, atom);
        term_universe_sb_append_cstr(sb, text);
        arena_free(&scratch);
        return;
    }

    switch ((AtomKind)hdr->tag) {
    case ATOM_SYMBOL:
        term_universe_sb_append_cstr(sb, symbol_bytes(g_symbols, hdr->sym_or_head));
        return;
    case ATOM_VAR: {
        char suffix_buf[16];
        VarId var_id = tu_var_id(universe, id);
        uint32_t epoch = var_epoch_suffix(var_id);
        if (hdr->subtag == CETTA_VAR_SPELLING_NAME_KEY) {
            term_universe_sb_append_cstr(sb, "$@");
            term_universe_sb_append_atom_text(
                sb, universe, tu_var_name_key_id(universe, id));
        } else {
            term_universe_sb_append_char(sb, '$');
            term_universe_sb_append_cstr(
                sb, symbol_bytes(g_symbols, hdr->sym_or_head));
        }
        if (epoch != 0) {
            int printed = snprintf(suffix_buf, sizeof(suffix_buf), "#%u", epoch);
            if (printed > 0)
                term_universe_sb_append_span(sb, suffix_buf, (size_t)printed);
        }
        return;
    }
    case ATOM_GROUNDED:
        switch ((GroundedKind)hdr->subtag) {
        case GV_INT: {
            char buf[32];
            int printed = snprintf(buf, sizeof(buf), "%" PRId64, tu_int(universe, id));
            if (printed > 0)
                term_universe_sb_append_span(sb, buf, (size_t)printed);
            return;
        }
        case GV_FLOAT: {
            char buf[64];
            int printed = cetta_format_float(buf, sizeof(buf),
                                             tu_float(universe, id));
            if (printed > 0)
                term_universe_sb_append_span(sb, buf, (size_t)printed);
            return;
        }
        case GV_BOOL:
            term_universe_sb_append_cstr(sb, tu_bool(universe, id) ? "True" : "False");
            return;
        case GV_STRING:
            term_universe_sb_append_escaped_string(sb, tu_string_cstr(universe, id));
            return;
        case GV_BIGINT:
            term_universe_sb_append_cstr(sb, tu_bigint_cstr(universe, id));
            return;
        case GV_RATIONAL:
            term_universe_sb_append_cstr(sb, tu_rational_cstr(universe, id));
            return;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            return;
        }
        return;
    case ATOM_EXPR:
        term_universe_sb_append_char(sb, '(');
        CettaExprLen len = tu_arity(universe, id);
        for (CettaExprIndex i = 0; i < len; i++) {
            if (i != 0)
                term_universe_sb_append_char(sb, ' ');
            term_universe_sb_append_atom_text(sb, universe, tu_child(universe, id, i));
        }
        term_universe_sb_append_char(sb, ')');
        return;
    }
}

static void term_universe_copy_memo_clear_slots(TermUniverseCopyMemoSlot *slots,
                                                size_t cap) {
    for (size_t i = 0; i < cap; i++) {
        slots[i].id = CETTA_ATOM_ID_NONE;
        slots[i].atom = NULL;
    }
}

static void term_universe_copy_memo_init(TermUniverseCopyMemo *memo) {
    if (!memo)
        return;
    memo->slots = memo->inline_slots;
    memo->cap = CETTA_TERM_UNIVERSE_COPY_MEMO_INLINE_CAP;
    memo->used = 0;
    term_universe_copy_memo_clear_slots(memo->slots, memo->cap);
}

static void term_universe_copy_memo_free(TermUniverseCopyMemo *memo) {
    if (!memo)
        return;
    if (memo->slots && memo->slots != memo->inline_slots)
        free(memo->slots);
    memo->slots = memo->inline_slots;
    memo->cap = CETTA_TERM_UNIVERSE_COPY_MEMO_INLINE_CAP;
    memo->used = 0;
}

static size_t term_universe_copy_memo_hash(AtomId id, size_t cap) {
    uint64_t h = (uint64_t)id;
    h ^= h >> 33;
    h *= UINT64_C(0xff51afd7ed558ccd);
    h ^= h >> 33;
    h *= UINT64_C(0xc4ceb9fe1a85ec53);
    h ^= h >> 33;
    return (size_t)h & (cap - 1u);
}

static Atom *term_universe_copy_memo_lookup(TermUniverseCopyMemo *memo,
                                            AtomId id) {
    if (!memo || !memo->slots || id == CETTA_ATOM_ID_NONE)
        return NULL;
    size_t idx = term_universe_copy_memo_hash(id, memo->cap);
    for (size_t probe = 0; probe < memo->cap; probe++) {
        TermUniverseCopyMemoSlot *slot =
            &memo->slots[(idx + probe) & (memo->cap - 1u)];
        if (slot->id == id)
            return slot->atom;
        if (slot->id == CETTA_ATOM_ID_NONE)
            return NULL;
    }
    return NULL;
}

static bool term_universe_copy_memo_insert_slot(TermUniverseCopyMemoSlot *slots,
                                                size_t cap,
                                                AtomId id,
                                                Atom *atom) {
    size_t idx = term_universe_copy_memo_hash(id, cap);
    for (size_t probe = 0; probe < cap; probe++) {
        TermUniverseCopyMemoSlot *slot = &slots[(idx + probe) & (cap - 1u)];
        if (slot->id == id) {
            slot->atom = atom;
            return true;
        }
        if (slot->id == CETTA_ATOM_ID_NONE) {
            slot->id = id;
            slot->atom = atom;
            return true;
        }
    }
    return false;
}

static bool term_universe_copy_memo_grow(TermUniverseCopyMemo *memo) {
    if (!memo || memo->cap > SIZE_MAX / 2u)
        return false;
    size_t next_cap = memo->cap * 2u;
    if (next_cap > SIZE_MAX / sizeof(TermUniverseCopyMemoSlot))
        return false;
    size_t bytes = sizeof(TermUniverseCopyMemoSlot) * next_cap;
    TermUniverseCopyMemoSlot *next = cetta_malloc(bytes);
    term_universe_copy_memo_clear_slots(next, next_cap);
    for (size_t i = 0; i < memo->cap; i++) {
        if (memo->slots[i].id == CETTA_ATOM_ID_NONE)
            continue;
        if (!term_universe_copy_memo_insert_slot(next, next_cap,
                                                 memo->slots[i].id,
                                                 memo->slots[i].atom)) {
            free(next);
            return false;
        }
    }
    if (memo->slots != memo->inline_slots)
        free(memo->slots);
    memo->slots = next;
    memo->cap = next_cap;
    cetta_runtime_stats_add(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_COPY_MEMO_HEAP_BYTES,
                            (uint64_t)bytes);
    return true;
}

static bool term_universe_copy_memo_store(TermUniverseCopyMemo *memo,
                                          AtomId id,
                                          Atom *atom) {
    if (!memo || id == CETTA_ATOM_ID_NONE || !atom)
        return true;
    if ((memo->used + 1u) * 4u >= memo->cap * 3u &&
        !term_universe_copy_memo_grow(memo)) {
        return false;
    }
    if (!term_universe_copy_memo_insert_slot(memo->slots, memo->cap, id, atom))
        return false;
    memo->used++;
    return true;
}

static size_t term_universe_copy_estimated_arena_bytes(
    const TermUniverse *universe,
    AtomId id,
    const CettaTermHdr *hdr) {
    size_t bytes = sizeof(Atom);
    if (!universe || id == CETTA_ATOM_ID_NONE)
        return 0;
    if (!hdr) {
        Atom *stored = term_universe_get_atom(universe, id);
        if (stored && stored->kind == ATOM_EXPR &&
            cetta_expr_len_mul_fits_size(stored->expr.len, sizeof(Atom *))) {
            bytes += sizeof(Atom *) * (size_t)stored->expr.len;
        }
        return bytes;
    }
    if ((AtomKind)hdr->tag == ATOM_EXPR) {
        CettaExprLen len = tu_arity(universe, id);
        if (cetta_expr_len_mul_fits_size(len, sizeof(Atom *))) {
            size_t ptr_bytes = sizeof(Atom *) * (size_t)len;
            if (SIZE_MAX - bytes >= ptr_bytes)
                bytes += ptr_bytes;
            if (SIZE_MAX - bytes >= ptr_bytes)
                bytes += ptr_bytes;
        }
    } else if ((AtomKind)hdr->tag == ATOM_GROUNDED) {
        const char *text = NULL;
        switch (tu_ground_kind(universe, id)) {
        case GV_STRING:
            text = tu_string_cstr(universe, id);
            break;
        case GV_BIGINT:
            text = tu_bigint_cstr(universe, id);
            break;
        case GV_RATIONAL:
            text = tu_rational_cstr(universe, id);
            break;
        default:
            break;
        }
        if (text)
            bytes += strlen(text) + 1u;
    }
    return bytes;
}

static Atom *term_universe_copy_atom_impl(const TermUniverse *universe,
                                          Arena *dst, AtomId id,
                                          uint32_t epoch,
                                          bool rename_epoch_vars,
                                          TermUniverseCopyMemo *memo) {
    Atom *out = NULL;
    if (!universe || !dst || id == CETTA_ATOM_ID_NONE)
        return NULL;
    Atom *memoized = term_universe_copy_memo_lookup(memo, id);
    if (memoized) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_COPY_MEMO_HIT);
        return memoized;
    }

    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!hdr) {
        Atom *stored = term_universe_get_atom(universe, id);
        if (!stored)
            return NULL;
        out = rename_epoch_vars ? atom_freshen_epoch(dst, stored, epoch)
                                : atom_deep_copy(dst, stored);
        goto done;
    }

    switch ((AtomKind)hdr->tag) {
    case ATOM_SYMBOL:
        out = atom_symbol_id(dst, hdr->sym_or_head);
        break;
    case ATOM_VAR: {
        VarId var_id = tu_var_id(universe, id);
        if (rename_epoch_vars)
            var_id = var_epoch_id(var_id, epoch);
        if (hdr->subtag == CETTA_VAR_SPELLING_NAME_KEY) {
            Atom *key = term_universe_copy_atom_impl(
                universe, dst, tu_var_name_key_id(universe, id), epoch, false,
                memo);
            out = key ? atom_var_with_name_key(dst, key, var_id) : NULL;
        } else {
            out = atom_var_with_spelling(dst, hdr->sym_or_head, var_id);
        }
        break;
    }
    case ATOM_GROUNDED:
        switch (tu_ground_kind(universe, id)) {
        case GV_INT:
            out = atom_int(dst, tu_int(universe, id));
            break;
        case GV_FLOAT:
            out = atom_float(dst, tu_float(universe, id));
            break;
        case GV_BOOL:
            out = atom_bool(dst, tu_bool(universe, id));
            break;
        case GV_STRING:
            out = atom_string(dst, tu_string_cstr(universe, id));
            break;
        case GV_BIGINT:
            out = atom_bigint(dst, tu_bigint_cstr(universe, id));
            break;
        case GV_RATIONAL:
            out = atom_rational(dst, tu_rational_cstr(universe, id));
            break;
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            return NULL;
        }
        break;
    case ATOM_EXPR: {
        CettaExprLen len = tu_arity(universe, id);
        Atom **elems = NULL;
        if (len > 0) {
            if (!cetta_expr_len_mul_fits_size(len, sizeof(Atom *)))
                return NULL;
            elems = arena_alloc(dst, sizeof(Atom *) * (size_t)len);
        }
        for (CettaExprIndex i = 0; i < len; i++) {
            elems[i] = term_universe_copy_atom_impl(
                universe, dst, tu_child(universe, id, i), epoch,
                rename_epoch_vars, memo);
            if (!elems[i])
                return NULL;
        }
        out = atom_expr_shared(dst, elems, len);
        break;
    }
    }

done:
    if (!out)
        return NULL;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_COPY_NODE);
    size_t estimated_arena_bytes =
        term_universe_copy_estimated_arena_bytes(universe, id, hdr);
    cetta_runtime_stats_add(
        CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_COPY_ESTIMATED_ARENA_BYTES,
        (uint64_t)estimated_arena_bytes);
    (void)estimated_arena_bytes;
    if (!term_universe_copy_memo_store(memo, id, out))
        return NULL;
    return out;
}

Atom *term_universe_copy_atom(const TermUniverse *universe, Arena *dst,
                              AtomId id) {
    TermUniverseCopyMemo memo;
    term_universe_copy_memo_init(&memo);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_COPY_CALL);
    Atom *out = term_universe_copy_atom_impl(universe, dst, id, 0u, false, &memo);
    term_universe_copy_memo_free(&memo);
    return out;
}

Atom *term_universe_copy_atom_epoch(const TermUniverse *universe, Arena *dst,
                                    AtomId id, uint32_t epoch) {
    TermUniverseCopyMemo memo;
    term_universe_copy_memo_init(&memo);
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_COPY_CALL);
    Atom *out = term_universe_copy_atom_impl(universe, dst, id, epoch, true, &memo);
    term_universe_copy_memo_free(&memo);
    return out;
}

char *term_universe_atom_to_string(Arena *a, const TermUniverse *universe,
                                   AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!a || !universe || id == CETTA_ATOM_ID_NONE ||
        !term_universe_entry(universe, id))
        return NULL;
    if (!hdr) {
        Atom *atom = term_universe_get_atom(universe, id);
        return atom ? atom_to_string(a, atom) : NULL;
    }
    if (hdr->tag == ATOM_GROUNDED && (GroundedKind)hdr->subtag == GV_STRING)
        return arena_strdup(a, tu_string_cstr(universe, id));

    TermUniverseStringBuilder sb = {0};
    term_universe_sb_append_atom_text(&sb, universe, id);
    return term_universe_sb_finish(&sb, a);
}

char *term_universe_atom_to_parseable_string(Arena *a,
                                             const TermUniverse *universe,
                                             AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!a || !universe || id == CETTA_ATOM_ID_NONE ||
        !term_universe_entry(universe, id))
        return NULL;
    if (!hdr) {
        Atom *atom = term_universe_get_atom(universe, id);
        return atom ? atom_to_parseable_string(a, atom) : NULL;
    }

    TermUniverseStringBuilder sb = {0};
    term_universe_sb_append_atom_text(&sb, universe, id);
    return term_universe_sb_finish(&sb, a);
}

static bool term_universe_intern_reserve(TermUniverse *universe,
                                         size_t min_slots) {
    size_t width = 0;
    if (!universe)
        return false;
    if (universe->intern_slots && universe->intern_mask + 1 >= min_slots)
        return true;
    width = term_universe_slot_storage_width_bytes(universe);
    if (width == 0) {
        term_universe_set_error(universe,
                                TERM_UNIVERSE_ERROR_UNSUPPORTED_STORE_FORMAT);
        return false;
    }
    size_t size = 1024;
    while (size < min_slots) {
        if (size > SIZE_MAX / 2u) {
            term_universe_set_error(universe,
                                    TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE);
            return false;
        }
        size <<= 1;
    }
    if (size > SIZE_MAX / width) {
        term_universe_set_error(universe,
                                TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE);
        return false;
    }
    uint8_t *next = cetta_malloc(width * size);
    if (!next) {
        term_universe_set_error(universe,
                                TERM_UNIVERSE_ERROR_ALLOCATION_FAILED);
        return false;
    }
    memset(next, 0, width * size);
    size_t next_mask = size - 1;
    if (universe->intern_slots) {
        for (size_t i = 0; i <= universe->intern_mask; i++) {
            uint64_t slot = term_universe_load_slot_value(
                universe, universe->intern_slots, i);
            if (slot == 0)
                continue;
            AtomId id = slot - 1;
            uint64_t h = term_universe_entry_slot_hash(universe, id);
            for (size_t probe = 0; probe < size; probe++) {
                size_t idx = ((size_t)h + probe) & next_mask;
                if (term_universe_load_slot_value(universe, next, idx) == 0) {
                    (void)term_universe_store_slot_value(
                        term_universe_store_format(universe), next, idx, slot);
                    break;
                }
            }
        }
        free(universe->intern_slots);
    }
    universe->intern_slots = next;
    universe->intern_mask = next_mask;
    return true;
}

static uint32_t term_universe_ptr_hash(Atom *atom) {
    uintptr_t bits = (uintptr_t)atom;
    bits ^= bits >> 17;
    bits *= UINT64_C(0xed5ad4bb);
    bits ^= bits >> 11;
    return (uint32_t)bits;
}

static bool term_universe_ptr_reserve(TermUniverse *universe,
                                      size_t min_slots) {
    size_t width = 0;
    if (!universe)
        return false;
    if (universe->ptr_slots && universe->ptr_mask + 1 >= min_slots)
        return true;
    width = term_universe_slot_storage_width_bytes(universe);
    if (width == 0) {
        term_universe_set_error(universe,
                                TERM_UNIVERSE_ERROR_UNSUPPORTED_STORE_FORMAT);
        return false;
    }
    size_t size = 1024;
    while (size < min_slots) {
        if (size > SIZE_MAX / 2u) {
            term_universe_set_error(universe,
                                    TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE);
            return false;
        }
        size <<= 1;
    }
    if (size > SIZE_MAX / width) {
        term_universe_set_error(universe,
                                TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE);
        return false;
    }
    uint8_t *next = cetta_malloc(width * size);
    if (!next) {
        term_universe_set_error(universe,
                                TERM_UNIVERSE_ERROR_ALLOCATION_FAILED);
        return false;
    }
    memset(next, 0, width * size);
    size_t next_mask = size - 1;
    if (universe->ptr_slots) {
        for (size_t i = 0; i <= universe->ptr_mask; i++) {
            uint64_t slot =
                term_universe_load_slot_value(universe, universe->ptr_slots, i);
            if (slot == 0)
                continue;
            AtomId id = slot - 1;
            const TermEntry *entry = term_universe_entry(universe, id);
            Atom *atom = entry ? entry->decoded_cache : NULL;
            if (!atom)
                continue;
            uint32_t h = term_universe_ptr_hash(atom);
            for (size_t probe = 0; probe < size; probe++) {
                size_t idx = ((size_t)h + probe) & next_mask;
                if (term_universe_load_slot_value(universe, next, idx) == 0) {
                    (void)term_universe_store_slot_value(
                        term_universe_store_format(universe), next, idx, slot);
                    break;
                }
            }
        }
        free(universe->ptr_slots);
    }
    universe->ptr_slots = next;
    universe->ptr_mask = next_mask;
    return true;
}

static AtomId term_universe_lookup_ptr_id(const TermUniverse *universe,
                                          Atom *src) {
    if (!universe || !universe->ptr_slots || !src)
        return CETTA_ATOM_ID_NONE;
    uint32_t h = term_universe_ptr_hash(src);
    for (size_t probe = 0; probe <= universe->ptr_mask; probe++) {
        size_t idx = ((size_t)h + probe) & universe->ptr_mask;
        uint64_t slot =
            term_universe_load_slot_value(universe, universe->ptr_slots, idx);
        if (slot == 0)
            return CETTA_ATOM_ID_NONE;
        AtomId id = slot - 1;
        const TermEntry *entry = term_universe_entry(universe, id);
        if (entry && entry->decoded_cache == src)
            return id;
    }
    return CETTA_ATOM_ID_NONE;
}

static bool term_universe_insert_ptr_id(TermUniverse *universe, AtomId id) {
    if (!universe || !universe->ptr_slots || !term_universe_entry(universe, id))
        return false;
    const TermEntry *entry = term_universe_entry(universe, id);
    Atom *atom = entry ? entry->decoded_cache : NULL;
    if (!atom)
        return false;
    uint32_t h = term_universe_ptr_hash(atom);
    for (size_t probe = 0; probe <= universe->ptr_mask; probe++) {
        size_t idx = ((size_t)h + probe) & universe->ptr_mask;
        uint64_t slot =
            term_universe_load_slot_value(universe, universe->ptr_slots, idx);
        if (slot == 0) {
            if (!term_universe_store_slot_value(term_universe_store_format(universe),
                                                universe->ptr_slots, idx,
                                                (uint64_t)id + 1u))
                return false;
            universe->ptr_used++;
            return true;
        }
        if (slot - 1 == id)
            return true;
    }
    return false;
}

static bool term_universe_entry_eq_atom(const TermUniverse *universe, AtomId id,
                                        Atom *src) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!src)
        return false;
    if (!hdr) {
        const TermEntry *entry = term_universe_entry(universe, id);
        return entry && entry->decoded_cache &&
               atom_eq(entry->decoded_cache, src);
    }

    const uint8_t *payload = term_universe_payload(universe, id);
    switch ((AtomKind)hdr->tag) {
    case ATOM_SYMBOL:
        return src->kind == ATOM_SYMBOL && src->sym_id == hdr->sym_or_head;
    case ATOM_VAR:
        return src->kind == ATOM_VAR &&
               src->var_id == (VarId)term_universe_load_u64(payload);
    case ATOM_GROUNDED:
        if (src->kind != ATOM_GROUNDED ||
            src->ground.gkind != (GroundedKind)hdr->subtag) {
            return false;
        }
        switch ((GroundedKind)hdr->subtag) {
        case GV_INT:
            return src->ground.ival == term_universe_load_i64(payload);
        case GV_FLOAT:
            return src->ground.fval == term_universe_load_double(payload);
        case GV_BOOL:
            return src->ground.bval == (term_universe_aux_data(hdr) != 0);
        case GV_STRING: {
            uint32_t len = term_universe_aux_data(hdr);
            return strlen(src->ground.sval) == len &&
                   memcmp(src->ground.sval, payload, len) == 0;
        }
        case GV_BIGINT: {
            uint32_t len = term_universe_aux_data(hdr);
            const char *text = atom_bigint_cstr(src);
            return strlen(text) == len &&
                   memcmp(text, payload, len) == 0;
        }
        case GV_RATIONAL: {
            uint32_t len = term_universe_aux_data(hdr);
            const char *text = atom_rational_cstr(src);
            return strlen(text) == len &&
                   memcmp(text, payload, len) == 0;
        }
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            return false;
        }
        return false;
    case ATOM_EXPR: {
        uint32_t len = term_universe_aux_data(hdr);
        if (src->kind != ATOM_EXPR || src->expr.len != len)
            return false;
        if (atom_head_symbol_id(src) != hdr->sym_or_head)
            return false;
        size_t atom_id_width = term_universe_atom_id_storage_width_bytes(universe);
        for (uint32_t i = 0; i < len; i++) {
            AtomId child_id = term_universe_load_stored_atom_id(
                universe, payload + ((size_t)i * atom_id_width));
            if (!term_universe_entry_eq_atom(universe, child_id,
                                             src->expr.elems[i])) {
                return false;
            }
        }
        return true;
    }
    }
    return false;
}

static AtomId term_universe_lookup_stable_id(const TermUniverse *universe,
                                             Atom *src) {
    if (!universe || !universe->intern_slots || !src)
        return CETTA_ATOM_ID_NONE;
    if (src->kind == ATOM_EXPR) {
        bool stable = false;
        AtomId id = term_universe_stable_atom_id_iterative(
            (TermUniverse *)universe, src, false, &stable);
        return stable ? id : CETTA_ATOM_ID_NONE;
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LOOKUP);
    TU_DIAG_INC((TermUniverse *)universe, legacy_hash_recompute_count);
    uint32_t h = atom_hash(src);
    uint64_t hslot = term_universe_atom_slot_hash(src);
    for (size_t probe = 0; probe <= universe->intern_mask; probe++) {
        size_t idx = ((size_t)hslot + probe) & universe->intern_mask;
        uint64_t slot = term_universe_load_slot_value(
            universe, universe->intern_slots, idx);
        if (slot == 0)
            return CETTA_ATOM_ID_NONE;
        AtomId id = slot - 1;
        const CettaTermHdr *hdr = tu_hdr(universe, id);
        if (term_universe_entry(universe, id) && hdr && hdr->hash32 == h &&
            term_universe_entry_eq_atom(universe, id, src)) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_HIT);
            return id;
        }
    }
    return CETTA_ATOM_ID_NONE;
}

AtomId term_universe_lookup_atom_id(const TermUniverse *universe, Atom *src) {
    if (!universe || !src)
        return CETTA_ATOM_ID_NONE;
    AtomId existing_ptr = term_universe_lookup_ptr_id(universe, src);
    if (existing_ptr != CETTA_ATOM_ID_NONE)
        return existing_ptr;
    if (!term_universe_atom_is_stable(src))
        return CETTA_ATOM_ID_NONE;
    bool stable = false;
    AtomId stable_id =
        term_universe_stable_atom_id_iterative((TermUniverse *)universe, src,
                                               false, &stable);
    if (!stable)
        return CETTA_ATOM_ID_NONE;
    return stable_id;
}

bool term_universe_atom_id_eq(const TermUniverse *universe, AtomId id,
                              Atom *src) {
    if (!universe || id == CETTA_ATOM_ID_NONE || !src)
        return false;
    return term_universe_entry_eq_atom(universe, id, src);
}

static bool term_universe_insert_stable_id(TermUniverse *universe, AtomId id) {
    if (!universe || !universe->intern_slots || !term_universe_entry(universe, id))
        return false;
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (!hdr)
        return false;
    uint64_t h = term_universe_entry_slot_hash(universe, id);
    for (size_t probe = 0; probe <= universe->intern_mask; probe++) {
        size_t idx = ((size_t)h + probe) & universe->intern_mask;
        uint64_t slot = term_universe_load_slot_value(
            universe, universe->intern_slots, idx);
        if (slot == 0) {
            if (!term_universe_store_slot_value(term_universe_store_format(universe),
                                                universe->intern_slots, idx,
                                                (uint64_t)id + 1u))
                return false;
            universe->intern_used++;
            return true;
        }
        if (slot - 1 == id)
            return true;
    }
    return false;
}

static Atom *term_universe_store_persistent_atom(TermUniverse *universe,
                                                 Atom *src) {
    Arena *dst = universe ? universe->persistent_arena : NULL;
    if (!dst || !src)
        return NULL;
    if (term_universe_atom_contains_epoch_var(src))
        return term_universe_canonicalize_atom(dst, src);
    if (term_universe_atom_is_stable(src))
        return atom_deep_copy_shared(dst, src);
    return atom_deep_copy(dst, src);
}

static void term_universe_track_ptr_id(TermUniverse *universe, AtomId id) {
    const TermEntry *entry = term_universe_entry(universe, id);
    Atom *atom = entry ? entry->decoded_cache : NULL;
    if (!atom)
        return;
    if (term_universe_lookup_ptr_id(universe, atom) != CETTA_ATOM_ID_NONE)
        return;
    size_t needed = universe->ptr_slots ? (universe->ptr_mask + 1) : 0;
    if (needed == 0 || (universe->ptr_used + 1) * 10 > needed * 7) {
        size_t min_slots = 0;
        if (!term_universe_double_request(universe, needed, 1024,
                                          &min_slots) ||
            !term_universe_ptr_reserve(universe, min_slots))
            return;
    }
    (void)term_universe_insert_ptr_id(universe, id);
}

static AtomId term_universe_store_prepared_atom_id(TermUniverse *universe,
                                                   Atom *src) {
    if (!universe || !src)
        return CETTA_ATOM_ID_NONE;

    AtomId existing_ptr = term_universe_lookup_ptr_id(universe, src);
    if (existing_ptr != CETTA_ATOM_ID_NONE)
        return existing_ptr;

    bool stable = term_universe_atom_is_stable(src);
    if (!stable) {
        if (!term_universe_atom_id_capacity_available(universe))
            return CETTA_ATOM_ID_NONE;
        if (!term_universe_reserve_entries(universe, universe->len + 1))
            return CETTA_ATOM_ID_NONE;
        TermEntry entry = {
            .byte_off = CETTA_TERM_ENTRY_BLOB_NONE,
            .byte_len = 0,
            .decoded_cache = NULL,
        };
        entry.decoded_cache = term_universe_store_persistent_atom(universe, src);
        if (!entry.decoded_cache)
            return CETTA_ATOM_ID_NONE;
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_FALLBACK_ENTRY);
        size_t physical_index = universe->len;
        AtomId id = term_universe_physical_index_to_atom_id(universe,
                                                            physical_index);
        if (id == CETTA_ATOM_ID_NONE)
            return CETTA_ATOM_ID_NONE;
        universe->len++;
        universe->entries[physical_index] = entry;
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_INSERT);
        term_universe_track_ptr_id(universe, id);
        return id;
    }

    switch (src->kind) {
    case ATOM_SYMBOL:
        return tu_intern_symbol(universe, src->sym_id);
    case ATOM_VAR:
        if (src->name_key) {
            AtomId key_id = term_universe_store_atom_id(
                universe, universe->persistent_arena, src->name_key);
            return key_id == CETTA_ATOM_ID_NONE
                       ? CETTA_ATOM_ID_NONE
                       : tu_intern_named_var(universe, key_id, src->var_id);
        }
        return tu_intern_var(universe, src->sym_id, src->var_id);
    case ATOM_GROUNDED:
        switch (src->ground.gkind) {
        case GV_INT:
            return tu_intern_int(universe, src->ground.ival);
        case GV_FLOAT:
            return tu_intern_float(universe, src->ground.fval);
        case GV_BOOL:
            return tu_intern_bool(universe, src->ground.bval);
        case GV_STRING:
            return tu_intern_string(universe, src->ground.sval);
        case GV_BIGINT:
            return tu_intern_bigint(universe, atom_bigint_cstr(src));
        case GV_RATIONAL:
            return tu_intern_rational(universe, atom_rational_cstr(src));
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            /* Filtered by the stable-grounded check above; keep the switch
               exhaustive for -Wswitch cleanliness. */
            return CETTA_ATOM_ID_NONE;
        }
        return CETTA_ATOM_ID_NONE;
    case ATOM_EXPR: {
        AtomId *child_ids = NULL;
        AtomId id = CETTA_ATOM_ID_NONE;
        if (!cetta_expr_len_fits_u32(src->expr.len)) {
            term_universe_set_error(universe,
                                    TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE);
            return CETTA_ATOM_ID_NONE;
        }
        if (src->expr.len != 0) {
            child_ids = cetta_malloc(sizeof(AtomId) * (size_t)src->expr.len);
            if (!child_ids)
                return CETTA_ATOM_ID_NONE;
            for (CettaExprIndex i = 0; i < src->expr.len; i++) {
                child_ids[i] = term_universe_store_prepared_atom_id(
                    universe, src->expr.elems[i]);
                if (child_ids[i] == CETTA_ATOM_ID_NONE) {
                    free(child_ids);
                    return CETTA_ATOM_ID_NONE;
                }
            }
        }
        id = tu_expr_from_ids(universe, child_ids, src->expr.len);
        free(child_ids);
        return id;
    }
    }
    return CETTA_ATOM_ID_NONE;
}

static AtomId term_universe_store_atom_id_impl(
    TermUniverse *universe, Arena *fallback, const Arena *source_arena,
    Atom *src) {
    TermUniverseSourceMemoRun source_memo = {0};
    AtomId result = CETTA_ATOM_ID_NONE;
    Arena canonical_scratch;
    bool have_scratch = false;
    if (!src)
        goto done;
    if (!universe || !universe->persistent_arena) {
        (void)fallback;
        goto done;
    }
    (void)fallback;
    term_universe_clear_error(universe);
    source_memo =
        term_universe_source_memo_begin(universe, source_arena);

    Atom *lookup = src;
    if (term_universe_atom_contains_epoch_var(src)) {
        arena_init(&canonical_scratch);
        arena_set_hashcons(&canonical_scratch, NULL);
        lookup = term_universe_canonicalize_atom(&canonical_scratch, src);
        have_scratch = true;
    }
    AtomId existing_ptr = term_universe_lookup_ptr_id(universe, lookup);
    if (existing_ptr != CETTA_ATOM_ID_NONE) {
        result = existing_ptr;
        goto done;
    }
    if (term_universe_atom_is_stable(lookup)) {
        bool stable = false;
        AtomId stable_id = source_memo.memo
            ? term_universe_stable_atom_id_iterative_source_memo(
                  universe, lookup, true, &stable, &source_memo)
            : term_universe_stable_atom_id_iterative(
                  universe, lookup, true, &stable);
        if (!stable)
            goto done;
        TU_DIAG_INC(universe, legacy_top_down_stable_admissions);
        result = stable_id;
        goto done;
    }

    result = term_universe_store_prepared_atom_id(universe, lookup);

done:
    if (have_scratch)
        arena_free(&canonical_scratch);
    term_universe_source_memo_finish(&source_memo);
    return result;
}

AtomId term_universe_store_atom_id(TermUniverse *universe, Arena *fallback,
                                   Atom *src) {
    return term_universe_store_atom_id_impl(universe, fallback, NULL, src);
}

AtomId term_universe_store_atom_id_from_source_arena(
    TermUniverse *universe, Arena *fallback, const Arena *source_arena,
    Atom *src) {
    return term_universe_store_atom_id_impl(
        universe, fallback, source_arena, src);
}

static Atom *term_universe_decode_atom(TermUniverse *universe, AtomId id) {
    Arena *dst = universe ? universe->persistent_arena : NULL;
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    const uint8_t *payload = term_universe_payload(universe, id);
    if (!hdr || !payload || !dst)
        return NULL;

    switch ((AtomKind)hdr->tag) {
    case ATOM_SYMBOL:
        return atom_symbol_id(dst, hdr->sym_or_head);
    case ATOM_VAR:
        if (hdr->subtag == CETTA_VAR_SPELLING_NAME_KEY) {
            Atom *key = term_universe_get_atom(
                universe, tu_var_name_key_id(universe, id));
            return key ? atom_var_with_name_key(
                             dst, key,
                             (VarId)term_universe_load_u64(payload))
                       : NULL;
        }
        return atom_var_with_spelling(
            dst, hdr->sym_or_head, (VarId)term_universe_load_u64(payload));
    case ATOM_GROUNDED:
        switch (tu_ground_kind(universe, id)) {
        case GV_INT:
            return atom_int(dst, term_universe_load_i64(payload));
        case GV_FLOAT:
            return atom_float(dst, term_universe_load_double(payload));
        case GV_BOOL:
            return atom_bool(dst, term_universe_aux_data(hdr) != 0);
        case GV_STRING:
            return atom_string(dst, (const char *)payload);
        case GV_BIGINT:
            return atom_bigint(dst, (const char *)payload);
        case GV_RATIONAL:
            return atom_rational(dst, (const char *)payload);
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_FOREIGN:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            return NULL;
        }
        return NULL;
    case ATOM_EXPR: {
        uint32_t len = term_universe_aux_data(hdr);
        Atom **elems = arena_alloc(dst, sizeof(Atom *) * len);
        size_t atom_id_width = term_universe_atom_id_storage_width_bytes(universe);
        for (uint32_t i = 0; i < len; i++) {
            AtomId child_id = term_universe_load_stored_atom_id(
                universe, payload + ((size_t)i * atom_id_width));
            elems[i] = term_universe_get_atom(universe, child_id);
            if (!elems[i])
                return NULL;
        }
        return atom_expr_shared(dst, elems, len);
    }
    }
    return NULL;
}

Atom *term_universe_get_atom(const TermUniverse *universe, AtomId id) {
    size_t physical_index = 0;
    if (!term_universe_atom_id_to_physical_index(universe, id, &physical_index))
        return NULL;
    TermUniverse *mutable_universe = (TermUniverse *)universe;
    TermEntry *entry = &mutable_universe->entries[physical_index];
    if (entry->decoded_cache)
        return entry->decoded_cache;
    if (!term_universe_entry_has_blob(entry))
        return NULL;
    entry->decoded_cache = term_universe_decode_atom(mutable_universe, id);
    if (entry->decoded_cache) {
        TU_DIAG_INC(mutable_universe, lazy_decode_count);
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE);
    }
    term_universe_track_ptr_id(mutable_universe, id);
    return entry->decoded_cache;
}

Atom *term_universe_store_atom(TermUniverse *universe, Arena *fallback,
                               Atom *src) {
    Arena *persistent = universe ? universe->persistent_arena : NULL;
    if (!src)
        return NULL;
    if (!persistent)
        return fallback ? atom_deep_copy(fallback, src) : NULL;
    AtomId id = term_universe_store_atom_id(universe, fallback, src);
    if (id == CETTA_ATOM_ID_NONE &&
        term_universe_last_error_code(universe) != TERM_UNIVERSE_ERROR_NONE) {
        return NULL;
    }
    Atom *stored = term_universe_get_atom(universe, id);
    if (!stored)
        stored = term_universe_store_persistent_atom(universe, src);
    cetta_provenance_assert_not_transient(stored, "term_universe.store_atom");
    return stored;
}
