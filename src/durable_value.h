#ifndef CETTA_DURABLE_VALUE_H
#define CETTA_DURABLE_VALUE_H
#include "atom.h"
#include "durable_store.h"

/* CDV1 is a bounded, closed-data format. Symbols, strings, booleans, integers,
 * floats (including their bits), exact numbers, and expressions are retained.
 * Variables, capabilities, native handles, spaces, and mutable state are not
 * serializable. Continuations must use ground data, not captured heap state.
 * Sharing is not part of this format; cyclic values fail the depth bound. */
CettaDurableStatus cetta_durable_value_encode(const Atom *value,
    unsigned char **bytes, size_t *length);
CettaDurableStatus cetta_durable_value_decode(Arena *arena,
    const unsigned char *bytes, size_t length, Atom **value);

/* Native producers can encode fixed, closed envelopes without constructing
 * atoms or touching the evaluator's symbol table. Text excludes embedded NUL;
 * symbols must come from a finite protocol vocabulary, never external IDs.
 * Borrowed fields need only live through encode. The usual CDV1 bounds apply. */
typedef enum { DURABLE_FIELD_SYMBOL, DURABLE_FIELD_TEXT, DURABLE_FIELD_INT,
    DURABLE_FIELD_BOOL, DURABLE_FIELD_EXPR } CettaDurableFieldKind;
typedef struct CettaDurableField CettaDurableField;
struct CettaDurableField {
    CettaDurableFieldKind kind;
    union {
        struct { const char *data; size_t size; } text;
        int64_t integer;
        bool boolean;
        struct { const CettaDurableField *items; size_t count; } expression;
    };
};
CettaDurableStatus cetta_durable_fields_encode(const CettaDurableField *value,
    unsigned char **bytes, size_t *length);
#endif
