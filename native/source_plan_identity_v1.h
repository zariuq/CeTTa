#ifndef CETTA_SOURCE_PLAN_IDENTITY_V1_H
#define CETTA_SOURCE_PLAN_IDENTITY_V1_H

#include "parser_pack_abi_v1.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct { uint8_t *bytes; size_t length, capacity; } SourceIdentityWriter;

static bool identity_put(SourceIdentityWriter *stream, const void *bytes, size_t length) {
    if(length > SIZE_MAX - stream->length)return false;
    size_t needed=stream->length+length;
    if(needed>stream->capacity) {
        size_t next=stream->capacity?stream->capacity:256u;
        while(next<needed) {if(next>SIZE_MAX/2u){next=needed;break;}next*=2u;}
        uint8_t *grown=realloc(stream->bytes,next);if(!grown)return false;
        stream->bytes=grown;stream->capacity=next;
    }
    if(length)memcpy(stream->bytes+stream->length,bytes,length);
    stream->length=needed;return true;
}

/* PNI1 describes the actual loaded tables. The Lean reader resolves these
   descriptors against its authored plan; file hashes confer no authority. */
static bool identity_form(const Atom *term, const char *name, uint32_t arity) {
    return term && term->kind == ATOM_EXPR && term->expr.len == arity + 1u &&
           atom_is_symbol(term->expr.elems[0], name);
}

static const char *identity_string(const Atom *term) {
    return term && term->kind == ATOM_GROUNDED && term->ground.gkind == GV_STRING
        ? term->ground.sval : NULL;
}

static const char *identity_sort(const Atom *term) {
    return identity_form(term, "pp-def", 1u) ? identity_string(term->expr.elems[1]) : NULL;
}

static bool identity_word(SourceIdentityWriter *stream, uint32_t value) {
    uint8_t bytes[4] = {(uint8_t)value, (uint8_t)(value >> 8),
                        (uint8_t)(value >> 16), (uint8_t)(value >> 24)};
    return identity_put(stream, bytes, 4u);
}

static bool identity_text(SourceIdentityWriter *stream, const char *text) {
    if (!text) return false;
    size_t size = strlen(text);
    return size <= UINT32_MAX && identity_word(stream, (uint32_t)size) &&
           identity_put(stream, text, size);
}

static bool identity_matcher(SourceIdentityWriter *stream, const PPABIV1Terminal *terminal) {
    const char *class_name = "";
    if (terminal->kind == PPABI_V1_TERMINAL_CLASS) {
        if (!identity_form(terminal->class_expression, "pp-class", 1u)) return false;
        class_name = identity_string(terminal->class_expression->expr.elems[1]);
        if (!class_name || !*class_name) return false;
    }
    return terminal->kind <= PPABI_V1_TERMINAL_CLASS &&
        identity_word(stream, (uint32_t)terminal->kind) &&
        identity_word(stream, terminal->kind == PPABI_V1_TERMINAL_CHAR ? terminal->codepoint : 0u) &&
        identity_text(stream, class_name);
}

static bool identity_slot(Atom *term, uint32_t *index) {
    if (!identity_form(term, "pa-slot", 1u)) return false;
    term = term->expr.elems[1];
    *index = 0u;
    while (identity_form(term, "q-succ", 1u)) {
        if (*index == UINT32_MAX) return false;
        ++*index;
        term = term->expr.elems[1];
    }
    return atom_is_symbol(term, "q-zero");
}

/* Exact arity and spine checks distinguish source data actions from arbitrary
   native actions. Repeated slots and their order are retained. */
static bool identity_pair_arguments(const Atom *action, const char *name,
                                     Atom **left, Atom **right) {
    if (!identity_form(action, "pa-apply", 2u) || !atom_is_symbol(action->expr.elems[1], name)) return false;
    const Atom *args = action->expr.elems[2];
    if (!identity_form(args, "pa-cons", 2u)) return false;
    *left = args->expr.elems[1];
    args = args->expr.elems[2];
    if (!identity_form(args, "pa-cons", 2u) || !atom_is_symbol(args->expr.elems[2], "pa-nil")) return false;
    *right = args->expr.elems[1];
    return true;
}

static bool identity_children(SourceIdentityWriter *stream, Atom *body, uint32_t slots, bool emit) {
    uint32_t count = 0u, index;
    if (identity_slot(body, &index)) {
        return index < slots && (!emit || (identity_word(stream, 1u) && identity_word(stream, index)));
    }
    Atom *cursor = body, *head, *tail;
    while (identity_pair_arguments(cursor, "pair", &head, &tail)) {
        if (count == UINT32_MAX || !identity_slot(head, &index) || index >= slots) return false;
        ++count;
        cursor = tail;
    }
    if (!identity_form(cursor, "pa-const", 1u) || !atom_is_symbol(cursor->expr.elems[1], "nil")) return false;
    if (!emit) return true;
    if (!identity_word(stream, count)) return false;
    cursor = body;
    while (identity_pair_arguments(cursor, "pair", &head, &tail)) {
        if (!identity_slot(head, &index) || !identity_word(stream, index)) return false;
        cursor = tail;
    }
    return true;
}

static bool identity_production(SourceIdentityWriter *stream, const PPABIV1Pack *pack, uint32_t index) {
    const PPABIV1Production *row = &pack->productions[index];
    if (!identity_form(row->identity, "pp-production", 4u) || row->lhs_state_id >= pack->state_len) return false;
    const Atom *label_term = row->identity->expr.elems[1];
    if (!identity_form(label_term, "pp-label", 2u)) return false;
    const char *label = identity_string(label_term->expr.elems[2]);
    const char *sort = identity_sort(pack->states[row->lhs_state_id].identity);
    Atom *head, *body;
    if (!label || !sort || !identity_pair_arguments(row->action, "node", &head, &body) ||
        !identity_form(head, "pa-const", 1u) || !atom_is_symbol(head->expr.elems[1], label) ||
        !identity_children(stream, body, row->item_len, false)) return false;
    /* CNF1 productionIndex is the physical array position, not the label id. */
    if (!identity_word(stream, index) || !identity_text(stream, label) || !identity_text(stream, sort) ||
        !identity_word(stream, row->item_len)) return false;
    for (uint32_t i = 0u; i < row->item_len; ++i) {
        PPABIV1Item item = row->items[i];
        if (item.kind == PPABI_V1_ITEM_TERMINAL) {
            if (item.dense_id >= pack->terminal_len || !identity_word(stream, 0u) ||
                !identity_matcher(stream, &pack->terminals[item.dense_id])) return false;
        } else if (item.kind == PPABI_V1_ITEM_NONTERMINAL) {
            if (item.dense_id >= pack->state_len || !identity_word(stream, 1u) ||
                !identity_text(stream, identity_sort(pack->states[item.dense_id].identity))) return false;
        } else return false;
    }
    return identity_children(stream, body, row->item_len, true);
}

static bool source_plan_identity_bytes(const PPABIV1Pack *pack, uint8_t **output,
    size_t *length, char *error, size_t error_size) {
    *output=NULL;*length=0u;
    SourceIdentityWriter writer={0}, *stream=&writer;
    bool ok=identity_put(stream,"PNI1",4u) &&
        identity_text(stream,pack->source_digest) && identity_text(stream,pack->environment_digest) &&
        identity_text(stream,pack->compiler_digest) && identity_text(stream,pack->pack_digest) &&
        identity_word(stream,pack->state_len) && identity_word(stream,pack->terminal_len) &&
        identity_word(stream,pack->production_len);
    for(uint32_t i=0u;ok&&i<pack->state_len;i++)
        ok=identity_word(stream,pack->states[i].dense_id) && identity_text(stream,identity_sort(pack->states[i].identity));
    for(uint32_t i=0u;ok&&i<pack->terminal_len;i++)
        ok=identity_word(stream,pack->terminals[i].dense_id) && identity_matcher(stream,&pack->terminals[i]);
    for(uint32_t i=0u;ok&&i<pack->production_len;i++)ok=identity_production(stream,pack,i);
    if(!ok) {free(writer.bytes);snprintf(error,error_size,"cannot export the loaded source-plan identity");return false;}
    *output=writer.bytes;*length=writer.length;return true;
}

#endif
