#ifndef CETTA_ERROR_PRESENTATION_H
#define CETTA_ERROR_PRESENTATION_H
#include "atom.h"
#include <stdbool.h>
#include <stdint.h>

enum { CETTA_ERROR_MESSAGE_BYTES = 256 };
typedef enum {
    CETTA_DIAGNOSTIC_EXECUTION = 1,
    CETTA_DIAGNOSTIC_PYTHON,
    CETTA_DIAGNOSTIC_TYPE,
    CETTA_DIAGNOSTIC_IO,
    CETTA_DIAGNOSTIC_TIMEOUT,
    CETTA_DIAGNOSTIC_UNAVAILABLE,
    CETTA_DIAGNOSTIC_INVALID_INPUT,
    CETTA_DIAGNOSTIC_ARITHMETIC,
    CETTA_DIAGNOSTIC_RESOURCE,
} CettaDiagnosticCode;

/* Owns only presentation bytes. No Atom, foreign object or arena survives
 * projection. Arbitrary exception detail is private unless explicitly allowed. */
typedef struct {
    CettaDiagnosticCode code;
    const char *classification; /* fixed vocabulary, never exception text */
    char message[CETTA_ERROR_MESSAGE_BYTES];
    bool details_hidden, truncated, formatter_failed;
} CettaErrorPresentation;

void cetta_error_present(Atom *exception, bool allow_detail,
                         CettaErrorPresentation *out);
/* Python adapter: bounded plain exception args, with no __str__/__repr__ or
 * attribute callbacks. False selects the caller's total presentation fallback. */
bool cetta_foreign_exception_detail(Atom *value, char *out, size_t capacity,
                                    bool *truncated);
#endif
