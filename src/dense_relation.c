#include "dense_relation.h"

#include "term_universe.h"

#include <stdlib.h>
#include <string.h>

enum {
    DENSE_PATTERNS = 32u,
    DENSE_VARIABLES = 64u,
    /* A row of bits over this many keys is 16K words. */
    DENSE_KEYS = 1u << 20,
    /* The matrices may take this much whatever the rows. */
    DENSE_FLOOR_BYTES = 8u << 20,
    /* ... and this much per row of their relations. */
    DENSE_ROW_BYTES = 64u,
    /* Rows of bits must hold, on average, a key in every this many words
     * of a row: sparser rows intersect in fewer steps as sorted keys. */
    DENSE_WORDS_PER_KEY = 8u,
};

/* A pattern's column: a variable's index, or the key it fixes (SIZE_MAX
 * when no row of the query's relations holds that atom). */
typedef struct {
    bool variable;
    size_t index;
} DenseColumn;

typedef struct {
    size_t relation;
    DenseColumn column[2];
} DensePattern;

typedef struct {
    SymbolId head;
    /* Each row's keys, first then second. */
    uint32_t *pairs;
    size_t rows;
    size_t pair_cap;
    /* A row of bits per first key, over the second keys, and the reverse,
     * made when a pattern reads it. */
    uint64_t *forward;
    uint64_t *backward;
    /* The keys in each column. */
    uint64_t *firsts;
    uint64_t *seconds;
} DenseRelation;

struct CettaDenseQuery {
    TermUniverse *universe;
    /* The numbering: each key's atom, and a table from atoms to keys. */
    AtomId *keys;
    size_t key_count;
    size_t key_cap;
    AtomId *table_ids;
    uint32_t *table_keys;
    size_t table_mask;
    size_t table_used;
    /* Words in a row of bits. */
    size_t words;
    DenseRelation relations[DENSE_PATTERNS];
    size_t relation_count;
    DensePattern patterns[DENSE_PATTERNS];
    size_t pattern_count;
    VarId variables[DENSE_VARIABLES];
    size_t variable_count;
    /* A pattern fixes an atom no row holds: there is no solution. */
    bool empty;
};

bool cetta_dense_query_enabled(void) {
    static _Thread_local int enabled = -1;
    if (enabled < 0) {
        const char *reference = getenv("CETTA_DENSE_RELATION_REFERENCE");
        enabled = !reference || !*reference || strcmp(reference, "0") == 0 ||
            strcmp(reference, "false") == 0 || strcmp(reference, "off") == 0;
    }
    return enabled != 0;
}

static size_t dense_hash(AtomId id) {
    id ^= id >> 30u;
    id *= UINT64_C(0xbf58476d1ce4e5b9);
    id ^= id >> 27u;
    id *= UINT64_C(0x94d049bb133111eb);
    return (size_t)(id ^ (id >> 31u));
}

/* The key of an atom, SIZE_MAX when it has none. */
static size_t dense_key(const CettaDenseQuery *query, AtomId id) {
    if (!query->table_ids)
        return SIZE_MAX;
    size_t slot = dense_hash(id) & query->table_mask;
    while (query->table_ids[slot] != CETTA_ATOM_ID_NONE) {
        if (query->table_ids[slot] == id)
            return query->table_keys[slot];
        slot = (slot + 1u) & query->table_mask;
    }
    return SIZE_MAX;
}

static bool dense_table_grow(CettaDenseQuery *query) {
    size_t capacity = query->table_ids ? (query->table_mask + 1u) * 2u : 256u;
    AtomId *ids = malloc(capacity * sizeof(*ids));
    uint32_t *keys = malloc(capacity * sizeof(*keys));
    if (!ids || !keys) {
        free(ids);
        free(keys);
        return false;
    }
    for (size_t i = 0u; i < capacity; i++)
        ids[i] = CETTA_ATOM_ID_NONE;
    for (size_t key = 0u; key < query->key_count; key++) {
        size_t slot = dense_hash(query->keys[key]) & (capacity - 1u);
        while (ids[slot] != CETTA_ATOM_ID_NONE)
            slot = (slot + 1u) & (capacity - 1u);
        ids[slot] = query->keys[key];
        keys[slot] = (uint32_t)key;
    }
    free(query->table_ids);
    free(query->table_keys);
    query->table_ids = ids;
    query->table_keys = keys;
    query->table_mask = capacity - 1u;
    return true;
}

/* Whether the atom is equal only to itself: a symbol, an integer, a truth
 * value, a string, or a ground expression of them. */
static bool dense_exact(const TermUniverse *universe, AtomId id,
                        unsigned depth) {
    const CettaTermHdr *header = tu_hdr(universe, id);
    if (!header)
        return false;
    if (header->tag == ATOM_SYMBOL)
        return true;
    if (header->tag == ATOM_GROUNDED)
        return header->subtag == GV_INT || header->subtag == GV_BOOL ||
            header->subtag == GV_STRING;
    if (header->tag != ATOM_EXPR || depth > 64u || tu_has_vars(universe, id))
        return false;
    for (CettaExprIndex c = 0u; c < tu_arity(universe, id); c++) {
        if (!dense_exact(universe, tu_child(universe, id, c), depth + 1u))
            return false;
    }
    return true;
}

/* Number an atom of a row, which must be equal only to itself. */
static bool dense_number(CettaDenseQuery *query, AtomId id, uint32_t *key) {
    size_t known = dense_key(query, id);
    if (known != SIZE_MAX) {
        *key = (uint32_t)known;
        return true;
    }
    if (query->key_count == DENSE_KEYS ||
        !dense_exact(query->universe, id, 0u))
        return false;
    if ((query->key_count + 1u) * 2u > (query->table_ids
            ? query->table_mask + 1u : 0u) && !dense_table_grow(query))
        return false;
    if (query->key_count == query->key_cap) {
        size_t cap = query->key_cap ? query->key_cap * 2u : 256u;
        AtomId *keys = realloc(query->keys, cap * sizeof(*keys));
        if (!keys)
            return false;
        query->keys = keys;
        query->key_cap = cap;
    }
    size_t slot = dense_hash(id) & query->table_mask;
    while (query->table_ids[slot] != CETTA_ATOM_ID_NONE)
        slot = (slot + 1u) & query->table_mask;
    query->table_ids[slot] = id;
    query->table_keys[slot] = (uint32_t)query->key_count;
    query->keys[query->key_count] = id;
    *key = (uint32_t)query->key_count++;
    return true;
}

/* Whether an atom a pattern fixes is equal only to itself. */
static bool dense_exact_atom(Atom *atom) {
    if (atom->kind == ATOM_SYMBOL)
        return true;
    if (atom->kind == ATOM_GROUNDED)
        return atom->ground.gkind == GV_INT || atom->ground.gkind == GV_BOOL ||
            atom->ground.gkind == GV_STRING;
    if (atom->kind != ATOM_EXPR)
        return false;
    for (CettaExprIndex i = 0u; i < atom->expr.len; i++) {
        if (!dense_exact_atom(atom->expr.elems[i]))
            return false;
    }
    return true;
}

static bool dense_pair_push(DenseRelation *relation, uint32_t first,
                            uint32_t second) {
    if (relation->rows == relation->pair_cap) {
        size_t cap = relation->pair_cap ? relation->pair_cap * 2u : 256u;
        if (cap > SIZE_MAX / (2u * sizeof(*relation->pairs)))
            return false;
        uint32_t *pairs = realloc(relation->pairs, cap * 2u * sizeof(*pairs));
        if (!pairs)
            return false;
        relation->pairs = pairs;
        relation->pair_cap = cap;
    }
    relation->pairs[2u * relation->rows] = first;
    relation->pairs[2u * relation->rows + 1u] = second;
    relation->rows++;
    return true;
}

/* Read the rows of one relation: those with its head and three elements.
 * False when a row could match the relation's patterns but is not a pair of
 * keys, or memory runs out. */
typedef struct {
    CettaDenseQuery *query;
    DenseRelation *relation;
} DenseBitsReader;

static bool dense_read_pair(void *context, CettaDenseId first_id,
                            CettaDenseId second_id) {
    DenseBitsReader *reader = context;
    uint32_t first = 0u, second = 0u;
    return dense_number(reader->query, (AtomId)first_id, &first) &&
           dense_number(reader->query, (AtomId)second_id, &second) &&
           dense_pair_push(reader->relation, first, second);
}

static bool dense_read_rows(CettaDenseQuery *query, Space *space,
                            Arena *scratch, Atom *head,
                            DenseRelation *relation) {
    /* A relation the space keeps as bits gives its pairs from them, when no
     * row stands beside them: reading its rows would give the bits back. */
    const CettaDenseRelation *bits = space_dense_relation(space, relation->head);
    if (bits) {
        DenseBitsReader reader = {query, relation};
        return space_dense_head_rows_absent(space, relation->head) &&
            cetta_dense_relation_each(bits, CETTA_DENSE_ANY, CETTA_DENSE_ANY,
                                      dense_read_pair, &reader);
    }
    Atom *elements[3] = {head, atom_var(scratch, "first"),
                         atom_var(scratch, "second")};
    Atom *probe = elements[1] && elements[2]
        ? atom_expr(scratch, elements, 3u) : NULL;
    if (!probe)
        return false;
    CettaIndex *rows = NULL;
    CettaIndex row_count = space_match_candidates64(space, probe, &rows);
    TermUniverse *universe = query->universe;
    bool ready = true;
    for (CettaIndex r = 0u; ready && r < row_count; r++) {
        AtomId id = space_get_atom_id_at64(space, rows[r]);
        const CettaTermHdr *header = tu_hdr(universe, id);
        if (!header || header->tag == ATOM_VAR) {
            ready = false;
            break;
        }
        if (header->tag != ATOM_EXPR || tu_arity(universe, id) != 3u)
            continue;
        AtomId row_head = tu_child(universe, id, 0u);
        if (tu_kind(universe, row_head) == ATOM_SYMBOL) {
            if (tu_sym(universe, row_head) != relation->head)
                continue;
        } else if (!tu_has_vars(universe, row_head)) {
            continue;
        }
        /* A row holding a variable may match where its keys do not. */
        uint32_t first = 0u, second = 0u;
        ready = !tu_has_vars(universe, id) &&
            dense_number(query, tu_child(universe, id, 1u), &first) &&
            dense_number(query, tu_child(universe, id, 2u), &second) &&
            dense_pair_push(relation, first, second);
    }
    free(rows);
    return ready;
}

static uint64_t *dense_words(size_t count) {
    return count ? calloc(count, sizeof(uint64_t)) : NULL;
}

static inline bool dense_bit(const uint64_t *row, size_t key) {
    return (row[key >> 6u] >> (key & 63u)) & 1u;
}

static inline void dense_set(uint64_t *row, size_t key) {
    row[key >> 6u] |= UINT64_C(1) << (key & 63u);
}

/* The set bits of a word: the instruction where the target has one, else
 * by halves, nibbles and bytes, not the C library's call. */
static inline uint64_t dense_bits(uint64_t word) {
#if defined(__POPCNT__)
    return (uint64_t)__builtin_popcountll(word);
#else
    word -= (word >> 1) & UINT64_C(0x5555555555555555);
    word = (word & UINT64_C(0x3333333333333333)) +
        ((word >> 2) & UINT64_C(0x3333333333333333));
    word = (word + (word >> 4)) & UINT64_C(0x0F0F0F0F0F0F0F0F);
    return (word * UINT64_C(0x0101010101010101)) >> 56;
#endif
}

/* The forward matrix and the column sets.  False on a repeated row: its
 * multiplicity would be lost. */
static bool dense_build_forward(CettaDenseQuery *query,
                                DenseRelation *relation) {
    size_t words = query->words;
    relation->forward = dense_words(query->key_count * words);
    relation->firsts = dense_words(words);
    relation->seconds = dense_words(words);
    if (!relation->forward || !relation->firsts || !relation->seconds)
        return false;
    for (size_t r = 0u; r < relation->rows; r++) {
        uint32_t first = relation->pairs[2u * r];
        uint32_t second = relation->pairs[2u * r + 1u];
        uint64_t *row = relation->forward + (size_t)first * words;
        if (dense_bit(row, second))
            return false;
        dense_set(row, second);
        dense_set(relation->firsts, first);
        dense_set(relation->seconds, second);
    }
    return true;
}

static bool dense_build_backward(CettaDenseQuery *query,
                                 DenseRelation *relation) {
    if (relation->backward)
        return true;
    size_t words = query->words;
    relation->backward = dense_words(query->key_count * words);
    if (!relation->backward)
        return false;
    for (size_t r = 0u; r < relation->rows; r++) {
        dense_set(relation->backward +
                      (size_t)relation->pairs[2u * r + 1u] * words,
                  relation->pairs[2u * r]);
    }
    return true;
}

static size_t dense_variable(CettaDenseQuery *query, VarId id) {
    for (size_t i = 0u; i < query->variable_count; i++) {
        if (var_id_eq(query->variables[i], id))
            return i;
    }
    if (query->variable_count == DENSE_VARIABLES)
        return SIZE_MAX;
    query->variables[query->variable_count] = id;
    return query->variable_count++;
}

void cetta_dense_query_free(CettaDenseQuery *query) {
    if (!query)
        return;
    for (size_t i = 0u; i < query->relation_count; i++) {
        DenseRelation *relation = &query->relations[i];
        free(relation->pairs);
        free(relation->forward);
        free(relation->backward);
        free(relation->firsts);
        free(relation->seconds);
    }
    free(query->keys);
    free(query->table_ids);
    free(query->table_keys);
    free(query);
}

bool cetta_dense_query_prepare(Space *space, Arena *scratch,
                               Atom *const *patterns, size_t pattern_count,
                               CettaDenseQuery **out) {
    if (!cetta_dense_query_enabled() || !space || !scratch || !patterns ||
        !out || pattern_count == 0u || pattern_count > DENSE_PATTERNS ||
        !space->native.universe || space->overlay_base || space->dep_count ||
        (space->match_backend.kind != SPACE_ENGINE_NATIVE &&
         space->match_backend.kind != SPACE_ENGINE_NATIVE_CANDIDATE_EXACT))
        return false;
    for (size_t i = 0u; i < pattern_count; i++) {
        Atom *pattern = patterns[i];
        if (!pattern || pattern->kind != ATOM_EXPR || pattern->expr.len != 3u ||
            pattern->expr.elems[0]->kind != ATOM_SYMBOL)
            return false;
        for (CettaExprIndex c = 1u; c < 3u; c++) {
            Atom *item = pattern->expr.elems[c];
            if (item->kind != ATOM_VAR &&
                (atom_has_vars(item) || !dense_exact_atom(item)))
                return false;
        }
    }
    CettaDenseQuery *query = calloc(1u, sizeof(*query));
    if (!query)
        return false;
    query->universe = space->native.universe;
    bool ready = true;
    for (size_t i = 0u; ready && i < pattern_count; i++) {
        Atom *pattern = patterns[i];
        SymbolId head = pattern->expr.elems[0]->sym_id;
        size_t relation = 0u;
        while (relation < query->relation_count &&
               query->relations[relation].head != head)
            relation++;
        if (relation == query->relation_count)
            query->relations[query->relation_count++].head = head;
        DensePattern *made = &query->patterns[query->pattern_count++];
        made->relation = relation;
        for (size_t c = 0u; ready && c < 2u; c++) {
            Atom *item = pattern->expr.elems[c + 1u];
            made->column[c].variable = item->kind == ATOM_VAR;
            if (made->column[c].variable) {
                made->column[c].index = dense_variable(query, item->var_id);
                ready = made->column[c].index != SIZE_MAX;
            }
        }
    }
    for (size_t r = 0u; ready && r < query->relation_count; r++) {
        Atom *head = NULL;
        for (size_t i = 0u; !head && i < pattern_count; i++) {
            if (query->patterns[i].relation == r)
                head = patterns[i]->expr.elems[0];
        }
        ready = dense_read_rows(query, space, scratch, head,
                                &query->relations[r]);
    }
    /* The atoms the patterns fix, by their keys. */
    for (size_t i = 0u; ready && i < pattern_count; i++) {
        for (size_t c = 0u; c < 2u; c++) {
            DenseColumn *column = &query->patterns[i].column[c];
            if (column->variable)
                continue;
            AtomId id = term_universe_lookup_atom_id(
                query->universe, patterns[i]->expr.elems[c + 1u]);
            column->index = id == CETTA_ATOM_ID_NONE
                ? SIZE_MAX : dense_key(query, id);
            query->empty |= column->index == SIZE_MAX;
        }
    }
    /* A conjunction of fixed rows is the relational plan's. */
    ready = ready && query->variable_count > 0u;
    size_t total_rows = 0u;
    for (size_t r = 0u; ready && r < query->relation_count; r++)
        total_rows += query->relations[r].rows;
    query->words = (query->key_count + 63u) / 64u;
    if (ready) {
        /* Two matrices per relation at most, each a row per key. */
        size_t keys = query->key_count;
        size_t budget = total_rows > DENSE_FLOOR_BYTES / DENSE_ROW_BYTES
            ? total_rows * DENSE_ROW_BYTES : DENSE_FLOOR_BYTES;
        size_t matrix = keys * query->words * sizeof(uint64_t);
        ready = keys > 0u &&
            matrix / sizeof(uint64_t) / query->words == keys &&
            matrix <= budget / 2u / query->relation_count &&
            total_rows >= keys * query->words / DENSE_WORDS_PER_KEY;
    }
    for (size_t r = 0u; ready && r < query->relation_count; r++)
        ready = dense_build_forward(query, &query->relations[r]);
    if (!ready) {
        cetta_dense_query_free(query);
        return false;
    }
    *out = query;
    return true;
}

size_t cetta_dense_query_variable_count(const CettaDenseQuery *query) {
    return query ? query->variable_count : 0u;
}

VarId cetta_dense_query_variable_at(const CettaDenseQuery *query,
                                    size_t index) {
    return query->variables[index];
}

/* ── Running ──────────────────────────────────────────────────────────── */

typedef struct {
    const uint64_t *matrix;
    size_t from;
} DenseLink;

typedef struct {
    CettaDenseQuery *query;
    size_t order[DENSE_VARIABLES];
    size_t level_of[DENSE_VARIABLES];
    size_t levels;
    /* With some variables summed out: the levels binding kept variables
     * before them, the summed levels, then the last, kept, level. */
    bool projected;
    size_t kept_levels;
    bool count_only;
    uint64_t *base;
    uint64_t *candidates;
    uint64_t *summed;
    DenseLink links[DENSE_VARIABLES][DENSE_PATTERNS];
    size_t link_count[DENSE_VARIABLES];
    uint32_t bound[DENSE_VARIABLES];
    AtomId values[DENSE_VARIABLES];
    CettaRelationalVisit visit;
    void *context;
    uint64_t count;
    CettaDenseWork work;
} DenseRun;

/* Patterns joining two distinct variables, V and one placed already. */
static size_t dense_links(const CettaDenseQuery *query, size_t v,
                          const bool *placed) {
    size_t links = 0u;
    for (size_t i = 0u; i < query->pattern_count; i++) {
        const DensePattern *p = &query->patterns[i];
        if (!p->column[0].variable || !p->column[1].variable ||
            p->column[0].index == p->column[1].index)
            continue;
        size_t a = p->column[0].index, b = p->column[1].index;
        links += (a == v && placed[b]) || (b == v && placed[a]);
    }
    return links;
}

static size_t dense_degree(const CettaDenseQuery *query, size_t v) {
    size_t degree = 0u;
    for (size_t i = 0u; i < query->pattern_count; i++) {
        const DensePattern *p = &query->patterns[i];
        degree += (p->column[0].variable && p->column[0].index == v) ||
            (p->column[1].variable && p->column[1].index == v);
    }
    return degree;
}

/* Place the variables of GROUP, each time the one most joined to those
 * placed, then the one in most patterns. */
static void dense_place(DenseRun *run, const bool *group, bool *placed) {
    const CettaDenseQuery *query = run->query;
    for (;;) {
        size_t best = SIZE_MAX, best_links = 0u, best_degree = 0u;
        for (size_t v = 0u; v < query->variable_count; v++) {
            if (!group[v] || placed[v])
                continue;
            size_t links = dense_links(query, v, placed);
            size_t degree = dense_degree(query, v);
            if (best == SIZE_MAX || links > best_links ||
                (links == best_links && degree > best_degree)) {
                best = v;
                best_links = links;
                best_degree = degree;
            }
        }
        if (best == SIZE_MAX)
            return;
        placed[best] = true;
        run->level_of[best] = run->levels;
        run->order[run->levels++] = best;
    }
}

static void dense_and(uint64_t *into, const uint64_t *row, size_t words) {
    for (size_t w = 0u; w < words; w++)
        into[w] &= row[w];
}

/* Each level's base, the rows no bound variable selects, and its links;
 * EMPTY when the query has no solution.  False when memory runs out. */
static bool dense_levels(DenseRun *run, bool *empty) {
    CettaDenseQuery *query = run->query;
    size_t words = query->words;
    *empty = query->empty;
    for (size_t level = 0u; !*empty && level < run->levels; level++) {
        size_t v = run->order[level];
        uint64_t *base = run->base + level * words;
        memset(base, 0xff, words * sizeof(*base));
        run->link_count[level] = 0u;
        for (size_t i = 0u; i < query->pattern_count; i++) {
            const DensePattern *p = &query->patterns[i];
            DenseRelation *relation = &query->relations[p->relation];
            bool first = p->column[0].variable && p->column[0].index == v;
            bool second = p->column[1].variable && p->column[1].index == v;
            if (!first && !second)
                continue;
            if (first)
                dense_and(base, relation->firsts, words);
            if (second)
                dense_and(base, relation->seconds, words);
            if (first && second) {
                /* (h $v $v): keys related to themselves. */
                for (size_t key = 0u; key < query->key_count; key++) {
                    if (dense_bit(base, key) &&
                        !dense_bit(relation->forward + key * words, key))
                        base[key >> 6u] &= ~(UINT64_C(1) << (key & 63u));
                }
                continue;
            }
            const DenseColumn *other = &p->column[first ? 1u : 0u];
            if (!other->variable) {
                /* A fixed key selects one row in each direction. */
                if (first) {
                    if (!dense_build_backward(query, relation))
                        return false;
                    dense_and(base, relation->backward + other->index * words,
                              words);
                } else {
                    dense_and(base, relation->forward + other->index * words,
                              words);
                }
                continue;
            }
            if (run->level_of[other->index] >= level)
                continue;
            if (first && !dense_build_backward(query, relation))
                return false;
            run->links[level][run->link_count[level]++] = (DenseLink){
                .matrix = first ? relation->backward : relation->forward,
                .from = run->level_of[other->index],
            };
        }
    }
    /* Patterns that fix both atoms hold or not, once for all. */
    for (size_t i = 0u; !*empty && i < query->pattern_count; i++) {
        const DensePattern *p = &query->patterns[i];
        if (p->column[0].variable || p->column[1].variable)
            continue;
        *empty = !dense_bit(query->relations[p->relation].forward +
                                p->column[0].index * words,
                            p->column[1].index);
    }
    return true;
}

/* The candidates of a level, given the levels above it bound. */
static uint64_t dense_candidates(DenseRun *run, size_t level, uint64_t *out) {
    size_t words = run->query->words;
    const uint64_t *base = run->base + level * words;
    size_t links = run->link_count[level];
    const uint64_t *rows[DENSE_PATTERNS];
    for (size_t l = 0u; l < links; l++) {
        rows[l] = run->links[level][l].matrix +
            (size_t)run->bound[run->links[level][l].from] * words;
    }
    uint64_t any = 0u;
    for (size_t w = 0u; w < words; w++) {
        uint64_t word = base[w];
        for (size_t l = 0u; l < links; l++)
            word &= rows[l][w];
        out[w] = word;
        any |= word;
    }
    run->work.words += words * (links + 1u);
    return any;
}

static bool dense_count_add(DenseRun *run, const uint64_t *row) {
    size_t words = run->query->words;
    uint64_t bits = 0u;
    for (size_t w = 0u; w < words; w++)
        bits += dense_bits(row[w]);
    run->work.words += words;
    return !__builtin_add_overflow(run->count, bits, &run->count);
}

static bool dense_visit_bits(DenseRun *run, size_t level, const uint64_t *row) {
    const CettaDenseQuery *query = run->query;
    size_t variable = run->order[level];
    for (size_t w = 0u; w < query->words; w++) {
        for (uint64_t word = row[w]; word; word &= word - 1u) {
            size_t key = w * 64u + (size_t)__builtin_ctzll(word);
            run->values[variable] = query->keys[key];
            run->work.visits++;
            if (!run->visit(run->context, run->values, 1u))
                return false;
        }
    }
    return true;
}

static bool dense_bind(DenseRun *run, size_t level);

static bool dense_descend(DenseRun *run, size_t level) {
    if (!run->projected || level != run->kept_levels)
        return dense_bind(run, level);
    /* The summed levels: whatever binds them, the last level's candidates
     * are gathered, and each is visited once after. */
    size_t words = run->query->words;
    memset(run->summed, 0, words * sizeof(*run->summed));
    if (!dense_bind(run, level))
        return false;
    return dense_visit_bits(run, run->levels - 1u, run->summed);
}

static bool dense_bind(DenseRun *run, size_t level) {
    size_t words = run->query->words;
    uint64_t *row = run->candidates + level * words;
    if (!dense_candidates(run, level, row))
        return true;
    if (level + 1u == run->levels) {
        if (!dense_count_add(run, row))
            return false;
        if (run->projected) {
            for (size_t w = 0u; w < words; w++)
                run->summed[w] |= row[w];
            run->work.words += words;
            return true;
        }
        return run->count_only || dense_visit_bits(run, level, row);
    }
    size_t variable = run->order[level];
    for (size_t w = 0u; w < words; w++) {
        for (uint64_t word = row[w]; word; word &= word - 1u) {
            size_t key = w * 64u + (size_t)__builtin_ctzll(word);
            run->bound[level] = (uint32_t)key;
            run->values[variable] = run->query->keys[key];
            run->work.candidates++;
            if (!dense_descend(run, level + 1u))
                return false;
        }
    }
    return true;
}

bool cetta_dense_query_run(CettaDenseQuery *query, const bool *kept,
                           CettaRelationalVisit visit, void *context,
                           uint64_t *count_out, CettaDenseWork *work) {
    if (count_out)
        *count_out = 0u;
    if (!query || !count_out)
        return false;
    DenseRun *run = calloc(1u, sizeof(*run));
    if (!run)
        return false;
    run->query = query;
    run->visit = visit;
    run->context = context;
    size_t variables = query->variable_count;
    bool kept_set[DENSE_VARIABLES] = {false};
    bool rest[DENSE_VARIABLES] = {false};
    size_t kept_count = 0u;
    for (size_t v = 0u; v < variables; v++) {
        kept_set[v] = !kept || kept[v];
        kept_count += kept_set[v];
    }
    run->count_only = kept_count == 0u || !visit;
    bool placed[DENSE_VARIABLES] = {false};
    if (run->count_only || kept_count == variables) {
        for (size_t v = 0u; v < variables; v++)
            rest[v] = true;
        dense_place(run, rest, placed);
    } else {
        /* The last level binds the kept variable most joined to the summed
         * ones; the other kept variables come first. */
        size_t last = SIZE_MAX, last_links = 0u;
        for (size_t v = 0u; v < variables; v++)
            rest[v] = !kept_set[v];
        for (size_t v = 0u; v < variables; v++) {
            if (!kept_set[v])
                continue;
            size_t links = dense_links(query, v, rest);
            if (last == SIZE_MAX || links > last_links) {
                last = v;
                last_links = links;
            }
        }
        bool first[DENSE_VARIABLES] = {false};
        for (size_t v = 0u; v < variables; v++)
            first[v] = kept_set[v] && v != last;
        dense_place(run, first, placed);
        run->kept_levels = run->levels;
        dense_place(run, rest, placed);
        bool only[DENSE_VARIABLES] = {false};
        only[last] = true;
        dense_place(run, only, placed);
        run->projected = true;
    }
    size_t words = query->words;
    size_t levels = run->levels ? run->levels : 1u;
    run->base = dense_words(levels * words);
    run->candidates = dense_words(levels * words);
    run->summed = dense_words(words);
    bool empty = false;
    bool done = run->base && run->candidates && run->summed &&
        dense_levels(run, &empty);
    if (done && !empty)
        done = dense_descend(run, 0u);
    *count_out = run->count;
    if (work) {
        work->words += run->work.words;
        work->candidates += run->work.candidates;
        work->visits += run->work.visits;
    }
    free(run->base);
    free(run->candidates);
    free(run->summed);
    free(run);
    return done;
}
