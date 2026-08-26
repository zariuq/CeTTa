#include "parser_pack_identity_wire_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *bytes;
    size_t length;
    size_t capacity;
} CettaPniV1Writer;

static bool pni_v1_error(char *error_buf, size_t error_buf_size,
                         const char *format, ...) {
    va_list args;
    if (error_buf && error_buf_size > 0u) {
        va_start(args, format);
        (void)vsnprintf(error_buf, error_buf_size, format, args);
        va_end(args);
    }
    return false;
}

static bool pni_v1_add_size(size_t *size, size_t amount) {
    if (!size || amount > SIZE_MAX - *size)
        return false;
    *size += amount;
    return true;
}

static bool pni_v1_add_u32_words(size_t *size, uint32_t count) {
    size_t available;
    if (!size)
        return false;
    available = SIZE_MAX - *size;
    if ((size_t)count > available / 4u)
        return false;
    *size += (size_t)count * 4u;
    return true;
}

static bool pni_v1_put(CettaPniV1Writer *writer,
                       const void *source, size_t length) {
    if (!writer || length > writer->capacity - writer->length)
        return false;
    if (length > 0u)
        memcpy(writer->bytes + writer->length, source, length);
    writer->length += length;
    return true;
}

static bool pni_v1_put_u32(CettaPniV1Writer *writer, uint32_t value) {
    uint8_t bytes[4] = {
        (uint8_t)(value & 0xffu),
        (uint8_t)((value >> 8u) & 0xffu),
        (uint8_t)((value >> 16u) & 0xffu),
        (uint8_t)((value >> 24u) & 0xffu),
    };
    return pni_v1_put(writer, bytes, sizeof(bytes));
}

static bool pni_v1_text_size(const char *text, bool nonempty,
                             size_t *size) {
    size_t length;
    if (!text)
        return false;
    length = strlen(text);
    if ((nonempty && length == 0u) || length > UINT32_MAX)
        return false;
    return pni_v1_add_size(size, 4u) && pni_v1_add_size(size, length);
}

static bool pni_v1_put_text(CettaPniV1Writer *writer, const char *text) {
    size_t length;
    if (!writer || !text)
        return false;
    length = strlen(text);
    return length <= UINT32_MAX &&
        pni_v1_put_u32(writer, (uint32_t)length) &&
        pni_v1_put(writer, text, length);
}

static bool pni_v1_expr_head(const Atom *atom,
                             const char *head, CettaExprLen arity) {
    return atom && atom->kind == ATOM_EXPR &&
        atom->expr.len == arity + 1u &&
        atom_is_symbol(atom->expr.elems[0], head);
}

static const char *pni_v1_ground_string(const Atom *atom) {
    if (!atom || atom->kind != ATOM_GROUNDED ||
        atom->ground.gkind != GV_STRING || !atom->ground.sval) {
        return NULL;
    }
    return atom->ground.sval;
}

static const char *pni_v1_named_identity(const Atom *atom,
                                         const char *head) {
    if (!pni_v1_expr_head(atom, head, 1u))
        return NULL;
    return pni_v1_ground_string(atom->expr.elems[1]);
}

static bool pni_v1_qindex(const Atom *atom, uint32_t *out) {
    uint32_t value = 0u;
    if (!out)
        return false;
    while (!atom_is_symbol((Atom *)atom, "q-zero")) {
        if (!pni_v1_expr_head(atom, "q-succ", 1u) || value == UINT32_MAX)
            return false;
        value++;
        atom = atom->expr.elems[1];
    }
    *out = value;
    return true;
}

static bool pni_v1_matcher_size(const PPABIV1Terminal *terminal,
                                size_t *size) {
    const char *class_name = "";
    if (!terminal || terminal->dense_id == UINT32_MAX)
        return false;
    if (terminal->kind == PPABI_V1_TERMINAL_CLASS) {
        class_name = pni_v1_named_identity(
            terminal->class_expression, "pp-class");
        if (!class_name || class_name[0] == '\0')
            return false;
    } else if (terminal->kind != PPABI_V1_TERMINAL_ANY &&
               terminal->kind != PPABI_V1_TERMINAL_EOF &&
               terminal->kind != PPABI_V1_TERMINAL_CHAR) {
        return false;
    }
    return pni_v1_add_size(size, 8u) &&
        pni_v1_text_size(class_name, false, size);
}

static bool pni_v1_put_matcher(CettaPniV1Writer *writer,
                               const PPABIV1Terminal *terminal) {
    const char *class_name = "";
    uint32_t codepoint = 0u;
    if (terminal->kind == PPABI_V1_TERMINAL_CLASS) {
        class_name = pni_v1_named_identity(
            terminal->class_expression, "pp-class");
        if (!class_name || class_name[0] == '\0')
            return false;
    } else if (terminal->kind == PPABI_V1_TERMINAL_CHAR) {
        codepoint = terminal->codepoint;
    } else if (terminal->kind != PPABI_V1_TERMINAL_ANY &&
               terminal->kind != PPABI_V1_TERMINAL_EOF) {
        return false;
    }
    return pni_v1_put_u32(writer, (uint32_t)terminal->kind) &&
        pni_v1_put_u32(writer, codepoint) &&
        pni_v1_put_text(writer, class_name);
}

static bool pni_v1_action_slots(const Atom *action, const char *label,
                                uint32_t arity, uint32_t **slots_out,
                                uint32_t *slot_len_out) {
    const Atom *list;
    const Atom *constant;
    uint32_t *slots = NULL;
    uint32_t slot_len = 0u;
    uint32_t slot_cap = 0u;

    if (!slots_out || !slot_len_out ||
        !pni_v1_expr_head(action, "pa-apply", 2u) ||
        !atom_is_symbol(action->expr.elems[1], "CstRuleV1")) {
        return false;
    }
    list = action->expr.elems[2];
    if (!pni_v1_expr_head(list, "pa-cons", 2u))
        return false;
    constant = list->expr.elems[1];
    if (!pni_v1_expr_head(constant, "pa-const", 1u) ||
        !pni_v1_ground_string(constant->expr.elems[1]) ||
        strcmp(pni_v1_ground_string(constant->expr.elems[1]), label) != 0) {
        return false;
    }
    list = list->expr.elems[2];
    while (!atom_is_symbol((Atom *)list, "pa-nil")) {
        const Atom *slot_term;
        uint32_t slot;
        uint32_t *next;
        uint32_t next_cap;
        if (!pni_v1_expr_head(list, "pa-cons", 2u))
            goto fail;
        slot_term = list->expr.elems[1];
        if (!pni_v1_expr_head(slot_term, "pa-slot", 1u) ||
            !pni_v1_qindex(slot_term->expr.elems[1], &slot) ||
            slot >= arity) {
            goto fail;
        }
        if (slot_len == slot_cap) {
            next_cap = slot_cap ? slot_cap * 2u : 4u;
            if (next_cap < slot_cap ||
                next_cap > UINT32_MAX / sizeof(*slots)) {
                goto fail;
            }
            next = (uint32_t *)realloc(
                slots, (size_t)next_cap * sizeof(*slots));
            if (!next)
                goto fail;
            slots = next;
            slot_cap = next_cap;
        }
        slots[slot_len++] = slot;
        list = list->expr.elems[2];
    }
    *slots_out = slots;
    *slot_len_out = slot_len;
    return true;

fail:
    free(slots);
    return false;
}

static bool pni_v1_production_shape(
    const PPABIV1Pack *pack, uint32_t production_index,
    const char **label_out, const char **sort_out,
    uint32_t **slots_out, uint32_t *slot_len_out) {
    const PPABIV1Production *production;
    const Atom *identity;
    const Atom *label;
    const Atom *lhs;
    const char *label_text;
    const char *sort_text;
    const Atom *items;
    uint32_t item_index;

    if (!pack || production_index >= pack->production_len)
        return false;
    production = &pack->productions[production_index];
    identity = production->identity;
    if (!pni_v1_expr_head(identity, "pp-production", 4u))
        return false;
    label = identity->expr.elems[1];
    lhs = identity->expr.elems[2];
    if (!pni_v1_expr_head(label, "pp-label", 2u) ||
        !atom_eq(label->expr.elems[1], (Atom *)lhs) ||
        production->lhs_state_id >= pack->state_len ||
        !atom_eq((Atom *)lhs,
                 pack->states[production->lhs_state_id].identity)) {
        return false;
    }
    label_text = pni_v1_ground_string(label->expr.elems[2]);
    sort_text = pni_v1_named_identity(lhs, "pp-def");
    if (!label_text || label_text[0] == '\0' ||
        !sort_text || sort_text[0] == '\0' ||
        !atom_eq(identity->expr.elems[4], production->action)) {
        return false;
    }
    items = identity->expr.elems[3];
    for (item_index = 0u; item_index < production->item_len; item_index++) {
        const PPABIV1Item *item = &production->items[item_index];
        const Atom *wire_item;
        const Atom *expected;
        const char *head;
        if (!pni_v1_expr_head(items, "pp-items-cons", 2u))
            return false;
        wire_item = items->expr.elems[1];
        if (item->kind == PPABI_V1_ITEM_TERMINAL) {
            if (item->dense_id >= pack->terminal_len)
                return false;
            head = "pp-terminal";
            expected = pack->terminals[item->dense_id].identity;
        } else if (item->kind == PPABI_V1_ITEM_NONTERMINAL) {
            if (item->dense_id >= pack->state_len)
                return false;
            head = "pp-nonterminal";
            expected = pack->states[item->dense_id].identity;
        } else {
            return false;
        }
        if (!pni_v1_expr_head(wire_item, head, 1u) ||
            !atom_eq(wire_item->expr.elems[1], (Atom *)expected)) {
            return false;
        }
        items = items->expr.elems[2];
    }
    if (!atom_is_symbol((Atom *)items, "pp-items-nil") ||
        !pni_v1_action_slots(production->action, label_text,
                             production->item_len,
                             slots_out, slot_len_out)) {
        return false;
    }
    *label_out = label_text;
    *sort_out = sort_text;
    return true;
}

static bool pni_v1_packet_size(const CettaLdParserPackV1 *compiled,
                               size_t *out_size, char *error_buf,
                               size_t error_buf_size) {
    const PPABIV1Pack *pack;
    size_t size = 4u + 12u;
    uint32_t index;

    if (!compiled || !out_size) {
        return pni_v1_error(error_buf, error_buf_size,
                            "bad ParserPack identity wire arguments");
    }
    pack = &compiled->pack;
    if ((pack->state_len > 0u && !pack->states) ||
        (pack->terminal_len > 0u && !pack->terminals) ||
        (pack->production_len > 0u && !pack->productions) ||
        strlen(compiled->language_source_sha256) != 64u ||
        strlen(compiled->profile_source_sha256) != 64u ||
        strlen(compiled->binding_sha256) != 64u ||
        strlen(pack->pack_digest) != 64u ||
        !pni_v1_text_size(compiled->language_source_sha256, true, &size) ||
        !pni_v1_text_size(compiled->profile_source_sha256, true, &size) ||
        !pni_v1_text_size(compiled->binding_sha256, true, &size) ||
        !pni_v1_text_size(pack->pack_digest, true, &size)) {
        return pni_v1_error(error_buf, error_buf_size,
                            "ParserPack identity provenance is malformed");
    }
    for (index = 0u; index < pack->state_len; index++) {
        const PPABIV1State *state = &pack->states[index];
        const char *sort = pni_v1_named_identity(state->identity, "pp-def");
        if (state->dense_id != index || !sort || sort[0] == '\0' ||
            !pni_v1_add_size(&size, 4u) ||
            !pni_v1_text_size(sort, true, &size)) {
            return pni_v1_error(error_buf, error_buf_size,
                                "ParserPack state identity is malformed");
        }
    }
    for (index = 0u; index < pack->terminal_len; index++) {
        const PPABIV1Terminal *terminal = &pack->terminals[index];
        if (terminal->dense_id != index ||
            !pni_v1_add_size(&size, 4u) ||
            !pni_v1_matcher_size(terminal, &size)) {
            return pni_v1_error(error_buf, error_buf_size,
                                "ParserPack terminal identity is malformed");
        }
    }
    for (index = 0u; index < pack->production_len; index++) {
        const PPABIV1Production *production = &pack->productions[index];
        const char *label = NULL;
        const char *sort = NULL;
        uint32_t *slots = NULL;
        uint32_t slot_len = 0u;
        uint32_t item_index;
        bool valid = pni_v1_production_shape(
            pack, index, &label, &sort, &slots, &slot_len);
        if (!valid || !pni_v1_add_size(&size, 4u) ||
            !pni_v1_text_size(label, true, &size) ||
            !pni_v1_text_size(sort, true, &size) ||
            !pni_v1_add_size(&size, 4u) ||
            !pni_v1_add_size(&size, 4u) ||
            !pni_v1_add_u32_words(&size, slot_len)) {
            free(slots);
            return pni_v1_error(error_buf, error_buf_size,
                                "ParserPack production identity is malformed");
        }
        for (item_index = 0u; item_index < production->item_len; item_index++) {
            const PPABIV1Item *item = &production->items[item_index];
            if (!pni_v1_add_size(&size, 4u)) {
                valid = false;
                break;
            }
            if (item->kind == PPABI_V1_ITEM_TERMINAL) {
                valid = item->dense_id < pack->terminal_len &&
                    pni_v1_matcher_size(
                        &pack->terminals[item->dense_id], &size);
            } else if (item->kind == PPABI_V1_ITEM_NONTERMINAL) {
                const char *item_sort = item->dense_id < pack->state_len
                    ? pni_v1_named_identity(
                        pack->states[item->dense_id].identity, "pp-def")
                    : NULL;
                valid = item_sort && item_sort[0] != '\0' &&
                    pni_v1_text_size(item_sort, true, &size);
            } else {
                valid = false;
            }
            if (!valid)
                break;
        }
        free(slots);
        if (!valid) {
            return pni_v1_error(error_buf, error_buf_size,
                                "ParserPack production item is malformed");
        }
    }
    *out_size = size;
    return true;
}

static bool pni_v1_packet_write(const CettaLdParserPackV1 *compiled,
                                uint8_t *output, size_t output_size) {
    const PPABIV1Pack *pack = &compiled->pack;
    CettaPniV1Writer writer = {
        .bytes = output, .length = 0u, .capacity = output_size,
    };
    uint32_t index;

    if (!pni_v1_put(&writer, "PNI1", 4u) ||
        !pni_v1_put_text(&writer, compiled->language_source_sha256) ||
        !pni_v1_put_text(&writer, compiled->profile_source_sha256) ||
        !pni_v1_put_text(&writer, compiled->binding_sha256) ||
        !pni_v1_put_text(&writer, pack->pack_digest) ||
        !pni_v1_put_u32(&writer, pack->state_len) ||
        !pni_v1_put_u32(&writer, pack->terminal_len) ||
        !pni_v1_put_u32(&writer, pack->production_len)) {
        return false;
    }
    for (index = 0u; index < pack->state_len; index++) {
        const char *sort = pni_v1_named_identity(
            pack->states[index].identity, "pp-def");
        if (!sort || !pni_v1_put_u32(&writer, index) ||
            !pni_v1_put_text(&writer, sort)) {
            return false;
        }
    }
    for (index = 0u; index < pack->terminal_len; index++) {
        if (!pni_v1_put_u32(&writer, index) ||
            !pni_v1_put_matcher(&writer, &pack->terminals[index])) {
            return false;
        }
    }
    for (index = 0u; index < pack->production_len; index++) {
        const PPABIV1Production *production = &pack->productions[index];
        const char *label = NULL;
        const char *sort = NULL;
        uint32_t *slots = NULL;
        uint32_t slot_len = 0u;
        uint32_t item_index;
        if (!pni_v1_production_shape(
                pack, index, &label, &sort, &slots, &slot_len) ||
            !pni_v1_put_u32(&writer, index) ||
            !pni_v1_put_text(&writer, label) ||
            !pni_v1_put_text(&writer, sort) ||
            !pni_v1_put_u32(&writer, production->item_len)) {
            free(slots);
            return false;
        }
        for (item_index = 0u; item_index < production->item_len; item_index++) {
            const PPABIV1Item *item = &production->items[item_index];
            if (!pni_v1_put_u32(&writer, (uint32_t)item->kind)) {
                free(slots);
                return false;
            }
            if (item->kind == PPABI_V1_ITEM_TERMINAL) {
                if (!pni_v1_put_matcher(
                        &writer, &pack->terminals[item->dense_id])) {
                    free(slots);
                    return false;
                }
            } else {
                const char *item_sort = pni_v1_named_identity(
                    pack->states[item->dense_id].identity, "pp-def");
                if (!item_sort || !pni_v1_put_text(&writer, item_sort)) {
                    free(slots);
                    return false;
                }
            }
        }
        if (!pni_v1_put_u32(&writer, slot_len)) {
            free(slots);
            return false;
        }
        for (item_index = 0u; item_index < slot_len; item_index++) {
            if (!pni_v1_put_u32(&writer, slots[item_index])) {
                free(slots);
                return false;
            }
        }
        free(slots);
    }
    return writer.length == output_size;
}

bool cetta_ld_parser_pack_identity_wire_v1_size(
    const CettaLdParserPackV1 *compiled,
    size_t *out_size,
    char *error_buf,
    size_t error_buf_size) {
    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out_size)
        *out_size = 0u;
    return pni_v1_packet_size(
        compiled, out_size, error_buf, error_buf_size);
}

bool cetta_ld_parser_pack_identity_wire_v1_write(
    const CettaLdParserPackV1 *compiled,
    uint8_t *output,
    size_t output_size,
    size_t *out_written,
    char *error_buf,
    size_t error_buf_size) {
    uint8_t *temporary = NULL;
    size_t required = 0u;
    bool ok = false;

    if (error_buf && error_buf_size > 0u)
        error_buf[0] = '\0';
    if (out_written)
        *out_written = 0u;
    if (!compiled || !output || !out_written ||
        !pni_v1_packet_size(
            compiled, &required, error_buf, error_buf_size)) {
        return false;
    }
    if (output_size < required) {
        return pni_v1_error(error_buf, error_buf_size,
                            "ParserPack identity wire output is undersized");
    }
    temporary = (uint8_t *)malloc(required ? required : 1u);
    if (!temporary) {
        return pni_v1_error(error_buf, error_buf_size,
                            "failed to allocate ParserPack identity wire");
    }
    if (!pni_v1_packet_write(compiled, temporary, required)) {
        pni_v1_error(error_buf, error_buf_size,
                     "ParserPack identity changed during wire encoding");
        goto done;
    }
    memcpy(output, temporary, required);
    *out_written = required;
    ok = true;

done:
    free(temporary);
    return ok;
}
