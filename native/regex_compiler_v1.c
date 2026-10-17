#include "regex_compiler_v1.h"
#include "src/symbol.h"
#include "prepared_unary_index_v1.h"
#include "unicode_class_v1.h"
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

extern const uint8_t cetta_regex_extended_compiler_v1_prepared[];
extern const size_t cetta_regex_extended_compiler_v1_prepared_len;
struct CettaReCompilerV1 { CettaDeterministicEquationPlanV1 *plan; };

static bool fault(char *error, size_t size, const char *message) {
    if (error && size) snprintf(error, size, "%s", message);
    return false;
}
static bool form(Atom *a, const char *head, uint32_t arity) {
    return a && a->kind == ATOM_EXPR && a->expr.len == arity + 1u && atom_is_symbol(a->expr.elems[0], head);
}
static bool number(Atom *a, uint32_t *out) {
    if (a && a->kind == ATOM_GROUNDED && a->ground.gkind == GV_STRING) {
        size_t n = atom_string_len(a); uint64_t value = 0;
        if (!n || (n > 1u && a->ground.sval[0] == '0')) return false;
        for (size_t i = 0; i < n; ++i) {
            unsigned char c = (unsigned char)a->ground.sval[i];
            if (c < '0' || c > '9' || value > (UINT32_MAX - (c - '0')) / 10u) return false;
            value = value * 10u + c - '0';
        }
        *out = (uint32_t)value; return true;
    }
    if (!a || a->kind != ATOM_GROUNDED || a->ground.gkind != GV_INT ||
        a->ground.ival < 0 || (uint64_t)a->ground.ival > UINT32_MAX) return false;
    *out = (uint32_t)a->ground.ival; return true;
}
/* The authored raw compiler carrier admits strings, not native integers.
 * The public structured convenience uses the injective decimal embedding;
 * graph relocation decodes only canonical, bounded numeral payloads. */
static Atom *encode_payload(const CettaDeterministicEquationPlanV1 *plan, Arena *arena,
                            Atom *value, uint32_t depth, uint64_t *work,
                            uint32_t *source_groups, uint32_t *reference_groups,
                            const char *const *names, uint32_t declared_groups, bool *invalid_group) {
    typedef struct { Atom *source; Atom **fields; CettaExprIndex next; } Frame;
    Frame *frames = NULL; size_t len = 0, capacity = 0;
    bool bounded = *work != 0u, entering = true;
    Atom *result = NULL;
    for (;;) {
        if (entering) {
            if (!value || (depth && len >= depth) || (bounded && !*work)) goto fail;
            if (bounded) --*work;
            result = value;
            if (value->kind == ATOM_GROUNDED && value->ground.gkind == GV_INT) {
                char text[32]; snprintf(text, sizeof(text), "%" PRId64, value->ground.ival);
                result = atom_string(arena, text);
            } else if (value->kind == ATOM_EXPR && value->expr.len) {
                /* The typed codec's (C) and the equation profile's C are
                 * connected only at a declared nullary constructor. */
                if (value->expr.len == 1u && cetta_deterministic_equation_plan_v1_declares_operator(plan, value->expr.elems[0], 0u)) {
                    result = value->expr.elems[0];
                } else {
                    if (len == capacity) {
                        size_t next = capacity ? capacity * 2u : 32u;
                        if (next < capacity || next > SIZE_MAX / sizeof(*frames)) goto fail;
                        void *grown = realloc(frames, next * sizeof(*frames));
                        if (!grown) goto fail;
                        frames = grown; capacity = next;
                    }
                    if (value->expr.len > SIZE_MAX / sizeof(Atom *)) goto fail;
                    Atom **fields = arena_alloc(arena, value->expr.len * sizeof(*fields));
                    frames[len++] = (Frame){value, fields, 0u};
                    value = value->expr.elems[0]; continue;
                }
            }
            entering = false;
        }
        if (!len) { free(frames); return result; }
        Frame *frame = &frames[len - 1u];
        frame->fields[frame->next++] = result;
        if (frame->next < frame->source->expr.len) {
            value = frame->source->expr.elems[frame->next]; entering = true; continue;
        }
        /* Assertion symbols have an explicit embedding into string data. */
        if (frame->source->expr.len == 2u && atom_is_symbol(frame->fields[0], "re:assert") && frame->fields[1]->kind == ATOM_SYMBOL)
            frame->fields[1] = atom_string_n(arena, atom_name_cstr(frame->fields[1]), symbol_len(g_symbols, frame->fields[1]->sym_id));
        /* Capture identities belong to source data, including groups whose
         * execution is subsequently erased by the authored {0} equations. */
        if (frame->source->expr.len == 3u && atom_is_symbol(frame->fields[0], "re:capture")) {
            uint32_t group = 0u;
            if (!number(frame->fields[1], &group) || !group || group >= UINT32_MAX / 2u) {
                *invalid_group = true; goto fail;
            }
            if (group >= *source_groups) *source_groups = group + 1u;
        }
        if (frame->source->expr.len == 3u && atom_is_symbol(frame->fields[0], "re:backref")) {
            Atom *identity = frame->fields[1]; uint32_t group = 0u;
            if (form(identity, "re:reference-name", 1u)) {
                Atom *name = identity->expr.elems[1];
                if (name->kind != ATOM_GROUNDED || name->ground.gkind != GV_STRING) {
                    *invalid_group = true; goto fail;
                }
                for (uint32_t i = 1u; names && i < declared_groups; ++i)
                    if (names[i] && strlen(names[i]) == atom_string_len(name) &&
                        !memcmp(names[i], name->ground.sval, atom_string_len(name))) { group = i; break; }
                if (!group) { *invalid_group = true; goto fail; }
                char text[32]; snprintf(text, sizeof(text), "%" PRIu32, group);
                frame->fields[1] = atom_string(arena, text);
            } else if (!number(identity, &group) || !group || group >= UINT32_MAX / 2u) {
                *invalid_group = true; goto fail;
            }
            if (group >= *reference_groups) *reference_groups = group + 1u;
        }
        result = atom_expr(arena, frame->fields, frame->source->expr.len); --len;
    }
fail:
    free(frames); return NULL;
}
bool cetta_re_v1_compiler_load(CettaReCompilerV1 **out, char *error, size_t size) {
    return cetta_re_v1_compiler_restore(cetta_regex_extended_compiler_v1_prepared,
        cetta_regex_extended_compiler_v1_prepared_len, false, out, error, size);
}
bool cetta_re_v1_compiler_restore(const uint8_t *bytes, size_t len, bool audit,
                                 CettaReCompilerV1 **out, char *error, size_t size) {
    if (!out) return fault(error, size, "invalid compiler owner");
    CettaReCompilerV1 *c = calloc(1u, sizeof(*c));
    if (!c) return fault(error, size, "allocation failure");
    if (!cetta_deterministic_equation_plan_v1_restore(bytes, len, "regex-selection-compiler-v1",
            NULL, audit, &c->plan, error, size)) { free(c); return false; }
    cetta_re_v1_compiler_free(*out); *out = c; return true;
}
bool cetta_re_v1_compiler_store(const CettaReCompilerV1 *c, uint8_t **bytes, size_t *len, char *error, size_t size) {
    return c && cetta_deterministic_equation_plan_v1_store(c->plan, "regex-selection-compiler-v1", bytes, len, error, size);
}
void cetta_re_v1_compiler_free(CettaReCompilerV1 *c) {
    if (c) { cetta_deterministic_equation_plan_v1_free(c->plan); free(c); }
}

static bool ranges(Atom *list, CettaReRangeV1 **data, uint32_t *len, uint32_t *cap) {
    while (form(list, "LCons", 2u)) {
        Atom *pair = list->expr.elems[1]; uint32_t low = 0, high = 0;
        if (!form(pair, "re:range", 2u) || !number(pair->expr.elems[1], &low) ||
            !number(pair->expr.elems[2], &high) || *len == UINT32_MAX) return false;
        if (*len == *cap) {
            uint32_t next = *cap > UINT32_MAX / 2u ? UINT32_MAX : (*cap ? *cap * 2u : 16u);
            if (sizeof(**data) > SIZE_MAX / next) return false;
            void *p = realloc(*data, (size_t)next * sizeof(**data));
            if (!p) return false;
            *data = p; *cap = next;
        }
        (*data)[(*len)++] = (CettaReRangeV1){low, high}; list = list->expr.elems[2];
    }
    return atom_is_symbol(list, "LNil");
}
bool cetta_re_v1_compile(CettaReCompilerV1 *c, Arena *arena, Atom *pattern,
    const CettaReRangeV1 *word, uint32_t word_len, uint32_t depth, uint64_t work,
    CettaReProgramV1 **out, CettaDeterministicEquationStatusV1 *status, char *error, size_t size) {
    return cetta_re_v1_compile_named(c, arena, pattern, NULL, 0u, word, word_len,
        depth, work, out, status, error, size);
}
bool cetta_re_v1_compile_named(CettaReCompilerV1 *c, Arena *arena, Atom *pattern,
    const char *const *names, uint32_t declared_groups, const CettaReRangeV1 *word, uint32_t word_len,
    uint32_t depth, uint64_t work, CettaReProgramV1 **out,
    CettaDeterministicEquationStatusV1 *status, char *error, size_t size) {
    if (!c || !arena || !pattern || !out)
        return fault(error, size, "invalid pattern compilation arguments");
    uint64_t remaining = work;
    uint32_t source_groups = 1u, reference_groups = 1u; bool invalid_group = false;
    Atom *encoded = encode_payload(c->plan, arena, pattern, depth, &remaining,
        &source_groups, &reference_groups, names, declared_groups, &invalid_group);
    if (!encoded) {
        if (status) *status = invalid_group ? CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE : CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT;
        return fault(error, size, invalid_group ? "capture or reference needs a declared positive slot identity" : "pattern payload preparation work bound or allocation failure");
    }
    if (reference_groups > (declared_groups ? declared_groups : source_groups)) {
        if (status) *status = CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE;
        return fault(error, size, "reference has no declared capture identity");
    }
    Atom *authored = atom_symbol(arena, "re:compile-authored");
    const char *entry_head = cetta_deterministic_equation_plan_v1_declares_operator(c->plan, authored, 1u)
        ? "re:compile-authored" : "re:compile";
    Atom *call = atom_expr2(arena, atom_symbol(arena, entry_head), encoded), *result = NULL;
    if (!cetta_deterministic_equation_plan_v1_apply(c->plan, call, NULL, NULL,
        arena, depth, work, &result, status, error, size)) return false;
    if (!form(result, "re:code", 2u)) return fault(error, size, "compiler returned no instruction graph");
    Atom *list = result->expr.elems[2]; uint32_t count = 0;
    for (Atom *p = list; form(p, "LCons", 2u); p = p->expr.elems[2]) {
        if ((work && (uint64_t)count >= work) || count == UINT32_MAX) return fault(error, size, "instruction graph work bound");
        ++count;
    }
    if (!count) return fault(error, size, "invalid instruction graph size");
    CettaPreparedUnaryIndexV1 addresses = {0};
    CettaReInstructionV1 *ops = calloc(count, sizeof(*ops));
    Atom **rows = calloc(count, sizeof(*rows));
    CettaReRangeV1 *classes = NULL; uint32_t class_len = 0, class_cap = 0, groups = 1u, entry = 0;
    uint32_t *ranks = NULL, *levels = NULL; CettaReFoldPairV1 *folds = NULL;
    uint32_t fold_len = 0; bool look = false, unicode_fold = false, ok = false;
    if (!ops || !rows) goto done;
    for (uint32_t i = 0; i < count; ++i, list = list->expr.elems[2]) {
        Atom *row = list->expr.elems[1]; rows[i] = row;
        if (!row || row->kind != ATOM_EXPR || row->expr.len < 2u ||
            !cpui_bind(&addresses, row->expr.elems[1], i)) goto done;
    }
    if (!atom_is_symbol(list, "LNil") || !cpui_row(&addresses, result->expr.elems[1], &entry)) goto done;
    for (uint32_t i = 0; i < count; ++i) {
        Atom *row = rows[i]; CettaReInstructionV1 *op = &ops[i]; Atom *target = NULL;
        if (form(row, "re:match", 1u)) op->opcode = CETTA_RE_V1_MATCH;
        else if (form(row, "re:fail", 1u)) op->opcode = CETTA_RE_V1_FAIL;
        else if (form(row, "re:jump", 2u)) { op->opcode = CETTA_RE_V1_JUMP; target = row->expr.elems[2]; }
        else if (form(row, "re:char", 3u)) {
            op->opcode = CETTA_RE_V1_CHAR; target = row->expr.elems[3];
            if (!number(row->expr.elems[2], &op->argument)) {
                if (status) *status = CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE;
                fault(error, size, "scalar payload needs a canonical bounded numeric interpretation"); goto done;
            }
            if (op->argument > 0x10ffffu || (op->argument >= 0xd800u && op->argument <= 0xdfffu)) {
                if (status) *status = CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE;
                fault(error, size, "literal needs a character interpretation outside this Unicode scalar profile"); goto done;
            }
        } else if (form(row, "re:any-at", 2u)) { op->opcode = CETTA_RE_V1_ANY; target = row->expr.elems[2]; }
        else if (form(row, "re:class-at", 3u)) {
            op->opcode = CETTA_RE_V1_CLASS; target = row->expr.elems[3]; op->argument = class_len;
            if (!ranges(row->expr.elems[2], &classes, &class_len, &class_cap)) goto done;
            op->second = class_len - op->argument;
        } else if (form(row, "re:split", 3u)) {
            op->opcode = CETTA_RE_V1_SPLIT; target = row->expr.elems[2];
            if (!cpui_row(&addresses, row->expr.elems[3], &op->second)) goto done;
        } else if (form(row, "re:save", 4u)) {
            uint32_t group = 0; op->opcode = CETTA_RE_V1_SAVE; target = row->expr.elems[4];
            if (!number(row->expr.elems[2], &group) || !group || group >= UINT32_MAX / 2u) {
                if (status) *status = CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE;
                fault(error, size, "capture identity needs an explicit register layout"); goto done;
            }
            if (!atom_is_symbol(row->expr.elems[3], "Start") && !atom_is_symbol(row->expr.elems[3], "End")) goto done;
            op->argument = group * 2u + (atom_is_symbol(row->expr.elems[3], "End") ? 1u : 0u);
            if (group >= groups) groups = group + 1u;
        } else if (form(row, "re:backref-at", 4u)) {
            op->opcode = CETTA_RE_V1_BACKREF; target = row->expr.elems[4];
            if (!number(row->expr.elems[2], &op->argument)) goto done;
            Atom *comparison = row->expr.elems[3];
            if (atom_is_symbol(comparison, "Exact")) op->second = 0u;
            else if (atom_is_symbol(comparison, "AsciiFold")) op->second = 1u;
            else if (atom_is_symbol(comparison, "UnicodeFold")) { op->second = 2u; unicode_fold = true; }
            else goto done;
        } else if (form(row, "re:look-at", 4u)) {
            Atom *kind = row->expr.elems[3]; target = row->expr.elems[4]; look = true;
            bool ahead = atom_is_symbol(kind, "Ahead") || atom_is_symbol(kind, "NotAhead");
            bool behind = atom_is_symbol(kind, "Behind") || atom_is_symbol(kind, "NotBehind") ||
                atom_is_symbol(kind, "ReverseBehind") || atom_is_symbol(kind, "ReverseNotBehind");
            if (!ahead && !behind) goto done;
            op->opcode = ahead ? CETTA_RE_V1_LOOKAHEAD : CETTA_RE_V1_LOOKBEHIND;
            op->argument = (atom_is_symbol(kind, "NotAhead") || atom_is_symbol(kind, "NotBehind") ||
                atom_is_symbol(kind, "ReverseNotBehind") ? 1u : 0u) |
                (atom_is_symbol(kind, "ReverseBehind") || atom_is_symbol(kind, "ReverseNotBehind") ? 2u : 0u);
            if (!cpui_row(&addresses, row->expr.elems[2], &op->second)) goto done;
        } else if (form(row, "re:assert-at", 3u)) {
            static const char *names[] = {"text-start", "text-end", "line-start", "line-end", "word-boundary", "not-word-boundary",
                "ascii-word-boundary", "not-ascii-word-boundary"};
            op->opcode = CETTA_RE_V1_ASSERT; target = row->expr.elems[3];
            const uint32_t assertion_count = (uint32_t)(sizeof(names) / sizeof(*names));
            op->argument = assertion_count;
            Atom *name = row->expr.elems[2];
            for (uint32_t j = 0; j < assertion_count; ++j)
                if (name->kind == ATOM_GROUNDED && name->ground.gkind == GV_STRING &&
                    atom_string_len(name) == strlen(names[j]) && !memcmp(name->ground.sval, names[j], strlen(names[j])))
                    op->argument = j;
            if (op->argument == assertion_count) {
                if (status) *status = CETTA_DETERMINISTIC_EQUATION_V1_NO_RULE;
                fault(error, size, "assertion payload needs its declared interpretation"); goto done;
            }
        } else goto done;
        if (target && !cpui_row(&addresses, target, &op->next)) goto done;
    }
    if (declared_groups) { if (groups > declared_groups || source_groups > declared_groups) goto done; groups = declared_groups; }
    else if (source_groups > groups) groups = source_groups;
    if (look) {
        levels = calloc(addresses.len, sizeof(*levels)); ranks = calloc(count, sizeof(*ranks));
        if (!levels || !ranks) goto done;
        SymbolId body_head = atom_symbol(arena, "re:look-body")->sym_id;
        SymbolId end_head = atom_symbol(arena, "re:look-end")->sym_id;
        uint32_t maximum = 0u;
        for (size_t i = 0; i < addresses.len; ++i) {
            CettaPreparedUnaryNodeV1 address = addresses.nodes[i];
            levels[i] = address.parent == UINT32_MAX ? 0u : levels[address.parent];
            if (address.head == body_head || address.head == end_head) {
                if (levels[i] == UINT32_MAX) goto done;
                ++levels[i];
            }
            if (levels[i] > maximum) maximum = levels[i];
        }
        for (size_t i = 0; i < addresses.len; ++i)
            if (addresses.nodes[i].row != UINT32_MAX) ranks[addresses.nodes[i].row] = maximum - levels[i];
    }
    if (unicode_fold) {
        size_t n = unicode_v1_simple_fold_count();
        if (n > UINT32_MAX || n > SIZE_MAX / sizeof(*folds)) goto done;
        folds = calloc(n ? n : 1u, sizeof(*folds));
        if (!folds) goto done;
        for (size_t i = 0; i < n; ++i) {
            CettaReFoldPairV1 pair;
            if (!unicode_v1_simple_fold_at(i, &pair.scalar, &pair.canonical)) goto done;
            /* The runtime already maps absent scalars to themselves. */
            if (pair.scalar != pair.canonical) folds[fold_len++] = pair;
        }
    }
    CettaReProgramSpecV1 spec = {.instructions = ops, .instruction_len = count, .entry = entry,
        .group_len = groups, .ranges = classes, .range_len = class_len, .word_ranges = word,
        .word_range_len = word_len, .group_names = names, .ranks = ranks,
        .fold_pairs = folds, .fold_pair_len = fold_len};
    ok = cetta_re_v1_program(&spec, out, error, size);
done:
    if (!ok && (!ops || !rows || addresses.unavailable)) {
        if (status) *status = CETTA_DETERMINISTIC_EQUATION_V1_RESOURCE_LIMIT;
        fault(error, size, "instruction address storage allocation or capacity exhausted");
    }
    if (!ok && error && size && !error[0]) fault(error, size, "malformed authored instruction graph");
    cpui_free(&addresses); free(ops); free(rows); free(classes); free(ranks); free(levels); free(folds); return ok;
}
