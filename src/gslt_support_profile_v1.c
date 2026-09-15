#include "gslt_support_profile_v1.h"

#include "native_sha256.h"
#include "native/operational_language_def_v1.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool profile_error(char *error, size_t error_size, const char *format, ...) {
    if (error && error_size) {
        va_list args;
        va_start(args, format);
        (void)vsnprintf(error, error_size, format, args);
        va_end(args);
    }
    return false;
}

/* Arity belongs to the selected native provider, not its user-chosen spelling.
 * Unrecognized identities remain declarations for unavailable providers. */
static uint32_t known_arity(const char *identity, bool source) {
    if (source) {
        if (!strcmp(identity, "support.snapshot-match.v1")) return 1u;
        if (!strcmp(identity, "support.equal.v1") ||
            !strcmp(identity, "support.not-equal.v1")) return 2u;
    } else {
        if (!strcmp(identity, "support.add.v1") ||
            !strcmp(identity, "support.remove.v1")) return 1u;
        if (!strcmp(identity, "support.head.v1") ||
            !strcmp(identity, "support.tail.v1")) return 2u;
        if (!strcmp(identity, "support.group-cardinality.v1") ||
            !strcmp(identity, "support.evaluate-project.mm2-pure-f64.v1")) return 3u;
    }
    return UINT32_MAX;
}

static bool text_present(const char *text) {
    return text && text[0] != '\0';
}

static bool sha256_text(const char *text) {
    if (!text || strlen(text) != 64u)
        return false;
    for (size_t index = 0u; index < 64u; index++) {
        char ch = text[index];
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')))
            return false;
    }
    return true;
}

static bool declarations_validate_v1(
    const CettaGsltSupportOperatorDeclV1 *declarations,
    size_t declaration_count, const char *kind,
    char *error, size_t error_size) {
    if (declaration_count > 0u && !declarations)
        return profile_error(error, error_size,
                             "support-transform %s declarations are missing",
                             kind);
    for (size_t index = 0u; index < declaration_count; index++) {
        const CettaGsltSupportOperatorDeclV1 *declaration =
            &declarations[index];
        if (!text_present(declaration->syntax_symbol) ||
            !text_present(declaration->operator_id))
            return profile_error(error, error_size,
                                 "support-transform %s declaration is empty",
                                 kind);
        if (declaration->argument_count > UINT8_MAX)
            return profile_error(error, error_size,
                                 "support-transform %s arity is invalid", kind);
        uint32_t expected = known_arity(declaration->operator_id,
                                        strcmp(kind, "source") == 0);
        if (expected != UINT32_MAX && declaration->argument_count != expected)
            return profile_error(error, error_size,
                                 "support-transform known %s provider arity mismatch: %s",
                                 kind, declaration->operator_id);
        for (size_t prior = 0u; prior < index; prior++) {
            if (strcmp(declarations[prior].syntax_symbol,
                       declaration->syntax_symbol) == 0)
                return profile_error(
                    error, error_size,
                    "support-transform %s syntax is declared twice: %s",
                    kind, declaration->syntax_symbol);
        }
    }
    return true;
}

static bool fields_validate(
    const CettaGsltSupportTransformProfileV1 *profile,
    char *error, size_t error_size) {
    if (!profile)
        return profile_error(error, error_size, "missing support-transform profile");
    if (profile->abi_version != 1u)
        return profile_error(error, error_size,
                             "unsupported support-transform ABI version %u",
                             profile->abi_version);
    const char *const required[] = {
        profile->language_name,
        profile->profile_name,
        profile->manifest_sha256,
        profile->compiler_sha256,
        profile->work_symbol,
        profile->compat_input_symbol,
        profile->compat_input_operator_id,
        profile->explicit_input_symbol,
        profile->compat_output_symbol,
        profile->compat_output_operator_id,
        profile->explicit_output_symbol,
    };
    for (size_t index = 0u;
         index < sizeof(required) / sizeof(required[0]); index++) {
        if (!text_present(required[index]))
            return profile_error(error, error_size,
                                 "support-transform profile has an empty field");
    }
    if (!sha256_text(profile->manifest_sha256) ||
        !sha256_text(profile->compiler_sha256))
        return profile_error(error, error_size,
                             "support-transform profile digest is malformed");
    if (profile->work_arity != 3u)
        return profile_error(error, error_size,
                             "support-transform V1 requires work arity 3");
    const uint32_t positions[] = {
        profile->location_position,
        profile->input_position,
        profile->output_position,
    };
    bool seen[3] = {false, false, false};
    for (size_t index = 0u; index < 3u; index++) {
        if (positions[index] >= 3u || seen[positions[index]])
            return profile_error(error, error_size,
                                 "support-transform work positions are invalid");
        seen[positions[index]] = true;
    }
    if (profile->scheduler !=
            CETTA_GSLT_SUPPORT_SCHEDULER_LEAST_MORK_COMPACT_EXPRESSION_KEY_V1)
        return profile_error(error, error_size,
                             "unsupported support-transform scheduler");
    if (profile->unsupported_policy !=
            CETTA_GSLT_SUPPORT_UNSUPPORTED_LEAVE_INERT)
        return profile_error(error, error_size,
                             "unsupported support-transform unknown-work policy");
    if (strcmp(profile->compat_input_operator_id,
               "support.snapshot-match.v1") != 0 ||
        strcmp(profile->compat_output_operator_id,
               "support.add.v1") != 0)
        return profile_error(error, error_size,
                             "unsupported compatibility operator identity");
    if (!declarations_validate_v1(
            profile->source_declarations, profile->source_declaration_count,
            "source", error, error_size) ||
        !declarations_validate_v1(
            profile->sink_declarations, profile->sink_declaration_count,
            "sink", error, error_size))
        return false;
    return true;
}


bool cetta_gslt_support_transform_profile_validate_v1(
    const CettaGsltSupportTransformProfileV1 *profile,
    char *error, size_t error_size) {
    if (!fields_validate(profile, error, error_size)) return false;
    if (!profile->physical_profile_packet || !profile->physical_profile_packet_size)
        return profile_error(error, error_size, "support-transform physical profile is missing");
    return true;
}

static bool utf8(const uint8_t *bytes, size_t size) {
    for (size_t i = 0; i < size;) {
        uint32_t code = bytes[i++], minimum = 0;
        unsigned continuation = 0;
        if (code < 0x80u) { if (!code) return false; continue; }
        if (code >= 0xc2u && code <= 0xdfu) {
            code &= 0x1fu; continuation = 1; minimum = 0x80u;
        } else if (code >= 0xe0u && code <= 0xefu) {
            code &= 0x0fu; continuation = 2; minimum = 0x800u;
        } else if (code >= 0xf0u && code <= 0xf4u) {
            code &= 0x07u; continuation = 3; minimum = 0x10000u;
        } else return false;
        if (continuation > size - i) return false;
        while (continuation--) {
            uint8_t next = bytes[i++];
            if ((next & 0xc0u) != 0x80u) return false;
            code = (code << 6) | (next & 0x3fu);
        }
        if (code < minimum || code > 0x10ffffu ||
            (code >= 0xd800u && code <= 0xdfffu)) return false;
    }
    return true;
}

static void profile_texts(const CettaGsltSupportTransformProfileV1 *p,
                          const char *texts[11]) {
    texts[0] = p->language_name; texts[1] = p->profile_name;
    texts[2] = p->manifest_sha256; texts[3] = p->compiler_sha256;
    texts[4] = p->work_symbol; texts[5] = p->compat_input_symbol;
    texts[6] = p->compat_input_operator_id; texts[7] = p->explicit_input_symbol;
    texts[8] = p->compat_output_symbol; texts[9] = p->compat_output_operator_id;
    texts[10] = p->explicit_output_symbol;
}

typedef struct { uint8_t *bytes; size_t length, capacity; bool ok; } Writer;

static void put(Writer *writer, const uint8_t *bytes, size_t size) {
    if (!writer->ok || size > writer->capacity - writer->length) {
        writer->ok = false; return;
    }
    if (writer->bytes && size) memcpy(writer->bytes + writer->length, bytes, size);
    writer->length += size;
}

static void put_u16(Writer *writer, size_t value) {
    if (value > UINT16_MAX) { writer->ok = false; return; }
    uint8_t bytes[2] = {(uint8_t)(value >> 8), (uint8_t)value};
    put(writer, bytes, sizeof(bytes));
}

static void put_text(Writer *writer, const char *text) {
    size_t size = strlen(text);
    if (!size || !utf8((const uint8_t *)text, size)) { writer->ok = false; return; }
    put_u16(writer, size);
    put(writer, (const uint8_t *)text, size);
}

static void put_declarations(Writer *writer,
                             const CettaGsltSupportOperatorDeclV1 *declarations,
                             size_t count) {
    put_u16(writer, count);
    for (size_t i = 0; writer->ok && i < count; i++) {
        put_text(writer, declarations[i].syntax_symbol);
        uint8_t arity = (uint8_t)declarations[i].argument_count;
        put(writer, &arity, 1);
        put_text(writer, declarations[i].operator_id);
    }
}

static void put_profile(Writer *writer, const CettaGsltSupportTransformProfileV1 *p) {
    const uint8_t header[] = {'C', 'S', 'T', 'P', 0, 1, 0, 0};
    put(writer, header, sizeof(header));
    const char *texts[11];
    profile_texts(p, texts);
    for (size_t i = 0; writer->ok && i < 11; i++) put_text(writer, texts[i]);
    const uint8_t control[] = {(uint8_t)p->work_arity,
        (uint8_t)p->location_position, (uint8_t)p->input_position,
        (uint8_t)p->output_position, (uint8_t)p->scheduler,
        (uint8_t)p->unsupported_policy};
    put(writer, control, sizeof(control));
    put_declarations(writer, p->source_declarations, p->source_declaration_count);
    put_declarations(writer, p->sink_declarations, p->sink_declaration_count);
}

bool cetta_gslt_support_profile_encode_packet_v1(
    Arena *arena, const CettaGsltSupportTransformProfileV1 *profile,
    const uint8_t **packet, size_t *packet_size, char *error, size_t error_size) {
    if (packet) *packet = NULL;
    if (packet_size) *packet_size = 0;
    if (!arena || !packet || !packet_size || !fields_validate(profile, error, error_size))
        return profile_error(error, error_size, "invalid support-profile packet input");
    Writer measured = {.capacity = SIZE_MAX, .ok = true};
    put_profile(&measured, profile);
    if (!measured.ok)
        return profile_error(error, error_size, "support profile exceeds CSTP string/count encoding or is not UTF-8");
    Writer written = {.bytes = arena_alloc(arena, measured.length),
                      .capacity = measured.length, .ok = true};
    put_profile(&written, profile);
    if (!written.ok || written.length != measured.length)
        return profile_error(error, error_size, "support profile changed during serialization");
    *packet = written.bytes;
    *packet_size = written.length;
    return true;
}

typedef struct { const uint8_t *bytes; size_t left; } Reader;

static bool take_u16(Reader *reader, size_t *value) {
    if (reader->left < 2) return false;
    *value = ((size_t)reader->bytes[0] << 8) | reader->bytes[1];
    reader->bytes += 2; reader->left -= 2;
    return true;
}

static bool take_equal(Reader *reader, const uint8_t *expected, size_t size) {
    if (size > reader->left || memcmp(reader->bytes, expected, size)) return false;
    reader->bytes += size; reader->left -= size;
    return true;
}

static bool take_text(Reader *reader, const char *expected) {
    size_t size;
    if (!take_u16(reader, &size) || !size || size > reader->left ||
        strlen(expected) != size || !utf8(reader->bytes, size)) return false;
    return take_equal(reader, (const uint8_t *)expected, size);
}

static bool take_declarations(Reader *reader,
                              const CettaGsltSupportOperatorDeclV1 *declarations,
                              size_t count) {
    size_t actual;
    if (!take_u16(reader, &actual) || actual != count) return false;
    for (size_t i = 0; i < count; i++) {
        uint8_t arity = (uint8_t)declarations[i].argument_count;
        if (!take_text(reader, declarations[i].syntax_symbol) ||
            !take_equal(reader, &arity, 1) ||
            !take_text(reader, declarations[i].operator_id)) return false;
    }
    return true;
}

bool cetta_gslt_support_profile_packet_matches_v1(
    const CettaGsltSupportTransformProfileV1 *p, char *error, size_t error_size) {
    if (!cetta_gslt_support_transform_profile_validate_v1(p, error, error_size)) return false;
    Reader reader = {p->physical_profile_packet, p->physical_profile_packet_size};
    const uint8_t header[] = {'C', 'S', 'T', 'P', 0, 1, 0, 0};
    bool ok = take_equal(&reader, header, sizeof(header));
    const char *texts[11];
    profile_texts(p, texts);
    for (size_t i = 0; ok && i < 11; i++) ok = take_text(&reader, texts[i]);
    const uint8_t control[] = {(uint8_t)p->work_arity,
        (uint8_t)p->location_position, (uint8_t)p->input_position,
        (uint8_t)p->output_position, (uint8_t)p->scheduler,
        (uint8_t)p->unsupported_policy};
    ok = ok && take_equal(&reader, control, sizeof(control)) &&
        take_declarations(&reader, p->source_declarations, p->source_declaration_count) &&
        take_declarations(&reader, p->sink_declarations, p->sink_declaration_count) && !reader.left;
    return ok || profile_error(error, error_size, "CSTP packet does not exactly match support-profile descriptor");
}

static const char *source_text(const CettaOpLangV1SExpr *term) {
    if (!term) return NULL;
    if (term->kind == CETTA_OP_LANG_V1_SEXPR_SYMBOL) return term->as.symbol;
    return NULL;
}

static bool is_form(const CettaOpLangV1SExpr *term, const char *head, uint32_t arguments) {
    return term && term->kind == CETTA_OP_LANG_V1_SEXPR_APPLICATION &&
        term->as.application.argument_len == arguments &&
        !strcmp(term->as.application.head, head);
}

static bool source_u32(const CettaOpLangV1SExpr *term, uint32_t *number) {
    if (!term || term->kind != CETTA_OP_LANG_V1_SEXPR_NATURAL || !*term->as.natural)
        return false;
    uint32_t value = 0;
    for (const unsigned char *p = (const unsigned char *)term->as.natural; *p; p++) {
        if (*p < '0' || *p > '9' || value > (UINT32_MAX - (*p - '0')) / 10) return false;
        value = value * 10 + (*p - '0');
    }
    *number = value;
    return true;
}

static const char *owned_text(Arena *arena, const CettaOpLangV1SExpr *term) {
    const char *text = source_text(term);
    if (text) return *text ? arena_strdup(arena, text) : NULL;
    if (!term || term->kind != CETTA_OP_LANG_V1_SEXPR_STRING || !term->as.string.len ||
        !utf8(term->as.string.bytes, term->as.string.len)) return NULL;
    char *copy = arena_alloc(arena, (size_t)term->as.string.len + 1);
    memcpy(copy, term->as.string.bytes, term->as.string.len);
    copy[term->as.string.len] = 0;
    return copy;
}

static bool exact_policy(const CettaOpLangV1SExpr *field,
                          const char *const *values, size_t count) {
    if (field->as.application.argument_len != count) return false;
    for (size_t i = 0; i < count; i++) {
        const CettaOpLangV1SExpr *term = field->as.application.arguments[i];
        if (term->kind == CETTA_OP_LANG_V1_SEXPR_STRING) {
            if (strlen(values[i]) != term->as.string.len ||
                memcmp(values[i], term->as.string.bytes, term->as.string.len)) return false;
        } else {
            const char *text = source_text(term);
            if (!text || strcmp(text, values[i])) return false;
        }
    }
    return true;
}

static bool source_declarations(Arena *arena, const CettaOpLangV1SExpr *field,
                                 const char *kind, const char **head,
                                 const CettaGsltSupportOperatorDeclV1 **declarations,
                                 size_t *count) {
    if (!field->as.application.argument_len ||
        !(*head = owned_text(arena, field->as.application.arguments[0]))) return false;
    uint32_t number = field->as.application.argument_len - 1;
    if (number > UINT16_MAX) return false;
    CettaGsltSupportOperatorDeclV1 *items = number ?
        arena_alloc(arena, (size_t)number * sizeof(*items)) : NULL;
    for (uint32_t i = 0; i < number; i++) {
        const CettaOpLangV1SExpr *item = field->as.application.arguments[i + 1];
        if (!is_form(item, kind, 3) ||
            !(items[i].syntax_symbol = owned_text(arena, item->as.application.arguments[0])) ||
            !source_u32(item->as.application.arguments[1], &items[i].argument_count) ||
            !(items[i].operator_id = owned_text(arena, item->as.application.arguments[2]))) return false;
    }
    *declarations = items; *count = number;
    return true;
}

static bool decode_document(Arena *arena, const CettaOpLangV1SExpr *root,
                             CettaGsltSupportTransformProfileV1 *p,
                             char *error, size_t error_size) {
    if (!root || root->kind != CETTA_OP_LANG_V1_SEXPR_APPLICATION ||
        strcmp(root->as.application.head, "gslt-support-transform-language-v1"))
        return profile_error(error, error_size, "expected one complete support profile form");
    const char *const tags[] = {"name", "profile", "carrier", "work-shell",
        "scheduler", "unsupported", "transition", "input-compat", "input-explicit",
        "output-compat", "output-explicit", "fuel", "completion", "observation"};
    const CettaOpLangV1SExpr *fields[14] = {0};
    for (uint32_t i = 0; i < root->as.application.argument_len; i++) {
        const CettaOpLangV1SExpr *field = root->as.application.arguments[i];
        if (!field || field->kind != CETTA_OP_LANG_V1_SEXPR_APPLICATION)
            return profile_error(error, error_size, "malformed support profile field");
        size_t tag = 0;
        while (tag < 14 && strcmp(field->as.application.head, tags[tag])) tag++;
        if (tag == 14 || fields[tag])
            return profile_error(error, error_size, "unknown or duplicate support profile field");
        fields[tag] = field;
    }
    for (size_t i = 0; i < 14; i++) if (!fields[i])
        return profile_error(error, error_size, "missing support profile field: %s", tags[i]);
    const char *const support[] = {"support"};
    const char *const scheduler[] = {"least-mork-compact-expression-key-v1"};
    const char *const unsupported[] = {"leave-inert"};
    const char *const transitions[] = {"consume-selected", "snapshot-includes-selected",
        "relational-product-may-reuse-support", "stage-all-matches-per-sink",
        "finalize-sinks-left-to-right"};
    const char *const fuel[] = {"exact-upper-bound"};
    const char *const completion[] = {"no-supported-work"};
    if (!exact_policy(fields[2], support, 1) || !exact_policy(fields[4], scheduler, 1) ||
        !exact_policy(fields[5], unsupported, 1) || !exact_policy(fields[6], transitions, 5) ||
        !exact_policy(fields[11], fuel, 1) || !exact_policy(fields[12], completion, 1) ||
        !exact_policy(fields[13], support, 1))
        return profile_error(error, error_size, "support profile requests a different execution policy");
    if (!is_form(fields[0], "name", 1) || !is_form(fields[1], "profile", 1) ||
        !is_form(fields[3], "work-shell", 5) ||
        !is_form(fields[7], "input-compat", 2) || !is_form(fields[9], "output-compat", 2))
        return profile_error(error, error_size, "support profile field has the wrong arity");
    p->abi_version = 1;
    p->scheduler = CETTA_GSLT_SUPPORT_SCHEDULER_LEAST_MORK_COMPACT_EXPRESSION_KEY_V1;
    p->unsupported_policy = CETTA_GSLT_SUPPORT_UNSUPPORTED_LEAVE_INERT;
    p->language_name = owned_text(arena, fields[0]->as.application.arguments[0]);
    p->profile_name = owned_text(arena, fields[1]->as.application.arguments[0]);
    p->work_symbol = owned_text(arena, fields[3]->as.application.arguments[0]);
    p->compat_input_symbol = owned_text(arena, fields[7]->as.application.arguments[0]);
    p->compat_input_operator_id = owned_text(arena, fields[7]->as.application.arguments[1]);
    p->compat_output_symbol = owned_text(arena, fields[9]->as.application.arguments[0]);
    p->compat_output_operator_id = owned_text(arena, fields[9]->as.application.arguments[1]);
    if (!source_u32(fields[3]->as.application.arguments[1], &p->work_arity) ||
        !source_u32(fields[3]->as.application.arguments[2], &p->location_position) ||
        !source_u32(fields[3]->as.application.arguments[3], &p->input_position) ||
        !source_u32(fields[3]->as.application.arguments[4], &p->output_position) ||
        !source_declarations(arena, fields[8], "source", &p->explicit_input_symbol,
                             &p->source_declarations, &p->source_declaration_count) ||
        !source_declarations(arena, fields[10], "sink", &p->explicit_output_symbol,
                             &p->sink_declarations, &p->sink_declaration_count))
        return profile_error(error, error_size, "malformed support profile work shell or declarations");
    return true;
}

bool cetta_gslt_support_profile_from_source_v1(
    Arena *arena, const uint8_t *source, size_t source_size,
    const char *compiler_sha256, CettaGsltSupportTransformProfileV1 *profile,
    char *error, size_t error_size) {
    if (profile) memset(profile, 0, sizeof(*profile));
    if (!arena || !profile || !source || !source_size ||
        !sha256_text(compiler_sha256) || !utf8(source, source_size))
        return profile_error(error, error_size, "invalid support profile source bytes or compiler identity");
    CettaOpLangV1Document document;
    cetta_op_lang_v1_document_init(&document);
    CettaOpLangV1Status status;
    bool ok = cetta_op_lang_v1_parse_commented_document_bytes(&document, source, source_size,
        UINT32_MAX, UINT32_MAX, &status, error, error_size);
    CettaGsltSupportTransformProfileV1 p = {0};
    if (ok) ok = decode_document(arena, document.root, &p, error, error_size);
    cetta_op_lang_v1_document_free(&document);
    if (!ok) return false;
    char digest[65];
    cetta_native_sha256_hex(source, source_size, digest);
    p.manifest_sha256 = arena_strdup(arena, digest);
    p.compiler_sha256 = arena_strdup(arena, compiler_sha256);
    if (!fields_validate(&p, error, error_size) ||
        !cetta_gslt_support_profile_encode_packet_v1(arena, &p,
            &p.physical_profile_packet, &p.physical_profile_packet_size, error, error_size) ||
        !cetta_gslt_support_profile_packet_matches_v1(&p, error, error_size)) return false;
    *profile = p;
    return true;
}
