#ifndef CETTA_JSON_PYTHON_V1_H
#define CETTA_JSON_PYTHON_V1_H

#include "atom.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* json.loads as CPython 3.12 runs it, over a text whose bytes are UTF-8 with
 * lone surrogates encoded as the surrogatepass error handler encodes them.
 * Values use the ordinary JSON constructors; strings keep that encoding. */
typedef enum {
    CETTA_JSON_PYTHON_V1_OK,
    /* json.JSONDecodeError: message, msg, and the code-point position. */
    CETTA_JSON_PYTHON_V1_DECODE_ERROR,
    /* ValueError from int() past sys.get_int_max_str_digits(). */
    CETTA_JSON_PYTHON_V1_VALUE_ERROR,
    /* The bytes are not UTF-8 even with surrogatepass: no Python text. */
    CETTA_JSON_PYTHON_V1_INVALID_TEXT,
    CETTA_JSON_PYTHON_V1_ALLOCATION_FAILURE
} CettaJsonPythonV1Status;

typedef struct {
    CettaJsonPythonV1Status status;
    const char *msg;      /* static: the exception's msg */
    size_t pos;           /* code points before the error */
    size_t lineno, colno;
    size_t digits;        /* for the integer-digit ValueError */
    size_t byte_offset;   /* for invalid text */
} CettaJsonPythonV1Error;

bool cetta_json_python_v1_parse(Arena *arena, const uint8_t *bytes, size_t len,
                                bool petta_booleans, Atom **out,
                                CettaJsonPythonV1Error *error);

#endif
