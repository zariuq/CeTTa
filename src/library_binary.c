#include "library_binary.h"
#include "binary_codec.h"
#include "eval.h"
#include "lang.h"
#include "petta_semantics.h"
#include "string_ops.h"
#include "tensor.h"
#include "utf8.h"
#include <stdlib.h>
#include <string.h>

static Atom *fault(Arena *arena, const char *reason) {
    return atom_expr2(arena, atom_symbol(arena, "Binary:Fault"), atom_symbol(arena, reason));
}

static int interrupted(void *context) { (void)context; return eval_native_interrupted(); }

static bool byte_vector(CettaTensor *tensor) {
    return tensor && cetta_tensor_dtype(tensor) == CETTA_TENSOR_U8 &&
        cetta_tensor_rank(tensor) == 1 &&
        cetta_tensor_stored(tensor) == cetta_tensor_count(tensor);
}

/* Classification of the physical portable-value profile, not a purity or
 * execution certificate. Inherited runtime identities/tags are not plain
 * protocol expressions, and structured symbol names are not flat text. */
static const char *value_kind(Atom *value) {
    if (!value || (value->structural_facts &
            (ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID | ATOM_STRUCTURAL_HAS_INTERNAL_TAG |
             ATOM_STRUCTURAL_HAS_RATIONAL))) return "Unsupported";
    if (value->kind == ATOM_SYMBOL)
        return value->name_key ? "Unsupported" : "Symbol";
    if (value->kind == ATOM_EXPR)
        return (value->structural_facts & ATOM_STRUCTURAL_FACTS_VALID) ? "Expression" : "Unsupported";
    if (value->kind != ATOM_GROUNDED) return "Unsupported";
    if (value->ground.gkind == GV_INT && value->ground.ival >= 0) return "Natural";
    if (value->ground.gkind == GV_STRING) return "Text";
    return byte_vector(cetta_tensor_from_atom(value)) ? "Bytes" : "Unsupported";
}

/* An explicit extended codec profile. BV1 keeps its narrower contract.
 * This is a physical classification, never an inertness or call permission. */
static const char *portable_kind(Atom *value) {
    const char *base = value_kind(value);
    if (strcmp(base, "Unsupported")) return base;
    if (!value || value->kind != ATOM_GROUNDED ||
        (value->structural_facts & (ATOM_STRUCTURAL_HAS_NATIVE_HANDLE_ID |
          ATOM_STRUCTURAL_HAS_INTERNAL_TAG | ATOM_STRUCTURAL_HAS_RATIONAL)))
        return "Unsupported";
    if (value->ground.gkind == GV_INT) return "Integer";
    if (value->ground.gkind == GV_FLOAT) return "Float";
    return cetta_tensor_from_atom(value) ? "Tensor" : "Unsupported";
}

static CettaTensorStatus copy_bytes(CettaTensor *tensor, uint8_t **out, size_t *size) {
    *size = cetta_tensor_count(tensor);
    *out = *size ? malloc(*size) : NULL;
    if (*size && !*out) return CETTA_TENSOR_NOMEM;
    CettaTensorStatus status = cetta_tensor_copy_values(tensor, *out, *size);
    if (status == CETTA_TENSOR_ENCODING) {
        /* A fully supported sparse byte vector has the same byte meaning. */
        for (size_t i = 0; i < *size; i++) {
            bool present = false;
            CettaTensorScalar value;
            status = cetta_tensor_get(tensor, i, &present, &value);
            if (status != CETTA_TENSOR_OK || !present) { status = CETTA_TENSOR_VALUE; break; }
            (*out)[i] = value.u8;
        }
        if (!*size) status = CETTA_TENSOR_OK;
    }
    if (status != CETTA_TENSOR_OK) { free(*out); *out = NULL; *size = 0; }
    return status;
}

static Atom *publish_bytes(Arena *arena, uint8_t *data, size_t size) {
    CettaTensor *tensor = NULL;
    CettaTensorStatus status = cetta_tensor_new_take(CETTA_TENSOR_U8, &size, 1, data, size, &tensor);
    if (status != CETTA_TENSOR_OK) { free(data); return fault(arena, cetta_tensor_status_name(status)); }
    Atom *value = cetta_tensor_atom(arena, tensor);
    cetta_tensor_release(tensor);
    return value ? value : fault(arena, "out-of-memory");
}

bool cetta_binary_module_call(struct CettaLibraryContext *context, struct Space *space,
    Arena *arena, Atom *head, Atom **arguments, uint32_t count, CettaCallOutcome *out) {
    (void)context; (void)space;
    if (!head || head->kind != ATOM_SYMBOL || count != 1) return false;
    const char *name = atom_name_cstr(head);
    if (!strcmp(name, "binary:value-kind")) {
        *out = cetta_call_completed_value(atom_symbol(arena, value_kind(arguments[0])));
        return true;
    }
    if (!strcmp(name, "binary:portable-kind")) {
        *out = cetta_call_completed_value(atom_symbol(arena, portable_kind(arguments[0])));
        return true;
    }
    bool pack = !strcmp(name, "binary:pack-fields");
    bool measure = !strcmp(name, "binary:packed-length");
    bool unpack = !strcmp(name, "binary:unpack-fields");
    bool from_text = !strcmp(name, "binary:utf8");
    bool to_text = !strcmp(name, "binary:from-utf8");
    bool from_uint = !strcmp(name, "binary:unsigned");
    bool to_uint = !strcmp(name, "binary:from-unsigned");
    bool from_symbol = !strcmp(name, "binary:symbol-utf8");
    bool to_symbol = !strcmp(name, "binary:from-symbol-utf8");
    if (!strcmp(name, "binary:unsigned?")) {
        Atom *value = arguments[0];
        bool known = value && value->kind == ATOM_GROUNDED &&
            value->ground.gkind == GV_INT && value->ground.ival >= 0;
        *out = cetta_call_completed_value(eval_current_language_id() == CETTA_LANGUAGE_PETTA
            ? petta_semantics_boolean_value(arena, known) : atom_bool(arena, known));
        return true;
    }
    if (!pack && !measure && !unpack && !from_text && !to_text && !from_uint && !to_uint &&
        !from_symbol && !to_symbol) return false;
    Atom *answer = NULL;
    if (from_uint) {
        Atom *number = arguments[0];
        if (!number || number->kind != ATOM_GROUNDED || number->ground.gkind != GV_INT) return false;
        if (number->ground.ival < 0) answer = fault(arena, "negative-integer");
        else {
            uint64_t value = (uint64_t)number->ground.ival, rest = value;
            size_t size = 0;
            while (rest) { size++; rest >>= 8; }
            uint8_t *data = size ? malloc(size) : NULL;
            if (size && !data) answer = fault(arena, "out-of-memory");
            else {
                for (size_t i = 0; i < size; i++) {
                    data[size - 1 - i] = (uint8_t)value;
                    value >>= 8;
                }
                answer = publish_bytes(arena, data, size);
            }
        }
    } else if (from_text || from_symbol) {
        Atom *text = arguments[0];
        if (!text) return false;
        if (from_symbol ? text->kind != ATOM_SYMBOL :
            (text->kind != ATOM_GROUNDED || text->ground.gkind != GV_STRING)) return false;
        const char *value = from_symbol ? atom_name_cstr(text) : text->ground.sval;
        size_t size = from_symbol ? strlen(value) : atom_string_len(text), scalars, bad;
        if (!cetta_utf8_count(value, size, &scalars, &bad)) {
            answer = fault(arena, "invalid-utf8");
        } else {
            uint8_t *bytes = size ? malloc(size) : NULL;
            if (size && !bytes) answer = fault(arena, "out-of-memory");
            else {
                if (size) memcpy(bytes, value, size);
                answer = publish_bytes(arena, bytes, size);
            }
        }
    } else if (pack || measure) {
        Atom *list = arguments[0];
        bool lengths = measure && list && list->kind == ATOM_EXPR && list->expr.len == 2u &&
            atom_is_symbol(list->expr.elems[0], "Binary:FieldLengths");
        if (lengths) list = list->expr.elems[1];
        if (petta_semantics_is_open_cons_value(list)) list = petta_semantics_closed_list(arena, list);
        Atom *const *items;
        CettaExprLen length;
        if (!atom_sequence_view(list, &items, &length)) return false;
        for (CettaExprLen i = 0; i < length; i++) {
            if (lengths) {
                if (!items[i] || items[i]->kind != ATOM_GROUNDED || items[i]->ground.gkind != GV_INT ||
                    items[i]->ground.ival < 0 || (uint64_t)items[i]->ground.ival > SIZE_MAX) {
                    *out = cetta_call_completed_value(fault(arena, "invalid-field-length"));
                    return true;
                }
            } else if (!cetta_tensor_from_atom(items[i])) return false;
        }
        if (length > SIZE_MAX / sizeof(CettaBinaryField)) answer = fault(arena, "size-overflow");
        else {
            CettaBinaryField *fields = length ? calloc((size_t)length, sizeof(*fields)) : NULL;
            if (length && !fields) answer = fault(arena, "out-of-memory");
            else {
                for (size_t i = 0; i < length && !answer; i++) {
                    if (lengths) { fields[i].size = (size_t)items[i]->ground.ival; continue; }
                    CettaTensor *tensor = cetta_tensor_from_atom(items[i]);
                    if (!byte_vector(tensor)) { answer = fault(arena, "expected-byte-vector"); break; }
                    fields[i].size = cetta_tensor_count(tensor);
                    if (pack) {
                        uint8_t *data = NULL;
                        CettaTensorStatus status = copy_bytes(tensor, &data, &fields[i].size);
                        fields[i].data = data;
                        if (status != CETTA_TENSOR_OK) answer = fault(arena, cetta_tensor_status_name(status));
                    }
                }
                if (!answer) {
                    size_t bits, bytes;
                    uint8_t *data = NULL;
                    CettaBinaryStatus status = pack
                        ? cetta_binary_pack(fields, (size_t)length, interrupted, NULL, &data, &bytes)
                        : cetta_binary_measure(fields, (size_t)length, &bits, &bytes);
                    if (status != CETTA_BINARY_OK) answer = fault(arena, cetta_binary_status_name(status));
                    else if (pack) answer = publish_bytes(arena, data, bytes);
                    else answer = bytes <= INT64_MAX ? atom_int(arena, (int64_t)bytes)
                                                     : fault(arena, "size-overflow");
                }
                for (size_t i = 0; i < length; i++) free((void *)fields[i].data);
                free(fields);
            }
        }
    } else {
        CettaTensor *tensor = cetta_tensor_from_atom(arguments[0]);
        if (!tensor) return false;
        if (!byte_vector(tensor)) answer = fault(arena, "expected-byte-vector");
        else {
            uint8_t *data = NULL;
            size_t size = 0;
            CettaTensorStatus copied = copy_bytes(tensor, &data, &size);
            if (copied != CETTA_TENSOR_OK) answer = fault(arena, cetta_tensor_status_name(copied));
            else if (to_uint) {
                if (size && data[0] == 0) answer = fault(arena, "noncanonical");
                else if (size > 8 || (size == 8 && data[0] > 127)) answer = fault(arena, "size-overflow");
                else {
                    uint64_t value = 0;
                    for (size_t i = 0; i < size; i++) value = (value << 8) | data[i];
                    answer = atom_int(arena, (int64_t)value);
                }
            } else if (to_text || to_symbol) {
                size_t scalars, bad;
                if (size > CETTA_STRING_BYTES_MAX) answer = fault(arena, "size-overflow");
                else if (!cetta_utf8_count(data ? (const char *)data : "", size, &scalars, &bad))
                    answer = fault(arena, "invalid-utf8");
                else if (to_symbol && size && memchr(data, 0, size)) answer = fault(arena, "symbol-nul");
                else if (to_symbol) {
                    char *name_bytes = malloc(size + 1);
                    if (!name_bytes) answer = fault(arena, "out-of-memory");
                    else {
                        if (size) memcpy(name_bytes, data, size);
                        name_bytes[size] = 0;
                        answer = atom_symbol(arena, name_bytes);
                        free(name_bytes);
                    }
                }
                else answer = atom_string_n(arena, data ? (const char *)data : "", size);
            } else {
                CettaBinaryOwnedField *fields = NULL;
                size_t length = 0;
                CettaBinaryStatus status = cetta_binary_unpack(data, size, interrupted, NULL, &fields, &length);
                if (status != CETTA_BINARY_OK) answer = fault(arena, cetta_binary_status_name(status));
                else if (length > SIZE_MAX / sizeof(Atom *)) answer = fault(arena, "size-overflow");
                else {
                    Atom **values = arena_alloc(arena, sizeof(*values) * (length ? length : 1));
                    for (size_t i = 0; i < length; i++) {
                        values[i] = publish_bytes(arena, fields[i].data, fields[i].size);
                        fields[i].data = NULL;
                        if (!cetta_tensor_from_atom(values[i])) { answer = values[i]; break; }
                    }
                    if (!answer)
                        answer = atom_expr2(arena, atom_symbol(arena, "Binary:Fields"),
                                            atom_expr(arena, values, (CettaExprLen)length));
                }
                cetta_binary_fields_free(fields, length);
            }
            free(data);
        }
    }
    *out = eval_native_interrupted() ? cetta_call_interrupted() : cetta_call_completed_value(answer);
    return true;
}
