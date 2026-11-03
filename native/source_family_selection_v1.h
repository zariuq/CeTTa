#ifndef GSLT_SOURCE_FAMILY_SELECTION_H
#define GSLT_SOURCE_FAMILY_SELECTION_H

#include "parser_pack_native_v1.h"
#include <stdlib.h>
#include <string.h>

/* A local observation over the immutable packed forest. The policy is data
   exported from the authored source module, not a language parser. */
typedef struct {
    uint32_t state;
    bool reject_words;
    bool complement;
    Atom *words, *points, *openers;
} SourceFamilyFilter;

typedef struct {
    SourceFamilyFilter *filters;
    uint32_t length;
} SourceFamilyPolicy;

static bool source_form(Atom *value, const char *tag, uint32_t arity) {
    return value && value->kind == ATOM_EXPR && value->expr.len == arity + 1u &&
           atom_is_symbol(value->expr.elems[0], tag);
}

static bool source_scalar(Atom *value, uint32_t *scalar) {
    if (!value || value->kind != ATOM_GROUNDED || value->ground.gkind != GV_INT ||
        value->ground.ival < 0 || value->ground.ival > 0x10ffff ||
        (value->ground.ival >= 0xd800 && value->ground.ival <= 0xdfff)) return false;
    *scalar = (uint32_t)value->ground.ival;
    return true;
}

static bool source_points(Atom *list) {
    while (source_form(list, "LCons", 2u)) {
        uint32_t scalar;
        if (!source_scalar(list->expr.elems[1], &scalar)) return false;
        list = list->expr.elems[2];
    }
    return atom_is_symbol(list, "LNil");
}

static bool source_words(Atom *list) {
    while (source_form(list, "LCons", 2u)) {
        if (!source_points(list->expr.elems[1])) return false;
        list = list->expr.elems[2];
    }
    return atom_is_symbol(list, "LNil");
}

static bool source_policy_read(SourceFamilyPolicy *out, const PPABIV1Pack *pack,
                               Atom *value) {
    if (!source_form(value, "source-selection-v1", 1u)) return false;
    Atom *list = value->expr.elems[1];
    uint32_t count = 0u;
    for (Atom *scan = list; source_form(scan, "LCons", 2u); scan = scan->expr.elems[2]) ++count;
    SourceFamilyFilter *filters = calloc(count ? count : 1u, sizeof(*filters));
    if (!filters) return false;
    bool ok = false;
    for (uint32_t i = 0u; i < count; ++i, list = list->expr.elems[2]) {
        Atom *row = list->expr.elems[1];
        bool reject = source_form(row, "reject-words", 2u);
        bool complement = source_form(row, "maximal-except", 3u);
        if (!reject && !complement && !source_form(row, "maximal-token", 3u)) goto done;
        uint32_t state = 0u;
        while (state < pack->state_len && !atom_eq(pack->states[state].identity, row->expr.elems[1])) ++state;
        if (state == pack->state_len) goto done;
        filters[i].state = pack->states[state].dense_id;
        filters[i].reject_words = reject;
        filters[i].complement = complement;
        if (reject) {
            if (!source_words(row->expr.elems[2])) goto done;
            filters[i].words = row->expr.elems[2];
        } else {
            if (!source_points(row->expr.elems[2]) || !source_words(row->expr.elems[3])) goto done;
            filters[i].points = row->expr.elems[2];
            filters[i].openers = row->expr.elems[3];
        }
    }
    if (!atom_is_symbol(list, "LNil")) goto done;
    out->filters = filters;
    out->length = count;
    ok = true;
done:
    if (!ok) free(filters);
    return ok;
}

static bool source_word_matches(Atom *word, const uint32_t *input,
                                uint32_t left, uint32_t right, bool prefix) {
    while (source_form(word, "LCons", 2u)) {
        uint32_t scalar;
        if (left == right || !source_scalar(word->expr.elems[1], &scalar) || input[left++] != scalar) return false;
        word = word->expr.elems[2];
    }
    return atom_is_symbol(word, "LNil") && (prefix || left == right);
}

static bool source_any_word(Atom *words, const uint32_t *input,
                            uint32_t left, uint32_t right, bool prefix) {
    while (source_form(words, "LCons", 2u)) {
        if (source_word_matches(words->expr.elems[1], input, left, right, prefix)) return true;
        words = words->expr.elems[2];
    }
    return false;
}

static bool source_family_accepts(void *context, const PPABIV1Pack *pack,
                                  const CettaLpNativeUtf8Forest *forest,
                                  uint32_t symbol, uint32_t production) {
    const SourceFamilyPolicy *policy = context;
    const CettaLpNativeUtf8ForestNode *node = &forest->nodes[symbol];
    for (uint32_t i = 0u; i < policy->length; ++i) {
        const SourceFamilyFilter *filter = &policy->filters[i];
        if (pack->productions[production].lhs_state_id != filter->state) continue;
        if (filter->reject_words) {
            if (source_any_word(filter->words, forest->codepoints,
                                node->scalar_left, node->scalar_right, false)) return false;
        } else {
            if (node->scalar_right < forest->scalar_len) {
                bool continues = filter->complement;
                for (Atom *points = filter->points; source_form(points, "LCons", 2u); points = points->expr.elems[2]) {
                    uint32_t scalar;
                    if (!source_scalar(points->expr.elems[1], &scalar)) return false;
                    if (scalar == forest->codepoints[node->scalar_right]) {
                        continues = !filter->complement;
                        break;
                    }
                }
                if (continues) return false;
            }
            if (source_any_word(filter->openers, forest->codepoints,
                                node->scalar_right, forest->scalar_len, true)) return false;
        }
    }
    return true;
}

#endif
