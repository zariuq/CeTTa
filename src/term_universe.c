#include "term_universe.h"
#include "match.h"
#include "stats.h"
#include "term_canon.h"
#include "string_literal.h"

#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CETTA_TERM_ENTRY_ALIGN 8u
#define CETTA_TERM_UNIVERSE_COPY_MEMO_INLINE_CAP 64u
#define CETTA_TERM_UNIVERSE_SOURCE_MEMO_CAP (1u << 16)
#define CETTA_TERM_VARIABLE_SUPPORT_BLOCK_SIZE 64u

typedef struct CettaTermVariableSupportBlock {
    _Atomic(CettaTermVariableSupport *)
        slots[CETTA_TERM_VARIABLE_SUPPORT_BLOCK_SIZE];
} CettaTermVariableSupportBlock;

enum { TERM_UNIVERSE_INTEGER_PAGE_BITS = 9,
       TERM_UNIVERSE_INTEGER_PAGE_SIZE = 1u << TERM_UNIVERSE_INTEGER_PAGE_BITS,
       TERM_UNIVERSE_INTEGER_DENSE_AT = 128u };

typedef struct {
    size_t physical_plus_one;
    uint16_t offset;
} CettaIntegerPageEntry;

typedef struct {
    uint16_t len, cap;
    bool dense;
    union {
        CettaIntegerPageEntry *sparse;
        size_t *positions;
    } data;
} CettaIntegerInternPage;

typedef struct CettaIntegerInternSlot {
    uint64_t prefix;
    CettaIntegerInternPage *page;
} CettaIntegerInternSlot;

static void term_universe_integer_free(TermUniverse *universe) {
    if (universe->integer_slots) {
        for (size_t i = 0u; i <= universe->integer_mask; i++) {
            CettaIntegerInternPage *page = universe->integer_slots[i].page;
            if (!page)
                continue;
            if (page->dense)
                free(page->data.positions);
            else
                free(page->data.sparse);
            free(page);
        }
    }
    free(universe->integer_slots);
}

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
/* A miss proves this position is the first vacancy on its probe path.
 * Constructors may reuse it only while layout and occupancy stay unchanged. */
typedef struct {
    size_t slot, mask, used;
    TermUniverseStoreFormat format;
    CettaIntegerInternPage *integer_page;
    uint16_t integer_position, integer_length;
    bool integer_dense;
} TermUniverseVacancy;

static AtomId term_universe_insert_new_record(TermUniverse *universe,
                                              const CettaTermHdr *hdr,
                                              const uint8_t *payload,
                                              size_t payload_len,
                                              uint32_t coordinate,
                                              const TermUniverseVacancy *vacancy);
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

static bool term_universe_atom_has_expression_child(const Atom *atom) {
    if (!atom || atom->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex index = 0u; index < atom->expr.len; index++) {
        if (atom->expr.elems[index] &&
            atom->expr.elems[index]->kind == ATOM_EXPR) {
            return true;
        }
    }
    return false;
}

static TermUniverseSourceMemoRun term_universe_source_memo_begin(
    TermUniverse *universe, const Arena *source_arena,
    const Atom *source_root) {
    TermUniverseSourceMemoRun run = {0};
    if (!universe || !source_arena || source_arena->identity == 0u ||
        !term_universe_atom_has_expression_child(source_root)) {
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
    /* An atom without variables has no epoch variable. */
    if (!atom || !atom_has_vars(atom))
        return false;
    Atom *inline_stack[32];
    Atom **stack = inline_stack;
    uint32_t len = 0;
    uint32_t cap = 32u;
    bool found = false;

#define PUSH_ATOM(candidate) do { \
    if (len == cap) { \
        uint32_t next_cap = cap * 2u; \
        Atom **next_stack = stack == inline_stack \
            ? cetta_malloc(sizeof(Atom *) * next_cap) \
            : cetta_realloc(stack, sizeof(Atom *) * next_cap); \
        if (!next_stack) goto done; \
        if (stack == inline_stack) \
            memcpy(next_stack, inline_stack, sizeof(Atom *) * len); \
        stack = next_stack; \
        cap = next_cap; \
    } \
    stack[len++] = (candidate); \
} while (0)

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
    if (stack != inline_stack)
        free(stack);
#undef PUSH_ATOM
    return found;
}

bool term_universe_atom_is_stable(Atom *atom) {
    Atom **stack = NULL;
    uint32_t len = 0;
    uint32_t cap = 0;
    bool stable = false;

    /* The scalar spelling of a managed native identifier is not its lifetime.
     * Preserve its owner in the existing pointer-backed fallback, never in the
     * pointer-free integer encoding. The fact composes through expressions. */
    if (atom && (atom->structural_facts &
                 ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID) != 0u)
        return false;

    /* The compositional leaf summary answers this for immutable atoms built
     * through the ordinary constructors.  The traversal remains authoritative
     * when that positive proof was not established. */
    if (atom && (atom->flags & ATOM_FLAG_TERM_STABLE) != 0u)
        return true;

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
            if ((cur->structural_facts &
                 ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID) != 0u ||
                !atom_grounded_is_term_stable(cur))
                goto done;
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
    cetta_frame_identity_scope_clear(&universe->frame_identities);
    TermUniverseStoreFormat format = term_universe_store_format_supported(
                                         universe->store_format)
                                         ? universe->store_format
                                         : TERM_UNIVERSE_STORE_FORMAT_COMPACT32_V1;
    for (size_t block_index = 0u;
         block_index < universe->variable_support_block_cap;
         block_index++) {
        CettaTermVariableSupportBlock *block =
            universe->variable_support_blocks
                ? atomic_load_explicit(
                      &universe->variable_support_blocks[block_index],
                      memory_order_relaxed)
                : NULL;
        if (!block)
            continue;
        for (size_t slot = 0u;
             slot < CETTA_TERM_VARIABLE_SUPPORT_BLOCK_SIZE; slot++) {
            free(atomic_load_explicit(
                &block->slots[slot], memory_order_relaxed));
        }
        free(block);
    }
    free(universe->variable_support_blocks);
    free(universe->blob_pool);
    free(universe->entries);
    free(universe->intern_slots);
    term_universe_integer_free(universe);
    free(universe->ptr_slots);
    universe->blob_pool = NULL;
    universe->blob_len = 0;
    universe->blob_cap = 0;
    universe->entries = NULL;
    universe->len = 0;
    universe->cap = 0;
    universe->variable_support_blocks = NULL;
    universe->variable_support_block_cap = 0u;
    universe->intern_slots = NULL;
    universe->intern_mask = 0;
    universe->intern_used = 0;
    universe->integer_slots = NULL;
    universe->integer_mask = 0;
    universe->integer_used = 0;
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
    universe->frame_identities = (CettaFrameIdentityScope){0};
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
    universe->variable_support_blocks = NULL;
    universe->variable_support_block_cap = 0u;
    universe->intern_slots = NULL;
    universe->intern_mask = 0;
    universe->intern_used = 0;
    universe->integer_slots = NULL;
    universe->integer_mask = 0;
    universe->integer_used = 0;
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
            .coordinate = src_entry.coordinate,
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
    size_t next_block_cap =
        (next_cap + CETTA_TERM_VARIABLE_SUPPORT_BLOCK_SIZE - 1u) /
        CETTA_TERM_VARIABLE_SUPPORT_BLOCK_SIZE;
    _Atomic(CettaTermVariableSupportBlock *) *next_blocks =
        cetta_malloc(sizeof(*next_blocks) * next_block_cap);
    if (!next_blocks) {
        term_universe_set_error(universe,
                                TERM_UNIVERSE_ERROR_ALLOCATION_FAILED);
        return false;
    }
    for (size_t i = 0u;
         i < universe->variable_support_block_cap; i++) {
        CettaTermVariableSupportBlock *block =
            universe->variable_support_blocks
            ? atomic_load_explicit(
                  &universe->variable_support_blocks[i],
                  memory_order_relaxed)
            : NULL;
        atomic_init(&next_blocks[i], block);
    }
    for (size_t i = universe->variable_support_block_cap;
         i < next_block_cap; i++) {
        atomic_init(&next_blocks[i], NULL);
    }

    TermEntry *next =
        cetta_realloc(universe->entries, sizeof(TermEntry) * next_cap);
    if (!next) {
        free(next_blocks);
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
    free(universe->variable_support_blocks);
    universe->variable_support_blocks = next_blocks;
    universe->variable_support_block_cap = next_block_cap;
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

const CettaTermHdr *term_universe_hdr_checked(const TermUniverse *universe,
                                              AtomId id) {
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

static bool term_universe_fill_expr_payload(const TermUniverse *universe,
                                            const AtomId *child_ids,
                                            uint32_t arity,
                                            uint8_t *payload,
                                            size_t payload_len) {
    if (arity == 0u)
        return payload_len == 0u;
    if (!universe || !child_ids || !payload)
        return false;
    size_t atom_id_width = term_universe_atom_id_storage_width_bytes(universe);
    if (atom_id_width == 0 || arity > SIZE_MAX / atom_id_width ||
        payload_len != (size_t)arity * atom_id_width) {
        return false;
    }
    for (uint32_t i = 0; i < arity; i++) {
        if (!term_universe_store_stored_atom_id(
                term_universe_store_format(universe),
                payload + ((size_t)i * atom_id_width),
                child_ids[i])) {
            term_universe_set_error((TermUniverse *)universe,
                                    TERM_UNIVERSE_ERROR_ATOM_ID_EXHAUSTED);
            return false;
        }
    }
    return true;
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
    if (!term_universe_fill_expr_payload(
            universe, child_ids, arity, payload, payload_len)) {
        free(payload);
        return false;
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
        case GV_BINDINGS:
        case GV_FOREIGN:
        case GV_TERM_GRAPH:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
            return false;
        case GV_INTERNAL_TAG:
            *out_len = 0;
            return cetta_internal_tag_is_term_stable(
                (int64_t)term_universe_aux_data(hdr));
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

/* A record's coordinate in the intern table: its slot hash folded to 32
 * bits, which the record's entry keeps. */
static inline uint32_t term_universe_coordinate(uint64_t slot_hash) {
    return (uint32_t)(slot_hash ^ (slot_hash >> 32u));
}

/* The intern table is open addressing over pairs: a record's tag, then its
 * id.  The tag is the record's coordinate, made nonzero, and a zero tag is
 * an empty pair.  A probe compares tags and reads the record only when they
 * agree, and probing is linear from the tag's home, so a probe sequence
 * stays within a cache line or two.  Coordinates are folded from mixed
 * hashes, so homes spread without a secondary stride. */
static inline uint32_t term_universe_intern_tag(uint32_t coordinate) {
    return coordinate ? coordinate : 1u;
}

static inline size_t term_universe_intern_index(
    uint32_t tag, size_t probe, size_t mask) {
    return ((size_t)tag + probe) & mask;
}

/* A pair's width: a four-byte tag, then the id at the store format's width,
 * aligned to it. */
static inline size_t term_universe_intern_pair_width(size_t id_width) {
    return id_width == 4u ? 8u : 16u;
}

static inline uint32_t term_universe_intern_tag_at(const uint8_t *pairs,
                                                   size_t pair_width,
                                                   size_t idx) {
    uint32_t tag = 0u;
    memcpy(&tag, pairs + idx * pair_width, sizeof(tag));
    return tag;
}

static inline AtomId term_universe_intern_id_at(const uint8_t *pairs,
                                                size_t pair_width,
                                                size_t idx) {
    const uint8_t *src = pairs + idx * pair_width;
    if (pair_width == 8u) {
        uint32_t id = 0u;
        memcpy(&id, src + 4u, sizeof(id));
        return id;
    }
    uint64_t id = 0u;
    memcpy(&id, src + 8u, sizeof(id));
    return id;
}

static inline void term_universe_intern_store_pair(uint8_t *pairs,
                                                   size_t pair_width,
                                                   size_t idx, uint32_t tag,
                                                   AtomId id) {
    uint8_t *dst = pairs + idx * pair_width;
    memcpy(dst, &tag, sizeof(tag));
    if (pair_width == 8u) {
        uint32_t narrow = (uint32_t)id;
        memcpy(dst + 4u, &narrow, sizeof(narrow));
    } else {
        memcpy(dst + 8u, &id, sizeof(id));
    }
}

static inline size_t term_universe_intern_width(const TermUniverse *universe) {
    return term_universe_intern_pair_width(
        term_universe_slot_storage_width_bytes(universe));
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
        case GV_BINDINGS:
        case GV_FOREIGN:
        case GV_TERM_GRAPH:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
            break;
        case GV_INTERNAL_TAG:
            h = term_universe_index_mix(h, term_universe_aux_data(hdr));
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

/* Compute the intern-table coordinate of an expression directly from its
 * canonical children.  The encoded record path above performs the same mix;
 * keeping this read-only form separate lets borrowed term views probe the
 * universe without allocating a temporary payload. */
static uint64_t term_universe_expr_ids_slot_hash(
        const CettaTermHdr *hdr, const AtomId *child_ids,
        uint32_t arity) {
    if (!hdr || (arity > 0u && !child_ids))
        return 0u;
    uint64_t h = UINT64_C(0x243f6a8885a308d3);
    h = term_universe_index_mix(h, hdr->tag);
    h = term_universe_index_mix(h, hdr->subtag);
    h = term_universe_index_mix(h, hdr->sym_or_head);
    h = term_universe_index_mix(h, term_universe_aux_data(hdr));
    h = term_universe_index_mix(h, hdr->hash32);
    for (uint32_t i = 0u; i < arity; i++)
        h = term_universe_index_mix(h, child_ids[i]);
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
            aux_data = atom->ground.slen;
        else if (atom->ground.gkind == GV_BIGINT)
            aux_data = (uint32_t)strlen(atom_bigint_cstr(atom));
        else if (atom->ground.gkind == GV_RATIONAL)
            aux_data = (uint32_t)strlen(atom_rational_cstr(atom));
        else if (atom->ground.gkind == GV_INTERNAL_TAG)
            aux_data = (uint32_t)atom->ground.ival;
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
                atom->ground.slen);
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
        case GV_BINDINGS:
        case GV_FOREIGN:
        case GV_TERM_GRAPH:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
            break;
        case GV_INTERNAL_TAG:
            h = term_universe_index_mix(h, (uint64_t)atom->ground.ival);
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
    h = term_universe_hash_mix(h, (uint32_t)(conv.u >> 32));
    return h;
}

static uint32_t term_universe_hash_bool_value(bool value) {
    uint32_t h = 5381u;
    h = term_universe_hash_mix(h, (uint32_t)ATOM_GROUNDED);
    h = term_universe_hash_mix(h, (uint32_t)GV_BOOL);
    h = term_universe_hash_mix(h, value ? 1u : 0u);
    return h;
}

static uint32_t term_universe_hash_list_tag_value(int64_t tag) {
    uint32_t h = 5381u;
    h = term_universe_hash_mix(h, (uint32_t)ATOM_GROUNDED);
    h = term_universe_hash_mix(h, (uint32_t)GV_INTERNAL_TAG);
    h = term_universe_hash_mix(h, (uint32_t)(tag & 0xFFFFFFFF));
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

/* A string hashes over its bytes, embedded NUL included, exactly as
 * atom_hash_compute() does. */
static uint32_t term_universe_hash_string_bytes(const char *bytes,
                                                size_t len) {
    uint32_t h = 5381u;
    h = term_universe_hash_mix(h, (uint32_t)ATOM_GROUNDED);
    h = term_universe_hash_mix(h, (uint32_t)GV_STRING);
    for (size_t i = 0; i < len; i++)
        h = term_universe_hash_mix(h, (uint32_t)bytes[i]);
    return h;
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
        case GV_BINDINGS:
        case GV_FOREIGN:
        case GV_TERM_GRAPH:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
            return false;
        case GV_INTERNAL_TAG:
            return term_universe_aux_data(have_hdr) ==
                   term_universe_aux_data(want_hdr);
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

static size_t term_universe_integer_hash(uint64_t prefix) {
    uint64_t x = prefix;
    x ^= x >> 30;
    x *= UINT64_C(0xbf58476d1ce4e5b9);
    x ^= x >> 27;
    return (size_t)x;
}

static bool term_universe_integer_reserve(TermUniverse *universe,
                                           size_t additional) {
    if (additional > SIZE_MAX - universe->integer_used)
        goto too_large;
    size_t required = universe->integer_used + additional;
    size_t cap = universe->integer_slots ? universe->integer_mask + 1u : 128u;
    while (required > cap - cap / 3u) {
        if (cap > SIZE_MAX / 2u)
            goto too_large;
        cap *= 2u;
    }
    if (universe->integer_slots && cap == universe->integer_mask + 1u)
        return true;
    if (cap > SIZE_MAX / sizeof(CettaIntegerInternSlot))
        goto too_large;
    CettaIntegerInternSlot *slots = calloc(cap, sizeof(*slots));
    if (!slots) {
        term_universe_set_error(universe, TERM_UNIVERSE_ERROR_ALLOCATION_FAILED);
        return false;
    }
    for (size_t i = 0u; universe->integer_slots && i <= universe->integer_mask; i++) {
        CettaIntegerInternSlot entry = universe->integer_slots[i];
        if (!entry.page)
            continue;
        size_t pos = term_universe_integer_hash(entry.prefix) & (cap - 1u);
        while (slots[pos].page)
            pos = (pos + 1u) & (cap - 1u);
        slots[pos] = entry;
    }
    free(universe->integer_slots);
    universe->integer_slots = slots;
    universe->integer_mask = cap - 1u;
    return true;
too_large:
    term_universe_set_error(universe, TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE);
    return false;
}

static CettaIntegerInternPage *term_universe_integer_page(
        const TermUniverse *universe, int64_t value, size_t *vacancy,
        size_t *probes) {
    if (probes)
        *probes = 0u;
    if (!universe || !universe->integer_slots)
        return NULL;
    uint64_t prefix = (uint64_t)value >> TERM_UNIVERSE_INTEGER_PAGE_BITS;
    size_t pos = term_universe_integer_hash(prefix) & universe->integer_mask;
    for (;; pos = (pos + 1u) & universe->integer_mask) {
        CettaIntegerInternSlot slot = universe->integer_slots[pos];
        if (probes)
            (*probes)++;
        if (!slot.page) {
            if (vacancy)
                *vacancy = pos;
            return NULL;
        }
        if (slot.prefix == prefix)
            return slot.page;
    }
}

static uint16_t term_universe_integer_sparse_position(
        const CettaIntegerInternPage *page, uint16_t offset) {
    uint16_t lo = 0u, hi = page->len;
    while (lo < hi) {
        uint16_t mid = lo + (hi - lo) / 2u;
        if (page->data.sparse[mid].offset < offset)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return lo;
}

static AtomId term_universe_lookup_integer(const TermUniverse *universe,
                                           int64_t value,
                                           TermUniverseVacancy *vacancy) {
    if (vacancy) *vacancy = (TermUniverseVacancy){.slot = SIZE_MAX};
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LOOKUP);
    size_t probes = 0u;
    CettaIntegerInternPage *page = term_universe_integer_page(universe, value, NULL, &probes);
    size_t physical = 0u;
    uint16_t offset = (uint64_t)value & (TERM_UNIVERSE_INTEGER_PAGE_SIZE - 1u);
    if (page) {
        if (page->dense) {
            physical = page->data.positions[offset];
        } else {
            uint16_t pos = term_universe_integer_sparse_position(page, offset);
            if (pos < page->len && page->data.sparse[pos].offset == offset)
                physical = page->data.sparse[pos].physical_plus_one;
            if (vacancy) vacancy->integer_position = pos;
        }
        if (vacancy) {
            vacancy->integer_page = page;
            vacancy->integer_length = page->len;
            vacancy->integer_dense = page->dense;
        }
    }
    TU_DIAG_LOOKUP_PROBES((TermUniverse *)universe, probes);
    if (physical) {
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_HIT);
        TU_DIAG_INC((TermUniverse *)universe, direct_lookup_hits);
        return term_universe_physical_index_to_atom_id(universe, physical - 1u);
    }
    TU_DIAG_INC((TermUniverse *)universe, direct_lookup_misses);
    return CETTA_ATOM_ID_NONE;
}

/* Reserve the publication coordinate before appending a record. Promotion
 * retains all old physical positions; zero remains the only absent value. */
static CettaIntegerInternPage *term_universe_integer_prepare(
        TermUniverse *universe, int64_t value, uint16_t *position,
        const TermUniverseVacancy *hint) {
    size_t vacancy = SIZE_MAX;
    /* An existing page keeps its address through slot-table growth. Only
     * sparse coordinates depend on occupancy; dense coordinates are offsets. */
    CettaIntegerInternPage *page = hint ? hint->integer_page : NULL;
    bool hint_current = page && page->len == hint->integer_length &&
        page->dense == hint->integer_dense;
    if (!hint_current)
        page = term_universe_integer_page(universe, value, &vacancy, NULL);
    if (!page) {
        if (!term_universe_integer_reserve(universe, 1u))
            return NULL;
        (void)term_universe_integer_page(universe, value, &vacancy, NULL);
        page = calloc(1u, sizeof(*page));
        if (!page)
            goto allocation_failed;
        universe->integer_slots[vacancy] = (CettaIntegerInternSlot){
            .prefix = (uint64_t)value >> TERM_UNIVERSE_INTEGER_PAGE_BITS,
            .page = page};
        universe->integer_used++;
    }
    uint16_t offset = (uint64_t)value & (TERM_UNIVERSE_INTEGER_PAGE_SIZE - 1u);
    if (!page->dense && page->len >= TERM_UNIVERSE_INTEGER_DENSE_AT) {
        size_t *positions = calloc(TERM_UNIVERSE_INTEGER_PAGE_SIZE, sizeof(*positions));
        if (!positions)
            goto allocation_failed;
        for (uint16_t i = 0u; i < page->len; i++)
            positions[page->data.sparse[i].offset] = page->data.sparse[i].physical_plus_one;
        free(page->data.sparse);
        page->data.positions = positions;
        page->dense = true;
    }
    if (page->dense) {
        *position = offset;
        return page;
    }
    if (page->len == page->cap) {
        uint16_t cap = page->cap ? page->cap * 2u : 4u;
        CettaIntegerPageEntry *entries = realloc(page->data.sparse, cap * sizeof(*entries));
        if (!entries)
            goto allocation_failed;
        page->data.sparse = entries;
        page->cap = cap;
    }
    *position = hint_current && !page->dense
        ? hint->integer_position : term_universe_integer_sparse_position(page, offset);
    return page;
allocation_failed:
    term_universe_set_error(universe, TERM_UNIVERSE_ERROR_ALLOCATION_FAILED);
    return NULL;
}

static AtomId term_universe_lookup_record_at(const TermUniverse *universe,
                                             const CettaTermHdr *hdr,
                                             const uint8_t *payload,
                                             size_t payload_len,
                                             uint32_t hslot,
                                             TermUniverseVacancy *vacancy) {
    if (vacancy) {
        *vacancy = (TermUniverseVacancy){.slot = SIZE_MAX};
        if (universe) {
            vacancy->mask = universe->intern_mask;
            vacancy->used = universe->intern_used;
            vacancy->format = term_universe_store_format(universe);
        }
    }
    if (!universe || !universe->intern_slots || !hdr)
        return CETTA_ATOM_ID_NONE;
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LOOKUP);
    uint32_t h = hdr->hash32;
    uint32_t tag = term_universe_intern_tag(hslot);
    size_t width = term_universe_intern_width(universe);
    for (size_t probe = 0; probe <= universe->intern_mask; probe++) {
        size_t idx = term_universe_intern_index(
            tag, probe, universe->intern_mask);
        uint32_t have_tag = term_universe_intern_tag_at(
            universe->intern_slots, width, idx);
        if (have_tag == 0u) {
            if (vacancy)
                vacancy->slot = idx;
            TU_DIAG_LOOKUP_PROBES(
                (TermUniverse *)universe, probe + 1u);
            TU_DIAG_INC((TermUniverse *)universe, direct_lookup_misses);
            return CETTA_ATOM_ID_NONE;
        }
        if (have_tag != tag)
            continue;
        AtomId id = term_universe_intern_id_at(
            universe->intern_slots, width, idx);
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

static AtomId term_universe_lookup_record_id(const TermUniverse *universe,
                                             const CettaTermHdr *hdr,
                                             const uint8_t *payload,
                                             size_t payload_len) {
    if (hdr && hdr->tag == ATOM_GROUNDED && hdr->subtag == GV_INT &&
        payload && payload_len == sizeof(int64_t))
        return term_universe_lookup_integer(
            universe, term_universe_load_i64(payload), NULL);
    return term_universe_lookup_record_at(
        universe, hdr, payload, payload_len,
        term_universe_coordinate(term_universe_record_slot_hash(
            universe, hdr, payload, payload_len)), NULL);
}

static bool term_universe_entry_eq_expr_ids(
        const TermUniverse *universe, AtomId id,
        const CettaTermHdr *want_hdr, const AtomId *child_ids,
        uint32_t arity) {
    const CettaTermHdr *have_hdr = tu_hdr(universe, id);
    if (!have_hdr || !want_hdr || have_hdr->tag != ATOM_EXPR ||
        have_hdr->tag != want_hdr->tag ||
        have_hdr->subtag != want_hdr->subtag ||
        have_hdr->arity_or_len != want_hdr->arity_or_len ||
        have_hdr->sym_or_head != want_hdr->sym_or_head ||
        have_hdr->aux32 != want_hdr->aux32 ||
        term_universe_aux_data(have_hdr) != arity) {
        return false;
    }
    const uint8_t *payload = term_universe_payload(universe, id);
    size_t width = term_universe_atom_id_storage_width_bytes(universe);
    if ((arity > 0u && (!payload || !child_ids)) || width == 0u)
        return false;
    for (uint32_t i = 0u; i < arity; i++) {
        if (term_universe_load_stored_atom_id(
                universe, payload + (size_t)i * width) != child_ids[i]) {
            return false;
        }
    }
    return true;
}

static AtomId term_universe_lookup_expr_id_from_ids(
        const TermUniverse *universe, const CettaTermHdr *hdr,
        const AtomId *child_ids, uint32_t arity, uint32_t hslot,
        TermUniverseVacancy *vacancy) {
    if (vacancy) {
        *vacancy = (TermUniverseVacancy){.slot = SIZE_MAX};
        if (universe) {
            vacancy->mask = universe->intern_mask;
            vacancy->used = universe->intern_used;
            vacancy->format = term_universe_store_format(universe);
        }
    }
    if (!universe || !universe->intern_slots || !hdr ||
        (arity > 0u && !child_ids)) {
        return CETTA_ATOM_ID_NONE;
    }
    cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LOOKUP);
    uint32_t tag = term_universe_intern_tag(hslot);
    size_t width = term_universe_intern_width(universe);
    for (size_t probe = 0u; probe <= universe->intern_mask; probe++) {
        size_t idx = term_universe_intern_index(
            tag, probe, universe->intern_mask);
        uint32_t have_tag = term_universe_intern_tag_at(
            universe->intern_slots, width, idx);
        if (have_tag == 0u) {
            if (vacancy)
                vacancy->slot = idx;
            TU_DIAG_LOOKUP_PROBES(
                (TermUniverse *)universe, probe + 1u);
            TU_DIAG_INC((TermUniverse *)universe, direct_lookup_misses);
            return CETTA_ATOM_ID_NONE;
        }
        if (have_tag != tag)
            continue;
        AtomId id = term_universe_intern_id_at(
            universe->intern_slots, width, idx);
        const CettaTermHdr *have_hdr = tu_hdr(universe, id);
        if (term_universe_entry(universe, id) && have_hdr &&
            have_hdr->hash32 == hdr->hash32 &&
            term_universe_entry_eq_expr_ids(
                universe, id, hdr, child_ids, arity)) {
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

    if (total_len > UINT32_MAX)
        return false;
    universe->blob_len += total_len;
    out_entry->byte_off = off;
    out_entry->byte_len = (uint32_t)total_len;
    out_entry->decoded_cache = NULL;
    return true;
}

static AtomId term_universe_intern_record(TermUniverse *universe,
                                          const CettaTermHdr *hdr,
                                          const uint8_t *payload,
                                          size_t payload_len) {
    if (!universe || !universe->persistent_arena || !hdr)
        return CETTA_ATOM_ID_NONE;

    bool integer = hdr->tag == ATOM_GROUNDED && hdr->subtag == GV_INT;
    uint32_t coordinate = integer ? hdr->hash32 : term_universe_coordinate(
        term_universe_record_slot_hash(universe, hdr, payload, payload_len));
    TermUniverseVacancy vacancy;
    AtomId existing = integer
        ? term_universe_lookup_integer(universe, term_universe_load_i64(payload), &vacancy)
        : term_universe_lookup_record_at(
            universe, hdr, payload, payload_len, coordinate, &vacancy);
    if (existing != CETTA_ATOM_ID_NONE)
        return existing;

    if (!term_universe_atom_id_capacity_available(universe))
        return CETTA_ATOM_ID_NONE;
    return term_universe_insert_new_record(universe, hdr, payload, payload_len,
                                           coordinate, &vacancy);
}

static AtomId term_universe_insert_new_record(TermUniverse *universe,
                                              const CettaTermHdr *hdr,
                                              const uint8_t *payload,
                                              size_t payload_len,
                                              uint32_t coordinate,
                                              const TermUniverseVacancy *vacancy) {
    if (!universe || !universe->persistent_arena || !hdr)
        return CETTA_ATOM_ID_NONE;
    if (!term_universe_reserve_entries(universe, universe->len + 1))
        return CETTA_ATOM_ID_NONE;

    bool integer = hdr->tag == ATOM_GROUNDED && hdr->subtag == GV_INT;
    uint16_t integer_position = 0u;
    CettaIntegerInternPage *integer_page = integer
        ? term_universe_integer_prepare(universe, term_universe_load_i64(payload),
                                        &integer_position, vacancy) : NULL;
    if (integer && !integer_page)
        return CETTA_ATOM_ID_NONE;
    size_t needed = universe->intern_slots ? (universe->intern_mask + 1) : 0;
    if (!integer && (needed == 0 || (universe->intern_used + 1) * 10 > needed * 7)) {
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
        .coordinate = coordinate,
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
    bool published = false;
    size_t width = term_universe_intern_width(universe);
    if (integer) {
        if (integer_page->dense) {
            integer_page->data.positions[integer_position] = physical_index + 1u;
        } else {
            memmove(integer_page->data.sparse + integer_position + 1u,
                    integer_page->data.sparse + integer_position,
                    (integer_page->len - integer_position) * sizeof(CettaIntegerPageEntry));
            integer_page->data.sparse[integer_position] = (CettaIntegerPageEntry){
                .offset = (uint64_t)term_universe_load_i64(payload) &
                    (TERM_UNIVERSE_INTEGER_PAGE_SIZE - 1u),
                .physical_plus_one = physical_index + 1u};
            integer_page->len++;
        }
        published = true;
    } else if (vacancy && vacancy->slot <= universe->intern_mask &&
        vacancy->mask == universe->intern_mask &&
        vacancy->used == universe->intern_used &&
        vacancy->format == term_universe_store_format(universe) &&
        term_universe_intern_tag_at(
            universe->intern_slots, width, vacancy->slot) == 0u) {
        term_universe_intern_store_pair(
            universe->intern_slots, width, vacancy->slot,
            term_universe_intern_tag(coordinate), id);
        universe->intern_used++;
        published = true;
    } else {
        published = term_universe_insert_stable_id(universe, id);
    }
    if (!published) {
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

AtomKind term_universe_kind_unrecorded(const TermUniverse *universe,
                                       AtomId id) {
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache) ? entry->decoded_cache->kind
                                           : ATOM_SYMBOL;
}

CettaExprLen term_universe_arity_unrecorded(const TermUniverse *universe,
                                            AtomId id) {
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

SymbolId term_universe_head_sym_unrecorded(const TermUniverse *universe,
                                           AtomId id) {
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

CettaExprLen tu_authored_arity(const TermUniverse *universe, AtomId id) {
    CettaExprLen arity = tu_arity(universe, id);
    if (arity < 3u || tu_kind(universe, id) != ATOM_EXPR)
        return arity;
    AtomId head = tu_child(universe, id, 0u);
    SymbolId sym = tu_kind(universe, head) == ATOM_SYMBOL
        ? tu_sym(universe, head) : SYMBOL_ID_NONE;
    if (!atom_prime_scope_metadata_shape(arity, sym))
        return arity;
    AtomId last = tu_child(universe, id, arity - 1u);
    if (tu_kind(universe, last) != ATOM_EXPR || tu_arity(universe, last) < 2u ||
        tu_internal_tag(universe, tu_child(universe, last, 0u)) !=
            (int64_t)CETTA_INTERNAL_TAG_PRIME_OWN)
        return arity;
    return arity - 1u;
}

int64_t tu_internal_tag(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr)
        return (AtomKind)hdr->tag == ATOM_GROUNDED &&
                       (GroundedKind)hdr->subtag == GV_INTERNAL_TAG
                   ? (int64_t)term_universe_aux_data(hdr)
                   : 0;
    Atom *atom = term_universe_get_atom(universe, id);
    return atom && atom->kind == ATOM_GROUNDED &&
                   atom->ground.gkind == GV_INTERNAL_TAG
               ? atom->ground.ival
               : 0;
}

PeTTaValueRepresentation tu_petta_value_representation(
    const TermUniverse *universe, AtomId id) {
    if (tu_kind(universe, id) != ATOM_EXPR)
        return PETTA_VALUE_ORDINARY;
    CettaExprLen length = tu_arity(universe, id);
    if (length != 2u && length != 3u)
        return PETTA_VALUE_ORDINARY;
    int64_t head = tu_internal_tag(universe, tu_child(universe, id, 0u));
    if ((length == 2u && head == CETTA_INTERNAL_TAG_PETTA_PROLOG_COMPOUND) ||
        (length == 3u && head == CETTA_INTERNAL_TAG_PETTA_PARTIAL))
        return PETTA_VALUE_COMPOUND;
    if (length == 3u) {
        if (head == CETTA_INTERNAL_TAG_PETTA_NULLARY_CALLABLE)
            return PETTA_VALUE_REGISTERED_CALLABLE;
        AtomId domain = tu_child(universe, id, 1u);
        if (tu_kind(universe, domain) == ATOM_EXPR &&
            tu_arity(universe, domain) == 2u &&
            tu_internal_tag(universe, tu_child(universe, domain, 0u)) ==
                CETTA_INTERNAL_TAG_PETTA_CALLABLE_IDENTITY)
            return PETTA_VALUE_REGISTERED_CALLABLE;
    }
    return PETTA_VALUE_ORDINARY;
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

size_t tu_string_len(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr) {
        if (hdr->tag == ATOM_GROUNDED &&
            (GroundedKind)hdr->subtag == GV_STRING)
            return term_universe_aux_data(hdr);
        return 0u;
    }
    const TermEntry *entry = term_universe_entry(universe, id);
    return (entry && entry->decoded_cache &&
            entry->decoded_cache->kind == ATOM_GROUNDED &&
            entry->decoded_cache->ground.gkind == GV_STRING)
               ? entry->decoded_cache->ground.slen
               : 0u;
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

bool tu_has_vars(const TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr)
        return term_universe_hdr_has_vars(hdr);
    const TermEntry *entry = term_universe_entry(universe, id);
    return entry && atom_has_vars(entry->decoded_cache);
}

static int term_variable_base_id_compare(const void *left,
                                         const void *right) {
    uint32_t a = *(const uint32_t *)left;
    uint32_t b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

static bool term_universe_variable_support_build(
    const TermUniverse *universe, AtomId root,
    CettaTermVariableSupport **out) {
    AtomId *work = NULL;
    size_t work_len = 0u;
    size_t work_cap = 0u;
    uint32_t *base_ids = NULL;
    size_t base_len = 0u;
    size_t base_cap = 0u;
    *out = NULL;

#define PUSH_VARIABLE_SUPPORT_ATOM(atom_id_)                                \
    do {                                                                    \
        if (work_len == work_cap) {                                         \
            size_t next_cap = work_cap ? work_cap * 2u : 16u;               \
            if (next_cap < work_cap ||                                      \
                next_cap > SIZE_MAX / sizeof(*work)) {                      \
                goto fail;                                                  \
            }                                                               \
            work = cetta_realloc(work, next_cap * sizeof(*work));           \
            if (!work)                                                      \
                goto fail;                                                  \
            work_cap = next_cap;                                            \
        }                                                                   \
        work[work_len++] = (atom_id_);                                      \
    } while (0)

    PUSH_VARIABLE_SUPPORT_ATOM(root);
    while (work_len > 0u) {
        AtomId id = work[--work_len];
        const CettaTermHdr *hdr = tu_hdr(universe, id);
        if (!hdr)
            goto fail;
        switch ((AtomKind)hdr->tag) {
        case ATOM_VAR:
            if (base_len == base_cap) {
                size_t next_cap = base_cap ? base_cap * 2u : 8u;
                if (next_cap < base_cap ||
                    next_cap > SIZE_MAX / sizeof(*base_ids)) {
                    goto fail;
                }
                base_ids = cetta_realloc(
                    base_ids, next_cap * sizeof(*base_ids));
                if (!base_ids)
                    goto fail;
                base_cap = next_cap;
            }
            base_ids[base_len++] = var_base_id(tu_var_id(universe, id));
            break;
        case ATOM_EXPR: {
            CettaExprLen arity = tu_arity(universe, id);
            for (CettaExprIndex i = 0u; i < arity; i++) {
                AtomId child = tu_child(universe, id, i);
                if (child == CETTA_ATOM_ID_NONE)
                    goto fail;
                if (tu_has_vars(universe, child))
                    PUSH_VARIABLE_SUPPORT_ATOM(child);
            }
            break;
        }
        case ATOM_SYMBOL:
        case ATOM_GROUNDED:
            break;
        }
    }
    free(work);
    work = NULL;
    if (base_len == 0u) {
        free(base_ids);
        return true;
    }

    qsort(base_ids, base_len, sizeof(*base_ids),
          term_variable_base_id_compare);
    size_t unique_len = 1u;
    for (size_t i = 1u; i < base_len; i++) {
        if (base_ids[i] != base_ids[unique_len - 1u])
            base_ids[unique_len++] = base_ids[i];
    }
    if (unique_len > UINT32_MAX)
        goto fail;

    uint64_t spread =
        (uint64_t)base_ids[unique_len - 1u] - (uint64_t)base_ids[0];
    bool dense = spread < 64u;
    size_t sparse_bytes = dense ? 0u : unique_len * sizeof(*base_ids);
    if (sparse_bytes > SIZE_MAX - sizeof(CettaTermVariableSupport))
        goto fail;
    CettaTermVariableSupport *support =
        cetta_malloc(sizeof(*support) + sparse_bytes);
    if (!support)
        goto fail;
    support->count = (uint32_t)unique_len;
    support->first_base_id = base_ids[0];
    support->representation = dense
        ? CETTA_TERM_VARIABLE_SUPPORT_DENSE64
        : CETTA_TERM_VARIABLE_SUPPORT_SPARSE32;
    support->dense_mask = 0u;
    if (dense) {
        for (size_t i = 0u; i < unique_len; i++) {
            uint32_t offset = base_ids[i] - support->first_base_id;
            support->dense_mask |= UINT64_C(1) << offset;
        }
    } else {
        memcpy(support->sparse_base_ids, base_ids, sparse_bytes);
    }
    free(base_ids);
    *out = support;
    return true;

fail:
    free(work);
    free(base_ids);
    return false;
#undef PUSH_VARIABLE_SUPPORT_ATOM
}

bool term_universe_variable_support(
    TermUniverse *universe, AtomId id,
    const CettaTermVariableSupport **out) {
    size_t physical_index = 0u;
    if (!universe || !out ||
        !term_universe_atom_id_to_physical_index(
            universe, id, &physical_index) ||
        physical_index >= universe->len ||
        !universe->variable_support_blocks) {
        return false;
    }
    *out = NULL;
    if (!tu_has_vars(universe, id))
        return true;

    size_t block_index =
        physical_index / CETTA_TERM_VARIABLE_SUPPORT_BLOCK_SIZE;
    size_t slot_index =
        physical_index % CETTA_TERM_VARIABLE_SUPPORT_BLOCK_SIZE;
    if (block_index >= universe->variable_support_block_cap)
        return false;
    CettaTermVariableSupportBlock *block = atomic_load_explicit(
        &universe->variable_support_blocks[block_index],
        memory_order_acquire);
    if (!block) {
        CettaTermVariableSupportBlock *candidate =
            cetta_malloc(sizeof(*candidate));
        if (!candidate)
            return false;
        for (size_t i = 0u;
             i < CETTA_TERM_VARIABLE_SUPPORT_BLOCK_SIZE; i++) {
            atomic_init(&candidate->slots[i], NULL);
        }
        CettaTermVariableSupportBlock *expected_block = NULL;
        if (!atomic_compare_exchange_strong_explicit(
                &universe->variable_support_blocks[block_index],
                &expected_block, candidate, memory_order_release,
                memory_order_acquire)) {
            free(candidate);
            block = expected_block;
        } else {
            block = candidate;
        }
    }
    CettaTermVariableSupport *cached = atomic_load_explicit(
        &block->slots[slot_index], memory_order_acquire);
    if (cached) {
        *out = cached;
        return true;
    }

    CettaTermVariableSupport *computed = NULL;
    if (!term_universe_variable_support_build(
            universe, id, &computed) || !computed) {
        return false;
    }
    CettaTermVariableSupport *expected = NULL;
    if (!atomic_compare_exchange_strong_explicit(
            &block->slots[slot_index],
            &expected, computed, memory_order_release,
            memory_order_acquire)) {
        free(computed);
        computed = expected;
    }
    *out = computed;
    return true;
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
    AtomId result = term_universe_intern_record(universe, &hdr, payload,
                                               sizeof(payload));
    if (result != CETTA_ATOM_ID_NONE)
        (void)cetta_frame_identity_scope_retain(
            &universe->frame_identities, var_epoch_suffix(var_id));
    return result;
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
    AtomId result = term_universe_intern_record(universe, &hdr, payload,
                                               sizeof(payload));
    if (result != CETTA_ATOM_ID_NONE)
        (void)cetta_frame_identity_scope_retain(
            &universe->frame_identities, var_epoch_suffix(var_id));
    return result;
}

AtomId tu_intern_int(TermUniverse *universe, int64_t value) {
    enum { TERM_UNIVERSE_SMALL_INT_CACHE = 256 };
    static _Thread_local struct {
        uint64_t instance_id;
        uint64_t storage_epoch;
        AtomId ids[TERM_UNIVERSE_SMALL_INT_CACHE];
    } small_ints;
    CettaTermHdr hdr = {0};
    uint8_t payload[sizeof(int64_t)] = {0};
    AtomId id;
    if (!universe)
        return CETTA_ATOM_ID_NONE;
    if (value >= 0 && value < TERM_UNIVERSE_SMALL_INT_CACHE) {
        if (small_ints.instance_id != universe->instance_id ||
            small_ints.storage_epoch != universe->storage_epoch) {
            for (size_t i = 0; i < TERM_UNIVERSE_SMALL_INT_CACHE; i++)
                small_ints.ids[i] = CETTA_ATOM_ID_NONE;
            small_ints.instance_id = universe->instance_id;
            small_ints.storage_epoch = universe->storage_epoch;
        }
        if (small_ints.ids[value] != CETTA_ATOM_ID_NONE)
            return small_ints.ids[value];
    }
    hdr.tag = (uint8_t)ATOM_GROUNDED;
    hdr.subtag = (uint8_t)GV_INT;
    hdr.aux32 = term_universe_aux_make(0u, false);
    hdr.hash32 = term_universe_hash_int_value(value);
    term_universe_store_i64(payload, value);
    TU_DIAG_INC(universe, direct_constructor_leaf_hits);
    id = term_universe_intern_record(universe, &hdr, payload,
                                     sizeof(payload));
    if (id != CETTA_ATOM_ID_NONE &&
        value >= 0 && value < TERM_UNIVERSE_SMALL_INT_CACHE &&
        small_ints.instance_id == universe->instance_id &&
        small_ints.storage_epoch == universe->storage_epoch) {
        small_ints.ids[value] = id;
    }
    return id;
}

/* Integer positions are independent of the encoded AtomId width. Batch
 * reservation and prefetching preserve scalar constructor identity and order. */
bool tu_intern_ints(TermUniverse *universe, const int64_t *values,
                    size_t count, AtomId *ids) {
    enum { WINDOW = 32 };
    if (!universe || !universe->persistent_arena ||
        (count && (!values || !ids)))
        return false;
    for (size_t first = 0u; first < count;) {
        size_t length = count - first < WINDOW ? count - first : WINDOW;
        if (!term_universe_integer_reserve(universe, length))
            return false;
        for (size_t i = 0u; i < length; i++) {
            size_t home = term_universe_integer_hash(
                (uint64_t)values[first + i] >> TERM_UNIVERSE_INTEGER_PAGE_BITS) &
                universe->integer_mask;
            __builtin_prefetch(&universe->integer_slots[home], 1, 1);
        }
        for (size_t i = 0u; i < length; i++) {
            ids[first + i] = tu_intern_int(universe, values[first + i]);
            if (ids[first + i] == CETTA_ATOM_ID_NONE)
                return false;
        }
        first += length;
    }
    return true;
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

/* Admit immutable structural tags under atom.h's retention judgment. */
AtomId tu_intern_stable_tag(TermUniverse *universe, int64_t tag) {
    if (!cetta_internal_tag_is_term_stable(tag))
        return CETTA_ATOM_ID_NONE;
    CettaTermHdr hdr = {0};
    hdr.tag = (uint8_t)ATOM_GROUNDED;
    hdr.subtag = (uint8_t)GV_INTERNAL_TAG;
    hdr.aux32 = term_universe_aux_make((uint32_t)tag, false);
    hdr.hash32 = term_universe_hash_list_tag_value(tag);
    TU_DIAG_INC(universe, direct_constructor_leaf_hits);
    return term_universe_intern_record(universe, &hdr, NULL, 0u);
}

AtomId tu_intern_string_n(TermUniverse *universe, const char *bytes,
                          size_t len_sz) {
    if (!bytes || len_sz > CETTA_STRING_BYTES_MAX)
        return CETTA_ATOM_ID_NONE;
    uint32_t len = (uint32_t)len_sz;
    CettaTermHdr hdr = {0};
    hdr.tag = (uint8_t)ATOM_GROUNDED;
    hdr.subtag = (uint8_t)GV_STRING;
    hdr.arity_or_len = len > UINT16_MAX ? UINT16_MAX : (uint16_t)len;
    hdr.aux32 = term_universe_aux_make(len, false);
    hdr.hash32 = term_universe_hash_string_bytes(bytes, len);
    TU_DIAG_INC(universe, direct_constructor_leaf_hits);
    /* The payload is the bytes and a NUL kept for C interoperation. */
    uint8_t stack_payload[256];
    uint8_t *payload = len + 1u <= sizeof stack_payload
        ? stack_payload
        : malloc((size_t)len + 1u);
    if (!payload)
        return CETTA_ATOM_ID_NONE;
    memcpy(payload, bytes, len);
    payload[len] = 0u;
    AtomId id = term_universe_intern_record(universe, &hdr, payload,
                                            len + 1u);
    if (payload != stack_payload)
        free(payload);
    return id;
}

AtomId tu_intern_string(TermUniverse *universe, const char *value) {
    if (!value)
        return CETTA_ATOM_ID_NONE;
    return tu_intern_string_n(universe, value, strlen(value));
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

/* The list [elems...], or [elems... | rest] when rest is not
 * CETTA_ATOM_ID_NONE: [x... | [y...]] is [x..., y...] and
 * [x... | [y... | r]] is [x..., y... | r]. */
AtomId tu_list_from_ids(TermUniverse *universe, const AtomId *elems,
                        CettaExprLen elem_len, AtomId rest) {
    int64_t tag = CETTA_INTERNAL_TAG_LIST;
    /* The rest's own elements, when it is a list or list pattern to splice. */
    CettaExprLen spliced = 0u;
    CettaExprLen len;
    AtomId *children;
    AtomId result;

    if (!universe || (elem_len > 0u && !elems))
        return CETTA_ATOM_ID_NONE;
    if (rest != CETTA_ATOM_ID_NONE) {
        tag = CETTA_INTERNAL_TAG_LIST_REST;
        if (tu_kind(universe, rest) == ATOM_EXPR &&
            tu_arity(universe, rest) > 0u) {
            int64_t rest_tag = tu_internal_tag(
                universe, tu_child(universe, rest, 0u));
            if (cetta_internal_tag_is_list(rest_tag)) {
                tag = rest_tag;
                spliced = tu_arity(universe, rest) - 1u;
            }
        }
    }
    len = 1u + elem_len +
          (rest == CETTA_ATOM_ID_NONE ? 0u
           : tag == CETTA_INTERNAL_TAG_LIST_REST && spliced == 0u ? 1u
           : spliced);
    if (!cetta_expr_len_mul_fits_size(len, sizeof(*children)))
        return CETTA_ATOM_ID_NONE;
    children = cetta_malloc((size_t)len * sizeof(*children));
    children[0] = tu_intern_stable_tag(universe, tag);
    for (CettaExprIndex i = 0u; i < elem_len; i++)
        children[1u + i] = elems[i];
    if (spliced > 0u) {
        for (CettaExprIndex i = 0u; i < spliced; i++)
            children[1u + elem_len + i] = tu_child(universe, rest, 1u + i);
    } else if (rest != CETTA_ATOM_ID_NONE &&
               tag == CETTA_INTERNAL_TAG_LIST_REST) {
        children[1u + elem_len] = rest;
    }
    result = children[0] == CETTA_ATOM_ID_NONE
        ? CETTA_ATOM_ID_NONE
        : tu_expr_from_ids(universe, children, len);
    free(children);
    return result;
}

AtomId tu_braces_from_ids(TermUniverse *universe, const AtomId *elems,
                          CettaExprLen elem_len) {
    if (!universe || (elem_len > 0u && !elems) ||
        !cetta_expr_len_mul_fits_size(elem_len + 1u, sizeof(AtomId)))
        return CETTA_ATOM_ID_NONE;
    AtomId *children = cetta_malloc(((size_t)elem_len + 1u) * sizeof(*children));
    children[0] = tu_intern_stable_tag(universe,
                                       CETTA_INTERNAL_TAG_PRIME_BRACES);
    for (CettaExprIndex i = 0u; i < elem_len; i++)
        children[1u + i] = elems[i];
    AtomId result = children[0] == CETTA_ATOM_ID_NONE
        ? CETTA_ATOM_ID_NONE
        : tu_expr_from_ids(universe, children, elem_len + 1u);
    free(children);
    return result;
}

/* Derive the expression header in one walk over its children's headers.
 * This copies metadata: no borrowed blob pointer survives admission. */
static bool term_universe_prepare_expr(const TermUniverse *universe,
                                       const AtomId *child_ids, uint32_t arity,
                                       CettaTermHdr *hdr, uint32_t *coordinate) {
    bool has_vars = false;
    SymbolId head_sym = SYMBOL_ID_NONE;
    uint32_t hash = term_universe_hash_mix(5381u, (uint32_t)ATOM_EXPR);
    hash = term_universe_hash_mix(hash, arity);
    hash = term_universe_hash_mix(hash, 0u);
    for (uint32_t i = 0u; i < arity; i++) {
        const CettaTermHdr *child = tu_hdr(universe, child_ids[i]);
        if (!child)
            return false;
        has_vars |= term_universe_hdr_has_vars(child);
        hash = term_universe_hash_mix(hash, child->hash32);
        if (i == 0u && child->tag == ATOM_SYMBOL)
            head_sym = child->sym_or_head;
    }
    *hdr = (CettaTermHdr){0};
    hdr->tag = (uint8_t)ATOM_EXPR;
    hdr->arity_or_len = arity > UINT16_MAX ? UINT16_MAX : (uint16_t)arity;
    hdr->sym_or_head = head_sym;
    hdr->aux32 = term_universe_aux_make(arity, has_vars);
    hdr->hash32 = hash;
    *coordinate = term_universe_coordinate(
        term_universe_expr_ids_slot_hash(hdr, child_ids, arity));
    return true;
}

static AtomId term_universe_admit_expr(TermUniverse *universe,
                                       const AtomId *child_ids, uint32_t arity32,
                                       const CettaTermHdr *hdr,
                                       uint32_t coordinate) {
    TU_DIAG_INC(universe, direct_constructor_expr_hits);
    TermUniverseVacancy vacancy;
    AtomId id = term_universe_lookup_expr_id_from_ids(
        universe, hdr, child_ids, arity32, coordinate, &vacancy);
    if (id != CETTA_ATOM_ID_NONE)
        return id;
    if (!term_universe_atom_id_capacity_available(universe))
        return CETTA_ATOM_ID_NONE;

    enum { TERM_UNIVERSE_INLINE_EXPR_PAYLOAD = 64 };
    uint8_t inline_payload[TERM_UNIVERSE_INLINE_EXPR_PAYLOAD];
    uint8_t *payload = NULL;
    bool heap_payload = false;
    size_t atom_id_width = term_universe_atom_id_storage_width_bytes(universe);
    size_t payload_len = 0;
    TermUniverseStoreFormat encoded_format = term_universe_store_format(universe);
    if (arity32 > 0u) {
        if (atom_id_width == 0 || arity32 > SIZE_MAX / atom_id_width) {
            term_universe_set_error(universe,
                                    TERM_UNIVERSE_ERROR_STORAGE_TOO_LARGE);
            return CETTA_ATOM_ID_NONE;
        }
        payload_len = (size_t)arity32 * atom_id_width;
        if (payload_len <= TERM_UNIVERSE_INLINE_EXPR_PAYLOAD) {
            payload = inline_payload;
        } else {
            payload = cetta_malloc(payload_len);
            if (!payload) {
                term_universe_set_error(
                    universe, TERM_UNIVERSE_ERROR_ALLOCATION_FAILED);
                return CETTA_ATOM_ID_NONE;
            }
            heap_payload = true;
        }
        if (!term_universe_fill_expr_payload(
                universe, child_ids, arity32, payload, payload_len)) {
            if (heap_payload)
                free(payload);
            return CETTA_ATOM_ID_NONE;
        }
    }
    if (encoded_format != term_universe_store_format(universe)) {
        if (heap_payload)
            free(payload);
        payload = NULL;
        heap_payload = false;
        payload_len = 0;
        if (!term_universe_encode_expr_payload(universe, child_ids, arity32,
                                               &payload, &payload_len))
            return CETTA_ATOM_ID_NONE;
        heap_payload = payload != NULL;
    }
    id = term_universe_insert_new_record(universe, hdr, payload, payload_len,
                                         coordinate, &vacancy);
    if (heap_payload)
        free(payload);
    return id;
}

AtomId tu_expr_from_ids(TermUniverse *universe, const AtomId *child_ids,
                        CettaExprLen arity) {
    uint32_t arity32 = 0u, coordinate = 0u;
    CettaTermHdr hdr;
    if (!universe ||
        !term_universe_expr_arity_checked(universe, arity, &arity32) ||
        (arity32 && !child_ids) ||
        !term_universe_prepare_expr(universe, child_ids, arity32, &hdr, &coordinate))
        return CETTA_ATOM_ID_NONE;
    return term_universe_admit_expr(universe, child_ids, arity32, &hdr, coordinate);
}

bool tu_exprs_from_ids(TermUniverse *universe, const AtomId *child_ids,
                       size_t count, CettaExprLen arity, const bool *present,
                       AtomId *ids) {
    enum { WINDOW = 32 };
    uint32_t arity32 = 0u;
    if (!universe || !universe->persistent_arena ||
        !term_universe_expr_arity_checked(universe, arity, &arity32) ||
        (count && (!ids || (arity32 && !child_ids))) ||
        (arity32 && count > SIZE_MAX / arity32 / sizeof(*child_ids)))
        return false;
    for (size_t row = 0u; row < count; row++)
        ids[row] = CETTA_ATOM_ID_NONE;
    for (size_t first = 0u; first < count;) {
        size_t length = count - first < WINDOW ? count - first : WINDOW;
        CettaTermHdr headers[WINDOW];
        uint32_t coordinates[WINDOW];
        bool valid[WINDOW];
        /* Child records are immutable during preparation. Fetch the bounded
         * window before following its entry-to-blob pointers. Preparation
         * validates each row; admission still publishes in occurrence order. */
        for (size_t i = 0u; i < length; i++) {
            if (present && !present[first + i])
                continue;
            const AtomId *children = arity32
                ? child_ids + (first + i) * arity32 : NULL;
            for (uint32_t child = 0u; child < arity32; child++) {
                const uint8_t *bytes = term_universe_entry_bytes(
                    universe, children[child]);
                if (bytes)
                    __builtin_prefetch(bytes, 0, 1);
            }
        }
        for (size_t i = 0u; i < length; i++) {
            if (present && !present[first + i]) {
                valid[i] = false;
                continue;
            }
            const AtomId *children = arity32
                ? child_ids + (first + i) * arity32 : NULL;
            valid[i] = term_universe_prepare_expr(
                universe, children, arity32, &headers[i], &coordinates[i]);
            if (valid[i] && universe->intern_slots) {
                size_t home = term_universe_intern_index(
                    term_universe_intern_tag(coordinates[i]), 0u,
                    universe->intern_mask);
                __builtin_prefetch(universe->intern_slots +
                    home * term_universe_intern_width(universe), 1, 1);
            }
        }
        for (size_t i = 0u; i < length; i++) {
            if (present && !present[first + i])
                continue;
            if (!valid[i])
                return false;
            const AtomId *children = arity32
                ? child_ids + (first + i) * arity32 : NULL;
            ids[first + i] = term_universe_admit_expr(
                universe, children, arity32, &headers[i], coordinates[i]);
            if (ids[first + i] == CETTA_ATOM_ID_NONE)
                return false;
        }
        first += length;
    }
    return true;
}

typedef struct {
    Atom *atom;
    /* Where this expression's children's ids start in the store stack's
     * ids, once reserved (UINT32_MAX before). */
    uint32_t child_base;
    uint32_t next_child;
    uint32_t parent_index;
} TermUniverseStoreFrame;

/* The work of storing one atom: a stack of frames, and the ids of each
 * expression frame's children, reserved when the frame is first visited
 * and released when it completes, so the ids grow and shrink with the
 * frames.  A small atom uses the inline storage and allocates nothing. */
enum {
    TERM_UNIVERSE_STORE_INLINE_FRAMES = 32u,
    TERM_UNIVERSE_STORE_INLINE_IDS = 128u,
};

typedef struct {
    TermUniverseStoreFrame *frames;
    uint32_t len, cap;
    AtomId *ids;
    uint32_t ids_len, ids_cap;
    TermUniverseStoreFrame inline_frames[TERM_UNIVERSE_STORE_INLINE_FRAMES];
    AtomId inline_ids[TERM_UNIVERSE_STORE_INLINE_IDS];
} TermUniverseStoreStack;

static void term_universe_store_stack_init(TermUniverseStoreStack *stack) {
    stack->frames = stack->inline_frames;
    stack->len = 0u;
    stack->cap = TERM_UNIVERSE_STORE_INLINE_FRAMES;
    stack->ids = stack->inline_ids;
    stack->ids_len = 0u;
    stack->ids_cap = TERM_UNIVERSE_STORE_INLINE_IDS;
}

static void term_universe_store_stack_free(TermUniverseStoreStack *stack) {
    if (stack->frames != stack->inline_frames)
        free(stack->frames);
    if (stack->ids != stack->inline_ids)
        free(stack->ids);
}

/* Grow `*buffer`, which starts as `inline_buffer`, to at least `need`
 * elements of `size` bytes. */
static bool term_universe_store_stack_grow(void **buffer,
                                           const void *inline_buffer,
                                           uint32_t *cap, uint32_t used,
                                           uint64_t need, size_t size) {
    if (need <= *cap)
        return true;
    uint64_t next = (uint64_t)*cap * 2u;
    if (next < need)
        next = need;
    if (next > UINT32_MAX || next > SIZE_MAX / size)
        return false;
    void *grown = *buffer == inline_buffer
        ? cetta_malloc((size_t)next * size)
        : cetta_realloc(*buffer, (size_t)next * size);
    if (!grown)
        return false;
    if (*buffer == inline_buffer)
        memcpy(grown, inline_buffer, (size_t)used * size);
    *buffer = grown;
    *cap = (uint32_t)next;
    return true;
}

static bool term_universe_store_stack_push(TermUniverseStoreStack *stack,
                                           Atom *atom,
                                           uint32_t parent_index) {
    if (!term_universe_store_stack_grow(
            (void **)&stack->frames, stack->inline_frames, &stack->cap,
            stack->len, (uint64_t)stack->len + 1u,
            sizeof(*stack->frames)))
        return false;
    stack->frames[stack->len++] = (TermUniverseStoreFrame){
        .atom = atom,
        .child_base = UINT32_MAX,
        .next_child = 0,
        .parent_index = parent_index,
    };
    return true;
}

/* Reserve the ids of the top frame's `count` children. */
static bool term_universe_store_stack_reserve(TermUniverseStoreStack *stack,
                                              uint32_t count) {
    if (!term_universe_store_stack_grow(
            (void **)&stack->ids, stack->inline_ids, &stack->ids_cap,
            stack->ids_len, (uint64_t)stack->ids_len + count,
            sizeof(*stack->ids)))
        return false;
    stack->frames[stack->len - 1u].child_base = stack->ids_len;
    stack->ids_len += count;
    return true;
}

/* Pop the top frame, releasing its children's ids, and give its id to the
 * frame beneath; true when it was the root, whose id is then `*root`. */
static bool term_universe_store_stack_pop(TermUniverseStoreStack *stack,
                                          AtomId id, AtomId *root) {
    TermUniverseStoreFrame *frame = &stack->frames[stack->len - 1u];
    if (frame->child_base != UINT32_MAX)
        stack->ids_len = frame->child_base;
    uint32_t parent_index = frame->parent_index;
    stack->len--;
    if (stack->len == 0u) {
        *root = id;
        return true;
    }
    stack->ids[stack->frames[stack->len - 1u].child_base + parent_index] = id;
    return false;
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
            return tu_intern_string_n(universe, src->ground.sval,
                                      src->ground.slen);
        case GV_BIGINT:
            return tu_intern_bigint(universe, atom_bigint_cstr(src));
        case GV_RATIONAL:
            return tu_intern_rational(universe, atom_rational_cstr(src));
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_BINDINGS:
        case GV_FOREIGN:
        case GV_TERM_GRAPH:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
            return CETTA_ATOM_ID_NONE;
        case GV_INTERNAL_TAG:
            return tu_intern_stable_tag(universe, src->ground.ival);
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
    return term_universe_lookup_expr_id_from_ids(
        universe, &hdr, child_ids, arity32,
        term_universe_coordinate(
            term_universe_expr_ids_slot_hash(&hdr, child_ids, arity32)), NULL);
}

/* Read-only term lookup for the common shallow case.  It shares canonical
 * child ids with the stored expression rather than creating an encoded term.
 * The complete flag distinguishes a proven miss from a shape that should use
 * the general iterative path. */
static AtomId term_universe_lookup_atom_id_borrowed(
        const TermUniverse *universe, Atom *src, uint32_t depth,
        bool *complete) {
    enum {
        TERM_UNIVERSE_LOOKUP_INLINE_CHILDREN = 16u,
        TERM_UNIVERSE_LOOKUP_MAX_DEPTH = 64u,
    };
    if (!complete || !*complete || !universe || !src) {
        if (complete)
            *complete = false;
        return CETTA_ATOM_ID_NONE;
    }
    AtomId existing_ptr = term_universe_lookup_ptr_id(universe, src);
    if (existing_ptr != CETTA_ATOM_ID_NONE)
        return existing_ptr;
    if (src->kind != ATOM_EXPR)
        return term_universe_leaf_id((TermUniverse *)universe, src, false);
    if (depth >= TERM_UNIVERSE_LOOKUP_MAX_DEPTH ||
        !cetta_expr_len_fits_u32(src->expr.len) ||
        (size_t)src->expr.len > SIZE_MAX / sizeof(AtomId)) {
        *complete = false;
        return CETTA_ATOM_ID_NONE;
    }

    AtomId inline_ids[TERM_UNIVERSE_LOOKUP_INLINE_CHILDREN];
    AtomId *child_ids =
        src->expr.len <= TERM_UNIVERSE_LOOKUP_INLINE_CHILDREN
            ? inline_ids
            : cetta_malloc((size_t)src->expr.len * sizeof(*child_ids));
    if (!child_ids) {
        *complete = false;
        return CETTA_ATOM_ID_NONE;
    }
    AtomId result = CETTA_ATOM_ID_NONE;
    for (CettaExprIndex i = 0u; i < src->expr.len; i++) {
        child_ids[i] = term_universe_lookup_atom_id_borrowed(
            universe, src->expr.elems[i], depth + 1u, complete);
        if (!*complete || child_ids[i] == CETTA_ATOM_ID_NONE)
            goto done;
    }
    result = term_universe_expr_id_from_ids(
        (TermUniverse *)universe, child_ids, src->expr.len, false);
done:
    if (child_ids != inline_ids)
        free(child_ids);
    return result;
}

static AtomId term_universe_stable_atom_id_iterative(TermUniverse *universe,
                                                     Atom *src, bool insert,
                                                     bool *out_stable) {
    TermUniverseStoreStack stack;
    term_universe_store_stack_init(&stack);
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

    if (!term_universe_store_stack_push(&stack, src, UINT32_MAX))
        goto done;

    while (stack.len > 0) {
        TermUniverseStoreFrame *frame = &stack.frames[stack.len - 1];
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
            if (term_universe_store_stack_pop(&stack, id, &result)) {
                stable = true;
                goto done;
            }
            continue;
        }

        if (frame->child_base == UINT32_MAX && atom->expr.len > 0 &&
            !term_universe_store_stack_reserve(&stack,
                                               (uint32_t)atom->expr.len))
            goto done;
        frame = &stack.frames[stack.len - 1];

        if (frame->next_child < atom->expr.len) {
            uint32_t child_index = frame->next_child++;
            if (!term_universe_store_stack_push(
                    &stack, atom->expr.elems[child_index], child_index))
                goto done;
            continue;
        }

        id = term_universe_expr_id_from_ids(
            universe,
            atom->expr.len ? &stack.ids[frame->child_base] : NULL,
            atom->expr.len, insert);
        if (id == CETTA_ATOM_ID_NONE)
            goto done;
        if (term_universe_store_stack_pop(&stack, id, &result)) {
            stable = true;
            goto done;
        }
    }

done:
    term_universe_store_stack_free(&stack);
    if (out_stable)
        *out_stable = stable;
    return result;
}

static AtomId term_universe_stable_atom_id_iterative_source_memo(
    TermUniverse *universe, Atom *src, bool insert, bool *out_stable,
    TermUniverseSourceMemoRun *source_memo) {
    TermUniverseStoreStack stack;
    term_universe_store_stack_init(&stack);
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

    if (!term_universe_store_stack_push(&stack, src, UINT32_MAX))
        goto done;

    while (stack.len > 0) {
        TermUniverseStoreFrame *frame = &stack.frames[stack.len - 1];
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
            if (term_universe_store_stack_pop(&stack, id, &result)) {
                stable = true;
                goto done;
            }
            continue;
        }

        if (frame->child_base == UINT32_MAX && atom->expr.len > 0 &&
            !term_universe_store_stack_reserve(&stack,
                                               (uint32_t)atom->expr.len))
            goto done;
        frame = &stack.frames[stack.len - 1];

        if (frame->next_child < atom->expr.len) {
            uint32_t child_index = frame->next_child++;
            Atom *child = atom->expr.elems[child_index];
            AtomId child_id = CETTA_ATOM_ID_NONE;
            bool source_hit = false;
            if (child && child->kind == ATOM_EXPR &&
                source_memo && source_memo->memo) {
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
                stack.ids[frame->child_base + child_index] = child_id;
                continue;
            }
            if (!term_universe_store_stack_push(&stack, child, child_index))
                goto done;
            continue;
        }

        id = term_universe_expr_id_from_ids(
            universe,
            atom->expr.len ? &stack.ids[frame->child_base] : NULL,
            atom->expr.len, insert);
        if (id == CETTA_ATOM_ID_NONE)
            goto done;
        if (frame->parent_index != UINT32_MAX)
            term_universe_source_memo_store(source_memo, atom, id);
        if (term_universe_store_stack_pop(&stack, id, &result)) {
            stable = true;
            goto done;
        }
    }

done:
    term_universe_store_stack_free(&stack);
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

static void term_universe_sb_emit(void *context, const char *bytes,
                                  size_t len) {
    term_universe_sb_append_span((TermUniverseStringBuilder *)context,
                                 bytes, len);
}

static void term_universe_sb_append_escaped_string(TermUniverseStringBuilder *sb,
                                                   const char *text,
                                                   size_t len) {
    term_universe_sb_append_char(sb, '"');
    if (text)
        cetta_string_literal_escape(text, len,
                                    atom_print_raw_string_bytes()
                                        ? CETTA_STRING_LITERAL_RAW_TEXT
                                        : CETTA_STRING_LITERAL_ESCAPED,
                                    false, term_universe_sb_emit, sb);
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
            term_universe_sb_append_escaped_string(
                sb, tu_string_cstr(universe, id), tu_string_len(universe, id));
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
        case GV_BINDINGS:
        case GV_FOREIGN:
        case GV_TERM_GRAPH:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
        case GV_INTERNAL_TAG:
            return;
        }
        return;
    case ATOM_EXPR: {
        CettaExprLen len = tu_arity(universe, id);
        int64_t list_tag = 0;
        /* T[...] and T{...}, touching, as the atom printer writes them. */
        if (len == 3u &&
            tu_kind(universe, tu_child(universe, id, 0)) == ATOM_SYMBOL &&
            tu_sym(universe, tu_child(universe, id, 0)) ==
                g_builtin_syms.prime_meta &&
            tu_kind(universe, tu_child(universe, id, 2)) == ATOM_EXPR &&
            tu_arity(universe, tu_child(universe, id, 2)) > 0u) {
            int64_t argument_tag = tu_internal_tag(
                universe, tu_child(universe, tu_child(universe, id, 2), 0));
            if (argument_tag == (int64_t)CETTA_INTERNAL_TAG_LIST ||
                argument_tag == (int64_t)CETTA_INTERNAL_TAG_PRIME_BRACES) {
                term_universe_sb_append_atom_text(sb, universe,
                                                  tu_child(universe, id, 1));
                term_universe_sb_append_atom_text(sb, universe,
                                                  tu_child(universe, id, 2));
                return;
            }
        }
        if (len > 0u) {
            const CettaTermHdr *head = tu_hdr(universe, tu_child(universe, id, 0));
            if (head && (AtomKind)head->tag == ATOM_GROUNDED &&
                (GroundedKind)head->subtag == GV_INTERNAL_TAG)
                list_tag = (int64_t)term_universe_aux_data(head);
        }
        if (cetta_internal_tag_is_list(list_tag)) {
            /* [x1 x2] and [x1 | rest], as the atom printer writes them */
            term_universe_sb_append_char(sb, '[');
            for (CettaExprIndex i = 1; i < len; i++) {
                if (i != 1)
                    term_universe_sb_append_cstr(
                        sb, list_tag == CETTA_INTERNAL_TAG_LIST_REST &&
                                    i + 1u == len
                                ? " | " : " ");
                term_universe_sb_append_atom_text(sb, universe, tu_child(universe, id, i));
            }
            term_universe_sb_append_char(sb, ']');
            return;
        }
        if (list_tag == (int64_t)CETTA_INTERNAL_TAG_PRIME_BRACES) {
            /* {x1 x2}, as the atom printer writes it */
            term_universe_sb_append_char(sb, '{');
            for (CettaExprIndex i = 1; i < len; i++) {
                if (i != 1)
                    term_universe_sb_append_char(sb, ' ');
                term_universe_sb_append_atom_text(sb, universe, tu_child(universe, id, i));
            }
            term_universe_sb_append_char(sb, '}');
            return;
        }
        term_universe_sb_append_char(sb, '(');
        for (CettaExprIndex i = 0; i < len; i++) {
            if (i != 0)
                term_universe_sb_append_char(sb, ' ');
            term_universe_sb_append_atom_text(sb, universe, tu_child(universe, id, i));
        }
        term_universe_sb_append_char(sb, ')');
        return;
    }
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
            bytes += tu_string_len(universe, id) + 1u;
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

static Atom *term_universe_copy_atom_finish(const TermUniverse *universe,
                                          Arena *dst, AtomId id,
                                          uint32_t epoch,
                                          bool rename_epoch_vars,
                                          TermUniverseCopyMemo *memo,
                                          Atom **children) {
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
            Atom *key = children ? children[0] : NULL;
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
            out = atom_string_n(dst, tu_string_cstr(universe, id),
                                tu_string_len(universe, id));
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
        case GV_BINDINGS:
        case GV_FOREIGN:
        case GV_TERM_GRAPH:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
            return NULL;
        case GV_INTERNAL_TAG:
            out = atom_internal_tag(
                dst, (CettaInternalTag)term_universe_aux_data(hdr));
            break;
        }
        break;
    case ATOM_EXPR: {
        CettaExprLen len = tu_arity(universe, id);
        out = atom_expr_shared(dst, children, len);
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

typedef struct {
    AtomId id;
    Atom **target;
    Atom **children;
    bool rename_epoch_vars;
    bool finish;
} TermUniverseCopyFrame;

static bool term_universe_copy_frame_push(TermUniverseCopyFrame **frames,
                                          size_t *len, size_t *cap,
                                          TermUniverseCopyFrame frame) {
    if (*len == *cap) {
        size_t next = *cap ? *cap * 2u : 64u;
        if (next < *cap || next > SIZE_MAX / sizeof(**frames))
            return false;
        *frames = cetta_realloc(*frames, next * sizeof(**frames));
        *cap = next;
    }
    (*frames)[(*len)++] = frame;
    return true;
}

/* Postorder copying uses a heap work stack, so a deep closed proof article
 * does not consume the C call stack. The existing memo still preserves DAG
 * sharing, and structural variable name keys remain outside epoch renaming. */
static Atom *term_universe_copy_atom_impl(const TermUniverse *universe,
                                          Arena *dst, AtomId id,
                                          uint32_t epoch,
                                          bool rename_epoch_vars,
                                          TermUniverseCopyMemo *memo) {
    if (!universe || !dst || id == CETTA_ATOM_ID_NONE)
        return NULL;
    TermUniverseCopyFrame *frames = NULL;
    size_t len = 0, cap = 0;
    Atom *out = NULL;
    if (!term_universe_copy_frame_push(&frames, &len, &cap,
            (TermUniverseCopyFrame){id, &out, NULL, rename_epoch_vars, false}))
        return NULL;
    while (len) {
        TermUniverseCopyFrame frame = frames[--len];
        if (frame.id == CETTA_ATOM_ID_NONE)
            goto failed;
        Atom *cached = term_universe_copy_memo_lookup(memo, frame.id);
        if (cached) {
            cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_COPY_MEMO_HIT);
            *frame.target = cached;
            continue;
        }
        const CettaTermHdr *hdr = tu_hdr(universe, frame.id);
        bool name_key = hdr && hdr->tag == ATOM_VAR &&
            hdr->subtag == CETTA_VAR_SPELLING_NAME_KEY;
        CettaExprLen count = name_key ? 1u :
            (hdr && hdr->tag == ATOM_EXPR ? tu_arity(universe, frame.id) : 0u);
        if (!frame.finish && count) {
            if (!cetta_expr_len_mul_fits_size(count, sizeof(Atom *)))
                goto failed;
            frame.children = arena_alloc(dst, (size_t)count * sizeof(Atom *));
            frame.finish = true;
            if (!term_universe_copy_frame_push(&frames, &len, &cap, frame))
                goto failed;
            for (CettaExprLen i = count; i > 0u; --i) {
                AtomId child = name_key ? tu_var_name_key_id(universe, frame.id) :
                    tu_child(universe, frame.id, i - 1u);
                if (!term_universe_copy_frame_push(&frames, &len, &cap,
                        (TermUniverseCopyFrame){child, &frame.children[i - 1u], NULL,
                            !name_key && frame.rename_epoch_vars, false}))
                    goto failed;
            }
            continue;
        }
        *frame.target = term_universe_copy_atom_finish(universe, dst, frame.id,
            epoch, frame.rename_epoch_vars, memo, frame.children);
        if (!*frame.target)
            goto failed;
    }
    free(frames);
    return out;
failed:
    free(frames);
    return NULL;
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
    if (hdr->tag == ATOM_GROUNDED && (GroundedKind)hdr->subtag == GV_STRING) {
        size_t len = tu_string_len(universe, id);
        char *copy = arena_alloc(a, len + 1u);
        memcpy(copy, tu_string_cstr(universe, id), len);
        copy[len] = '\0';
        return copy;
    }

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
    width = term_universe_intern_pair_width(width);
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
    /* Each pair moves by its tag alone; no record is read. */
    if (universe->intern_slots) {
        for (size_t i = 0; i <= universe->intern_mask; i++) {
            uint32_t tag = term_universe_intern_tag_at(
                universe->intern_slots, width, i);
            if (tag == 0u)
                continue;
            AtomId id = term_universe_intern_id_at(
                universe->intern_slots, width, i);
            for (size_t probe = 0; probe < size; probe++) {
                size_t idx = term_universe_intern_index(
                    tag, probe, next_mask);
                if (term_universe_intern_tag_at(next, width, idx) == 0u) {
                    term_universe_intern_store_pair(
                        next, width, idx, tag, id);
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
        {
            /* A record is one term: a float by its bits. */
            double stored = term_universe_load_double(payload);
            return memcmp(&src->ground.fval, &stored, sizeof(stored)) == 0;
        }
        case GV_BOOL:
            return src->ground.bval == (term_universe_aux_data(hdr) != 0);
        case GV_STRING: {
            uint32_t len = term_universe_aux_data(hdr);
            return src->ground.slen == len &&
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
        case GV_BINDINGS:
        case GV_FOREIGN:
        case GV_TERM_GRAPH:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
            return false;
        case GV_INTERNAL_TAG:
            return src->ground.ival == (int64_t)term_universe_aux_data(hdr);
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
    if (!universe || !src)
        return CETTA_ATOM_ID_NONE;
    if (src->kind == ATOM_GROUNDED && src->ground.gkind == GV_INT)
        return term_universe_lookup_integer(universe, src->ground.ival, NULL);
    if (!universe->intern_slots)
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
    uint32_t tag = term_universe_intern_tag(
        term_universe_coordinate(term_universe_atom_slot_hash(src)));
    size_t width = term_universe_intern_width(universe);
    for (size_t probe = 0; probe <= universe->intern_mask; probe++) {
        size_t idx = term_universe_intern_index(
            tag, probe, universe->intern_mask);
        uint32_t have_tag = term_universe_intern_tag_at(
            universe->intern_slots, width, idx);
        if (have_tag == 0u)
            return CETTA_ATOM_ID_NONE;
        if (have_tag != tag)
            continue;
        AtomId id = term_universe_intern_id_at(
            universe->intern_slots, width, idx);
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

AtomId term_universe_lookup_expression_coordinates(
        const TermUniverse *universe, Atom *const *coordinates,
        CettaExprLen coordinate_count) {
    enum { TERM_UNIVERSE_LOOKUP_INLINE_COORDINATES = 16u };
    if (!universe ||
        (coordinate_count > 0u && !coordinates) ||
        !cetta_expr_len_mul_fits_size(
            coordinate_count, sizeof(AtomId))) {
        return CETTA_ATOM_ID_NONE;
    }
    AtomId inline_ids[TERM_UNIVERSE_LOOKUP_INLINE_COORDINATES];
    AtomId *child_ids =
        coordinate_count <= TERM_UNIVERSE_LOOKUP_INLINE_COORDINATES
            ? inline_ids
            : cetta_malloc((size_t)coordinate_count * sizeof(*child_ids));
    if (!child_ids)
        return CETTA_ATOM_ID_NONE;
    AtomId result = CETTA_ATOM_ID_NONE;
    for (CettaExprIndex index = 0u;
         index < coordinate_count; index++) {
        bool complete = true;
        child_ids[index] = term_universe_lookup_atom_id_borrowed(
            universe, coordinates[index], 0u, &complete);
        if (!complete) {
            child_ids[index] = term_universe_lookup_atom_id(
                universe, coordinates[index]);
        }
        if (child_ids[index] == CETTA_ATOM_ID_NONE)
            goto done;
    }
    result = term_universe_expr_id_from_ids(
        (TermUniverse *)universe, child_ids, coordinate_count, false);
done:
    if (child_ids != inline_ids)
        free(child_ids);
    return result;
}

AtomId term_universe_lookup_symbol_application(
        const TermUniverse *universe, SymbolId head,
        Atom *const *arguments, CettaExprLen argument_count) {
    enum { TERM_UNIVERSE_LOOKUP_INLINE_APPLICATION = 16u };
    if (!universe || head == SYMBOL_ID_NONE ||
        (argument_count > 0u && !arguments) ||
        argument_count == UINT64_MAX ||
        !cetta_expr_len_mul_fits_size(
            argument_count + 1u, sizeof(AtomId))) {
        return CETTA_ATOM_ID_NONE;
    }

    CettaExprLen coordinate_count = argument_count + 1u;
    AtomId inline_ids[TERM_UNIVERSE_LOOKUP_INLINE_APPLICATION];
    AtomId *child_ids =
        coordinate_count <= TERM_UNIVERSE_LOOKUP_INLINE_APPLICATION
            ? inline_ids
            : cetta_malloc((size_t)coordinate_count * sizeof(*child_ids));
    if (!child_ids)
        return CETTA_ATOM_ID_NONE;

    CettaTermHdr head_hdr = {0};
    head_hdr.tag = (uint8_t)ATOM_SYMBOL;
    head_hdr.sym_or_head = head;
    head_hdr.aux32 = term_universe_aux_make(0u, false);
    head_hdr.hash32 = term_universe_hash_symbol_id(head);
    child_ids[0] = term_universe_lookup_record_id(
        universe, &head_hdr, NULL, 0u);
    AtomId result = CETTA_ATOM_ID_NONE;
    if (child_ids[0] == CETTA_ATOM_ID_NONE)
        goto done;

    for (CettaExprIndex index = 0u;
         index < argument_count; index++) {
        bool complete = true;
        child_ids[index + 1u] = term_universe_lookup_atom_id_borrowed(
            universe, arguments[index], 0u, &complete);
        if (!complete) {
            child_ids[index + 1u] = term_universe_lookup_atom_id(
                universe, arguments[index]);
        }
        if (child_ids[index + 1u] == CETTA_ATOM_ID_NONE)
            goto done;
    }
    result = term_universe_expr_id_from_ids(
        (TermUniverse *)universe, child_ids, coordinate_count, false);
done:
    if (child_ids != inline_ids)
        free(child_ids);
    return result;
}

bool term_universe_root_token_capture(
        const TermUniverse *universe, AtomId root_id,
        TermUniverseRootToken *out) {
    if (out)
        *out = (TermUniverseRootToken){0};
    if (!universe || !out || root_id == CETTA_ATOM_ID_NONE ||
        !term_universe_get_atom(universe, root_id)) {
        return false;
    }
    *out = (TermUniverseRootToken){
        .universe = universe,
        .instance_id = universe->instance_id,
        .storage_epoch = universe->storage_epoch,
        .root_id = root_id,
    };
    return true;
}

bool term_universe_root_token_matches_live_universe(
        TermUniverseRootToken token,
        const TermUniverse *live_universe) {
    return live_universe && token.universe == live_universe &&
        token.instance_id != 0u &&
        token.instance_id == live_universe->instance_id &&
        token.storage_epoch != 0u &&
        token.storage_epoch == live_universe->storage_epoch &&
        token.root_id != CETTA_ATOM_ID_NONE &&
        term_universe_get_atom(live_universe, token.root_id) != NULL;
}

Atom *term_universe_root_token_resolve(
        TermUniverseRootToken token,
        const TermUniverse *live_universe) {
    return term_universe_root_token_matches_live_universe(
               token, live_universe)
        ? term_universe_get_atom(live_universe, token.root_id)
        : NULL;
}

bool term_universe_atom_id_eq(const TermUniverse *universe, AtomId id,
                              Atom *src) {
    if (!universe || id == CETTA_ATOM_ID_NONE || !src)
        return false;
    return term_universe_entry_eq_atom(universe, id, src);
}

static bool term_universe_insert_stable_id(TermUniverse *universe, AtomId id) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    if (hdr && hdr->tag == ATOM_GROUNDED && hdr->subtag == GV_INT)
        return term_universe_lookup_integer(universe,
            term_universe_load_i64(term_universe_payload(universe, id)), NULL) == id;
    const TermEntry *entry =
        universe && universe->intern_slots ? term_universe_entry(universe, id)
                                           : NULL;
    if (!entry || !tu_hdr(universe, id))
        return false;
    uint32_t tag = term_universe_intern_tag(entry->coordinate);
    size_t width = term_universe_intern_width(universe);
    if (width == 8u && id >= UINT32_MAX)
        return false;
    for (size_t probe = 0; probe <= universe->intern_mask; probe++) {
        size_t idx = term_universe_intern_index(
            tag, probe, universe->intern_mask);
        uint32_t have_tag = term_universe_intern_tag_at(
            universe->intern_slots, width, idx);
        if (have_tag == 0u) {
            term_universe_intern_store_pair(
                universe->intern_slots, width, idx, tag, id);
            universe->intern_used++;
            return true;
        }
        if (have_tag == tag &&
            term_universe_intern_id_at(universe->intern_slots, width, idx) ==
                id)
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
            return tu_intern_string_n(universe, src->ground.sval,
                                      src->ground.slen);
        case GV_BIGINT:
            return tu_intern_bigint(universe, atom_bigint_cstr(src));
        case GV_RATIONAL:
            return tu_intern_rational(universe, atom_rational_cstr(src));
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_BINDINGS:
        case GV_FOREIGN:
        case GV_TERM_GRAPH:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
            /* Filtered by the stable-grounded check above; keep the switch
               exhaustive for -Wswitch cleanliness. */
            return CETTA_ATOM_ID_NONE;
        case GV_INTERNAL_TAG:
            return tu_intern_stable_tag(universe, src->ground.ival);
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
    source_memo = term_universe_source_memo_begin(
        universe, source_arena, src);

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
            return atom_string_n(dst, (const char *)payload,
                                 term_universe_aux_data(hdr));
        case GV_BIGINT:
            return atom_bigint(dst, (const char *)payload);
        case GV_RATIONAL:
            return atom_rational(dst, (const char *)payload);
        case GV_SPACE:
        case GV_STATE:
        case GV_CAPTURE:
        case GV_BINDINGS:
        case GV_FOREIGN:
        case GV_TERM_GRAPH:
        case GV_PRIME_NEED_CAPABILITY:
        case GV_PRIME_CONTEXT:
            return NULL;
        case GV_INTERNAL_TAG:
            return cetta_internal_tag_is_term_stable(
                       (int64_t)term_universe_aux_data(hdr))
                ? atom_internal_tag(
                      dst, (CettaInternalTag)term_universe_aux_data(hdr))
                : NULL;
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

/* Decode one entry and remember the atom.  The children of an expression are
 * decoded on the way, so the depth of this call is the depth of the entries
 * under it that are not decoded yet. */
static Atom *term_universe_decode_entry(TermUniverse *universe, AtomId id) {
    size_t physical_index = 0;
    if (!term_universe_atom_id_to_physical_index(universe, id, &physical_index))
        return NULL;
    TermEntry *entry = &universe->entries[physical_index];
    if (entry->decoded_cache)
        return entry->decoded_cache;
    if (!term_universe_entry_has_blob(entry))
        return NULL;
    Atom *decoded = term_universe_decode_atom(universe, id);
    entry = &universe->entries[physical_index];
    entry->decoded_cache = decoded;
    if (entry->decoded_cache) {
        TU_DIAG_INC(universe, lazy_decode_count);
        cetta_runtime_stats_inc(CETTA_RUNTIME_COUNTER_TERM_UNIVERSE_LAZY_DECODE);
    }
    term_universe_track_ptr_id(universe, id);
    return entry->decoded_cache;
}

/* The expression child of an expression entry, from its `next` child on,
 * that has a blob and is not decoded yet.  Children of other kinds decode
 * without recursion and are left to the decoding of the expression. */
static bool term_universe_next_undecoded_expr_child(
    const TermUniverse *universe, AtomId id, uint32_t *next,
    AtomId *child_out) {
    const CettaTermHdr *hdr = tu_hdr(universe, id);
    const uint8_t *payload = term_universe_payload(universe, id);
    if (!hdr || !payload || (AtomKind)hdr->tag != ATOM_EXPR)
        return false;
    uint32_t len = term_universe_aux_data(hdr);
    size_t atom_id_width = term_universe_atom_id_storage_width_bytes(universe);
    while (*next < len) {
        AtomId child_id = term_universe_load_stored_atom_id(
            universe, payload + ((size_t)*next * atom_id_width));
        (*next)++;
        const TermEntry *child = term_universe_entry(universe, child_id);
        if (!child || child->decoded_cache ||
            !term_universe_entry_has_blob(child))
            continue;
        const CettaTermHdr *child_hdr = tu_hdr(universe, child_id);
        if (child_hdr && (AtomKind)child_hdr->tag == ATOM_EXPR) {
            *child_out = child_id;
            return true;
        }
    }
    return false;
}

/* An expression is decoded without recursing on its depth: the expressions
 * under it that are not decoded yet are decoded first, deepest first, from a
 * stack of ids, so that each one finds its expression children decoded. */
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
    uint32_t probe = 0u;
    AtomId first_child = CETTA_ATOM_ID_NONE;
    if (!term_universe_next_undecoded_expr_child(
            universe, id, &probe, &first_child))
        return term_universe_decode_entry(mutable_universe, id);

    typedef struct {
        AtomId id;
        uint32_t next;
    } DecodeFrame;
    enum { DECODE_INLINE_FRAMES = 32u };
    DecodeFrame inline_frames[DECODE_INLINE_FRAMES];
    DecodeFrame *frames = inline_frames;
    size_t len = 0u, cap = DECODE_INLINE_FRAMES;
    frames[len++] = (DecodeFrame){.id = id, .next = 0u};
    Atom *result = NULL;
    while (len > 0u) {
        DecodeFrame *top = &frames[len - 1u];
        AtomId child = CETTA_ATOM_ID_NONE;
        if (term_universe_next_undecoded_expr_child(
                universe, top->id, &top->next, &child)) {
            if (len == cap) {
                if (cap > SIZE_MAX / 2u / sizeof *frames)
                    break;
                size_t next_cap = cap * 2u;
                DecodeFrame *grown = frames == inline_frames
                    ? cetta_malloc(sizeof *frames * next_cap)
                    : cetta_realloc(frames, sizeof *frames * next_cap);
                if (frames == inline_frames)
                    memcpy(grown, inline_frames, sizeof *frames * len);
                frames = grown;
                cap = next_cap;
            }
            frames[len++] = (DecodeFrame){.id = child, .next = 0u};
            continue;
        }
        Atom *decoded = term_universe_decode_entry(mutable_universe,
                                                   frames[len - 1u].id);
        if (!decoded)
            break;
        if (--len == 0u)
            result = decoded;
    }
    if (frames != inline_frames)
        free(frames);
    return result;
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
