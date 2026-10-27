/* lib/toml: TOML 1.1 documents (a superset of TOML 1.0) as JSON text,
 * through the vendored tomlc17.
 *
 * (__cetta_lib_toml to-json (TEXT)) answers the document as JSON text, or
 * (toml:failed KIND DETAIL).  Tables become objects with their keys in
 * document order.  Dates and times become strings in the form Python's
 * isoformat writes them: seconds always, microseconds when nonzero, an
 * offset as +HH:MM ("Z" is +00:00).  A float JSON cannot carry, an infinity
 * or NaN, is refused rather than altered. */
#include "library_toml.h"
#include "library.h"
#include "vendor/tomlc17/tomlc17.h"
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *bytes;
    size_t length, capacity;
    const char *refusal;
} JsonText;

static void put(JsonText *out, const char *bytes, size_t length) {
    if (out->refusal) return;
    if (out->length + length + 1u > out->capacity) {
        size_t capacity = out->capacity ? out->capacity : 256u;
        while (capacity < out->length + length + 1u) capacity *= 2u;
        char *grown = realloc(out->bytes, capacity);
        if (!grown) {
            out->refusal = "out of memory";
            return;
        }
        out->bytes = grown;
        out->capacity = capacity;
    }
    memcpy(out->bytes + out->length, bytes, length);
    out->length += length;
    out->bytes[out->length] = '\0';
}

static void put_text(JsonText *out, const char *text) { put(out, text, strlen(text)); }

static void put_string(JsonText *out, const char *text, size_t length) {
    put(out, "\"", 1u);
    for (size_t i = 0u; i < length; i++) {
        unsigned char c = (unsigned char)text[i];
        char escape[8];
        if (c == '"') put(out, "\\\"", 2u);
        else if (c == '\\') put(out, "\\\\", 2u);
        else if (c == '\n') put(out, "\\n", 2u);
        else if (c == '\r') put(out, "\\r", 2u);
        else if (c == '\t') put(out, "\\t", 2u);
        else if (c == '\b') put(out, "\\b", 2u);
        else if (c == '\f') put(out, "\\f", 2u);
        else if (c < 0x20u) {
            snprintf(escape, sizeof(escape), "\\u%04x", c);
            put(out, escape, 6u);
        } else {
            put(out, (const char *)&text[i], 1u);
        }
    }
    put(out, "\"", 1u);
}

/* The shortest %g text that reads back as the same double, kept a float for
 * JSON readers by a ".0" when it has neither point nor exponent. */
static void put_float(JsonText *out, double value) {
    char text[40];
    if (!isfinite(value)) {
        out->refusal = "a float JSON cannot carry, an infinity or NaN";
        return;
    }
    for (int precision = 15; precision <= 17; precision++) {
        snprintf(text, sizeof(text), "%.*g", precision, value);
        if (strtod(text, NULL) == value) break;
    }
    put_text(out, text);
    if (!strpbrk(text, ".eE")) put(out, ".0", 2u);
}

static void put_time_of_day(JsonText *out, const toml_datum_t *d) {
    char text[32];
    snprintf(text, sizeof(text), "%02d:%02d:%02d", d->u.ts.hour, d->u.ts.minute, d->u.ts.second);
    put_text(out, text);
    if (d->u.ts.usec) {
        snprintf(text, sizeof(text), ".%06d", (int)d->u.ts.usec);
        put_text(out, text);
    }
}

static void put_date(JsonText *out, const toml_datum_t *d) {
    char text[32];
    snprintf(text, sizeof(text), "%04d-%02d-%02d", d->u.ts.year, d->u.ts.month, d->u.ts.day);
    put_text(out, text);
}

static void put_datum(JsonText *out, const toml_datum_t *d) {
    char text[64];
    switch (d->type) {
    case TOML_STRING:
        put_string(out, d->u.str.ptr, (size_t)d->u.str.len);
        return;
    case TOML_INT64:
        snprintf(text, sizeof(text), "%" PRId64, d->u.int64);
        put_text(out, text);
        return;
    case TOML_FP64:
        put_float(out, d->u.fp64);
        return;
    case TOML_BOOLEAN:
        put_text(out, d->u.boolean ? "true" : "false");
        return;
    case TOML_DATE:
        put(out, "\"", 1u);
        put_date(out, d);
        put(out, "\"", 1u);
        return;
    case TOML_TIME:
        put(out, "\"", 1u);
        put_time_of_day(out, d);
        put(out, "\"", 1u);
        return;
    case TOML_DATETIME:
    case TOML_DATETIMETZ:
        put(out, "\"", 1u);
        put_date(out, d);
        put(out, "T", 1u);
        put_time_of_day(out, d);
        if (d->type == TOML_DATETIMETZ) {
            int minutes = d->u.ts.tz < 0 ? -d->u.ts.tz : d->u.ts.tz;
            snprintf(text, sizeof(text), "%c%02d:%02d", d->u.ts.tz < 0 ? '-' : '+', minutes / 60, minutes % 60);
            put_text(out, text);
        }
        put(out, "\"", 1u);
        return;
    case TOML_ARRAY:
        put(out, "[", 1u);
        for (int32_t i = 0; i < d->u.arr.size; i++) {
            if (i) put(out, ",", 1u);
            put_datum(out, &d->u.arr.elem[i]);
        }
        put(out, "]", 1u);
        return;
    case TOML_TABLE:
        put(out, "{", 1u);
        for (int32_t i = 0; i < d->u.tab.size; i++) {
            if (i) put(out, ",", 1u);
            put_string(out, d->u.tab.key[i], (size_t)d->u.tab.len[i]);
            put(out, ":", 1u);
            put_datum(out, &d->u.tab.value[i]);
        }
        put(out, "}", 1u);
        return;
    case TOML_UNKNOWN:
        break;
    }
    out->refusal = "a value of unknown type";
}

static pthread_once_t toml_options_once = PTHREAD_ONCE_INIT;

/* TOML text is UTF-8; the parser checks it only when asked. */
static void toml_options(void) {
    toml_option_t option = toml_default_option();
    option.check_utf8 = true;
    toml_set_option(option);
}

static Atom *failure(Arena *a, const char *kind, const char *detail) {
    return atom_expr3(a, atom_symbol(a, "toml:failed"), atom_symbol(a, kind), atom_string(a, detail));
}

static Atom *to_json(Arena *a, Atom *document) {
    const char *text;
    size_t length;
    if (document && document->kind == ATOM_GROUNDED && document->ground.gkind == GV_STRING) {
        text = document->ground.sval;
        length = atom_string_len(document);
    } else {
        return failure(a, "toml:invalid-argument", "expected the document as text");
    }
    if (length > (size_t)INT_MAX) return failure(a, "toml:invalid-argument", "the document is too long");
    pthread_once(&toml_options_once, toml_options);
    toml_result_t result = toml_parse(text, (int)length);
    if (!result.ok) {
        Atom *refused = failure(a, "toml:invalid", result.errmsg);
        toml_free(result);
        return refused;
    }
    JsonText out = {0};
    put_datum(&out, &result.toptab);
    toml_free(result);
    Atom *value = out.refusal ? failure(a, "toml:unrepresentable", out.refusal)
                              : atom_string_n(a, out.bytes, out.length);
    free(out.bytes);
    return value;
}

bool cetta_toml_call(CettaLibraryContext *ctx, Arena *a, Atom *head, Atom **args,
                     uint32_t nargs, CettaCallOutcome *out) {
    (void)ctx;
    if (!head || head->kind != ATOM_SYMBOL || atom_symbol_current_id(head) != g_builtin_syms.lib_toml)
        return false;
    const char *operation = nargs == 2u && args[0] && args[0]->kind == ATOM_SYMBOL &&
        args[1] && args[1]->kind == ATOM_EXPR ? atom_name_cstr(args[0]) : NULL;
    Atom *value = operation && !strcmp(operation, "to-json") && args[1]->expr.len == 1u
        ? to_json(a, args[1]->expr.elems[0])
        : failure(a, "toml:invalid-argument", "expected a toml operation and inert operand tuple");
    *out = cetta_call_completed_value(value);
    return true;
}
