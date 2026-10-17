#include "regex_selection_internal_v1.h"

#include <stdlib.h>
#include <string.h>

/* Extended observations use ordered, path-local search. Register contents
 * are part of a configuration: merging solely by (instruction, position)
 * would erase distinctions inspected by a backreference. */
typedef struct {
    uint32_t pc;
    size_t pos, priority_len, hash_previous;
    size_t reference_at, reference_end, reference_pos;
    uint64_t hash;
    unsigned stage;
} ReFrame;

typedef struct {
    ReFrame *frames;
    size_t *captures;
    uint8_t *open;
    size_t len, capacity, *buckets, bucket_len;
    uint8_t *priority, *best_priority;
    size_t priority_len, priority_capacity, best_priority_len;
    size_t *initial, *best;
    uint8_t *initial_open, *best_open;
    uint32_t entry;
    size_t start, goal, best_start;
    bool require_end, backwards, behind, negative, found, complete;
} ReTask;

typedef struct {
    const CettaReProgramV1 *program;
    const uint8_t *bytes;
    size_t byte_len, slots;
    uint64_t visits, budget;
    const CettaReExecutionPolicyV1 *policy;
    CettaReOutcomeV1 failure;
    size_t *scratch;
    uint8_t *scratch_open;
    ReTask *tasks;
    size_t task_len, task_capacity;
    bool relation, yielded;
} ReExtended;

struct CettaReRelationV1 {
    ReExtended run;
    uint8_t *bytes;
    size_t start;
    CettaReModeV1 mode;
    CettaReOutcomeV1 terminal;
    bool started, exhausted;
};

static bool tick(ReExtended *r) {
    if (r->policy && r->policy->cancelled && r->policy->cancelled(r->policy->context)) {
        r->failure = CETTA_RE_V1_CANCELLED; return false;
    }
    if (r->budget && r->visits == r->budget) {
        r->failure = CETTA_RE_V1_INCOMPLETE; return false;
    }
    if (r->visits != UINT64_MAX) ++r->visits;
    return true;
}

static bool decode(const uint8_t *bytes, size_t at, uint32_t *cp, size_t *after) {
    uint32_t x = bytes[at++]; unsigned count = 0;
    if (x >= 0xf0u) { count = 3u; x &= 7u; }
    else if (x >= 0xe0u) { count = 2u; x &= 15u; }
    else if (x >= 0xc0u) { count = 1u; x &= 31u; }
    while (count--) x = (x << 6u) | (bytes[at++] & 63u);
    *cp = x; *after = at; return true;
}

static size_t previous(const uint8_t *bytes, size_t pos) {
    --pos;
    while (pos && (bytes[pos] & 0xc0u) == 0x80u) --pos;
    return pos;
}

static bool contains(const CettaReRangeV1 *ranges, uint32_t len, uint32_t cp) {
    for (uint32_t i = 0; i < len; ++i)
        if (ranges[i].low <= cp && cp <= ranges[i].high) return true;
    return false;
}

static bool assertion(ReExtended *r, uint32_t which, size_t pos) {
    uint32_t before = 0, after = 0; size_t ignored;
    bool left = pos != 0u, right = pos != r->byte_len;
    if (left) decode(r->bytes, previous(r->bytes, pos), &before, &ignored);
    if (right) decode(r->bytes, pos, &after, &ignored);
    if (which == CETTA_RE_V1_TEXT_START) return !left;
    if (which == CETTA_RE_V1_TEXT_END) return !right;
    if (which == CETTA_RE_V1_LINE_START) return !left || before == '\n';
    if (which == CETTA_RE_V1_LINE_END) return !right || after == '\n';
    static const CettaReRangeV1 ascii[] = {{48,57}, {65,90}, {95,95}, {97,122}};
    bool ascii_mode = which == CETTA_RE_V1_ASCII_WORD_BOUNDARY ||
        which == CETTA_RE_V1_NOT_ASCII_WORD_BOUNDARY;
    const CettaReRangeV1 *ranges = ascii_mode ? ascii : r->program->word_ranges;
    uint32_t len = ascii_mode ? 4u : r->program->word_range_len;
    bool boundary = (left && contains(ranges, len, before)) !=
        (right && contains(ranges, len, after));
    return which == CETTA_RE_V1_WORD_BOUNDARY || which == CETTA_RE_V1_ASCII_WORD_BOUNDARY ?
        boundary : !boundary;
}

static uint32_t folded(const CettaReProgramV1 *p, uint32_t cp, uint32_t mode) {
    if (mode == 1u) return cp >= 'A' && cp <= 'Z' ? cp + 32u : cp;
    if (mode != 2u) return cp;
    size_t lo = 0u, hi = p->fold_pair_len;
    while (lo < hi) {
        size_t middle = lo + (hi - lo) / 2u;
        if (p->fold_pairs[middle].scalar < cp) lo = middle + 1u;
        else hi = middle;
    }
    return lo < p->fold_pair_len && p->fold_pairs[lo].scalar == cp ?
        p->fold_pairs[lo].canonical : cp;
}

static bool backreference(ReExtended *r, const ReTask *t, CettaReInstructionV1 op,
                          const size_t *caps, const uint8_t *open, size_t pos, size_t *end) {
    size_t left = caps[2u * op.argument], right = caps[2u * op.argument + 1u];
    if (open[op.argument] || left == SIZE_MAX || right == SIZE_MAX) return false;
    if (left > right) { size_t swap = left; left = right; right = swap; }
    size_t at = t->backwards ? right : left;
    while (t->backwards ? at > left : at < right) {
        if (!tick(r)) return false;
        if ((!t->backwards && pos == r->byte_len) || (t->backwards && !pos)) return false;
        uint32_t expected, actual; size_t next_at, next_pos;
        if (t->backwards) {
            next_at = previous(r->bytes, at); next_pos = previous(r->bytes, pos);
            size_t ignored; decode(r->bytes, next_at, &expected, &ignored);
            decode(r->bytes, next_pos, &actual, &ignored);
        } else {
            decode(r->bytes, at, &expected, &next_at);
            decode(r->bytes, pos, &actual, &next_pos);
        }
        if (folded(r->program, expected, op.second) != folded(r->program, actual, op.second)) return false;
        at = next_at; pos = next_pos;
        if (t->require_end && !t->backwards && pos > t->goal) return false;
    }
    *end = pos; return true;
}

static uint64_t mix(uint64_t hash, uint64_t value) {
    value ^= value >> 30u; value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27u; value *= UINT64_C(0x94d049bb133111eb);
    return (hash ^ (value ^ (value >> 31u))) * UINT64_C(0x100000001b3);
}

static bool reserve_frames(ReExtended *r, ReTask *t) {
    if (t->len < t->capacity) return true;
    size_t cap = t->capacity ? t->capacity * 2u : 16u;
    if (cap < t->capacity || cap > SIZE_MAX / sizeof(*t->frames) ||
        cap > SIZE_MAX / r->slots / sizeof(*t->captures) ||
        cap > SIZE_MAX / r->program->group_len) return false;
    void *frames = realloc(t->frames, cap * sizeof(*t->frames));
    if (!frames) return false;
    t->frames = frames;
    void *captures = realloc(t->captures, cap * r->slots * sizeof(*t->captures));
    if (!captures) return false;
    t->captures = captures;
    void *open = realloc(t->open, cap * r->program->group_len);
    if (!open) return false;
    t->open = open; t->capacity = cap;
    return true;
}

static bool rehash(ReTask *t) {
    size_t n = t->bucket_len ? t->bucket_len * 2u : 32u;
    if (n < t->bucket_len || n > SIZE_MAX / sizeof(*t->buckets)) return false;
    size_t *buckets = malloc(n * sizeof(*buckets));
    if (!buckets) return false;
    for (size_t i = 0; i < n; ++i) buckets[i] = SIZE_MAX;
    for (size_t i = 0; i < t->len; ++i) {
        size_t bucket = (size_t)t->frames[i].hash & (n - 1u);
        t->frames[i].hash_previous = buckets[bucket]; buckets[bucket] = i;
    }
    free(t->buckets); t->buckets = buckets; t->bucket_len = n; return true;
}

/* Source arrays may belong to a frame array that reserve_frames moves. */
static bool push(ReExtended *r, ReTask *t, uint32_t pc, size_t pos,
                 const size_t *caps, const uint8_t *open) {
    uint64_t hash = mix(mix(UINT64_C(0xcbf29ce484222325), pc), pos);
    bool full_key = !r->relation || r->program->extended;
    if (full_key) {
        for (size_t i = 0; i < r->slots; ++i) hash = mix(hash, caps[i]);
        for (uint32_t i = 0; i < r->program->group_len; ++i) hash = mix(hash, open[i]);
    }
    if (!t->bucket_len && !rehash(t)) goto allocation;
    size_t bucket = (size_t)hash & (t->bucket_len - 1u);
    for (size_t i = t->buckets[bucket]; i != SIZE_MAX; i = t->frames[i].hash_previous) {
        ReFrame f = t->frames[i];
        if (f.hash == hash && f.pc == pc && f.pos == pos && (!full_key ||
            (!memcmp(t->captures + i * r->slots, caps, r->slots * sizeof(*caps)) &&
             !memcmp(t->open + i * r->program->group_len, open, r->program->group_len)))) return false;
    }
    if (!r->relation && !tick(r)) return false;
    bool local_caps = t->captures && (uintptr_t)caps >= (uintptr_t)t->captures &&
        (uintptr_t)caps < (uintptr_t)(t->captures + t->len * r->slots);
    bool local_open = t->open && (uintptr_t)open >= (uintptr_t)t->open &&
        (uintptr_t)open < (uintptr_t)(t->open + t->len * r->program->group_len);
    size_t cap_offset = local_caps ? (size_t)(caps - t->captures) : 0u;
    size_t open_offset = local_open ? (size_t)(open - t->open) : 0u;
    if (!reserve_frames(r, t)) goto allocation;
    if (local_caps) caps = t->captures + cap_offset;
    if (local_open) open = t->open + open_offset;
    if (t->len >= t->bucket_len / 2u) {
        if (!rehash(t)) goto allocation;
        bucket = (size_t)hash & (t->bucket_len - 1u);
    }
    memcpy(t->captures + t->len * r->slots, caps, r->slots * sizeof(*caps));
    memcpy(t->open + t->len * r->program->group_len, open, r->program->group_len);
    t->frames[t->len] = (ReFrame){.pc = pc, .pos = pos, .priority_len = t->priority_len,
        .hash_previous = t->buckets[bucket], .hash = hash};
    t->buckets[bucket] = t->len++; return true;
allocation:
    r->failure = CETTA_RE_V1_ALLOCATION_FAILURE; return false;
}

static void pop(ReTask *t) {
    ReFrame f = t->frames[--t->len];
    t->buckets[(size_t)f.hash & (t->bucket_len - 1u)] = f.hash_previous;
    t->priority_len = f.priority_len;
}

static bool priority_add(ReExtended *r, ReTask *t, const uint8_t *data, size_t len) {
    if (len > SIZE_MAX - t->priority_len) goto allocation;
    size_t needed = t->priority_len + len;
    if (needed > t->priority_capacity) {
        size_t cap = t->priority_capacity ? t->priority_capacity : 32u;
        while (cap < needed) {
            if (cap > SIZE_MAX / 2u) { cap = needed; break; }
            cap *= 2u;
        }
        void *p = realloc(t->priority, cap);
        if (!p) goto allocation;
        t->priority = p; t->priority_capacity = cap;
    }
    if (len) memcpy(t->priority + t->priority_len, data, len);
    t->priority_len = needed; return true;
allocation:
    r->failure = CETTA_RE_V1_ALLOCATION_FAILURE; return false;
}

static bool preferred(const ReTask *t) {
    if (!t->found) return true;
    size_t n = t->priority_len < t->best_priority_len ? t->priority_len : t->best_priority_len;
    int order = n ? memcmp(t->priority, t->best_priority, n) : 0;
    if (order) return order < 0;
    if (t->priority_len != t->best_priority_len) return t->priority_len < t->best_priority_len;
    return t->start > t->best_start;
}

static bool remember(ReExtended *r, ReTask *t, const size_t *caps, const uint8_t *open, size_t pos) {
    if (preferred(t)) {
        uint8_t *priority = realloc(t->best_priority, t->priority_len ? t->priority_len : 1u);
        if (!priority) { r->failure = CETTA_RE_V1_ALLOCATION_FAILURE; return false; }
        t->best_priority = priority; t->best_priority_len = t->priority_len;
        if (t->priority_len) memcpy(priority, t->priority, t->priority_len);
        memcpy(t->best, caps, r->slots * sizeof(*caps));
        if (r->task_len == 1u) t->best[1] = pos;
        memcpy(t->best_open, open, r->program->group_len);
        t->best_start = t->start;
    }
    t->found = true; return true;
}

static void task_free(ReTask *t) {
    free(t->frames); free(t->captures); free(t->open); free(t->buckets);
    free(t->priority); free(t->best_priority);
    free(t->initial); free(t->best); free(t->initial_open); free(t->best_open);
    *t = (ReTask){0};
}

static bool task_push(ReExtended *r, uint32_t entry, size_t start, size_t goal,
                      bool require_end, bool backwards, bool behind, bool negative,
                      const size_t *caps, const uint8_t *open) {
    if (r->task_len == r->task_capacity) {
        size_t cap = r->task_capacity ? r->task_capacity * 2u : 8u;
        if (cap < r->task_capacity || cap > SIZE_MAX / sizeof(*r->tasks)) goto allocation;
        void *tasks = realloc(r->tasks, cap * sizeof(*r->tasks));
        if (!tasks) goto allocation;
        r->tasks = tasks; r->task_capacity = cap;
    }
    ReTask *t = &r->tasks[r->task_len++];
    *t = (ReTask){.entry = entry, .start = start, .goal = goal, .require_end = require_end,
        .backwards = backwards, .behind = behind, .negative = negative};
    t->initial = malloc(r->slots * sizeof(*t->initial));
    t->best = malloc(r->slots * sizeof(*t->best));
    t->initial_open = malloc(r->program->group_len); t->best_open = malloc(r->program->group_len);
    if (!t->initial || !t->best || !t->initial_open || !t->best_open) goto allocation;
    memcpy(t->initial, caps, r->slots * sizeof(*caps));
    memcpy(t->initial_open, open, r->program->group_len);
    (void)push(r, t, entry, start, t->initial, t->initial_open);
    return r->failure == CETTA_RE_V1_FOUND;
allocation:
    r->failure = CETTA_RE_V1_ALLOCATION_FAILURE; return false;
}

static void candidate_done(ReExtended *r, ReTask *t) {
    while (t->len) pop(t);
    if (t->behind && !t->backwards && t->start && !(t->negative && t->found)) {
        t->start = previous(r->bytes, t->start);
        (void)push(r, t, t->entry, t->start, t->initial, t->initial_open);
    } else t->complete = true;
}

/* One scalar comparison is one resumable transition. The frame remains the
 * same semantic configuration until its reference has been fully checked. */
static void reference_step(ReExtended *r, ReTask *t, ReFrame *frame,
                           CettaReInstructionV1 op, const size_t *caps, const uint8_t *open) {
    size_t at = frame->reference_at, pos = frame->reference_pos;
    bool finished = at == frame->reference_end;
    if (!finished) {
        if ((!t->backwards && pos == r->byte_len) || (t->backwards && !pos)) {
            frame->stage = 2u; return;
        }
        uint32_t expected, actual; size_t next_at, next_pos;
        if (t->backwards) {
            next_at = previous(r->bytes, at); next_pos = previous(r->bytes, pos);
            size_t ignored; decode(r->bytes, next_at, &expected, &ignored);
            decode(r->bytes, next_pos, &actual, &ignored);
        } else {
            decode(r->bytes, at, &expected, &next_at);
            decode(r->bytes, pos, &actual, &next_pos);
        }
        if (folded(r->program, expected, op.second) != folded(r->program, actual, op.second) ||
            (t->require_end && !t->backwards && next_pos > t->goal)) {
            frame->stage = 2u; return;
        }
        frame->reference_at = at = next_at;
        frame->reference_pos = pos = next_pos;
        finished = at == frame->reference_end;
    }
    if (finished) {
        frame->stage = 2u;
        (void)push(r, t, op.next, pos, caps, open);
    }
}

static void step(ReExtended *r, ReTask *t) {
    if (!t->len) { candidate_done(r, t); return; }
    size_t index = t->len - 1u;
    ReFrame *frame = &t->frames[index];
    CettaReInstructionV1 op = r->program->instructions[frame->pc];
    size_t *caps = t->captures + index * r->slots;
    uint8_t *open = t->open + index * r->program->group_len;
    if (frame->stage == 4u) { reference_step(r, t, frame, op, caps, open); return; }
    if (frame->stage == 2u) { pop(t); return; }
    if (op.opcode == CETTA_RE_V1_SPLIT) {
        uint8_t choice = frame->stage ? 1u : 0u;
        uint32_t target = frame->stage ? op.second : op.next;
        frame->stage = frame->stage ? 2u : 1u;
        t->priority_len = frame->priority_len;
        if (priority_add(r, t, &choice, 1u)) (void)push(r, t, target, frame->pos, caps, open);
        return;
    }
    frame->stage = 2u;
    if (op.opcode == CETTA_RE_V1_MATCH) {
        if (!t->require_end || frame->pos == t->goal) {
            if (r->relation && r->task_len == 1u) t->found = false;
            if (!remember(r, t, caps, open, frame->pos)) return;
            if (r->relation && r->task_len == 1u) r->yielded = true;
            else candidate_done(r, t);
        }
        return;
    }
    if (op.opcode == CETTA_RE_V1_FAIL) return;
    size_t pos = frame->pos;
    if (op.opcode == CETTA_RE_V1_LOOKAHEAD || op.opcode == CETTA_RE_V1_LOOKBEHIND) {
        bool behind = op.opcode == CETTA_RE_V1_LOOKBEHIND;
        bool backwards = (op.argument & 2u) != 0u;
        frame->stage = 3u;
        (void)task_push(r, op.second, pos, pos, behind && !backwards, backwards,
            behind, (op.argument & 1u) != 0u, caps, open);
        return;
    }
    if (op.opcode == CETTA_RE_V1_SAVE) {
        /* Update the successor snapshot, not the current frame's hash key. */
        memcpy(r->scratch, caps, r->slots * sizeof(*caps));
        memcpy(r->scratch_open, open, r->program->group_len);
        if (r->relation && !r->program->extended) {
            /* The regular profile uses the Pike SAVE last-write semantics,
             * not the extended open/closed register transition. */
            r->scratch[op.argument] = pos;
            r->scratch_open[op.argument / 2u] = 0u;
        } else if (!(op.argument & 1u)) {
            r->scratch[op.argument] = pos;
            r->scratch[op.argument + 1u] = SIZE_MAX;
            r->scratch_open[op.argument / 2u] = 1u;
        } else if (r->scratch_open[op.argument / 2u]) {
            r->scratch[op.argument] = pos;
            size_t start = r->scratch[op.argument - 1u];
            if (start > pos) {
                r->scratch[op.argument - 1u] = pos;
                r->scratch[op.argument] = start;
            }
            r->scratch_open[op.argument / 2u] = 0u;
        }
        (void)push(r, t, op.next, pos, r->scratch, r->scratch_open);
        return;
    }
    if (op.opcode == CETTA_RE_V1_ASSERT && !assertion(r, op.argument, pos)) return;
    if (op.opcode == CETTA_RE_V1_BACKREF) {
        if (r->relation) {
            size_t left = caps[2u * op.argument], right = caps[2u * op.argument + 1u];
            if (open[op.argument] || left == SIZE_MAX || right == SIZE_MAX) return;
            frame->reference_at = t->backwards ? right : left;
            frame->reference_end = t->backwards ? left : right;
            frame->reference_pos = pos;
            frame->stage = 4u;
            return;
        }
        size_t end;
        if (backreference(r, t, op, caps, open, pos, &end)) (void)push(r, t, op.next, end, caps, open);
        return;
    }
    if (op.opcode == CETTA_RE_V1_CHAR || op.opcode == CETTA_RE_V1_CLASS || op.opcode == CETTA_RE_V1_ANY) {
        if ((!t->backwards && pos == r->byte_len) || (t->backwards && !pos)) return;
        uint32_t cp; size_t after;
        if (t->backwards) {
            after = previous(r->bytes, pos); size_t ignored;
            decode(r->bytes, after, &cp, &ignored);
        } else decode(r->bytes, pos, &cp, &after);
        if (t->require_end && !t->backwards && after > t->goal) return;
        if (op.opcode == CETTA_RE_V1_CHAR && cp != op.argument) return;
        if (op.opcode == CETTA_RE_V1_CLASS && !contains(r->program->ranges + op.argument, op.second, cp)) return;
        pos = after;
    }
    (void)push(r, t, op.next, pos, caps, open);
}

static bool run(ReExtended *r) {
    while (r->task_len && r->failure == CETTA_RE_V1_FOUND) {
        if (r->relation && !tick(r)) break;
        size_t index = r->task_len - 1u;
        ReTask *t = &r->tasks[index];
        if (!t->complete) {
            step(r, t);
            if (r->yielded) return true;
            continue;
        }
        if (!index) return t->found;
        ReTask *parent = &r->tasks[index - 1u];
        size_t frame_index = parent->len - 1u;
        ReFrame *frame = &parent->frames[frame_index];
        CettaReInstructionV1 op = r->program->instructions[frame->pc];
        frame->stage = 2u;
        if (t->found != t->negative) {
            const size_t *caps = t->negative ? parent->captures + frame_index * r->slots : t->best;
            const uint8_t *open = t->negative ? parent->open + frame_index * r->program->group_len : t->best_open;
            if (t->negative || priority_add(r, parent, t->best_priority, t->best_priority_len))
                (void)push(r, parent, op.next, frame->pos, caps, open);
        }
        task_free(t); --r->task_len;
    }
    return false;
}

void cetta_re_v1_relation_release(CettaReRelationV1 *cursor) {
    if (!cursor) return;
    ReExtended *r = &cursor->run;
    for (size_t i = 0; i < r->task_len; ++i) task_free(&r->tasks[i]);
    free(r->tasks); free(r->scratch); free(r->scratch_open);
    cetta_re_v1_release((CettaReProgramV1 *)r->program);
    free(cursor->bytes); free(cursor);
}

bool cetta_re_v1_relation_open(CettaReProgramV1 *program, const uint8_t *bytes, size_t len,
    size_t start, CettaReModeV1 mode, CettaReRelationV1 **out, CettaReOutcomeV1 *error) {
    CettaReOutcomeV1 failure = CETTA_RE_V1_INVALID_ARGUMENT;
    if (!program || !out || (len && !bytes) || start > len ||
        (mode != CETTA_RE_V1_SEARCH && mode != CETTA_RE_V1_PREFIX && mode != CETTA_RE_V1_FULL) ||
        (mode == CETTA_RE_V1_FULL && start) ||
        (start < len && (bytes[start] & 0xc0u) == 0x80u)) goto fail;
    if (!cetta_re_v1_valid_utf8(bytes, len)) { failure = CETTA_RE_V1_INVALID_UTF8; goto fail; }
    failure = CETTA_RE_V1_ALLOCATION_FAILURE;
    CettaReRelationV1 *cursor = calloc(1u, sizeof(*cursor));
    if (!cursor) goto fail;
    ReExtended *r = &cursor->run;
    r->program = cetta_re_v1_retain(program);
    r->slots = (size_t)program->group_len * 2u;
    cursor->bytes = malloc(len ? len : 1u);
    if (r->slots <= SIZE_MAX / sizeof(*r->scratch))
        r->scratch = malloc(r->slots * sizeof(*r->scratch));
    r->scratch_open = malloc(program->group_len);
    if (!r->program || !cursor->bytes || !r->scratch || !r->scratch_open) {
        cetta_re_v1_relation_release(cursor); goto fail;
    }
    if (len) memcpy(cursor->bytes, bytes, len);
    r->bytes = cursor->bytes; r->byte_len = len; r->relation = true;
    cursor->start = start; cursor->mode = mode;
    CettaReRelationV1 *old = *out; *out = cursor;
    cetta_re_v1_relation_release(old);
    if (error) *error = CETTA_RE_V1_FOUND;
    return true;
fail:
    if (error) *error = failure;
    return false;
}

CettaReResultV1 cetta_re_v1_relation_next(CettaReRelationV1 *cursor,
                                         const CettaReExecutionPolicyV1 *policy) {
    CettaReResultV1 result = {.outcome = CETTA_RE_V1_INVALID_ARGUMENT};
    if (!cursor) return result;
    if (cursor->terminal) { result.outcome = cursor->terminal; return result; }
    if (cursor->exhausted) { result.outcome = CETTA_RE_V1_NO_MATCH; return result; }
    ReExtended *r = &cursor->run;
    r->failure = CETTA_RE_V1_FOUND; r->yielded = false; r->visits = 0u;
    r->budget = policy ? policy->work_budget : 0u; r->policy = policy;
    result.groups = calloc(r->program->group_len, sizeof(*result.groups));
    if (!result.groups) { cursor->terminal = result.outcome = CETTA_RE_V1_ALLOCATION_FAILURE; return result; }
    for (;;) {
        if (!cursor->started) {
            if (!tick(r)) break;
            for (size_t i = 0; i < r->slots; ++i) r->scratch[i] = SIZE_MAX;
            memset(r->scratch_open, 0, r->program->group_len);
            r->scratch[0] = cursor->start;
            if (!task_push(r, r->program->entry, cursor->start, r->byte_len,
                cursor->mode == CETTA_RE_V1_FULL, false, false, false, r->scratch, r->scratch_open)) break;
            cursor->started = true;
        }
        (void)run(r);
        if (r->failure != CETTA_RE_V1_FOUND) break;
        if (r->yielded) {
            ReTask *task = &r->tasks[0];
            for (uint32_t i = 0; i < r->program->group_len; ++i) {
                size_t left = task->best[2u * i], right = task->best[2u * i + 1u];
                bool closed = !task->best_open[i] && left != SIZE_MAX && right != SIZE_MAX;
                if (closed && (left > right || right > r->byte_len)) {
                    r->failure = CETTA_RE_V1_INVALID_ARGUMENT; break;
                }
                result.groups[i] = (CettaReCaptureV1){closed, closed ? left : 0u, closed ? right : 0u};
            }
            if (r->failure != CETTA_RE_V1_FOUND) break;
            result.group_len = r->program->group_len; result.outcome = CETTA_RE_V1_FOUND;
            result.state_visits = r->visits; r->policy = NULL;
            return result;
        }
        task_free(&r->tasks[0]); r->task_len = 0u;
        if (cursor->mode != CETTA_RE_V1_SEARCH || cursor->start == r->byte_len) {
            cursor->exhausted = true; result.outcome = CETTA_RE_V1_NO_MATCH; break;
        }
        uint32_t ignored; decode(r->bytes, cursor->start, &ignored, &cursor->start);
        cursor->started = false;
    }
    if (r->failure != CETTA_RE_V1_FOUND) {
        result.outcome = r->failure;
        if (r->failure != CETTA_RE_V1_INCOMPLETE && r->failure != CETTA_RE_V1_CANCELLED)
            cursor->terminal = r->failure;
    }
    result.state_visits = r->visits; r->policy = NULL;
    free(result.groups); result.groups = NULL;
    return result;
}

CettaReResultV1 cetta_re_v1_extended_find(const CettaReProgramV1 *p, const uint8_t *bytes,
    size_t len, size_t start, CettaReModeV1 mode, const CettaReExecutionPolicyV1 *policy) {
    CettaReResultV1 result = {.outcome = CETTA_RE_V1_NO_MATCH};
    ReExtended r = {.program = p, .bytes = bytes, .byte_len = len,
        .slots = (size_t)p->group_len * 2u, .budget = policy ? policy->work_budget : 0u,
        .policy = policy, .failure = CETTA_RE_V1_FOUND};
    size_t *caps = malloc(r.slots * sizeof(*caps));
    uint8_t *open = calloc(p->group_len, 1u);
    r.scratch = malloc(r.slots * sizeof(*r.scratch));
    r.scratch_open = malloc(p->group_len);
    if (!caps || !open || !r.scratch || !r.scratch_open) {
        result.outcome = CETTA_RE_V1_ALLOCATION_FAILURE; goto done;
    }
    for (size_t i = 0; i < r.slots; ++i) caps[i] = SIZE_MAX;
    for (size_t at = start;;) {
        caps[0] = at;
        if (!task_push(&r, p->entry, at, len, mode == CETTA_RE_V1_FULL, false, false, false, caps, open)) break;
        bool found = run(&r);
        if (r.failure != CETTA_RE_V1_FOUND) break;
        if (found) {
            ReTask *t = &r.tasks[0];
            result.groups = calloc(p->group_len, sizeof(*result.groups));
            if (!result.groups) { result.outcome = CETTA_RE_V1_ALLOCATION_FAILURE; break; }
            result.outcome = CETTA_RE_V1_FOUND; result.group_len = p->group_len;
            for (uint32_t i = 0; i < p->group_len; ++i) {
                size_t left = t->best[2u * i], right = t->best[2u * i + 1u];
                bool closed = !t->best_open[i] && left != SIZE_MAX && right != SIZE_MAX;
                if (left > right) { size_t swap = left; left = right; right = swap; }
                result.groups[i] = (CettaReCaptureV1){closed, closed ? left : 0u, closed ? right : 0u};
            }
            break;
        }
        task_free(&r.tasks[0]); r.task_len = 0u;
        if (mode != CETTA_RE_V1_SEARCH || at == len) break;
        uint32_t ignored; decode(bytes, at, &ignored, &at);
    }
    if (r.failure != CETTA_RE_V1_FOUND) result.outcome = r.failure;
done:
    result.state_visits = r.visits;
    for (size_t i = 0; i < r.task_len; ++i) task_free(&r.tasks[i]);
    free(r.tasks); free(caps); free(open); free(r.scratch); free(r.scratch_open); return result;
}
