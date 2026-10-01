#include "error_presentation.h"
#include "foreign.h"
#include <stdio.h>
#include <string.h>

static Atom *formal_head(Atom *formal) {
    return formal && formal->kind == ATOM_EXPR && formal->expr.len
        ? formal->expr.elems[0] : formal;
}

static void classify(CettaErrorPresentation *out, Atom *kind, bool python) {
    out->code = python ? CETTA_DIAGNOSTIC_PYTHON : CETTA_DIAGNOSTIC_EXECUTION;
    out->classification = python ? "PythonError" : "ExecutionError";
    const char *message = python ? "Python call failed" : "Evaluation raised an exception";
#define KIND(name, code_, message_) \
    if (kind && atom_is_symbol(kind, name)) { \
        out->code = code_; out->classification = name; message = message_; \
    }
    KIND("type_error", CETTA_DIAGNOSTIC_TYPE, "An argument has the wrong type")
    KIND("evaluation_error", CETTA_DIAGNOSTIC_ARITHMETIC, "Arithmetic evaluation failed")
    KIND("existence_error", CETTA_DIAGNOSTIC_UNAVAILABLE, "The requested object does not exist")
    KIND("permission_error", CETTA_DIAGNOSTIC_IO, "The operation was refused")
    KIND("resource_error", CETTA_DIAGNOSTIC_RESOURCE, "A required resource was exhausted")
    KIND("instantiation_error", CETTA_DIAGNOSTIC_INVALID_INPUT, "An argument is insufficiently instantiated")
    if (python) {
        KIND("TypeError", CETTA_DIAGNOSTIC_TYPE, "Python received an incompatible argument")
        KIND("ValueError", CETTA_DIAGNOSTIC_INVALID_INPUT, "Python rejected an argument value")
        KIND("ZeroDivisionError", CETTA_DIAGNOSTIC_ARITHMETIC, "Python arithmetic divided by zero")
        KIND("OverflowError", CETTA_DIAGNOSTIC_ARITHMETIC, "Python arithmetic overflowed")
        KIND("OSError", CETTA_DIAGNOSTIC_IO, "A Python I/O operation failed")
        KIND("FileNotFoundError", CETTA_DIAGNOSTIC_IO, "A requested file does not exist")
        KIND("PermissionError", CETTA_DIAGNOSTIC_IO, "A Python operation was refused")
        KIND("TimeoutError", CETTA_DIAGNOSTIC_TIMEOUT, "A Python operation timed out")
        KIND("ImportError", CETTA_DIAGNOSTIC_UNAVAILABLE, "A Python import failed")
        KIND("ModuleNotFoundError", CETTA_DIAGNOSTIC_UNAVAILABLE, "A requested Python module does not exist")
        KIND("MemoryError", CETTA_DIAGNOSTIC_RESOURCE, "Python could not allocate a resource")
    }
#undef KIND
    snprintf(out->message, sizeof(out->message), "%s", message);
}

/* Copy a bounded plain string; no recursive printer or arbitrary callback.
 * UTF-8 is copied only through complete code points. Controls cannot inject
 * another line into the human diagnostic. */
static bool string_detail(Atom *value, CettaErrorPresentation *out) {
    if (!value || value->kind != ATOM_GROUNDED || value->ground.gkind != GV_STRING)
        return false;
    const char *bytes = value->ground.sval;
    size_t length = atom_string_len(value), used = 0u, read = 0u;
    while (read < length) {
        unsigned char first = (unsigned char)bytes[read];
        size_t width = first < 0x80u ? 1u : first >= 0xc2u && first <= 0xdfu ? 2u
            : first >= 0xe0u && first <= 0xefu ? 3u
            : first >= 0xf0u && first <= 0xf4u ? 4u : 0u;
        bool valid = width != 0u && width <= length - read;
        for (size_t i = 1u; valid && i < width; i++)
            valid = ((unsigned char)bytes[read+i] & 0xc0u) == 0x80u;
        if (valid && width >= 3u) {
            unsigned char second = (unsigned char)bytes[read+1u];
            valid = !(first == 0xe0u && second < 0xa0u) &&
                    !(first == 0xedu && second >= 0xa0u) &&
                    !(first == 0xf0u && second < 0x90u) &&
                    !(first == 0xf4u && second >= 0x90u);
        }
        size_t emitted = valid ? width : 3u;
        if (emitted >= sizeof(out->message) - used) {
            out->truncated = true; break;
        }
        if (!valid) {
            memcpy(out->message + used, "\xef\xbf\xbd", 3u); used += 3u; width = 1u;
        } else if (width == 1u && (first < 0x20u || first == 0x7fu))
            out->message[used++] = ' ';
        else if (width == 3u && first == 0xe2u &&
                 (unsigned char)bytes[read+1u] == 0x80u &&
                 ((unsigned char)bytes[read+2u] == 0xa8u ||
                  (unsigned char)bytes[read+2u] == 0xa9u))
            out->message[used++] = ' ';
        else { memcpy(out->message + used, bytes + read, width); used += width; }
        read += width;
    }
    out->message[used] = '\0';
    return true;
}

void cetta_error_present(Atom *exception, bool allow_detail,
                         CettaErrorPresentation *out) {
    *out = (CettaErrorPresentation){.details_hidden = !allow_detail};
    Atom *formal = exception && atom_is_error(exception) && exception->expr.len >= 2u
        ? exception->expr.elems[1] : exception;
    bool python = formal && formal->kind == ATOM_EXPR && formal->expr.len >= 3u &&
        atom_is_symbol(formal->expr.elems[0], "python_error");
    classify(out, python ? formal->expr.elems[1] : formal_head(formal), python);
    if (!allow_detail) return;
    if (python) {
        char detail[CETTA_ERROR_MESSAGE_BYTES] = {0};
        bool truncated = false;
        if (cetta_foreign_exception_detail(formal->expr.elems[2], detail,
                                           sizeof(detail), &truncated)) {
            memcpy(out->message, detail, sizeof(out->message));
            out->truncated = truncated;
        } else out->formatter_failed = true;
    } else if (!string_detail(formal, out) && exception && atom_is_error(exception) &&
               exception->expr.len >= 3u) {
        (void)string_detail(exception->expr.elems[2], out);
    }
}
