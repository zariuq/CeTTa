#define _POSIX_C_SOURCE 200809L
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * One already-reconciled unary outer relation, Schema(class), and a Range/3
 * recipe relation. Look up a bound recipe coordinate, check the other
 * coordinates, then seek the resulting classes. Position, class and payload
 * are distinct. This is not an implementation of congruence closure.
 */
enum { KEYS = 64, TYPES = 64, BLOCK = 512, WORDS = BLOCK / 64, EVENTS = 1800 };
enum { NO_TYPE = TYPES, ZERO = 0, ONE = 1 };
typedef enum { SCAN, RUN, BITMAP, BLOCKED } Layout;
typedef enum { RENT, PREPAY, PROMOTE, ROUTE } Policy;
typedef enum { INSERT, DELETE, RECIPE, COUNT, FIRST, ORDERED } Action;
typedef uint32_t RowId;
#define NO_ROW UINT32_MAX

typedef struct { uint8_t type, lo, hi; } Recipe;
typedef struct { uint8_t key, alive; uint16_t payload; } Row;
typedef struct { Action action; RowId id; uint8_t key, type, lo, hi; } Event;

typedef struct {
    uint64_t probes, rows, words, shifts, adds, drops, builds, deltas;
    size_t bytes, peak;
} Meter;

typedef struct {
    uint32_t number;
    uint16_t len, cap;
    uint8_t dense;
    void *data;
} Page;

typedef struct {
    uint32_t count, cap, length;
    RowId head;
    union { RowId *runs; uint64_t *bits; Page *pages; } u;
} Posting;

typedef struct {
    Layout layout;
    Policy policy;
    uint32_t used, capacity;
    Row *rows;
    RowId *output;
    Posting post[KEYS];
    Recipe recipe[KEYS];
    uint64_t by_type[TYPES];
    struct { uint32_t count; uint8_t warm, ready; } summary[TYPES];
    Meter work;
    uint64_t checksum;
} State;

typedef struct { uint32_t count; RowId first; } Answer;

static void require(int condition, const char *reason) {
    if (!condition) {
        fputs(reason, stderr);
        fputc('\n', stderr);
        exit(1);
    }
}

static void *resize(Meter *m, void *old, size_t from, size_t to) {
    if (!to) {
        free(old);
        m->bytes -= from;
        return NULL;
    }
    void *fresh = realloc(old, to);
    require(fresh != NULL, "allocation failed");
    if (to > from)
        memset((char *)fresh + from, 0, to - from);
    m->bytes = m->bytes - from + to;
    if (m->peak < m->bytes) m->peak = m->bytes;
    return fresh;
}

static uint32_t grow(uint32_t size) { return size ? 2u * size : 4u; }

static unsigned bit_index(uint64_t word) { return (unsigned)__builtin_ctzll(word); }

static uint32_t lower32(const uint32_t *xs, uint32_t len, uint32_t value) {
    uint32_t lo = 0, hi = len;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (xs[mid] < value) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static uint32_t page_at(const Posting *p, uint32_t number) {
    uint32_t lo = 0, hi = p->length;
    while (lo < hi) {
        uint32_t m = lo + (hi - lo) / 2;
        if (p->u.pages[m].number < number) lo = m + 1;
        else hi = m;
    }
    return lo;
}

static void make_dense(Page *page, Meter *m) {
    uint64_t *bits = resize(m, NULL, 0, WORDS * sizeof(*bits));
    uint16_t *xs = page->data;
    for (uint32_t i = 0; i < page->len; ++i)
        bits[xs[i] / 64u] |= UINT64_C(1) << (xs[i] % 64u);
    resize(m, xs, (size_t)page->cap * sizeof(*xs), 0);
    page->data = bits;
    page->cap = 0;
    page->dense = 1;
    ++m->adds;
}

static void make_sparse(Page *page, Meter *m) {
    uint16_t *xs = resize(m, NULL, 0, 32u * sizeof(*xs));
    uint64_t *bits = page->data;
    unsigned n = 0;
    for (unsigned i = 0; i < WORDS; ++i)
        for (uint64_t w = bits[i]; w; w &= w - 1)
            xs[n++] = (uint16_t)(64u * i + bit_index(w));
    require(n == page->len, "bad dense page");
    resize(m, bits, WORDS * sizeof(*bits), 0);
    page->data = xs;
    page->cap = 32;
    page->dense = 0;
    ++m->drops;
}

static void posting_add(State *s, unsigned key, RowId row) {
    Posting *p = &s->post[key];
    Meter *m = &s->work;
    if (!p->count++) p->head = row;
    if (s->layout == RUN) {
        if (p->count > p->cap) {
            uint32_t cap = grow(p->cap);
            p->u.runs = resize(m, p->u.runs, (size_t)p->cap * sizeof(RowId),
                               (size_t)cap * sizeof(RowId));
            p->cap = cap;
        }
        p->u.runs[p->count - 1] = row;
    } else if (s->layout == BITMAP) {
        uint32_t word = row / 64u;
        if (word >= p->cap) {
            uint32_t cap = grow(p->cap);
            while (cap <= word) cap = grow(cap);
            p->u.bits = resize(m, p->u.bits, (size_t)p->cap * sizeof(uint64_t),
                              (size_t)cap * sizeof(uint64_t));
            p->cap = cap;
        }
        p->length = word + 1;
        p->u.bits[word] |= UINT64_C(1) << (row % 64u);
    } else {
        uint32_t number = row / BLOCK;
        if (!p->length || p->u.pages[p->length - 1].number != number) {
            if (p->length == p->cap) {
                uint32_t cap = grow(p->cap);
                p->u.pages = resize(m, p->u.pages, (size_t)p->cap * sizeof(Page),
                                    (size_t)cap * sizeof(Page));
                p->cap = cap;
            }
            p->u.pages[p->length++] = (Page){.number = number};
        }
        Page *page = &p->u.pages[p->length - 1];
        uint32_t offset = row % BLOCK;
        /* Hysteresis: sparse offsets cross the bitmap size at 32 entries.
         * Promote on the way up, demote after a substantial decrease. */
        if (!page->dense && page->len == 32u) make_dense(page, m);
        if (page->dense) {
            ((uint64_t *)page->data)[offset / 64u] |= UINT64_C(1) << (offset % 64u);
        } else {
            if (page->len == page->cap) {
                uint16_t cap = (uint16_t)grow(page->cap);
                page->data = resize(m, page->data, (size_t)page->cap * sizeof(uint16_t),
                                    (size_t)cap * sizeof(uint16_t));
                page->cap = cap;
            }
            ((uint16_t *)page->data)[page->len] = (uint16_t)offset;
        }
        ++page->len;
    }
}

typedef struct {
    const Posting *p;
    uint32_t index, word, base;
    uint64_t active;
} Cursor;

static RowId next(State *s, Cursor *it) {
    const Posting *p = it->p;
    if (s->layout == RUN)
        return it->index < p->count ? p->u.runs[it->index++] : NO_ROW;
    for (;;) {
        if (it->active) {
            RowId result = it->base + bit_index(it->active);
            it->active &= it->active - 1u;
            return result;
        }
        if (s->layout == BITMAP) {
            if (it->word == p->length) return NO_ROW;
            it->base = 64u * it->word;
            it->active = p->u.bits[it->word++];
            ++s->work.words;
        } else {
            if (it->index == p->length) return NO_ROW;
            const Page *b = &p->u.pages[it->index];
            if (b->dense && b->len && it->word < WORDS) {
                it->base = b->number * BLOCK + 64u * it->word;
                it->active = ((const uint64_t *)b->data)[it->word++];
                ++s->work.words;
            } else if (!b->dense && it->word < b->len) {
                return b->number * BLOCK + ((const uint16_t *)b->data)[it->word++];
            } else {
                it->word = 0;
                ++it->index;
            }
        }
    }
}

static void posting_remove(State *s, unsigned key, RowId row) {
    Posting *p = &s->post[key];
    Meter *m = &s->work;
    if (s->layout == RUN) {
        uint32_t at = lower32(p->u.runs, p->count, row);
        require(at < p->count && p->u.runs[at] == row, "missing run ID");
        size_t bytes = (p->count - at - 1u) * sizeof(RowId);
        memmove(p->u.runs + at, p->u.runs + at + 1, bytes);
        m->shifts += bytes;
    } else if (s->layout == BITMAP) {
        p->u.bits[row / 64u] &= ~(UINT64_C(1) << (row % 64u));
    } else {
        uint32_t i = page_at(p, row / BLOCK);
        require(i < p->length && p->u.pages[i].number == row / BLOCK, "missing page");
        Page *b = &p->u.pages[i];
        uint32_t offset = row % BLOCK;
        if (b->dense) {
            ((uint64_t *)b->data)[offset / 64u] &= ~(UINT64_C(1) << (offset % 64u));
            if (--b->len == 20u) make_sparse(b, m);
        } else {
            uint16_t *xs = b->data;
            unsigned j = 0;
            while (j < b->len && xs[j] < offset) ++j;
            require(j < b->len && xs[j] == offset, "missing sparse offset");
            size_t bytes = (--b->len - j) * sizeof(uint16_t);
            memmove(xs + j, xs + j + 1, bytes);
            m->shifts += bytes;
        }
        /* Keep the directory position available to readers until the next
         * epoch. Free an emptied page's payload (no accumulating dead bits). */
        if (!b->len && b->data) {
            const size_t bytes = b->dense ? WORDS * sizeof(uint64_t)
                                          : b->cap * sizeof(uint16_t);
            b->data = resize(m, b->data, bytes, 0);
            b->dense = 0;
            b->cap = 0;
        }
    }
    --p->count;
    if (p->head == row) p->head = p->count ? next(s, &(Cursor){.p = p}) : NO_ROW;
}

static int usable(Recipe r) { return r.type < TYPES && r.lo == ZERO && r.hi == ONE; }

static void change_summary(State *s, Recipe recipe, int64_t delta) {
    if (!usable(recipe) || !s->summary[recipe.type].ready) return;
    uint32_t *count = &s->summary[recipe.type].count;
    *count = (uint32_t)((int64_t)*count + delta);
    ++s->work.deltas;
}

static void edit(State *s, Event e) {
    if (e.action == RECIPE) {
        Recipe old = s->recipe[e.key], now = {e.type, e.lo, e.hi};
        uint64_t mask = UINT64_C(1) << e.key;
        if (old.type < TYPES) s->by_type[old.type] &= ~mask;
        if (now.type < TYPES) s->by_type[now.type] |= mask;
        change_summary(s, old, -(int64_t)s->post[e.key].count);
        change_summary(s, now, s->post[e.key].count);
        s->recipe[e.key] = now;
        return;
    }
    if (e.action == INSERT) {
        require(e.id == s->used && e.id < s->capacity, "bad source position");
        s->rows[s->used++] = (Row){e.key, 1, (uint16_t)(e.id % 11u)};
        if (s->layout != SCAN) posting_add(s, e.key, e.id);
        change_summary(s, s->recipe[e.key], 1);
    } else {
        Row *row = &s->rows[e.id];
        require(row->alive, "delete of absent occurrence");
        row->alive = 0;
        if (s->layout != SCAN) posting_remove(s, row->key, e.id);
        change_summary(s, s->recipe[row->key], -1);
    }
}

typedef struct { Cursor cursor; RowId value; } Pending;
static void sift(Pending *heap, unsigned size, Pending top) {
    unsigned at = 0;
    while (at * 2u + 1u < size) {
        unsigned child = at * 2u + 1u;
        if (child + 1u < size && heap[child + 1u].value < heap[child].value) ++child;
        if (top.value <= heap[child].value) break;
        heap[at] = heap[child];
        at = child;
    }
    heap[at] = top;
}

static Answer scan_rows(State *s, Action action, unsigned type) {
    Answer a = {.first = NO_ROW};
    for (RowId i = 0; i < s->used; ++i) {
        ++s->work.rows;
        Row row = s->rows[i];
        Recipe r = s->recipe[row.key];
        if (!row.alive || r.type != type || r.lo != 0u || r.hi != 1u) continue;
        if (action == FIRST) return (Answer){.first = i};
        if (action == ORDERED) s->output[a.count] = i;
        ++a.count;
    }
    return a;
}

static Answer select_rows(State *s, Action action, unsigned type) {
    if (s->layout == SCAN) return scan_rows(s, action, type);
    Answer a = {.first = NO_ROW};
    if (action == COUNT && s->summary[type].ready) {
        a.count = s->summary[type].count;
        return a;
    }
    Pending heap[KEYS];
    unsigned size = 0, probes = 0;
    uint64_t output_rows = 0;
    for (uint64_t keys = s->by_type[type]; keys; keys &= keys - 1u) {
        unsigned k = bit_index(keys);
        ++probes;
        ++s->work.probes; /* distinct recipe keys, not rows */
        if (!usable(s->recipe[k])) continue;
        const Posting *p = &s->post[k];
        if (action == COUNT) a.count += p->count;
        else if (action == FIRST) {
            if (p->head < a.first) a.first = p->head;
        } else if (p->count) {
            output_rows += p->count;
            Pending n = {.cursor = {.p = p}};
            n.value = next(s, &n.cursor);
            unsigned at = size++;
            while (at && heap[(at - 1u) / 2u].value > n.value) {
                heap[at] = heap[(at - 1u) / 2u];
                at = (at - 1u) / 2u;
            }
            heap[at] = n;
        }
    }
    if (action == COUNT && (s->policy == PROMOTE || s->policy == ROUTE) && probes >= 2u &&
        ++s->summary[type].warm == 2u) {
        /* The current read already paid to get the exact summary. Maintain
         * it with deltas from here; do not copy occurrences into a union. */
        s->summary[type].ready = 1;
        s->summary[type].count = a.count;
        ++s->work.builds;
    }
    if (action == ORDERED && s->policy == ROUTE && size > 1u) {
        /* Prediction in units of visited positions / heap levels, not a
         * calibration or a universal threshold. A one-bucket stream never
         * pays heap coordination; a broad union can use the source scan. */
        unsigned levels = 0;
        for (unsigned n = size - 1u; n; n >>= 1u) ++levels;
        if (output_rows * (levels + 1u) >= s->used)
            return scan_rows(s, action, type);
    }
    while (size) {
        Pending top = heap[0];
        s->output[a.count++] = top.value;
        top.value = next(s, &top.cursor);
        if (top.value == NO_ROW) top = heap[--size];
        if (size) sift(heap, size, top);
    }
    return a;
}

typedef enum { NEEDLE, SCATTER, DENSE, CHURN, SHORT } Profile;
typedef struct { const char *name; Profile profile; uint32_t n; } Case;

static uint32_t random_word(uint32_t *seed) {
    uint32_t x = *seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *seed = x;
}

static unsigned row_key(Case c, uint32_t id, uint32_t r) {
    if (c.profile == NEEDLE) return id == c.n - 1 ? 63u : r % 63u;
    if (c.profile == DENSE) return r % 10u ? 0u : r % KEYS;
    return r % KEYS;
}

static void trace(Case c, Event *events) {
    uint32_t seed = 0xadaffe2u, high = c.n, live = c.n;
    RowId *ids = malloc((c.n + EVENTS) * sizeof(*ids));
    require(ids != NULL, "no trace storage");
    for (uint32_t i = 0; i < live; ++i) ids[i] = i;
    for (unsigned j = 0; j < EVENTS; ++j) {
        uint32_t r = random_word(&seed);
        unsigned slot = j % 12u;
        if (slot == 0 || (c.profile == CHURN && slot == 5)) {
            uint32_t at = r % live;
            events[j] = (Event){.action = DELETE, .id = ids[at]};
            ids[at] = ids[--live];
        } else if (slot == 1 || (c.profile == CHURN && slot == 6)) {
            events[j] = (Event){.action = INSERT, .id = high,
                               .key = (uint8_t)row_key(c, high, r)};
            ids[live++] = high++;
        } else if (slot == 2 || (c.profile == CHURN && slot == 7)) {
            events[j] = (Event){.action = RECIPE, .key = (uint8_t)(r % 63u),
                               .type = (uint8_t)((r >> 9) % 3u),
                               .hi = (uint8_t)((r >> 16) % 4u ? ONE : 2u)};
        } else {
            unsigned type = c.profile == NEEDLE ? 63u :
                            c.profile == SHORT ? (j / 2) % TYPES : 0u;
            Action a = slot == 4 ? FIRST : slot == 9 ? ORDERED : COUNT;
            events[j] = (Event){.action = a, .type = (uint8_t)type};
        }
    }
    free(ids);
}

static void setup(State *s, Case c, Layout l, Policy policy) {
    *s = (State){.layout = l, .policy = policy, .capacity = c.n + EVENTS};
    s->rows = calloc(s->capacity, sizeof(*s->rows));
    s->output = malloc(s->capacity * sizeof(*s->output));
    require(s->rows && s->output, "no rows or output");
    uint32_t seed = 0x2644321u;
    for (unsigned k = 0; k < KEYS; ++k) {
        s->post[k].head = NO_ROW;
        s->recipe[k].type = NO_TYPE;
        unsigned type = c.profile == NEEDLE || c.profile == SHORT ? k : 0u;
        edit(s, (Event){.action = RECIPE, .key = (uint8_t)k, .type = (uint8_t)type, .hi = ONE});
        s->summary[k].ready = policy == PREPAY;
    }
    for (uint32_t i = 0; i < c.n; ++i)
        edit(s, (Event){.action = INSERT, .id = i,
                        .key = (uint8_t)row_key(c, i, random_word(&seed))});
}

static void cleanup(State *s) {
    for (unsigned k = 0; k < KEYS; ++k) {
        Posting *p = &s->post[k];
        if (s->layout == RUN) free(p->u.runs);
        if (s->layout == BITMAP) free(p->u.bits);
        if (s->layout == BLOCKED) {
            for (uint32_t i = 0; i < p->length; ++i) free(p->u.pages[i].data);
            free(p->u.pages);
        }
    }
    free(s->rows);
    free(s->output);
}

static const struct {
    Layout layout;
    Policy policy;
    const char *name;
} methods[] = {{SCAN, RENT, "scan"}, {RUN, RENT, "run32"}, {BITMAP, RENT, "bitmap"},
               {BLOCKED, RENT, "blocked"}, {BLOCKED, PREPAY, "prepaid-count"},
               {BLOCKED, PROMOTE, "promoted-count"}, {BLOCKED, ROUTE, "routed"}};
enum { METHOD_COUNT = sizeof methods / sizeof *methods };

static void verify(Case c, const Event *events) {
    State all[METHOD_COUNT];
    for (unsigned m = 0; m < METHOD_COUNT; ++m)
        setup(&all[m], c, methods[m].layout, methods[m].policy);
    for (unsigned j = 0; j < EVENTS; ++j) {
        Event e = events[j];
        if (e.action < COUNT) {
            for (unsigned m = 0; m < METHOD_COUNT; ++m) edit(&all[m], e);
        } else {
            Answer oracle = select_rows(&all[0], e.action, e.type);
            for (unsigned m = 1; m < METHOD_COUNT; ++m) {
                Answer a = select_rows(&all[m], e.action, e.type);
                if (a.count != oracle.count || a.first != oracle.first ||
                    (e.action == ORDERED && memcmp(all[m].output, all[0].output,
                                                  a.count * sizeof(RowId)))) {
                    fprintf(stderr, "%s: %s disagrees at event %u\n", c.name, methods[m].name, j);
                    exit(1);
                }
            }
        }
    }
    for (unsigned m = 0; m < METHOD_COUNT; ++m) cleanup(&all[m]);
}

static void controls(void) {
    const Case empty = {"block-conversion", SCATTER, 0};
    State s;
    setup(&s, empty, BLOCKED, PROMOTE);
    for (RowId i = 0; i < 33u; ++i)
        edit(&s, (Event){.action = INSERT, .id = i});
    require(s.work.adds == 1u, "page did not promote");
    for (RowId i = 0; i < 13u; ++i)
        edit(&s, (Event){.action = DELETE, .id = i});
    require(s.work.drops == 1u, "page did not demote");
    Answer a = select_rows(&s, ORDERED, 0u);
    require(a.count == 20u, "wrong count after page conversion");
    for (RowId i = 0; i < a.count; ++i)
        require(s.output[i] == 13u + i, "position changed after page conversion");
    /* Promotion must not grant a match when the unsought coordinate fails. */
    require(select_rows(&s, COUNT, 0u).count == 20u, "wrong count before promote");
    require(select_rows(&s, COUNT, 0u).count == 20u, "wrong promoted count");
    require(s.work.builds == 1u, "summary did not promote");
    edit(&s, (Event){.action = RECIPE, .type = 0u, .hi = 2u});
    require(select_rows(&s, COUNT, 0u).count == 0u, "stale promoted count");
    require(select_rows(&s, FIRST, 0u).first == NO_ROW, "a coordinate hint became a match");
    for (RowId i = 13u; i < 33u; ++i)
        edit(&s, (Event){.action = DELETE, .id = i});
    edit(&s, (Event){.action = RECIPE, .type = 0u, .hi = ONE});
    edit(&s, (Event){.action = INSERT, .id = 33u});
    require(select_rows(&s, ORDERED, 0u).count == 1u && s.output[0] == 33u,
            "an emptied page could not be reused");
    cleanup(&s);
}

static uint64_t now(void) {
    struct timespec t;
    require(clock_gettime(CLOCK_MONOTONIC, &t) == 0, "clock unavailable");
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}

static void run(Case c, const Event *events, unsigned m) {
    State s;
    uint64_t start = now();
    setup(&s, c, methods[m].layout, methods[m].policy);
    uint64_t build = now() - start;
    uint64_t writes = 0, reads[3] = {0}, emitted = 0;
    for (unsigned j = 0; j < EVENTS; ++j) {
        Event e = events[j];
        start = now();
        if (e.action < COUNT) {
            edit(&s, e);
            writes += now() - start;
        } else {
            Answer a = select_rows(&s, e.action, e.type);
            reads[e.action - COUNT] += now() - start;
            if (e.action == ORDERED) {
                emitted += a.count;
                for (uint32_t i = 0; i < a.count; ++i) {
                    RowId id = s.output[i];
                    s.checksum = 31u * s.checksum + id + s.rows[id].payload;
                }
            }
            s.checksum += a.count + a.first;
        }
    }
    Meter v = s.work;
    printf("%s,%u,%s,%llu,%llu,%llu,%llu,%llu,%zu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
           c.name, c.n, methods[m].name, (unsigned long long)build,
           (unsigned long long)writes, (unsigned long long)reads[0],
           (unsigned long long)reads[1], (unsigned long long)reads[2], v.peak,
           (unsigned long long)v.rows, (unsigned long long)v.probes,
           (unsigned long long)v.words, (unsigned long long)v.shifts,
           (unsigned long long)v.adds, (unsigned long long)v.drops,
           (unsigned long long)v.builds, (unsigned long long)v.deltas,
           (unsigned long long)emitted, (unsigned long long)s.checksum);
    cleanup(&s);
}

int main(int argc, char **argv) {
    unsigned trials = 3;
    if (argc == 2) {
        char *end;
        unsigned long n = strtoul(argv[1], &end, 10);
        require(*argv[1] && !*end && n <= 9, "usage: bench [0..9 trials; 0 verifies only]");
        trials = (unsigned)n;
    } else require(argc == 1, "usage: bench [trials]");
    const Case cases[] = {
        {"needle", NEEDLE, 4096}, {"needle", NEEDLE, 65536},
        {"scatter", SCATTER, 32768}, {"dense", DENSE, 32768},
        {"churn", CHURN, 32768}, {"short", SHORT, 4096},
    };
    Event events[EVENTS];
    controls();
    puts("case,n,method,build_ns,write_ns,count_ns,first_ns,ordered_ns,peak_posting_bytes,"
         "rows_scanned,recipe_probes,"
         "bitmap_words,shift_bytes,blocks_promoted,blocks_demoted,summaries_built,"
         "summary_deltas,emitted,checksum");
    for (unsigned i = 0; i < sizeof cases / sizeof *cases; ++i) {
        trace(cases[i], events);
        verify(cases[i], events);
        for (unsigned t = 0; t < trials; ++t)
            for (unsigned j = 0; j < METHOD_COUNT; ++j)
                run(cases[i], events, (j + t) % METHOD_COUNT);
    }
    fputs("PASS: all equation-posting layouts match the ordered scan on every read\n", stderr);
    return 0;
}
