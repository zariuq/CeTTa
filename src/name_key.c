#include "name_key.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef CETTA_NAME_KEY_MUTATION
#define CETTA_NAME_KEY_MUTATION 0
#endif

typedef struct NameKeyEntry NameKeyEntry;
struct NameKeyEntry {
    uint32_t digest;
    NameId id;
    Atom *key;
    NameKeyEntry *next;
};

struct NameKeyTable {
    Arena arena;
    NameKeyEntry **buckets;
    size_t bucket_count;
    NameKeyEntry **by_id;
    size_t by_id_cap;
    uint32_t len;
    uint32_t hash_mask;
    pthread_mutex_t mutex;
};

typedef struct {
    Atom *atom;
    bool leave;
} NameKeyVisit;

static bool name_key_stack_push(NameKeyVisit **items, size_t *len,
                                size_t *cap, NameKeyVisit item) {
    if (*len == *cap) {
        if (*cap > SIZE_MAX / 2u ||
            *cap * 2u > SIZE_MAX / sizeof(**items))
            return false;
        *cap *= 2u;
        *items = cetta_realloc(*items, sizeof(**items) * *cap);
    }
    (*items)[(*len)++] = item;
    return true;
}

static bool name_key_active_contains(Atom **active, size_t len, Atom *atom) {
    for (size_t i = 0u; i < len; i++)
        if (active[i] == atom) return true;
    return false;
}

bool name_key_is_admissible(Atom *root) {
    if (!root || atom_has_vars(root) ||
        (root->flags & ATOM_FLAG_HASH_STABLE) == 0u)
        return false;

    size_t stack_cap = 64u, stack_len = 0u;
    size_t active_cap = 64u, active_len = 0u;
    NameKeyVisit *stack = cetta_malloc(sizeof(*stack) * stack_cap);
    Atom **active = cetta_malloc(sizeof(*active) * active_cap);
    bool ok = name_key_stack_push(
        &stack, &stack_len, &stack_cap, (NameKeyVisit){root, false});

    while (ok && stack_len > 0u) {
        NameKeyVisit visit = stack[--stack_len];
        Atom *atom = visit.atom;
        if (visit.leave) {
            if (active_len == 0u || active[active_len - 1u] != atom) {
                ok = false;
                break;
            }
            active_len--;
            continue;
        }
        if (!atom || atom->kind == ATOM_VAR ||
            (atom->flags & ATOM_FLAG_HASH_STABLE) == 0u) {
            ok = false;
            break;
        }
        if (atom->kind != ATOM_EXPR) continue;
        if (name_key_active_contains(active, active_len, atom)) {
            ok = false;
            break;
        }
        if (active_len == active_cap) {
            if (active_cap > SIZE_MAX / 2u ||
                active_cap * 2u > SIZE_MAX / sizeof(*active)) {
                ok = false;
                break;
            }
            active_cap *= 2u;
            active = cetta_realloc(active, sizeof(*active) * active_cap);
        }
        active[active_len++] = atom;
        if (!name_key_stack_push(
                &stack, &stack_len, &stack_cap,
                (NameKeyVisit){atom, true})) {
            ok = false;
            break;
        }
        for (CettaExprIndex i = atom->expr.len; i > 0u; i--)
            if (!name_key_stack_push(
                    &stack, &stack_len, &stack_cap,
                    (NameKeyVisit){atom->expr.elems[i - 1u], false})) {
                ok = false;
                break;
            }
    }

    free(active);
    free(stack);
    return ok && active_len == 0u;
}

static size_t name_key_bucket_count(size_t requested) {
    size_t count = 8u;
    while (count < requested) {
        if (count > SIZE_MAX / 2u) return 0u;
        count *= 2u;
    }
    return count;
}

NameKeyTable *name_key_table_new_with_hash_mask(size_t bucket_count,
                                                uint32_t hash_mask) {
    size_t count = name_key_bucket_count(bucket_count);
    if (count == 0u) return NULL;
    NameKeyTable *table = cetta_malloc(sizeof(*table));
    memset(table, 0, sizeof(*table));
    arena_init(&table->arena);
    table->buckets = cetta_malloc(sizeof(*table->buckets) * count);
    memset(table->buckets, 0, sizeof(*table->buckets) * count);
    table->bucket_count = count;
    table->by_id_cap = 16u;
    table->by_id = cetta_malloc(sizeof(*table->by_id) * table->by_id_cap);
    memset(table->by_id, 0, sizeof(*table->by_id) * table->by_id_cap);
    table->hash_mask = hash_mask;
    if (pthread_mutex_init(&table->mutex, NULL) != 0) {
        arena_free(&table->arena);
        free(table->by_id);
        free(table->buckets);
        free(table);
        return NULL;
    }
    return table;
}

NameKeyTable *name_key_table_new(size_t bucket_count) {
    return name_key_table_new_with_hash_mask(bucket_count, UINT32_MAX);
}

void name_key_table_delete(NameKeyTable *table) {
    if (!table) return;
    for (size_t i = 0u; i < table->bucket_count; i++) {
        NameKeyEntry *entry = table->buckets[i];
        while (entry) {
            NameKeyEntry *next = entry->next;
            free(entry);
            entry = next;
        }
    }
    pthread_mutex_destroy(&table->mutex);
    arena_free(&table->arena);
    free(table->by_id);
    free(table->buckets);
    free(table);
}

static bool name_key_by_id_grow(NameKeyTable *table, NameId id) {
    if ((size_t)id < table->by_id_cap) return true;
    size_t cap = table->by_id_cap;
    while (cap <= (size_t)id) {
        if (cap > SIZE_MAX / 2u || cap * 2u > SIZE_MAX / sizeof(*table->by_id))
            return false;
        cap *= 2u;
    }
    table->by_id = cetta_realloc(table->by_id, sizeof(*table->by_id) * cap);
    memset(table->by_id + table->by_id_cap, 0,
           sizeof(*table->by_id) * (cap - table->by_id_cap));
    table->by_id_cap = cap;
    return true;
}

static bool name_key_rehash_locked(NameKeyTable *table, size_t count) {
    if (!table || count <= table->bucket_count ||
        count > SIZE_MAX / sizeof(*table->buckets))
        return count == table->bucket_count;
    NameKeyEntry **buckets = cetta_malloc(sizeof(*buckets) * count);
    memset(buckets, 0, sizeof(*buckets) * count);
    for (size_t i = 0u; i < table->bucket_count; i++) {
        NameKeyEntry *entry = table->buckets[i];
        while (entry) {
            NameKeyEntry *next = entry->next;
            size_t bucket = (size_t)entry->digest & (count - 1u);
            entry->next = buckets[bucket];
            buckets[bucket] = entry;
            entry = next;
        }
    }
    free(table->buckets);
    table->buckets = buckets;
    table->bucket_count = count;
    return true;
}

static bool name_key_ensure_bucket_capacity_locked(NameKeyTable *table,
                                                   uint32_t needed) {
    size_t count = table->bucket_count;
    while ((uint64_t)needed * 4u > (uint64_t)count * 3u) {
        if (count > SIZE_MAX / 2u) return false;
        count *= 2u;
    }
    return name_key_rehash_locked(table, count);
}

NameId name_key_intern(NameKeyTable *table, Atom *key) {
    if (!table || !name_key_is_admissible(key)) return NAME_ID_NONE;
    uint32_t digest = atom_hash(key) & table->hash_mask;

    pthread_mutex_lock(&table->mutex);
    size_t bucket = (size_t)digest & (table->bucket_count - 1u);
    for (NameKeyEntry *entry = table->buckets[bucket]; entry;
         entry = entry->next) {
        if (entry->digest == digest &&
            (CETTA_NAME_KEY_MUTATION == 1 || atom_eq(entry->key, key))) {
            NameId id = entry->id;
            pthread_mutex_unlock(&table->mutex);
            return id;
        }
    }
    if (table->len == UINT32_MAX) {
        pthread_mutex_unlock(&table->mutex);
        return NAME_ID_NONE;
    }
    NameId id = table->len + 1u;
    if (!name_key_by_id_grow(table, id) ||
        !name_key_ensure_bucket_capacity_locked(table, id)) {
        pthread_mutex_unlock(&table->mutex);
        return NAME_ID_NONE;
    }
    bucket = (size_t)digest & (table->bucket_count - 1u);
    Atom *owned = atom_deep_copy(&table->arena, key);
    if (!owned) {
        pthread_mutex_unlock(&table->mutex);
        return NAME_ID_NONE;
    }
    NameKeyEntry *entry = cetta_malloc(sizeof(*entry));
    entry->digest = digest;
    entry->id = id;
    entry->key = owned;
    entry->next = table->buckets[bucket];
    table->buckets[bucket] = entry;
    table->by_id[id] = entry;
    table->len++;
    pthread_mutex_unlock(&table->mutex);
    return id;
}

NameId name_key_find(const NameKeyTable *table, Atom *key) {
    if (!table || !name_key_is_admissible(key)) return NAME_ID_NONE;
    uint32_t digest = atom_hash(key) & table->hash_mask;

    pthread_mutex_lock((pthread_mutex_t *)&table->mutex);
    size_t bucket = (size_t)digest & (table->bucket_count - 1u);
    for (NameKeyEntry *entry = table->buckets[bucket]; entry;
         entry = entry->next) {
        if (entry->digest == digest && atom_eq(entry->key, key)) {
            NameId id = entry->id;
            pthread_mutex_unlock((pthread_mutex_t *)&table->mutex);
            return id;
        }
    }
    pthread_mutex_unlock((pthread_mutex_t *)&table->mutex);
    return NAME_ID_NONE;
}

const Atom *name_key_lookup(const NameKeyTable *table, NameId id) {
    if (!table || id == NAME_ID_NONE || (size_t)id >= table->by_id_cap)
        return NULL;
    pthread_mutex_lock((pthread_mutex_t *)&table->mutex);
    const Atom *key = table->by_id[id] ? table->by_id[id]->key : NULL;
    pthread_mutex_unlock((pthread_mutex_t *)&table->mutex);
    return key;
}

uint32_t name_key_count(const NameKeyTable *table) {
    if (!table) return 0u;
    pthread_mutex_lock((pthread_mutex_t *)&table->mutex);
    uint32_t count = table->len;
    pthread_mutex_unlock((pthread_mutex_t *)&table->mutex);
    return count;
}
