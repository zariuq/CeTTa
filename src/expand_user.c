/* os.path.expanduser as CPython 3.12's posixpath has it: "~" alone or
 * before "/" reads $HOME when it is set, even empty, else the account
 * database for getuid(); "~name" reads the database for that name. An
 * account the database does not give leaves the path unchanged, as
 * pwd's KeyError does. Trailing slashes of the home are dropped, and an
 * empty result is "/". */
#define _GNU_SOURCE
#include "expand_user.h"
#include <errno.h>
#include <pwd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The entry for `name`, or for getuid() when name is NULL, in a buffer that
 * grows on ERANGE as pwd's does: until the entry fits or the size would
 * overflow. Any other refusal is an absent account. */
static CettaExpandUserStatus account_home(const char *name, char **buffer, const char **home) {
    struct passwd entry, *found = NULL;
    size_t size = 1024u;
    int status;
    for (;;) {
        char *grown = realloc(*buffer, size);
        if (!grown) return CETTA_EXPAND_USER_NO_MEMORY;
        *buffer = grown;
        status = name ? getpwnam_r(name, &entry, *buffer, size, &found)
                      : getpwuid_r(getuid(), &entry, *buffer, size, &found);
        if (status != ERANGE) break;
        if (size > SIZE_MAX / 2u) return CETTA_EXPAND_USER_NO_MEMORY;
        size *= 2u;
    }
    if (status || !found || !entry.pw_dir) return CETTA_EXPAND_USER_UNCHANGED;
    *home = entry.pw_dir;
    return CETTA_EXPAND_USER_EXPANDED;
}

CettaExpandUserStatus cetta_expand_user(const char *path, size_t length, size_t limit,
                                        char **out, size_t *out_length) {
    if (!length || path[0] != '~') return CETTA_EXPAND_USER_UNCHANGED;
    const char *slash = memchr(path + 1, '/', length - 1u);
    size_t name_end = slash ? (size_t)(slash - path) : length;
    const char *home = name_end == 1u ? getenv("HOME") : NULL;
    char *buffer = NULL;
    if (!home) {
        char *name = NULL;
        if (name_end > 1u && !(name = strndup(path + 1, name_end - 1u)))
            return CETTA_EXPAND_USER_NO_MEMORY;
        CettaExpandUserStatus found = account_home(name, &buffer, &home);
        free(name);
        if (found != CETTA_EXPAND_USER_EXPANDED) {
            free(buffer);
            return found;
        }
    }
    size_t home_length = strlen(home);
    while (home_length && home[home_length - 1u] == '/') home_length--;
    size_t rest = length - name_end;
    /* The result is home_length + rest bytes, or "/" when that is none. */
    if (home_length > SIZE_MAX - 2u - rest ||
        (home_length + rest ? home_length + rest : 1u) > limit) {
        free(buffer);
        return CETTA_EXPAND_USER_TOO_LONG;
    }
    char *expanded = malloc(home_length + rest + 2u);
    if (!expanded) {
        free(buffer);
        return CETTA_EXPAND_USER_NO_MEMORY;
    }
    memcpy(expanded, home, home_length);
    memcpy(expanded + home_length, path + name_end, rest);
    size_t expanded_length = home_length + rest;
    if (!expanded_length) expanded[expanded_length++] = '/';
    expanded[expanded_length] = '\0';
    free(buffer);
    *out = expanded;
    *out_length = expanded_length;
    return CETTA_EXPAND_USER_EXPANDED;
}
