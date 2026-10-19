#include "regex_selection_v1.h"
#include "regex_selection_internal_v1.h"
#include "regex_trie_v1.h"
#include "prepared_bytes_v1.h"

#include <limits.h>
#include <stdatomic.h>
#include <stdio.h>

static bool re_error(char *error, size_t size, const char *message) {
    if (error && size) snprintf(error, size, "%s", message);
    return false;
}
static bool scalar(uint32_t value) {
    return value <= 0x10ffffu && !(value >= 0xd800u && value <= 0xdfffu);
}
static bool range_valid(CettaReRangeV1 range) {
    return scalar(range.low) && scalar(range.high) && range.low <= range.high &&
        !(range.low < 0xd800u && range.high > 0xdfffu);
}
static void *array_copy(const void *source, size_t count, size_t width) {
    if (count > SIZE_MAX / width || (count && !source)) return NULL;
    void *out = malloc(count ? count * width : 1u);
    if (out && count) memcpy(out, source, count * width);
    return out;
}
static int compare_name(const void *left, const void *right) {
    return strcmp(*(const char *const *)left, *(const char *const *)right);
}

static bool re_program(const CettaReProgramSpecV1 *s, CettaReProgramV1 **out,
                        char *error, size_t error_size, bool accelerate) {
    if (!s || !out || !s->instructions || !s->instruction_len ||
        s->entry >= s->instruction_len || !s->group_len || s->group_len > UINT32_MAX / 2u ||
        (s->range_len && !s->ranges) || (s->word_range_len && !s->word_ranges) ||
        (s->fold_pair_len && !s->fold_pairs))
        return re_error(error, error_size, "invalid ordered-program shape");
    for (uint32_t i = 0; i < s->range_len; ++i)
        if (!range_valid(s->ranges[i])) return re_error(error, error_size, "invalid scalar range");
    for (uint32_t i = 0; i < s->word_range_len; ++i) {
        if (!range_valid(s->word_ranges[i]) ||
            (i && s->word_ranges[i - 1u].high >= s->word_ranges[i].low))
            return re_error(error, error_size, "word ranges must be sorted and disjoint");
    }
    for (uint32_t i = 0; i < s->fold_pair_len; ++i) {
        CettaReFoldPairV1 f = s->fold_pairs[i];
        if (!scalar(f.scalar) || !scalar(f.canonical) ||
            (i && s->fold_pairs[i - 1u].scalar >= f.scalar))
            return re_error(error, error_size, "invalid simple-fold provider");
    }
    bool extended = false;
    for (uint32_t i = 0; i < s->instruction_len; ++i) {
        CettaReInstructionV1 op = s->instructions[i];
        bool look = op.opcode == CETTA_RE_V1_LOOKAHEAD || op.opcode == CETTA_RE_V1_LOOKBEHIND;
        if (op.opcode > CETTA_RE_V1_LOOKBEHIND ||
            (op.opcode > CETTA_RE_V1_MATCH && op.next >= s->instruction_len) ||
            (op.opcode == CETTA_RE_V1_SPLIT && op.second >= s->instruction_len) ||
            (op.opcode == CETTA_RE_V1_CHAR && !scalar(op.argument)) ||
            (op.opcode == CETTA_RE_V1_SAVE &&
                (op.argument < 2u || op.argument >= 2u * s->group_len)) ||
            (op.opcode == CETTA_RE_V1_ASSERT && op.argument > CETTA_RE_V1_NOT_ASCII_WORD_BOUNDARY) ||
            (op.opcode == CETTA_RE_V1_BACKREF &&
                (!op.argument || op.argument >= s->group_len || op.second > 2u)) ||
            (look && (op.second >= s->instruction_len || op.argument > 3u || !s->ranks)) ||
            (op.opcode == CETTA_RE_V1_CLASS &&
                (op.argument > s->range_len || op.second > s->range_len - op.argument)))
            return re_error(error, error_size, "invalid ordered-program instruction");
        if (s->ranks && op.opcode > CETTA_RE_V1_MATCH &&
            (s->ranks[op.next] > s->ranks[i] ||
             (op.opcode == CETTA_RE_V1_SPLIT && s->ranks[op.second] > s->ranks[i]) ||
             (look && s->ranks[op.second] >= s->ranks[i])))
            return re_error(error, error_size, "invalid look-observation rank certificate");
        if (op.opcode >= CETTA_RE_V1_BACKREF) extended = true;
    }
    CettaReProgramV1 *p = calloc(1, sizeof(*p));
    if (!p) return re_error(error, error_size, "allocation failure");
    atomic_init(&p->references, 1u);
    p->instruction_len = s->instruction_len; p->entry = s->entry; p->group_len = s->group_len;
    p->range_len = s->range_len; p->word_range_len = s->word_range_len;
    p->extended = extended; p->fold_pair_len = s->fold_pair_len;
    p->instructions = array_copy(s->instructions, s->instruction_len, sizeof(*s->instructions));
    p->ranges = array_copy(s->ranges, s->range_len, sizeof(*s->ranges));
    p->word_ranges = array_copy(s->word_ranges, s->word_range_len, sizeof(*s->word_ranges));
    p->group_names = calloc(s->group_len, sizeof(*p->group_names));
    p->ranks = s->ranks ? array_copy(s->ranks, s->instruction_len, sizeof(*s->ranks)) : NULL;
    p->fold_pairs = array_copy(s->fold_pairs, s->fold_pair_len, sizeof(*s->fold_pairs));
    if (!p->instructions || !p->ranges || !p->word_ranges || !p->group_names ||
        (s->ranks && !p->ranks) || !p->fold_pairs) {
        cetta_re_v1_release(p); return re_error(error, error_size, "allocation failure");
    }
    for (uint32_t i = 0; s->group_names && i < s->group_len; ++i) {
        const char *name = s->group_names[i];
        if (!name) continue;
        if (!i || !*name || !cetta_re_v1_valid_utf8((const uint8_t *)name, strlen(name))) {
            cetta_re_v1_release(p); return re_error(error, error_size, "invalid capture name");
        }
        p->group_names[i] = array_copy(name, strlen(name) + 1u, 1u);
        if (!p->group_names[i]) { cetta_re_v1_release(p); return re_error(error, error_size, "allocation failure"); }
    }
    if (s->group_names) {
        const char **names = array_copy(p->group_names, s->group_len, sizeof(*names));
        if (!names) { cetta_re_v1_release(p); return re_error(error, error_size, "allocation failure"); }
        uint32_t n = 0;
        for (uint32_t i = 1; i < s->group_len; ++i) if (p->group_names[i]) names[n++] = p->group_names[i];
        qsort(names, n, sizeof(*names), compare_name);
        bool duplicate = false;
        for (uint32_t i = 1; i < n; ++i) if (!strcmp(names[i - 1u], names[i])) { duplicate = true; break; }
        free(names);
        if (duplicate) { cetta_re_v1_release(p); return re_error(error, error_size, "duplicate capture name"); }
    }
    if (accelerate) p->literal_trie = cetta_re_v1_literal_trie_build(p);
    CettaReProgramV1 *old = *out; *out = p; cetta_re_v1_release(old);
    return true;
}
bool cetta_re_v1_program(const CettaReProgramSpecV1 *s, CettaReProgramV1 **out,
                        char *error, size_t error_size) {
    return re_program(s, out, error, error_size, true);
}
CettaReProgramV1 *cetta_re_v1_retain(CettaReProgramV1 *p) {
    if (!p) return NULL;
    unsigned n = atomic_load_explicit(&p->references, memory_order_relaxed);
    do { if (n == UINT_MAX) return NULL; }
    while (!atomic_compare_exchange_weak_explicit(&p->references, &n, n + 1u,
        memory_order_relaxed, memory_order_relaxed));
    return p;
}
void cetta_re_v1_release(CettaReProgramV1 *p) {
    if (p && atomic_fetch_sub_explicit(&p->references, 1u, memory_order_acq_rel) == 1u) {
        if (p->group_names) for (uint32_t i = 0; i < p->group_len; ++i) free(p->group_names[i]);
        free(p->group_names);
        free(p->instructions); free(p->ranges); free(p->word_ranges);
        free(p->ranks); free(p->fold_pairs); cetta_scalar_trie_free(p->literal_trie); free(p);
    }
}
const char *cetta_re_v1_group_name(const CettaReProgramV1 *p, uint32_t group) {
    return p && group < p->group_len ? p->group_names[group] : NULL;
}
uint32_t cetta_re_v1_group_count(const CettaReProgramV1 *p) { return p ? p->group_len : 0u; }

bool cetta_re_v1_store(const CettaReProgramV1 *p, uint8_t **bytes, size_t *len,
                      char *error, size_t error_size) {
    if (!p || !bytes || !len) return re_error(error, error_size, "invalid store arguments");
    CettaPreparedWriterV1 w = {0}; cpw_bytes(&w, p->extended ? "CRP3" : "CRP2", 4u);
    const char *abi = p->extended ? "regex-observed-captures-v1" : "regex-ordered-captures-v1";
    cpw_text(&w, abi, strlen(abi));
    cpw_u64(&w, p->instruction_len); cpw_u64(&w, p->entry); cpw_u64(&w, p->group_len);
    cpw_u64(&w, p->range_len); cpw_u64(&w, p->word_range_len);
    for (uint32_t i = 0; i < p->instruction_len; ++i) {
        CettaReInstructionV1 op = p->instructions[i];
        cpw_u64(&w, op.opcode); cpw_u64(&w, op.next); cpw_u64(&w, op.second); cpw_u64(&w, op.argument);
    }
    for (uint32_t i = 0; i < p->range_len; ++i) {
        cpw_u64(&w, p->ranges[i].low); cpw_u64(&w, p->ranges[i].high);
    }
    for (uint32_t i = 0; i < p->word_range_len; ++i) {
        cpw_u64(&w, p->word_ranges[i].low); cpw_u64(&w, p->word_ranges[i].high);
    }
    for (uint32_t i = 0; i < p->group_len; ++i) {
        const char *name = p->group_names[i]; cpw_text(&w, name, name ? strlen(name) : 0u);
    }
    if (p->extended) {
        cpw_u64(&w, p->ranks ? p->instruction_len : 0u);
        if (p->ranks) for (uint32_t i = 0; i < p->instruction_len; ++i) cpw_u64(&w, p->ranks[i]);
        cpw_u64(&w, p->fold_pair_len);
        for (uint32_t i = 0; i < p->fold_pair_len; ++i) {
            cpw_u64(&w, p->fold_pairs[i].scalar); cpw_u64(&w, p->fold_pairs[i].canonical);
        }
    }
    if (w.failed) { free(w.data); return re_error(error, error_size, "allocation failure"); }
    *bytes = w.data; *len = w.len; return true;
}
static uint32_t read_u32(CettaPreparedReaderV1 *r) {
    uint64_t value = cpr_u64(r);
    if (value > UINT32_MAX) r->failed = true;
    return (uint32_t)value;
}
bool cetta_re_v1_restore(const uint8_t *bytes, size_t len, CettaReProgramV1 **out,
                        char *error, size_t error_size) {
    if (!bytes || !out) return re_error(error, error_size, "invalid restore arguments");
    CettaPreparedReaderV1 r = {bytes, len, 0u, false};
    bool extended = len >= 4u && !memcmp(bytes, "CRP3", 4u);
    if (!cpr_literal(&r, extended ? "CRP3" : "CRP2", 4u) ||
        !cpr_abi(&r, extended ? "regex-observed-captures-v1" : "regex-ordered-captures-v1"))
        return re_error(error, error_size, "ordered-program profile mismatch");
    CettaReProgramSpecV1 s = {0};
    s.instruction_len = read_u32(&r); s.entry = read_u32(&r); s.group_len = read_u32(&r);
    s.range_len = read_u32(&r); s.word_range_len = read_u32(&r);
    uint64_t payload = (uint64_t)s.instruction_len * 32u +
        ((uint64_t)s.range_len + s.word_range_len) * 16u;
    if (r.failed || payload + (uint64_t)s.group_len * 8u > r.len - r.at || !s.instruction_len)
        return re_error(error, error_size, "truncated or trailing ordered program");
    CettaReInstructionV1 *ops = calloc(s.instruction_len, sizeof(*ops));
    CettaReRangeV1 *ranges = calloc(s.range_len ? s.range_len : 1u, sizeof(*ranges));
    CettaReRangeV1 *word = calloc(s.word_range_len ? s.word_range_len : 1u, sizeof(*word));
    char **names = calloc(s.group_len ? s.group_len : 1u, sizeof(*names));
    uint32_t *ranks = NULL;
    CettaReFoldPairV1 *fold_pairs = NULL;
    if (!ops || !ranges || !word || !names) {
        free(ops); free(ranges); free(word); free(names); return re_error(error, error_size, "allocation failure");
    }
    for (uint32_t i = 0; i < s.instruction_len; ++i) {
        ops[i].opcode = read_u32(&r); ops[i].next = read_u32(&r);
        ops[i].second = read_u32(&r); ops[i].argument = read_u32(&r);
    }
    bool has_extended = false;
    for (uint32_t i = 0; i < s.instruction_len; ++i)
        if (ops[i].opcode >= CETTA_RE_V1_BACKREF) has_extended = true;
    if (has_extended != extended) r.failed = true;
    for (uint32_t i = 0; i < s.range_len; ++i) {
        ranges[i].low = read_u32(&r); ranges[i].high = read_u32(&r);
    }
    for (uint32_t i = 0; i < s.word_range_len; ++i) {
        word[i].low = read_u32(&r); word[i].high = read_u32(&r);
    }
    for (uint32_t i = 0; i < s.group_len && !r.failed; ++i) {
        size_t n = 0; const char *name = cpr_text(&r, &n);
        if (!name || memchr(name, 0, n)) { r.failed = true; break; }
        if (n) {
            names[i] = malloc(n + 1u);
            if (!names[i]) { r.failed = true; break; }
            memcpy(names[i], name, n); names[i][n] = 0;
        }
    }
    if (extended && !r.failed) {
        uint32_t rank_len = read_u32(&r);
        if (rank_len && rank_len != s.instruction_len) r.failed = true;
        if (!r.failed && (uint64_t)rank_len * 8u <= r.len - r.at) {
            if (rank_len) ranks = calloc(rank_len, sizeof(*ranks));
            if (rank_len && !ranks) r.failed = true;
            for (uint32_t i = 0; i < rank_len && !r.failed; ++i) ranks[i] = read_u32(&r);
        } else r.failed = true;
        s.fold_pair_len = read_u32(&r);
        if ((uint64_t)s.fold_pair_len * 16u > r.len - r.at) r.failed = true;
        if (!r.failed && s.fold_pair_len) {
            fold_pairs = calloc(s.fold_pair_len, sizeof(*fold_pairs));
            if (!fold_pairs) r.failed = true;
            for (uint32_t i = 0; i < s.fold_pair_len && !r.failed; ++i) {
                fold_pairs[i].scalar = read_u32(&r); fold_pairs[i].canonical = read_u32(&r);
            }
        }
    }
    s.instructions = ops; s.ranges = ranges; s.word_ranges = word; s.group_names = (const char *const *)names;
    s.ranks = ranks; s.fold_pairs = fold_pairs;
    /* CRP2/3 stores the ordered graph, not a prepared trie. Restoring it must
     * not silently run another preparation pass. */
    bool ok = !r.failed && r.at == r.len && re_program(&s, out, error, error_size, false);
    if (r.failed) re_error(error, error_size, "ordered-program field exceeds its ABI");
    else if (r.at != r.len) re_error(error, error_size, "trailing ordered-program bytes");
    for (uint32_t i = 0; i < s.group_len; ++i) free(names[i]);
    free(names); free(ops); free(ranges); free(word); free(ranks); free(fold_pairs); return ok;
}

/* Strict scalar decoding: overlong encodings, surrogates and out-of-range
 * values are faults; U+0000 is ordinary input, not a string terminator. */
static bool decode(const uint8_t *bytes, size_t len, size_t at, uint32_t *cp, size_t *after) {
    if (at >= len) return false;
    uint32_t x = bytes[at++]; unsigned more;
    if (x < 0x80u) more = 0;
    else if (x >= 0xc2u && x <= 0xdfu) { x &= 0x1fu; more = 1; }
    else if (x >= 0xe0u && x <= 0xefu) { x &= 0x0fu; more = 2; }
    else if (x >= 0xf0u && x <= 0xf4u) { x &= 7u; more = 3; }
    else return false;
    unsigned width = more;
    if (more > len - at) return false;
    while (more--) { uint8_t b = bytes[at++]; if ((b & 0xc0u) != 0x80u) return false; x = (x << 6u) | (b & 0x3fu); }
    if ((width == 1u && x < 0x80u) || (width == 2u && x < 0x800u) ||
        (width == 3u && x < 0x10000u) || !scalar(x)) return false;
    *cp = x; *after = at; return true;
}
static bool contains(const CettaReRangeV1 *ranges, uint32_t len, uint32_t cp) {
    for (uint32_t i = 0; i < len; ++i)
        if (ranges[i].low <= cp && cp <= ranges[i].high) return true;
    return false;
}
typedef struct {
    const CettaReProgramV1 *program;
    const uint8_t *bytes;
    size_t byte_len, slots;
    uint8_t *seen;
    uint32_t *stack;
    size_t *stack_captures, *working;
    uint64_t visits, budget;
    const CettaReExecutionPolicyV1 *policy;
    bool incomplete, cancelled;
} ReRun;
typedef struct { uint32_t *pc; size_t *captures, len; } ReThreads;

static bool assertion(ReRun *r, uint32_t which, size_t pos) {
    uint32_t before = 0, after = 0; size_t ignored = 0;
    bool has_before = pos > 0, has_after = pos < r->byte_len;
    if (has_before) {
        size_t previous = pos - 1u;
        while (previous && (r->bytes[previous] & 0xc0u) == 0x80u) --previous;
        (void)decode(r->bytes, r->byte_len, previous, &before, &ignored);
    }
    if (has_after) (void)decode(r->bytes, r->byte_len, pos, &after, &ignored);
    switch (which) {
    case CETTA_RE_V1_TEXT_START: return !has_before;
    case CETTA_RE_V1_TEXT_END: return !has_after;
    case CETTA_RE_V1_LINE_START: return !has_before || before == '\n';
    case CETTA_RE_V1_LINE_END: return !has_after || after == '\n';
    default: {
        static const CettaReRangeV1 ascii_word[] = {{48,57}, {65,90}, {95,95}, {97,122}};
        bool ascii = which == CETTA_RE_V1_ASCII_WORD_BOUNDARY || which == CETTA_RE_V1_NOT_ASCII_WORD_BOUNDARY;
        const CettaReRangeV1 *ranges = ascii ? ascii_word : r->program->word_ranges;
        uint32_t range_len = ascii ? 4u : r->program->word_range_len;
        bool left = has_before && contains(ranges, range_len, before);
        bool right = has_after && contains(ranges, range_len, after);
        return which == CETTA_RE_V1_WORD_BOUNDARY || which == CETTA_RE_V1_ASCII_WORD_BOUNDARY ? left != right : left == right;
    }
    }
}

/* At one input position the first visit to a PC wins. A branch owns its
 * register snapshot, so a failed branch cannot mutate a surviving branch. */
static void closure(ReRun *r, ReThreads *out, uint32_t root, const size_t *initial, size_t pos) {
    size_t pending = 1u;
    r->stack[0] = root;
    memcpy(r->stack_captures, initial, r->slots * sizeof(size_t));
    while (pending && !r->incomplete) {
        --pending;
        uint32_t pc = r->stack[pending];
        memcpy(r->working, r->stack_captures + pending * r->slots, r->slots * sizeof(size_t));
        while (!r->seen[pc]) {
            if (r->policy && r->policy->cancelled && r->policy->cancelled(r->policy->context)) {
                r->incomplete = r->cancelled = true; return;
            }
            if (r->budget && r->visits == r->budget) { r->incomplete = true; return; }
            ++r->visits; r->seen[pc] = 1u;
            CettaReInstructionV1 op = r->program->instructions[pc];
            if (op.opcode == CETTA_RE_V1_JUMP) pc = op.next;
            else if (op.opcode == CETTA_RE_V1_SAVE) { r->working[op.argument] = pos; pc = op.next; }
            else if (op.opcode == CETTA_RE_V1_SPLIT) {
                r->stack[pending] = op.second;
                memcpy(r->stack_captures + pending * r->slots, r->working, r->slots * sizeof(size_t));
                ++pending; pc = op.next;
            } else if (op.opcode == CETTA_RE_V1_ASSERT && assertion(r, op.argument, pos)) pc = op.next;
            else {
                if (op.opcode == CETTA_RE_V1_MATCH || op.opcode == CETTA_RE_V1_CHAR ||
                    op.opcode == CETTA_RE_V1_CLASS || op.opcode == CETTA_RE_V1_ANY) {
                    out->pc[out->len] = pc;
                    memcpy(out->captures + out->len * r->slots, r->working, r->slots * sizeof(size_t));
                    ++out->len;
                }
                break;
            }
        }
    }
}

bool cetta_re_v1_valid_utf8(const uint8_t *bytes, size_t len) {
    if (len && !bytes) return false;
    uint32_t cp; size_t next;
    for (size_t pos = 0; pos < len; pos = next)
        if (!decode(bytes, len, pos, &cp, &next)) return false;
    return true;
}
CettaReResultV1 cetta_re_v1_find(const CettaReProgramV1 *p, const uint8_t *bytes, size_t len,
                               size_t start, CettaReModeV1 mode, uint64_t budget) {
    if (len && !bytes) return (CettaReResultV1){.outcome = CETTA_RE_V1_INVALID_ARGUMENT};
    if (!cetta_re_v1_valid_utf8(bytes, len)) return (CettaReResultV1){.outcome = CETTA_RE_V1_INVALID_UTF8};
    return cetta_re_v1_find_valid_utf8(p, bytes, len, start, mode, budget);
}
CettaReResultV1 cetta_re_v1_find_valid_utf8(const CettaReProgramV1 *p, const uint8_t *bytes, size_t len,
                                         size_t start, CettaReModeV1 mode, uint64_t budget) {
    CettaReExecutionPolicyV1 policy = {.work_budget = budget};
    return cetta_re_v1_find_policy_valid_utf8(p, bytes, len, start, mode, &policy);
}
CettaReResultV1 cetta_re_v1_find_policy_valid_utf8(const CettaReProgramV1 *p, const uint8_t *bytes, size_t len,
    size_t start, CettaReModeV1 mode, const CettaReExecutionPolicyV1 *policy) {
    if (p && p->literal_trie &&
        !(len && !bytes) && start <= len && mode <= CETTA_RE_V1_FULL &&
        (mode != CETTA_RE_V1_FULL || !start) &&
        (start == len || (bytes[start] & 0xc0u) != 0x80u))
        return cetta_re_v1_literal_find(p, bytes, len, start, mode, policy);
    return cetta_re_v1_find_reference_valid_utf8(p, bytes, len, start, mode, policy);
}
CettaReResultV1 cetta_re_v1_find_reference_valid_utf8(const CettaReProgramV1 *p, const uint8_t *bytes, size_t len,
    size_t start, CettaReModeV1 mode, const CettaReExecutionPolicyV1 *policy) {
    CettaReResultV1 result = {.outcome = CETTA_RE_V1_INVALID_ARGUMENT};
    if (!p || (len && !bytes) || start > len || mode > CETTA_RE_V1_FULL ||
        (mode == CETTA_RE_V1_FULL && start)) return result;
    if (start < len && (bytes[start] & 0xc0u) == 0x80u) return result;
    if (p->extended) return cetta_re_v1_extended_find(p, bytes, len, start, mode, policy);
    uint32_t cp = 0; size_t after = 0;
    size_t n = p->instruction_len, slots = (size_t)p->group_len * 2u;
    if (n > SIZE_MAX / slots / sizeof(size_t) || n + 1u > SIZE_MAX / slots / sizeof(size_t)) {
        result.outcome = CETTA_RE_V1_ALLOCATION_FAILURE; return result;
    }
    ReRun r = {.program = p, .bytes = bytes, .byte_len = len, .slots = slots,
        .budget = policy ? policy->work_budget : 0u, .policy = policy};
    ReThreads current = {0}, next = {0};
    r.seen = calloc(n, 1u); r.stack = malloc((n + 1u) * sizeof(*r.stack));
    r.stack_captures = malloc((n + 1u) * slots * sizeof(size_t)); r.working = malloc(slots * sizeof(size_t));
    current.pc = malloc(n * sizeof(uint32_t)); next.pc = malloc(n * sizeof(uint32_t));
    current.captures = malloc(n * slots * sizeof(size_t)); next.captures = malloc(n * slots * sizeof(size_t));
    size_t *initial = malloc(slots * sizeof(size_t)), *best = malloc(slots * sizeof(size_t));
    if (!r.seen || !r.stack || !r.stack_captures || !r.working || !current.pc || !next.pc ||
        !current.captures || !next.captures || !initial || !best) {
        result.outcome = CETTA_RE_V1_ALLOCATION_FAILURE; goto done;
    }
    for (size_t i = 0; i < slots; ++i) initial[i] = SIZE_MAX;
    bool found = false;
    result.outcome = CETTA_RE_V1_NO_MATCH;
    for (size_t pos = start;; pos = after) {
        bool at_end = pos == len;
        if (!at_end) (void)decode(bytes, len, pos, &cp, &after);
        if (!found && (mode == CETTA_RE_V1_SEARCH || pos == start)) {
            initial[0] = pos;
            closure(&r, &current, p->entry, initial, pos);
            if (r.incomplete) break;
        }
        next.len = 0;
        /* The seen set now belongs to the following position's closure. */
        memset(r.seen, 0, n);
        for (size_t i = 0; i < current.len; ++i) {
            CettaReInstructionV1 op = p->instructions[current.pc[i]];
            size_t *captures = current.captures + i * slots;
            if (op.opcode == CETTA_RE_V1_MATCH) {
                if (mode == CETTA_RE_V1_FULL && !at_end) continue;
                memcpy(best, captures, slots * sizeof(size_t)); best[1] = pos; found = true;
                break;
            }
            if (!at_end && (op.opcode == CETTA_RE_V1_ANY ||
                (op.opcode == CETTA_RE_V1_CHAR && op.argument == cp) ||
                (op.opcode == CETTA_RE_V1_CLASS && contains(p->ranges + op.argument, op.second, cp)))) {
                closure(&r, &next, op.next, captures, after);
                if (r.incomplete) break;
            }
        }
        if (r.incomplete) break;
        if (at_end || (found && !next.len)) break;
        if (mode != CETTA_RE_V1_SEARCH && !next.len) break;
        ReThreads swap = current; current = next; next = swap;
    }
    if (r.incomplete) result.outcome = r.cancelled ? CETTA_RE_V1_CANCELLED : CETTA_RE_V1_INCOMPLETE;
    else if (found) {
        result.groups = calloc(p->group_len, sizeof(*result.groups));
        if (!result.groups) result.outcome = CETTA_RE_V1_ALLOCATION_FAILURE;
        else {
            result.outcome = CETTA_RE_V1_FOUND; result.group_len = p->group_len;
            for (uint32_t i = 0; i < p->group_len; ++i) {
                size_t left = best[2u * i], right = best[2u * i + 1u];
                if (left != SIZE_MAX && right != SIZE_MAX && (left > right || right > len)) {
                    free(result.groups); result.groups = NULL; result.group_len = 0;
                    result.outcome = CETTA_RE_V1_INVALID_ARGUMENT; break;
                }
                result.groups[i] = (CettaReCaptureV1){left != SIZE_MAX && right != SIZE_MAX,
                    left == SIZE_MAX ? 0u : left, right == SIZE_MAX ? 0u : right};
            }
        }
    }
done:
    result.state_visits = r.visits;
    free(r.seen); free(r.stack); free(r.stack_captures); free(r.working);
    free(current.pc); free(next.pc); free(current.captures); free(next.captures); free(initial); free(best);
    return result;
}
void cetta_re_v1_result_free(CettaReResultV1 *r) { if (r) { free(r->groups); *r = (CettaReResultV1){0}; } }
bool cetta_re_v1_cursor(CettaReCursorV1 *out, const uint8_t *bytes, size_t len) {
    if (!out || !cetta_re_v1_valid_utf8(bytes, len)) return false;
    *out = (CettaReCursorV1){.bytes = bytes, .byte_len = len}; return true;
}
CettaReResultV1 cetta_re_v1_next(const CettaReProgramV1 *p, CettaReCursorV1 *cursor, uint64_t budget) {
    CettaReExecutionPolicyV1 policy = {.work_budget = budget};
    return cetta_re_v1_next_policy(p, cursor, &policy);
}
CettaReResultV1 cetta_re_v1_next_policy(const CettaReProgramV1 *p, CettaReCursorV1 *cursor,
                                      const CettaReExecutionPolicyV1 *policy) {
    if (!p || !cursor) return (CettaReResultV1){.outcome = CETTA_RE_V1_INVALID_ARGUMENT};
    if (cursor->finished) return (CettaReResultV1){.outcome = CETTA_RE_V1_NO_MATCH};
    CettaReCursorV1 *published = cursor, working = *cursor;
    cursor = &working;
    uint64_t used = 0;
    uint64_t budget = policy ? policy->work_budget : 0u;
    for (;;) {
        if (budget && used == budget) return (CettaReResultV1){.outcome = CETTA_RE_V1_INCOMPLETE, .state_visits = used};
        CettaReExecutionPolicyV1 remaining = policy ? *policy : (CettaReExecutionPolicyV1){0};
        remaining.work_budget = budget ? budget - used : 0u;
        CettaReResultV1 r = cetta_re_v1_find_policy_valid_utf8(p, cursor->bytes, cursor->byte_len,
            cursor->cursor, CETTA_RE_V1_SEARCH, &remaining);
        used += r.state_visits; r.state_visits = used;
        if (r.outcome != CETTA_RE_V1_FOUND) {
            if (r.outcome == CETTA_RE_V1_NO_MATCH) { cursor->finished = true; *published = *cursor; }
            return r;
        }
        CettaReCaptureV1 span = r.groups[0];
        bool suppressed = span.start == span.end && cursor->has_previous && cursor->last_end == span.end;
        cursor->cursor = span.end;
        if (span.start == span.end) {
            if (span.end == cursor->byte_len) cursor->finished = true;
            else {
                ++cursor->cursor;
                while (cursor->cursor < cursor->byte_len && (cursor->bytes[cursor->cursor] & 0xc0u) == 0x80u)
                    ++cursor->cursor;
            }
        }
        if (!suppressed) {
            cursor->has_previous = true; cursor->last_end = span.end; *published = *cursor; return r;
        }
        cetta_re_v1_result_free(&r);
        if (cursor->finished) {
            *published = *cursor;
            return (CettaReResultV1){.outcome = CETTA_RE_V1_NO_MATCH, .state_visits = used};
        }
    }
}
const char *cetta_re_v1_outcome_name(CettaReOutcomeV1 outcome) {
    switch (outcome) {
    case CETTA_RE_V1_FOUND: return "match";
    case CETTA_RE_V1_NO_MATCH: return "no-match";
    case CETTA_RE_V1_INCOMPLETE: return "incomplete";
    case CETTA_RE_V1_CANCELLED: return "cancelled";
    case CETTA_RE_V1_INVALID_UTF8: return "invalid-utf8";
    case CETTA_RE_V1_INVALID_ARGUMENT: return "invalid-argument";
    case CETTA_RE_V1_ALLOCATION_FAILURE: return "allocation-failure";
    }
    return "invalid-outcome";
}
