#ifndef CETTA_EXPAND_USER_H
#define CETTA_EXPAND_USER_H

#include <stddef.h>

typedef enum {
    CETTA_EXPAND_USER_EXPANDED,
    CETTA_EXPAND_USER_UNCHANGED,
    CETTA_EXPAND_USER_NO_MEMORY,
    CETTA_EXPAND_USER_TOO_LONG,
} CettaExpandUserStatus;

/* os.path.expanduser as CPython 3.12's posixpath has it. EXPANDED gives a
 * malloc'd result in *out, at most `limit` bytes before its NUL; UNCHANGED
 * means the path is its own answer. NO_MEMORY is CPython's MemoryError: an
 * allocation failed, or the account entry outgrew every buffer size. */
CettaExpandUserStatus cetta_expand_user(const char *path, size_t length, size_t limit,
                                        char **out, size_t *out_length);

#endif
