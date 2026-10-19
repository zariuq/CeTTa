/* mm0_base.c - allocation, name maps, step accounting and theory tables. */
#include "mm0_internal.h"

#include <stdatomic.h>
#include <stdlib.h>

_Static_assert(alignof(uint64_t) <= 8 && alignof(void *) <= 8,
               "arena alignment covers every stored type");

const char *mm0_status_name(Mm0Status status) {
    switch (status) {
    case MM0_OK: return "OK";
    case MM0_MALFORMED: return "MALFORMED";
    case MM0_REFUSED: return "REFUSED";
    case MM0_UNSUPPORTED: return "UNSUPPORTED";
    case MM0_LIMIT: return "LIMIT";
    case MM0_NOMEM: return "NOMEM";
    case MM0_INTERRUPTED: return "INTERRUPTED";
    case MM0_FOREIGN: return "FOREIGN";
    case MM0_CAPACITY: return "CAPACITY";
    case MM0_CLOSED: return "CLOSED";
    }
    return "UNKNOWN";
}

void mm0_options_init(Mm0Options *options) {
    *options = (Mm0Options){.budget = UINT64_MAX};
}

void mm0_error_set(Mm0Error *error, Mm0Status status, const char *reason) {
    if (!error || error->status != MM0_OK) return;
    error->status = status;
    error->reason = reason;
}

/* ---- arena ------------------------------------------------------------ */

struct Mm0Chunk {
    Mm0Chunk *prev;
    size_t size;
    max_align_t data[];
};

#define MM0_CHUNK_FIRST ((size_t)64 << 10)
#define MM0_CHUNK_DOUBLING_CEILING ((size_t)64 << 20)

void mm0_arena_init(Mm0Arena *arena) { *arena = (Mm0Arena){0}; }

void mm0_arena_free(Mm0Arena *arena) {
    for (Mm0Chunk *c = arena->chunk; c;) {
        Mm0Chunk *prev = c->prev;
        free(c);
        c = prev;
    }
    free(arena->spare);
    *arena = (Mm0Arena){0};
}

/* The inline fast path has rounded `size` up to 8 unless it overflowed. */
void *mm0_arena_alloc_slow(Mm0Arena *arena, size_t size) {
    if (size > SIZE_MAX - 7) return NULL;
    size = (size + 7) & ~(size_t)7;
    Mm0Chunk *chunk = NULL;
    if (arena->spare && arena->spare->size >= size) {
        chunk = arena->spare;
        arena->spare = NULL;
    } else {
        size_t want = arena->chunk ? arena->chunk->size * 2 : MM0_CHUNK_FIRST;
        if (want > MM0_CHUNK_DOUBLING_CEILING) want = MM0_CHUNK_DOUBLING_CEILING;
        if (want < size) want = size;
        if (want > SIZE_MAX - sizeof(Mm0Chunk)) return NULL;
        chunk = malloc(sizeof(Mm0Chunk) + want);
        if (!chunk) return NULL;
        chunk->size = want;
    }
    chunk->prev = arena->chunk;
    arena->chunk = chunk;
    arena->held += chunk->size;
    arena->cur = (char *)chunk->data + size;
    arena->end = (char *)chunk->data + chunk->size;
    return chunk->data;
}

Mm0ArenaMark mm0_arena_mark(const Mm0Arena *arena) {
    return (Mm0ArenaMark){arena->chunk, arena->cur};
}

void mm0_arena_reset(Mm0Arena *arena, Mm0ArenaMark mark) {
    while (arena->chunk != mark.chunk) {
        Mm0Chunk *c = arena->chunk;
        arena->chunk = c->prev;
        arena->held -= c->size;
        if (!arena->spare || arena->spare->size < c->size) {
            free(arena->spare);
            arena->spare = c;
        } else {
            free(c);
        }
    }
    arena->cur = mark.cur;
    arena->end = arena->chunk ? (char *)arena->chunk->data + arena->chunk->size : NULL;
}

char *mm0_arena_strndup(Mm0Arena *arena, const char *text, size_t len) {
    if (len == SIZE_MAX) return NULL;
    char *out = mm0_arena_alloc(arena, len + 1);
    if (!out) return NULL;
    memcpy(out, text, len);
    out[len] = 0;
    return out;
}

bool mm0_reserve(void **items, size_t *capacity, size_t need, size_t size) {
    if (need <= *capacity) return true;
    size_t next = *capacity ? *capacity : 8;
    while (next < need) {
        if (next > SIZE_MAX / 2) return false;
        next *= 2;
    }
    if (next > SIZE_MAX / size) return false;
    void *grown = realloc(*items, next * size);
    if (!grown) return false;
    *items = grown;
    *capacity = next;
    return true;
}

/* ---- name maps ---------------------------------------------------------- */

uint64_t mm0_hash_bytes(const void *data, size_t len) {
    const uint8_t *p = data;
    uint64_t h = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < len; ++i) h = (h ^ p[i]) * UINT64_C(1099511628211);
    h ^= h >> 32;
    return h ? h : 1;
}

void mm0_names_free(Mm0NameMap *map) {
    free(map->slots);
    *map = (Mm0NameMap){0};
}

static size_t names_find(const Mm0NameMap *map, const char *key, size_t len, uint64_t h) {
    size_t mask = map->capacity - 1, i = (size_t)h & mask;
    while (map->slots[i].key &&
           (map->slots[i].hash != h || map->slots[i].len != len ||
            memcmp(map->slots[i].key, key, len) != 0))
        i = (i + 1) & mask;
    return i;
}

bool mm0_names_get(const Mm0NameMap *map, const char *key, size_t len, uint32_t *value) {
    if (!map->count || len > UINT32_MAX) return false;
    size_t i = names_find(map, key, len, mm0_hash_bytes(key, len));
    if (!map->slots[i].key) return false;
    *value = map->slots[i].value;
    return true;
}

static bool names_grow(Mm0NameMap *map) {
    size_t capacity = map->capacity ? map->capacity * 2 : 16;
    if (capacity < map->capacity || capacity > SIZE_MAX / sizeof(Mm0NameSlot)) return false;
    Mm0NameSlot *slots = calloc(capacity, sizeof *slots);
    if (!slots) return false;
    for (size_t i = 0; i < map->capacity; ++i) {
        if (!map->slots[i].key) continue;
        size_t j = (size_t)map->slots[i].hash & (capacity - 1);
        while (slots[j].key) j = (j + 1) & (capacity - 1);
        slots[j] = map->slots[i];
    }
    free(map->slots);
    map->slots = slots;
    map->capacity = capacity;
    return true;
}

bool mm0_names_put(Mm0NameMap *map, const char *key, size_t len, uint32_t value) {
    if (len > UINT32_MAX) return false;
    if ((map->count + 1) * 2 > map->capacity && !names_grow(map)) return false;
    uint64_t h = mm0_hash_bytes(key, len);
    size_t i = names_find(map, key, len, h);
    map->slots[i] = (Mm0NameSlot){key, (uint32_t)len, value, h};
    ++map->count;
    return true;
}

/* Backward-shift deletion keeps probe chains intact without tombstones. */
void mm0_names_remove(Mm0NameMap *map, const char *key, size_t len) {
    if (!map->count) return;
    size_t mask = map->capacity - 1;
    size_t hole = names_find(map, key, len, mm0_hash_bytes(key, len));
    if (!map->slots[hole].key) return;
    for (size_t i = (hole + 1) & mask; map->slots[i].key; i = (i + 1) & mask) {
        size_t home = (size_t)map->slots[i].hash & mask;
        if (((hole - home) & mask) < ((i - home) & mask)) {
            map->slots[hole] = map->slots[i];
            hole = i;
        }
    }
    map->slots[hole] = (Mm0NameSlot){0};
    --map->count;
}

/* ---- steps ------------------------------------------------------------- */

#define MM0_POLL_INTERVAL 4096u

static uint32_t next_window(const Mm0Steps *steps) {
    if (steps->budget == UINT64_MAX) return MM0_POLL_INTERVAL;
    uint64_t left = steps->budget - steps->used;   /* used <= budget here */
    return left < MM0_POLL_INTERVAL ? (uint32_t)left + 1 : MM0_POLL_INTERVAL;
}

void mm0_steps_init(Mm0Steps *steps, const Mm0Options *options) {
    steps->budget = options ? options->budget : UINT64_MAX;
    steps->used = 0;
    steps->poll = options ? options->poll : NULL;
    steps->poll_context = options ? options->poll_context : NULL;
    steps->window = steps->until_check = next_window(steps);
}

Mm0Status mm0_steps_slow(Mm0Steps *steps) {
    steps->used += steps->window;
    if (steps->budget != UINT64_MAX && steps->used > steps->budget) {
        /* every later step is refused too */
        steps->used = steps->budget;
        steps->window = steps->until_check = 1;
        return MM0_LIMIT;
    }
    if (steps->poll && steps->poll(steps->poll_context)) {
        steps->window = steps->until_check = 1;
        return MM0_INTERRUPTED;
    }
    steps->window = steps->until_check = next_window(steps);
    return MM0_OK;
}

/* ---- theories ---------------------------------------------------------- */

static atomic_uint_fast64_t g_theory_identity = 1;

Mm0Theory *mm0_theory_new(void) {
    Mm0Theory *theory = calloc(1, sizeof *theory);
    if (!theory) return NULL;
    mm0_arena_init(&theory->arena);
    theory->identity = atomic_fetch_add(&g_theory_identity, 1);
    return theory;
}

void mm0_theory_free(Mm0Theory *theory) {
    if (!theory) return;
    mm0_arena_free(&theory->arena);
    free(theory->sorts);
    free(theory->terms);
    free(theory->thms);
    free(theory->order);
    mm0_names_free(&theory->sort_names);
    mm0_names_free(&theory->term_names);
    mm0_names_free(&theory->thm_names);
    free(theory);
}

static bool push_order(Mm0Theory *theory, uint8_t kind, size_t index) {
    if (!MM0_RESERVE(theory->order, theory->cap_order, theory->norder + 1)) return false;
    theory->order[theory->norder++] = (Mm0DeclRef){kind, (uint32_t)index};
    return true;
}

/* Every table, the name map and the order list are grown before any of them
 * changes, so a failed push leaves the theory as it was. */
bool mm0_theory_push_sort(Mm0Theory *theory, const char *name, uint8_t mods) {
    if (theory->nsorts >= UINT32_MAX ||
        !MM0_RESERVE(theory->sorts, theory->cap_sorts, theory->nsorts + 1) ||
        !MM0_RESERVE(theory->order, theory->cap_order, theory->norder + 1))
        return false;
    if (name && !mm0_names_put(&theory->sort_names, name, strlen(name),
                               (uint32_t)theory->nsorts))
        return false;
    theory->sorts[theory->nsorts] = (Mm0SortDecl){name, mods};
    push_order(theory, MM0_KIND_SORT, theory->nsorts++);
    return true;
}

bool mm0_theory_push_term(Mm0Theory *theory, const Mm0TermDecl *decl) {
    if (theory->nterms >= UINT32_MAX ||
        !MM0_RESERVE(theory->terms, theory->cap_terms, theory->nterms + 1) ||
        !MM0_RESERVE(theory->order, theory->cap_order, theory->norder + 1))
        return false;
    if (decl->name && !mm0_names_put(&theory->term_names, decl->name, strlen(decl->name),
                                     (uint32_t)theory->nterms))
        return false;
    theory->terms[theory->nterms] = *decl;
    theory->terms[theory->nterms].order = theory->norder;
    push_order(theory, MM0_KIND_TERM, theory->nterms++);
    return true;
}

bool mm0_theory_push_thm(Mm0Theory *theory, const Mm0ThmDecl *decl) {
    if (theory->nthms >= UINT32_MAX ||
        !MM0_RESERVE(theory->thms, theory->cap_thms, theory->nthms + 1) ||
        !MM0_RESERVE(theory->order, theory->cap_order, theory->norder + 1))
        return false;
    if (decl->name && !mm0_names_put(&theory->thm_names, decl->name, strlen(decl->name),
                                     (uint32_t)theory->nthms))
        return false;
    theory->thms[theory->nthms] = *decl;
    theory->thms[theory->nthms].order = theory->norder;
    push_order(theory, MM0_KIND_THM, theory->nthms++);
    return true;
}

void mm0_theory_rollback(Mm0Theory *theory, size_t count, Mm0ArenaMark mark) {
    while (theory->norder > count) {
        Mm0DeclRef ref = theory->order[--theory->norder];
        const char *name = NULL;
        Mm0NameMap *names = NULL;
        switch (ref.kind) {
        case MM0_KIND_SORT:
            name = theory->sorts[--theory->nsorts].name; names = &theory->sort_names; break;
        case MM0_KIND_TERM:
            name = theory->terms[--theory->nterms].name; names = &theory->term_names; break;
        default:
            name = theory->thms[--theory->nthms].name; names = &theory->thm_names; break;
        }
        if (name) mm0_names_remove(names, name, strlen(name));
    }
    mm0_arena_reset(&theory->arena, mark);
}

Mm0TheoryStats mm0_theory_stats(const Mm0Theory *theory) {
    Mm0TheoryStats stats = {theory->nsorts, theory->nterms, theory->nthms, 0};
    stats.bytes = sizeof *theory + theory->arena.held +
        theory->cap_sorts * sizeof *theory->sorts +
        theory->cap_terms * sizeof *theory->terms +
        theory->cap_thms * sizeof *theory->thms +
        theory->cap_order * sizeof *theory->order +
        (theory->sort_names.capacity + theory->term_names.capacity +
         theory->thm_names.capacity) * sizeof(Mm0NameSlot);
    return stats;
}

Mm0Expr *mm0_expr_var(Mm0Arena *arena, uint32_t index) {
    Mm0Expr *e = mm0_arena_alloc(arena, sizeof *e);
    if (e) *e = (Mm0Expr){.head = index, .nargs = 0, .var = 1};
    return e;
}

Mm0Expr *mm0_expr_app(Mm0Arena *arena, uint32_t term, uint32_t nargs) {
#if SIZE_MAX <= UINT32_MAX
    if (nargs > (SIZE_MAX - sizeof(Mm0Expr)) / sizeof(Mm0Expr *)) return NULL;
#endif
    Mm0Expr *e = mm0_arena_alloc(arena, sizeof *e + (size_t)nargs * sizeof(Mm0Expr *));
    if (e) {
        e->head = term;
        e->nargs = nargs;
        e->var = 0;
    }
    return e;
}
