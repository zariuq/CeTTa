#include "library_re.h"
#include "library.h"
#include "eval.h"
#include "string_ops.h"
#include "native/langdef_module.h"
#include "native/regex_compiler_v1.h"
#include "native/regex_source_v1.h"
#include "native/regex_text_v1.h"
#include "native/regex_trie_v1.h"
#include "native/regex_selection_internal_v1.h"
#include "native/unicode_class_v1.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static const char *pattern_kind = "regular-capture-pattern";
typedef struct RePreparedEntry RePreparedEntry;
typedef struct ReTextAlias ReTextAlias;
typedef struct { CettaReProgramV1 *program; RePreparedEntry *entry; } RePatternOwner;
struct RePreparedEntry {
    CettaLibraryContext *context;
    uint64_t id, hash;
    size_t references;
    CettaReProgramV1 *program;
    Arena keys;
    Atom *pattern, *groups;
    RePreparedEntry *next, **previous;
    ReTextAlias *aliases;
};
/* A pattern description names its preparation only through a live entry.
 * The frontend's result is a function of the description text, its
 * look-behind policy and the context (which fixes the baked reader, the
 * authored compiler and the Unicode providers), so a later use of an equal
 * description borrows the entry instead of reading the text again. The
 * entry's last owner retires its aliases with it. */
struct ReTextAlias {
    CettaLibraryContext *context;
    uint64_t hash;
    RePreparedEntry *entry;
    ReTextAlias *next, **previous, *sibling;
    size_t len;
    bool reverse;
    char text[];
};
/* Registry storage has one allocation seam, so failure controls can refuse
 * any registry allocation. A refused registration leaves the compiled owner
 * correct and merely unshared. The controls also count frontend readings. */
#ifdef CETTA_RE_REGISTRY_CONTROLS
static bool (*registry_refuse)(void);
static uint64_t frontend_runs;
#define REGISTRY_ADMITS() (!registry_refuse || !registry_refuse())
#define FRONTEND_READ() (++frontend_runs)
#else
#define REGISTRY_ADMITS() true
#define FRONTEND_READ() ((void)0)
#endif
static void *registry_calloc(size_t count, size_t size) {
    return REGISTRY_ADMITS() ? calloc(count, size) : NULL;
}
/* Weak entries hold no program reference. An existing prepared owner keeps
 * its entry alive; releasing the last value or explicitly closing it removes
 * the entry. Keys are the actual semantic compiler operands, including the
 * capture layout, not an unverified text-to-program association. The context
 * identifies the immutable baked compiler and Unicode provider environment. */
static pthread_mutex_t prepared_mutex = PTHREAD_MUTEX_INITIALIZER;
static RePreparedEntry **prepared_buckets;
static size_t prepared_capacity, prepared_length;
static ReTextAlias **alias_buckets;
static size_t alias_capacity, alias_length;

static uint64_t prepared_hash(CettaLibraryContext *context, Atom *pattern, Atom *groups) {
    uint64_t hash = atom_hash(pattern);
    hash ^= groups ? (uint64_t)atom_hash(groups) << 32 : UINT64_C(0x9e3779b97f4a7c15);
    hash ^= (uint64_t)(uintptr_t)context;
    hash ^= hash >> 30; hash *= UINT64_C(0xbf58476d1ce4e5b9);
    hash ^= hash >> 27; hash *= UINT64_C(0x94d049bb133111eb);
    return hash ^ (hash >> 31);
}
static void prepared_link(RePreparedEntry *entry) {
    size_t slot = entry->hash & (prepared_capacity - 1u);
    entry->next = prepared_buckets[slot];
    entry->previous = &prepared_buckets[slot];
    if (entry->next) entry->next->previous = &entry->next;
    prepared_buckets[slot] = entry;
}
static bool prepared_reserve(void) {
    if (prepared_capacity && prepared_length < prepared_capacity - prepared_capacity / 4u) return true;
    size_t capacity = prepared_capacity ? prepared_capacity * 2u : 16u;
    if (capacity < prepared_capacity || capacity > SIZE_MAX / sizeof(*prepared_buckets)) return false;
    RePreparedEntry **buckets = registry_calloc(capacity, sizeof(*buckets));
    if (!buckets) return false;
    RePreparedEntry **old = prepared_buckets; size_t old_capacity = prepared_capacity;
    prepared_buckets = buckets; prepared_capacity = capacity;
    for (size_t i = 0; i < old_capacity; ++i) {
        RePreparedEntry *entry = old[i];
        while (entry) { RePreparedEntry *next = entry->next; prepared_link(entry); entry = next; }
    }
    free(old); return true;
}
static uint64_t alias_hash(CettaLibraryContext *context, const char *text, size_t len, bool reverse) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325) ^ (uint64_t)(uintptr_t)context ^ (reverse ? 0x9e3779b9u : 0u);
    for (size_t i = 0; i < len; ++i) { hash ^= (unsigned char)text[i]; hash *= UINT64_C(0x100000001b3); }
    hash ^= hash >> 31; hash *= UINT64_C(0x94d049bb133111eb);
    return hash ^ (hash >> 29);
}
static void alias_link(ReTextAlias *alias) {
    size_t slot = alias->hash & (alias_capacity - 1u);
    alias->next = alias_buckets[slot];
    alias->previous = &alias_buckets[slot];
    if (alias->next) alias->next->previous = &alias->next;
    alias_buckets[slot] = alias;
}
static bool alias_reserve(void) {
    if (alias_capacity && alias_length < alias_capacity - alias_capacity / 4u) return true;
    size_t capacity = alias_capacity ? alias_capacity * 2u : 16u;
    if (capacity < alias_capacity || capacity > SIZE_MAX / sizeof(*alias_buckets)) return false;
    ReTextAlias **buckets = registry_calloc(capacity, sizeof(*buckets));
    if (!buckets) return false;
    ReTextAlias **old = alias_buckets; size_t old_capacity = alias_capacity;
    alias_buckets = buckets; alias_capacity = capacity;
    for (size_t i = 0; i < old_capacity; ++i) {
        ReTextAlias *alias = old[i];
        while (alias) { ReTextAlias *next = alias->next; alias_link(alias); alias = next; }
    }
    free(old); return true;
}
static ReTextAlias *alias_find(CettaLibraryContext *context, uint64_t hash,
                               const char *text, size_t len, bool reverse) {
    if (!alias_capacity) return NULL;
    for (ReTextAlias *alias = alias_buckets[hash & (alias_capacity - 1u)]; alias; alias = alias->next)
        if (alias->hash == hash && alias->context == context && alias->reverse == reverse &&
            alias->len == len && !memcmp(alias->text, text, len)) return alias;
    return NULL;
}
/* Called with the mutex held when an entry retires: its aliases go too. */
static ReTextAlias *alias_detach_all(RePreparedEntry *entry) {
    ReTextAlias *aliases = entry->aliases;
    for (ReTextAlias *alias = aliases; alias; alias = alias->sibling) {
        *alias->previous = alias->next;
        if (alias->next) alias->next->previous = alias->previous;
        --alias_length;
    }
    entry->aliases = NULL;
    if (!alias_length && alias_buckets) { free(alias_buckets); alias_buckets = NULL; alias_capacity = 0u; }
    return aliases;
}
static void alias_free_all(ReTextAlias *alias) {
    while (alias) { ReTextAlias *sibling = alias->sibling; free(alias); alias = sibling; }
}
static void entry_release(RePreparedEntry *entry) {
    if (!entry) return;
    RePreparedEntry *retired = NULL; ReTextAlias *aliases = NULL;
    pthread_mutex_lock(&prepared_mutex);
    if (--entry->references == 0u) {
        *entry->previous = entry->next;
        if (entry->next) entry->next->previous = entry->previous;
        retired = entry;
        aliases = alias_detach_all(entry);
        if (--prepared_length == 0u) {
            free(prepared_buckets); prepared_buckets = NULL; prepared_capacity = 0u;
        }
    }
    pthread_mutex_unlock(&prepared_mutex);
    alias_free_all(aliases);
    if (retired) { arena_free(&retired->keys); free(retired); }
}
static void release_pattern(void *resource) {
    RePatternOwner *owner = resource;
    entry_release(owner->entry);
    cetta_re_v1_release(owner->program); free(owner);
}
static Atom *failure(Arena *a, const char *kind, const char *detail) {
    return atom_expr3(a, atom_symbol(a, "re:failed"), atom_symbol(a, kind), atom_string(a, detail));
}
static bool string(Atom *x) { return x && x->kind == ATOM_GROUNDED && x->ground.gkind == GV_STRING; }
static bool match_cancelled(void *unused) { (void)unused; return eval_native_interrupted(); }
static Atom *boolean_value(const CettaLibraryContext *context, Arena *arena, bool value) {
    if (context->session.language_id == CETTA_LANGUAGE_PETTA)
        return petta_semantics_boolean_value(arena, value);
    return atom_symbol_id(arena, value ? g_builtin_syms.true_text : g_builtin_syms.false_text);
}
/* The owner adopts one program reference and, when given, one reference to
 * the live entry it was found through. */
static Atom *owned_entry(CettaLibraryContext *ctx, Arena *a, CettaReProgramV1 *p, RePreparedEntry *entry) {
    RePatternOwner *owner = calloc(1u, sizeof(*owner));
    if (!owner) {
        entry_release(entry); cetta_re_v1_release(p);
        return failure(a, "re:capacity", "cannot allocate compiled pattern owner");
    }
    owner->program = p; owner->entry = entry;
    uint64_t id = 0;
    if (!cetta_native_handle_alloc(ctx, pattern_kind, owner, release_pattern, &id)) {
        release_pattern(owner); return failure(a, "re:capacity", "cannot retain compiled pattern");
    }
    return cetta_native_handle_owned_atom(ctx, a, pattern_kind, id);
}
static Atom *owned(CettaLibraryContext *ctx, Arena *a, CettaReProgramV1 *p) {
    return owned_entry(ctx, a, p, NULL);
}
static CettaReProgramV1 *prepared_lookup(CettaLibraryContext *context, Atom *pattern, Atom *groups) {
    uint64_t hash = prepared_hash(context, pattern, groups);
    CettaReProgramV1 *program = NULL;
    pthread_mutex_lock(&prepared_mutex);
    if (prepared_capacity)
        for (RePreparedEntry *entry = prepared_buckets[hash & (prepared_capacity - 1u)]; entry; entry = entry->next)
            if (entry->hash == hash && entry->context == context &&
                (!!entry->groups == !!groups) && (!groups || atom_eq(entry->groups, groups)) &&
                atom_eq(entry->pattern, pattern)) {
                program = cetta_re_v1_retain(entry->program); break;
            }
    pthread_mutex_unlock(&prepared_mutex);
    return program;
}
/* Returns the entry the owner now belongs to, if any. */
static RePreparedEntry *prepared_publish(CettaLibraryContext *context, Atom *pattern, Atom *groups, Atom *handle) {
    uint64_t id;
    if (!cetta_native_handle_arg(handle, pattern_kind, &id)) return NULL;
    RePatternOwner *owner = cetta_native_handle_get(context, pattern_kind, id);
    if (!owner) return NULL;
    pthread_mutex_lock(&prepared_mutex);
    uint64_t hash = prepared_hash(context, pattern, groups);
    if (!owner->entry && prepared_capacity) {
        for (RePreparedEntry *entry = prepared_buckets[hash & (prepared_capacity - 1u)]; entry; entry = entry->next)
            if (entry->hash == hash && entry->context == context &&
                (!!entry->groups == !!groups) && (!groups || atom_eq(entry->groups, groups)) &&
                atom_eq(entry->pattern, pattern)) {
                if (entry->program == owner->program && entry->references < SIZE_MAX) {
                    ++entry->references; owner->entry = entry;
                }
                RePreparedEntry *joined = owner->entry;
                pthread_mutex_unlock(&prepared_mutex); return joined;
            }
    }
    if (!owner->entry && prepared_reserve()) {
        RePreparedEntry *entry = registry_calloc(1u, sizeof(*entry));
        if (entry) {
            entry->context = context; entry->id = id;
            entry->hash = hash; entry->program = owner->program; entry->references = 1u;
            arena_init_detached(&entry->keys);
            entry->pattern = atom_deep_copy(&entry->keys, pattern);
            entry->groups = groups ? atom_deep_copy(&entry->keys, groups) : NULL;
            prepared_link(entry); owner->entry = entry; ++prepared_length;
        }
    }
    RePreparedEntry *joined = owner->entry;
    pthread_mutex_unlock(&prepared_mutex);
    return joined;
}
/* A later equal description borrows the live entry. The new owner's entry
 * reference is taken while the registry is locked, so retirement cannot
 * intervene. */
static RePreparedEntry *alias_acquire(CettaLibraryContext *context, const char *text, size_t len,
                                      bool reverse, CettaReProgramV1 **program) {
    uint64_t hash = alias_hash(context, text, len, reverse);
    RePreparedEntry *entry = NULL;
    pthread_mutex_lock(&prepared_mutex);
    ReTextAlias *alias = alias_find(context, hash, text, len, reverse);
    if (alias && alias->entry->references < SIZE_MAX) {
        entry = alias->entry; ++entry->references;
        *program = cetta_re_v1_retain(entry->program);
    }
    pthread_mutex_unlock(&prepared_mutex);
    return entry;
}
/* The caller's owner keeps entry live for the duration of this call. */
static void alias_publish(CettaLibraryContext *context, const char *text, size_t len,
                          bool reverse, RePreparedEntry *entry) {
    if (!entry || len > SIZE_MAX - sizeof(ReTextAlias)) return;
    ReTextAlias *alias = registry_calloc(1u, sizeof(*alias) + len);
    if (!alias) return;
    alias->context = context; alias->hash = alias_hash(context, text, len, reverse);
    alias->entry = entry; alias->len = len; alias->reverse = reverse;
    memcpy(alias->text, text, len);
    bool linked = false;
    pthread_mutex_lock(&prepared_mutex);
    if (!alias_find(context, alias->hash, text, len, reverse) && alias_reserve()) {
        alias_link(alias); alias->sibling = entry->aliases; entry->aliases = alias;
        ++alias_length; linked = true;
    }
    pthread_mutex_unlock(&prepared_mutex);
    if (!linked) free(alias);
}
static Atom *match_value(Arena *a, const CettaReProgramV1 *program, const CettaReResultV1 *result) {
    if (result->outcome == CETTA_RE_V1_NO_MATCH) return atom_symbol(a, "re:no-match");
    if (result->outcome == CETTA_RE_V1_INCOMPLETE)
        return atom_expr2(a, atom_symbol(a, "re:incomplete"), atom_int(a, (int64_t)result->state_visits));
    if (result->outcome != CETTA_RE_V1_FOUND)
        return failure(a, cetta_re_v1_outcome_name(result->outcome), "regex execution did not complete normally");
    Atom *groups = atom_symbol(a, "LNil");
    for (uint32_t i = result->group_len; i > 0; --i) {
        CettaReCaptureV1 g = result->groups[i - 1u];
        Atom *entry = g.participated ? atom_expr(a, (Atom *[]){atom_symbol(a, "re:capture-span"),
            atom_int(a, i - 1u), atom_int(a, (int64_t)g.start), atom_int(a, (int64_t)g.end)}, 4u) :
            atom_expr2(a, atom_symbol(a, "re:unmatched"), atom_int(a, i - 1u));
        const char *name = cetta_re_v1_group_name(program, i - 1u);
        if (name) entry = atom_expr3(a, atom_symbol(a, "re:named-group"), atom_string(a, name), entry);
        groups = atom_expr3(a, atom_symbol(a, "LCons"), entry, groups);
    }
    CettaReCaptureV1 whole = result->groups[0];
    return atom_expr(a, (Atom *[]){atom_symbol(a, "re:match"), atom_int(a, (int64_t)whole.start),
        atom_int(a, (int64_t)whole.end), groups}, 4u);
}

static Atom *symbol_query(Arena *arena, const CettaReProgramV1 *program,
                          CettaReModeV1 mode, bool *interrupted) {
    CettaReExecutionPolicyV1 policy = {.cancelled = match_cancelled};
    CettaReNameAutomatonV1 *automaton = NULL;
    bool indexed = cetta_re_v1_name_automaton(program, mode, &policy, &automaton);
    SymbolNameQueryResult snapshot = {0};
    bool queried = symbol_table_query_names(g_symbols,
        indexed ? cetta_re_v1_name_automaton_view(automaton) : NULL,
        indexed ? cetta_re_v1_name_automaton_initial(automaton) : NULL, &snapshot);
    CettaReOutcomeV1 status = indexed ? cetta_re_v1_name_automaton_outcome(automaton) : CETTA_RE_V1_NO_MATCH;
    cetta_re_v1_name_automaton_free(automaton);
    Atom *answer = NULL;
    if (status == CETTA_RE_V1_CANCELLED || eval_native_interrupted()) {
        *interrupted = true; goto done;
    }
    if (!queried) {
        answer = failure(arena, status == CETTA_RE_V1_INCOMPLETE ? "re:incomplete" : "re:capacity",
            "symbol-name query did not complete"); goto done;
    }
    Atom *names = atom_symbol(arena, "LNil"), **tail = &names;
    for (size_t i = 0; i < snapshot.len; ++i) {
        if (eval_native_interrupted()) { *interrupted = true; goto done; }
        SymbolId id = snapshot.ids[i];
        if (!indexed) {
            SymbolSpelling spelling = symbol_spelling(g_symbols, id);
            CettaReResultV1 result = cetta_re_v1_find_policy_valid_utf8(program,
                (const uint8_t *)spelling.bytes, spelling.len, 0u, mode, &policy);
            bool found = result.outcome == CETTA_RE_V1_FOUND;
            CettaReOutcomeV1 outcome = result.outcome;
            cetta_re_v1_result_free(&result);
            if (outcome == CETTA_RE_V1_CANCELLED) { *interrupted = true; goto done; }
            if (outcome != CETTA_RE_V1_FOUND && outcome != CETTA_RE_V1_NO_MATCH) {
                answer = failure(arena, cetta_re_v1_outcome_name(outcome),
                    "symbol-name filtering did not complete"); goto done;
            }
            if (!found) continue;
        }
        Atom *cell = atom_expr3(arena, atom_symbol(arena, "LCons"),
            atom_symbol_id(arena, id), atom_symbol(arena, "LNil"));
        *tail = cell; tail = &cell->expr.elems[2];
    }
    answer = atom_expr3(arena, atom_symbol(arena, "re:symbols"), names,
        atom_expr3(arena, atom_symbol(arena, "re:name-snapshot"),
            atom_int(arena, snapshot.snapshot_count ? snapshot.snapshot_count - 1u : 0u),
            atom_int(arena, snapshot.invalid_utf8_count)));
done:
    symbol_name_query_free(&snapshot); return answer;
}

static const char *relation_kind = "regex-match-relation";
typedef struct {
    CettaReRelationV1 *cursor;
    CettaReProgramV1 *program;
    CettaReResultV1 *answers;
    size_t length, capacity;
    bool finished;
} ReRelationOwner;

static void release_relation(void *resource) {
    ReRelationOwner *owner = resource;
    if (!owner) return;
    cetta_re_v1_relation_release(owner->cursor);
    cetta_re_v1_release(owner->program);
    for (size_t i = 0u; i < owner->length; ++i) cetta_re_v1_result_free(&owner->answers[i]);
    free(owner->answers); free(owner);
}

static Atom *relation_token(CettaLibraryContext *ctx, Arena *arena, uint64_t id, size_t index) {
    Atom *handle = cetta_native_handle_owned_atom(ctx, arena, relation_kind, id);
    return handle ? atom_expr3(arena, atom_symbol(arena, "re:relation-token"), handle,
        atom_int(arena, (int64_t)index)) : failure(arena, "re:invalid-handle", "relation owner was released");
}

static ReRelationOwner *relation_owner(CettaLibraryContext *ctx, Atom *token,
                                       uint64_t *id, size_t *index) {
    if (!token || token->kind != ATOM_EXPR || token->expr.len != 3u ||
        !atom_is_symbol(token->expr.elems[0], "re:relation-token") ||
        !cetta_native_handle_arg(token->expr.elems[1], relation_kind, id)) return NULL;
    Atom *position = token->expr.elems[2];
    if (!position || position->kind != ATOM_GROUNDED || position->ground.gkind != GV_INT ||
        position->ground.ival < 0 || (uint64_t)position->ground.ival > SIZE_MAX) return NULL;
    *index = (size_t)position->ground.ival;
    ReRelationOwner *owner = cetta_native_handle_get(ctx, relation_kind, *id);
    return owner && *index <= owner->length ? owner : NULL;
}

static bool relation_reserve(ReRelationOwner *owner) {
    if (owner->length < owner->capacity) return true;
    size_t capacity = owner->capacity ? owner->capacity * 2u : 8u;
    if (capacity < owner->capacity || capacity > SIZE_MAX / sizeof(*owner->answers)) return false;
    void *answers = realloc(owner->answers, capacity * sizeof(*owner->answers));
    if (!answers) return false;
    owner->answers = answers; owner->capacity = capacity; return true;
}

static Atom *relation_step(CettaLibraryContext *ctx, Arena *arena, ReRelationOwner *owner,
                           uint64_t id, size_t index, uint64_t budget, bool *interrupted) {
    if (index == owner->length && !owner->finished) {
        /* Reserve publication before advancing the native frontier. A failed
         * allocation cannot consume an answer that was never cached. */
        if (index >= INT64_MAX || !relation_reserve(owner))
            return failure(arena, "re:capacity", "cannot retain the next demanded relation occurrence");
        CettaReExecutionPolicyV1 policy = {.work_budget = budget, .cancelled = match_cancelled};
        CettaReResultV1 answer = cetta_re_v1_relation_next(owner->cursor, &policy);
        if (answer.outcome == CETTA_RE_V1_CANCELLED) {
            cetta_re_v1_result_free(&answer); *interrupted = true; return NULL;
        }
        if (answer.outcome == CETTA_RE_V1_INCOMPLETE) {
            Atom *value = atom_expr3(arena, atom_symbol(arena, "re:relation-pending"),
                relation_token(ctx, arena, id, index), atom_int(arena, (int64_t)answer.state_visits));
            cetta_re_v1_result_free(&answer); return value;
        }
        owner->answers[owner->length++] = answer;
        if (answer.outcome != CETTA_RE_V1_FOUND) {
            owner->finished = true;
            cetta_re_v1_relation_release(owner->cursor); owner->cursor = NULL;
        }
    }
    if (index >= owner->length)
        return failure(arena, "re:invalid-argument", "relation token does not identify a demanded continuation");
    const CettaReResultV1 *answer = &owner->answers[index];
    if (answer->outcome == CETTA_RE_V1_NO_MATCH) return atom_symbol(arena, "re:relation-complete");
    Atom *value = match_value(arena, owner->program, answer);
    if (answer->outcome != CETTA_RE_V1_FOUND) return value;
    return atom_expr3(arena, atom_symbol(arena, "re:relation-yield"), value,
        relation_token(ctx, arena, id, index + 1u));
}

static bool nonnegative(Atom *value, size_t *out) {
    if (!value || value->kind != ATOM_GROUNDED || value->ground.gkind != GV_INT ||
        value->ground.ival < 0 || (uint64_t)value->ground.ival > SIZE_MAX) return false;
    *out = (size_t)value->ground.ival; return true;
}

static Atom *capture_entry(Atom *match, Atom *identifier) {
    if (!match || match->kind != ATOM_EXPR || match->expr.len != 4u ||
        !atom_is_symbol(match->expr.elems[0], "re:match")) return NULL;
    size_t number = 0u; bool numbered = nonnegative(identifier, &number);
    const char *name = string(identifier) ? identifier->ground.sval :
        identifier && identifier->kind == ATOM_SYMBOL ? atom_name_cstr(identifier) : NULL;
    size_t name_len = string(identifier) ? atom_string_len(identifier) : name ? strlen(name) : 0u;
    if (!numbered && !name) return NULL;
    Atom *list = match->expr.elems[3];
    while (list && list->kind == ATOM_EXPR && list->expr.len == 3u &&
        atom_is_symbol(list->expr.elems[0], "LCons")) {
        Atom *entry = list->expr.elems[1]; bool named = false;
        if (entry && entry->kind == ATOM_EXPR && entry->expr.len == 3u &&
            atom_is_symbol(entry->expr.elems[0], "re:named-group") && string(entry->expr.elems[1])) {
            Atom *entry_name = entry->expr.elems[1];
            named = name && name_len == atom_string_len(entry_name) && !memcmp(name, entry_name->ground.sval, name_len);
            entry = entry->expr.elems[2];
        }
        size_t group;
        if (entry && entry->kind == ATOM_EXPR &&
            ((entry->expr.len == 2u && atom_is_symbol(entry->expr.elems[0], "re:unmatched")) ||
             (entry->expr.len == 4u && atom_is_symbol(entry->expr.elems[0], "re:capture-span"))) &&
            nonnegative(entry->expr.elems[1], &group) && (numbered ? group == number : named)) return entry;
        list = list->expr.elems[2];
    }
    return NULL;
}

static Atom *group_observation(Arena *arena, Atom **operands, bool text) {
    Atom *entry = capture_entry(operands[0], operands[text ? 2u : 1u]);
    if (!entry) return failure(arena, "re:group-reference", "group is not present in this match layout");
    if (entry->expr.len == 2u) return atom_deep_copy(arena, entry);
    size_t start, end;
    if (!nonnegative(entry->expr.elems[2], &start) || !nonnegative(entry->expr.elems[3], &end) || start > end)
        return failure(arena, "re:invalid-argument", "capture is not a valid half-open byte span");
    if (!text) return atom_expr3(arena, atom_symbol(arena, "re:span"),
        atom_int(arena, (int64_t)start), atom_int(arena, (int64_t)end));
    Atom *input = operands[1];
    if (!string(input) || end > atom_string_len(input))
        return failure(arena, "re:invalid-argument", "capture is outside the supplied text");
    const uint8_t *bytes = (const uint8_t *)input->ground.sval; size_t len = atom_string_len(input);
    if (!cetta_re_v1_valid_utf8(bytes, len) ||
        (start < len && (bytes[start] & 0xc0u) == 0x80u) || (end < len && (bytes[end] & 0xc0u) == 0x80u))
        return failure(arena, "re:invalid-utf8", "capture endpoints must be scalar boundaries in the original text");
    return atom_string_n(arena, input->ground.sval + start, end - start);
}

typedef struct { const char *bytes; size_t len; uint32_t group; bool capture; } ReplacementPart;
typedef enum { TEMPLATE_INVALID, TEMPLATE_OK, TEMPLATE_ALLOCATION, TEMPLATE_REFERENCE, TEMPLATE_UTF8 } TemplateStatus;
static bool push_part(ReplacementPart **parts, size_t *count, size_t *capacity, ReplacementPart part) {
    if (*count == *capacity) {
        size_t next = *capacity ? *capacity * 2u : 8u;
        if (next < *capacity || next > SIZE_MAX / sizeof(**parts)) return false;
        void *grown = realloc(*parts, next * sizeof(**parts));
        if (!grown) return false;
        *parts = grown; *capacity = next;
    }
    (*parts)[(*count)++] = part; return true;
}
static bool group_reference(const CettaReProgramV1 *program, const char *bytes, size_t len,
                             const CettaUtf8Unit *units, size_t first, size_t end, uint32_t *group) {
    bool numeric = true, overflow = false; uint32_t value = 0;
    for (size_t i = first; i < end; ++i) {
        uint32_t digit;
        if (!unicode_v1_decimal_value(units[i].scalar, &digit, NULL)) { numeric = false; break; }
        if (value > (UINT32_MAX - digit) / 10u) overflow = true;
        else if (!overflow) value = value * 10u + digit;
    }
    if (numeric) {
        if (overflow || value >= cetta_re_v1_group_count(program)) return false;
        *group = value; return true;
    }
    for (uint32_t i = 1; i < cetta_re_v1_group_count(program); ++i) {
        const char *name = cetta_re_v1_group_name(program, i);
        if (name && strlen(name) == len && !memcmp(name, bytes, len)) { *group = i; return true; }
    }
    return false;
}
static TemplateStatus text_parts(const CettaReProgramV1 *program, Atom *input,
                                 ReplacementPart **out, size_t *count) {
    const char *bytes = input->ground.sval; size_t len = atom_string_len(input), n = 0, capacity = 0;
    CettaUtf8Unit *units = NULL; size_t size = 0, bad = SIZE_MAX;
    if (!cetta_utf8_units(bytes, len, &units, &size, &bad))
        return bad == SIZE_MAX ? TEMPLATE_ALLOCATION : TEMPLATE_UTF8;
    ReplacementPart *parts = NULL; TemplateStatus status = TEMPLATE_OK;
    for (size_t i = 0; i < size;) {
        ReplacementPart part = {0};
        if (units[i].scalar != '$') {
            size_t start = units[i].offset;
            do { ++i; } while (i < size && units[i].scalar != '$');
            part.bytes = bytes + start; part.len = (i < size ? units[i].offset : len) - start;
        } else {
            if (++i == size) { status = TEMPLATE_INVALID; break; }
            if (units[i].scalar == '$') { part.bytes = bytes + units[i++].offset; part.len = 1u; }
            else {
                size_t first, end;
                if (units[i].scalar == '{') {
                    first = ++i;
                    while (i < size && units[i].scalar != '}') ++i;
                    end = i;
                    if (i == size || first == end) { status = TEMPLATE_INVALID; break; }
                    ++i;
                } else {
                    uint32_t ignored;
                    if (!unicode_v1_decimal_value(units[i].scalar, &ignored, NULL)) { status = TEMPLATE_INVALID; break; }
                    first = i++;
                    while (i < size && unicode_v1_decimal_value(units[i].scalar, &ignored, NULL)) ++i;
                    end = i;
                }
                size_t start = units[first].offset, stop = end < size ? units[end].offset : len;
                part.capture = true;
                if (!group_reference(program, bytes + start, stop - start, units, first, end, &part.group)) {
                    status = TEMPLATE_REFERENCE; break;
                }
            }
        }
        if (!push_part(&parts, &n, &capacity, part)) { status = TEMPLATE_ALLOCATION; break; }
    }
    free(units);
    if (status != TEMPLATE_OK) { free(parts); return status; }
    *out = parts; *count = n; return TEMPLATE_OK;
}
static TemplateStatus replacement_parts(const CettaReProgramV1 *program, Atom *input, bool literal,
                               ReplacementPart **out, size_t *count) {
    if (!literal && string(input)) return text_parts(program, input, out, count);
    ReplacementPart *parts = NULL; size_t n = 0, capacity = 0;
    if (literal) {
        if (!string(input)) return TEMPLATE_INVALID;
        if (!(parts = malloc(sizeof(*parts)))) return TEMPLATE_ALLOCATION;
        parts[n++] = (ReplacementPart){.bytes = input->ground.sval, .len = atom_string_len(input)};
    } else while (input && input->kind == ATOM_EXPR && input->expr.len == 3u && atom_is_symbol(input->expr.elems[0], "LCons")) {
        Atom *part = input->expr.elems[1]; ReplacementPart parsed = {0};
        if (!part || part->kind != ATOM_EXPR || part->expr.len != 2u) goto fail;
        Atom *value = part->expr.elems[1];
        if (atom_is_symbol(part->expr.elems[0], "re:text") && string(value)) {
            parsed.bytes = value->ground.sval; parsed.len = atom_string_len(value);
        } else if (atom_is_symbol(part->expr.elems[0], "re:group")) {
            parsed.capture = true;
            if (value && value->kind == ATOM_GROUNDED && value->ground.gkind == GV_INT && value->ground.ival >= 0 &&
                (uint64_t)value->ground.ival < cetta_re_v1_group_count(program)) parsed.group = (uint32_t)value->ground.ival;
            else if (string(value)) {
                bool found = false;
                for (uint32_t i = 1; i < cetta_re_v1_group_count(program); ++i) {
                    const char *name = cetta_re_v1_group_name(program, i);
                    if (name && strlen(name) == atom_string_len(value) && !memcmp(name, value->ground.sval, atom_string_len(value))) {
                        parsed.group = i; found = true; break;
                    }
                }
                if (!found) goto fail;
            } else goto fail;
        } else goto fail;
        if (n == capacity) {
            size_t next = capacity ? capacity * 2u : 8u;
            if (next < capacity || next > SIZE_MAX / sizeof(*parts)) goto allocation;
            void *grown = realloc(parts, next * sizeof(*parts));
            if (!grown) goto allocation;
            parts = grown; capacity = next;
        }
        parts[n++] = parsed; input = input->expr.elems[2];
    }
    if (!literal && !atom_is_symbol(input, "LNil")) goto fail;
    *out = parts; *count = n; return TEMPLATE_OK;
allocation:
    free(parts); return TEMPLATE_ALLOCATION;
fail:
    free(parts); return TEMPLATE_INVALID;
}
static Atom *render_matches(Arena *arena, const CettaReProgramV1 *program,
                            const uint8_t *bytes, size_t len, bool split, Atom *replacement, bool literal,
                            bool *interrupted) {
    ReplacementPart *parts = NULL; size_t part_count = 0;
    if (!split) {
        TemplateStatus status = replacement_parts(program, replacement, literal, &parts, &part_count);
        if (status != TEMPLATE_OK) return failure(arena,
            status == TEMPLATE_ALLOCATION ? "re:allocation-failure" : status == TEMPLATE_REFERENCE ? "re:template-reference" :
            status == TEMPLATE_UTF8 ? "re:invalid-utf8" : "re:template",
            status == TEMPLATE_ALLOCATION ? "replacement template allocation" :
            status == TEMPLATE_REFERENCE ? "template reference is not in the compiled capture layout" :
            status == TEMPLATE_UTF8 ? "replacement template is not UTF8" :
            "expected $digits/${name}/$$ text or constructed re:text/re:group parts");
    }
    CettaByteBuffer buffer; cetta_byte_buffer_init(&buffer);
    CettaReWorkspaceV1 workspace = {0};
    CettaReCursorV1 cursor = {.bytes = bytes, .byte_len = len};
    Atom *list = atom_symbol(arena, "LNil"), **tail = &list, *answer = NULL;
    size_t previous = 0;
    CettaReExecutionPolicyV1 policy = {.cancelled = match_cancelled};
    for (;;) {
        if (eval_native_interrupted()) { *interrupted = true; break; }
        CettaReResultV1 result = cetta_re_v1_next_workspace(program, &cursor, &policy, &workspace);
        if (result.outcome == CETTA_RE_V1_CANCELLED) {
            cetta_re_v1_result_free(&result); *interrupted = true; break;
        }
        if (result.outcome != CETTA_RE_V1_FOUND && result.outcome != CETTA_RE_V1_NO_MATCH) {
            answer = match_value(arena, program, &result); cetta_re_v1_result_free(&result); break;
        }
        bool done = result.outcome == CETTA_RE_V1_NO_MATCH;
        size_t until = done ? len : result.groups[0].start;
        if (split) {
            Atom *piece = atom_string_n(arena, (const char *)bytes + previous, until - previous);
            Atom *cell = atom_expr3(arena, atom_symbol(arena, "LCons"), piece, atom_symbol(arena, "LNil"));
            *tail = cell; tail = &cell->expr.elems[2];
        } else {
            bool ok = cetta_byte_buffer_append(&buffer, (const char *)bytes + previous, until - previous);
            for (size_t i = 0; !done && ok && i < part_count; ++i) {
                ReplacementPart part = parts[i];
                if (!part.capture) ok = cetta_byte_buffer_append(&buffer, part.bytes, part.len);
                else {
                    CettaReCaptureV1 span = result.groups[part.group];
                    if (span.participated) ok = cetta_byte_buffer_append(&buffer, (const char *)bytes + span.start, span.end - span.start);
                }
            }
            if (!ok) { answer = failure(arena, "re:allocation-failure", "replacement output allocation"); cetta_re_v1_result_free(&result); break; }
        }
        if (done) {
            answer = split ? atom_expr2(arena, atom_symbol(arena, "re:pieces"), list) : atom_string_n(arena, buffer.bytes ? buffer.bytes : "", buffer.len);
            cetta_re_v1_result_free(&result); break;
        }
        previous = result.groups[0].end; cetta_re_v1_result_free(&result);
    }
    cetta_re_v1_workspace_free(&workspace);
    free(parts); cetta_byte_buffer_free(&buffer); return answer;
}
/* Compile a constructed or lowered pattern under an optional capture layout.
 * The compiler's intermediate terms live in work; only the observation is
 * allocated in a. *joined reports the live entry the new owner belongs to. */
static Atom *compile_pattern(CettaLibraryContext *ctx, Arena *a, Arena *work,
                             Atom *pattern, Atom *groups, RePreparedEntry **joined) {
    if (joined) *joined = NULL;
    CettaReProgramV1 *cached = prepared_lookup(ctx, pattern, groups);
    if (cached) {
        Atom *value = owned(ctx, a, cached);
        RePreparedEntry *entry = prepared_publish(ctx, pattern, groups, value);
        if (joined) *joined = entry;
        return value;
    }
    CettaReCompilerV1 *compiler = NULL; CettaReProgramV1 *p = NULL;
    CettaDeterministicEquationStatusV1 status = CETTA_DETERMINISTIC_EQUATION_V1_BAD_ARGUMENT;
    char error[512] = {0};
    /* Word membership is an explicit versioned dependency, never a locale
     * query. Its ranges are copied into each independently owned artifact. */
    UnicodeV1Ranges word_view;
    if (!unicode_v1_lexical_class("RegexWord", &word_view) || word_view.len > UINT32_MAX ||
        word_view.len > SIZE_MAX / sizeof(CettaReRangeV1))
        return failure(a, "re:interpretation-needed", "Unicode word provider is unresolved");
    CettaReRangeV1 *word = arena_alloc(work, (word_view.len ? word_view.len : 1u) * sizeof(*word));
    for (size_t i = 0; i < word_view.len; ++i)
        word[i] = (CettaReRangeV1){word_view.items[i].low, word_view.items[i].high};
    const char **names = NULL; uint32_t group_len = 0;
    if (groups) {
        Atom *list = groups; size_t n = 0;
        while (list && list->kind == ATOM_EXPR && list->expr.len == 3 && atom_is_symbol(list->expr.elems[0], "LCons")) {
            ++n; list = list->expr.elems[2];
        }
        if (!atom_is_symbol(list, "LNil") || n >= UINT32_MAX || n + 1u > SIZE_MAX / sizeof(*names))
            return failure(a, "re:invalid-argument", "expected a regex operation and inert operand tuple");
        group_len = (uint32_t)n + 1u; names = arena_alloc(work, (n + 1u) * sizeof(*names));
        memset(names, 0, (n + 1u) * sizeof(*names));
        bool *seen = arena_alloc(work, (n + 1u) * sizeof(*seen));
        memset(seen, 0, (n + 1u) * sizeof(*seen));
        for (list = groups; !atom_is_symbol(list, "LNil"); list = list->expr.elems[2]) {
            Atom *row = list->expr.elems[1];
            if (!row || row->kind != ATOM_EXPR || row->expr.len != 3 || !atom_is_symbol(row->expr.elems[0], "re:group") ||
                row->expr.elems[1]->kind != ATOM_GROUNDED || row->expr.elems[1]->ground.gkind != GV_INT ||
                row->expr.elems[1]->ground.ival < 1 || (uint64_t)row->expr.elems[1]->ground.ival >= group_len || !string(row->expr.elems[2]) ||
                memchr(row->expr.elems[2]->ground.sval, 0, atom_string_len(row->expr.elems[2])))
                return failure(a, "re:invalid-argument", "expected a regex operation and inert operand tuple");
            uint32_t group = (uint32_t)row->expr.elems[1]->ground.ival;
            if (seen[group]) return failure(a, "re:invalid-argument", "expected a regex operation and inert operand tuple");
            seen[group] = true;
            names[group] = atom_string_len(row->expr.elems[2]) ? row->expr.elems[2]->ground.sval : NULL;
        }
    }
    bool ok = cetta_re_v1_compiler_load(&compiler, error, sizeof(error)) &&
        cetta_re_v1_compile_named(compiler, work, pattern, names, group_len, word, (uint32_t)word_view.len, 0u, 0u,
            &p, &status, error, sizeof(error));
    cetta_re_v1_compiler_free(compiler);
    if (!ok && status == CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE)
        return atom_expr3(a, atom_symbol(a, "re:interpretation-needed"),
            atom_string(a, error), atom_deep_copy(a, pattern));
    if (!ok && status == CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT)
        return atom_expr2(a, atom_symbol(a, "re:incomplete"), atom_string(a, error));
    if (!ok) return failure(a, "re:compile", error);
    Atom *value = owned(ctx, a, p);
    RePreparedEntry *entry = prepared_publish(ctx, pattern, groups, value);
    if (joined) *joined = entry;
    return value;
}

/* Reads the parse while its replayed syntax is still borrowed: the same
 * numeric-reference selection, source location and lowering as the staged
 * operations, keeping only the lowered observation. */
typedef struct { Atom *cleaned, *mapping; bool reverse, identity; } ReFrontendRead;
static Atom *frontend_lower(void *opaque, Arena *arena, Atom *observation) {
    ReFrontendRead *read = opaque;
    Atom *syntax = cetta_re_v1_select_numeric_references(arena, observation, read->cleaned);
    /* Under the identity map, location is a copy of the observation. */
    if (!read->identity) syntax = cetta_re_v1_locate_source(arena, syntax, read->mapping);
    Atom *lowered = cetta_re_v1_lower_source_policy(arena, syntax, read->reverse, 0u, 0u);
    return lowered ? atom_deep_copy(arena, lowered) : NULL;
}

/* The whole textual frontend in one native composition: lexical preparation,
 * the baked module-derived reader, selection, lowering and compilation. The
 * reader's value-only observation comes first; any other outcome is read
 * again with its complete packed forest as evidence. A live preparation of
 * an equal description is borrowed instead. */
static Atom *compile_text(CettaLibraryContext *ctx, Arena *a, Atom *text, Atom *policy) {
    bool reverse = false;
    bool policy_ok = policy && policy->kind == ATOM_EXPR && policy->expr.len == 2u &&
        atom_is_symbol(policy->expr.elems[0], "re:lookbehind-policy") &&
        ((reverse = atom_is_symbol(policy->expr.elems[1], "Reverse")) ||
         atom_is_symbol(policy->expr.elems[1], "Forward"));
    if (policy_ok && string(text)) {
        CettaReProgramV1 *program = NULL;
        RePreparedEntry *entry = alias_acquire(ctx, text->ground.sval, atom_string_len(text), reverse, &program);
        if (entry) return owned_entry(ctx, a, program, entry);
    }
    Arena work; arena_init_detached(&work);
    Atom *value = NULL, *prepared = cetta_re_v1_prepare_text(&work, text);
    if (!prepared || prepared->kind != ATOM_EXPR || prepared->expr.len != 3u ||
        !atom_is_symbol(prepared->expr.elems[0], "re:source-text") || !string(prepared->expr.elems[1])) {
        value = prepared ? atom_deep_copy(a, prepared) :
            failure(a, "re:allocation-failure", "regex lexical source storage");
        goto done;
    }
    if (!policy_ok) {
        value = failure(a, "re:invalid-argument", "expected a regex operation and inert operand tuple");
        goto done;
    }
    FRONTEND_READ();
    Atom *cleaned = prepared->expr.elems[1], *mapping = prepared->expr.elems[2];
    ReFrontendRead read = {.cleaned = cleaned, .mapping = mapping, .reverse = reverse,
        .identity = mapping && mapping->kind == ATOM_EXPR && mapping->expr.len == 2u &&
            atom_is_symbol(mapping->expr.elems[0], "re:source-identity-v1") && string(mapping->expr.elems[1])};
    Atom *selection = atom_expr2(&work, atom_symbol(&work, "ParserSelectionV1"),
        atom_expr3(&work, atom_symbol(&work, "LCons"),
            atom_expr3(&work, atom_symbol(&work, "ParserRejectFollowingClassV1"),
                atom_string(&work, "re-src:backref"), atom_string(&work, "RxDigitClass")),
            atom_symbol(&work, "LNil")));
    char error[512] = {0};
    const uint8_t *bytes = (const uint8_t *)cleaned->ground.sval; size_t len = atom_string_len(cleaned);
    Atom *lowered = cetta_langdef_baked_parse_consume_v1(ctx, "regex-pattern-reader", bytes, len,
        selection, false, &work, frontend_lower, &read, error, sizeof(error));
    if (lowered && !(lowered->kind == ATOM_EXPR && lowered->expr.len == 3u &&
                     atom_is_symbol(lowered->expr.elems[0], "re:source-value")))
        lowered = cetta_langdef_baked_parse_consume_v1(ctx, "regex-pattern-reader", bytes, len,
            selection, true, &work, frontend_lower, &read, error, sizeof(error));
    if (!lowered) {
        value = atom_expr2(a, atom_symbol(a, "re:source-observation"),
            atom_expr2(a, atom_symbol(a, "ParserFailureV1"),
                atom_string(a, error[0] ? error : "regex reader unavailable")));
    } else if (lowered->kind == ATOM_EXPR && lowered->expr.len == 3u &&
               atom_is_symbol(lowered->expr.elems[0], "re:source-value")) {
        RePreparedEntry *entry = NULL;
        value = compile_pattern(ctx, a, &work, lowered->expr.elems[1], lowered->expr.elems[2], &entry);
        alias_publish(ctx, text->ground.sval, atom_string_len(text), reverse, entry);
    } else value = atom_deep_copy(a, lowered);
done:
    arena_free(&work);
    return value;
}

bool cetta_re_call(CettaLibraryContext *ctx, Arena *a, Atom *head, Atom **args,
                   uint32_t nargs, CettaCallOutcome *out) {
    if (!atom_is_symbol(head, "__cetta_lib_re")) return false;
    if (eval_native_interrupted()) { *out = cetta_call_interrupted(); return true; }
    Atom *value = NULL;
    if (nargs != 2 || !args[0] || args[0]->kind != ATOM_SYMBOL ||
        !args[1] || args[1]->kind != ATOM_EXPR) goto arguments;
    const char *operation = atom_name_cstr(args[0]);
    Atom **operands = args[1]->expr.elems; CettaExprLen count = args[1]->expr.len;
    char error[512] = {0};
    if ((!strcmp(operation, "group-span") && count == 2u) ||
        (!strcmp(operation, "group-text") && count == 3u)) {
        value = group_observation(a, operands, !strcmp(operation, "group-text"));
        goto completed;
    }
    if (!strcmp(operation, "relation-next") || !strcmp(operation, "relation-close") ||
        !strcmp(operation, "relation-progress")) {
        bool next = !strcmp(operation, "relation-next");
        if (count != 1u && !(next && count == 2u)) goto arguments;
        uint64_t relation_id; size_t index;
        ReRelationOwner *owner = relation_owner(ctx, operands[0], &relation_id, &index);
        if (!owner) {
            value = failure(a, "re:invalid-handle", "expected a live relation continuation token"); goto completed;
        }
        if (!strcmp(operation, "relation-close")) {
            (void)cetta_native_handle_close(ctx, relation_kind, relation_id);
            value = atom_symbol(a, "re:closed"); goto completed;
        }
        if (!strcmp(operation, "relation-progress")) {
            size_t demanded = owner->length - (owner->finished ? 1u : 0u);
            const char *state = !owner->finished ? "Pending" :
                owner->answers[owner->length - 1u].outcome == CETTA_RE_V1_NO_MATCH ? "Complete" : "Failed";
            value = atom_expr3(a, atom_symbol(a, "re:relation-state"),
                atom_int(a, (int64_t)demanded), atom_symbol(a, state)); goto completed;
        }
        size_t budget = 0u;
        if (count == 2u && !nonnegative(operands[1], &budget)) goto arguments;
        bool interrupted = false;
        value = relation_step(ctx, a, owner, relation_id, index, budget, &interrupted);
        if (interrupted) { *out = cetta_call_interrupted(); return true; }
        goto completed;
    }
    if (!strcmp(operation, "prepare-source") && count == 1) {
        value = cetta_re_v1_prepare_text(a, operands[0]);
        goto completed;
    }
    if (!strcmp(operation, "lower-source") && count >= 1 && count <= 4) {
        Atom *syntax = count == 4 ? cetta_re_v1_select_numeric_references(a, operands[0], operands[3]) : operands[0];
        if (count >= 2) syntax = cetta_re_v1_locate_source(a, syntax, operands[1]);
        bool reverse = false;
        if (count >= 3) {
            Atom *policy = operands[2];
            if (!policy || policy->kind != ATOM_EXPR || policy->expr.len != 2 ||
                !atom_is_symbol(policy->expr.elems[0], "re:lookbehind-policy")) goto arguments;
            if (atom_is_symbol(policy->expr.elems[1], "Reverse")) reverse = true;
            else if (!atom_is_symbol(policy->expr.elems[1], "Forward")) goto arguments;
        }
        value = cetta_re_v1_lower_source_policy(a, syntax, reverse, 0u, 0u);
        goto completed;
    }
    if ((!strcmp(operation, "compile-value") && count == 1) ||
        (!strcmp(operation, "compile-source") && count == 2)) {
        Arena work; arena_init_detached(&work);
        value = compile_pattern(ctx, a, &work, operands[0], count == 2 ? operands[1] : NULL, NULL);
        arena_free(&work);
        goto completed;
    }
    if (!strcmp(operation, "compile-text") && count == 2) {
        value = compile_text(ctx, a, operands[0], operands[1]);
        goto completed;
    }
    if (!strcmp(operation, "restore") && count == 1 && string(operands[0])) {
        CettaReProgramV1 *p = NULL;
        value = cetta_re_v1_restore((const uint8_t *)operands[0]->ground.sval, atom_string_len(operands[0]),
            &p, error, sizeof(error)) ? owned(ctx, a, p) : failure(a, "re:artifact", error);
        goto completed;
    }
    uint64_t id = 0;
    if (count && operands[0] && operands[0]->kind == ATOM_EXPR && operands[0]->expr.len >= 2u &&
        (atom_is_symbol(operands[0]->expr.elems[0], "re:failed") ||
         atom_is_symbol(operands[0]->expr.elems[0], "re:interpretation-needed") ||
         atom_is_symbol(operands[0]->expr.elems[0], "re:source-observation") ||
         atom_is_symbol(operands[0]->expr.elems[0], "re:incomplete"))) {
        value = operands[0]; goto completed;
    }
    if (!count || !cetta_native_handle_arg(operands[0], pattern_kind, &id)) goto handle;
    RePatternOwner *pattern_owner = cetta_native_handle_get(ctx, pattern_kind, id);
    if (!pattern_owner) goto handle;
    CettaReProgramV1 *p = pattern_owner->program;
    if (!strcmp(operation, "close") && count == 1) {
        (void)cetta_native_handle_close(ctx, pattern_kind, id); value = atom_symbol(a, "re:closed"); goto completed;
    }
    if (!strcmp(operation, "store") && count == 1) {
        uint8_t *bytes = NULL; size_t len = 0;
        value = cetta_re_v1_store(p, &bytes, &len, error, sizeof(error)) ?
            atom_string_n(a, (const char *)bytes, len) : failure(a, "re:artifact", error);
        free(bytes); goto completed;
    }
    if (!strcmp(operation, "symbol-query") && (count == 1u || count == 2u)) {
        CettaReModeV1 mode = CETTA_RE_V1_SEARCH;
        if (count == 2u) {
            if (atom_is_symbol(operands[1], "Full")) mode = CETTA_RE_V1_FULL;
            else if (atom_is_symbol(operands[1], "Prefix")) mode = CETTA_RE_V1_PREFIX;
            else if (!atom_is_symbol(operands[1], "Search")) goto arguments;
        }
        bool interrupted = false;
        value = symbol_query(a, p, mode, &interrupted);
        if (interrupted) { *out = cetta_call_interrupted(); return true; }
        goto completed;
    }
    if (!strcmp(operation, "relation-open")) {
        if ((count != 2u && count != 4u) || !string(operands[1])) goto arguments;
        CettaReModeV1 mode = CETTA_RE_V1_SEARCH; size_t start = 0u;
        if (count == 4u) {
            if (atom_is_symbol(operands[2], "Search")) mode = CETTA_RE_V1_SEARCH;
            else if (atom_is_symbol(operands[2], "Prefix")) mode = CETTA_RE_V1_PREFIX;
            else if (atom_is_symbol(operands[2], "Full")) mode = CETTA_RE_V1_FULL;
            else goto arguments;
            if (!nonnegative(operands[3], &start)) goto arguments;
        }
        ReRelationOwner *owner = calloc(1u, sizeof(*owner));
        if (!owner) { value = failure(a, "re:capacity", "cannot allocate relation owner"); goto completed; }
        CettaReOutcomeV1 reason;
        bool opened = cetta_re_v1_relation_open(p, (const uint8_t *)operands[1]->ground.sval,
            atom_string_len(operands[1]), start, mode, &owner->cursor, &reason);
        if (opened) owner->program = cetta_re_v1_retain(p);
        uint64_t relation_id;
        if (!opened || !owner->program || !cetta_native_handle_alloc(ctx, relation_kind, owner,
            release_relation, &relation_id)) {
            release_relation(owner);
            value = failure(a, opened ? "re:capacity" : cetta_re_v1_outcome_name(reason),
                "cannot open an owned match relation"); goto completed;
        }
        value = relation_token(ctx, a, relation_id, 0u); goto completed;
    }
    if (count < 2 || !string(operands[1])) goto arguments;
    bool split = !strcmp(operation, "split"), replace_literal = !strcmp(operation, "replace-literal"),
         replace_template = !strcmp(operation, "replace-template");
    if (split || replace_literal || replace_template) {
        if (count != (split ? 2u : 3u)) goto arguments;
        const uint8_t *bytes = (const uint8_t *)operands[1]->ground.sval; size_t len = atom_string_len(operands[1]);
        if (!cetta_re_v1_valid_utf8(bytes, len)) { value = failure(a, "re:invalid-utf8", "expected UTF8 input"); goto completed; }
        bool interrupted = false;
        value = render_matches(a, p, bytes, len, split, split ? NULL : operands[2], replace_literal, &interrupted);
        if (interrupted) { *out = cetta_call_interrupted(); return true; }
        goto completed;
    }
    uint64_t budget = 0;
    if (!strcmp(operation, "find-bounded")) {
        if (count != 3 || operands[2]->kind != ATOM_GROUNDED || operands[2]->ground.gkind != GV_INT ||
            operands[2]->ground.ival < 0) goto arguments;
        budget = (uint64_t)operands[2]->ground.ival;
    } else if (count != 2) goto arguments;
    const uint8_t *text = (const uint8_t *)operands[1]->ground.sval; size_t len = atom_string_len(operands[1]);
    if (!cetta_re_v1_valid_utf8(text, len)) {
        value = failure(a, "re:invalid-utf8", "expected scalar text without malformed UTF-8"); goto completed;
    }
    bool all = !strcmp(operation, "find-all");
    if (!all && strcmp(operation, "find") && strcmp(operation, "captures") && strcmp(operation, "full-match") &&
        strcmp(operation, "is-match") && strcmp(operation, "find-bounded")) goto arguments;
    CettaReModeV1 mode = !strcmp(operation, "full-match") ? CETTA_RE_V1_FULL : CETTA_RE_V1_SEARCH;
    CettaReCursorV1 cursor = {.bytes = text, .byte_len = len};
    Atom *list = atom_symbol(a, "LNil"), **tail = &list;
    CettaReExecutionPolicyV1 policy = {.work_budget = budget, .cancelled = match_cancelled};
    CettaReWorkspaceV1 workspace = {0};
    for (;;) {
        if (eval_native_interrupted()) {
            cetta_re_v1_workspace_free(&workspace); *out = cetta_call_interrupted(); return true;
        }
        CettaReResultV1 r = all ? cetta_re_v1_next_workspace(p, &cursor, &policy, &workspace) :
            cetta_re_v1_find_policy_valid_utf8(p, text, len, 0, mode, &policy);
        if (r.outcome == CETTA_RE_V1_CANCELLED) {
            cetta_re_v1_workspace_free(&workspace);
            cetta_re_v1_result_free(&r); *out = cetta_call_interrupted(); return true;
        }
        value = match_value(a, p, &r);
        if (!strcmp(operation, "is-match") && (r.outcome == CETTA_RE_V1_FOUND || r.outcome == CETTA_RE_V1_NO_MATCH))
            value = boolean_value(ctx, a, r.outcome == CETTA_RE_V1_FOUND);
        if (!all || (r.outcome != CETTA_RE_V1_FOUND && r.outcome != CETTA_RE_V1_NO_MATCH)) {
            cetta_re_v1_result_free(&r); break;
        }
        if (r.outcome == CETTA_RE_V1_NO_MATCH) {
            value = atom_expr2(a, atom_symbol(a, "re:matches"), list); cetta_re_v1_result_free(&r); break;
        }
        Atom *cell = atom_expr3(a, atom_symbol(a, "LCons"), value, atom_symbol(a, "LNil"));
        *tail = cell; tail = &cell->expr.elems[2];
        cetta_re_v1_result_free(&r);
    }
    cetta_re_v1_workspace_free(&workspace);
    goto completed;
handle:
    value = failure(a, "re:invalid-handle", "expected a live compiled pattern owner"); goto completed;
arguments:
    value = failure(a, "re:invalid-argument", "expected a regex operation and inert operand tuple");
completed:
    *out = cetta_call_completed_value(value); return true;
}
