/* system:expanduser's account lookup through bounded seams. The account
 * database and the allocator are wrapped: lookups are counted and answered
 * from a fake entry, any allocation can be made to fail, and buffers past
 * REAL_LIMIT are a shared stand-in that the fake lookup never writes, so an
 * entry that outgrows every buffer size is tested without large real
 * allocations. Every case also checks that nothing stays allocated. */
#define _GNU_SOURCE
#include "expand_user.h"
#include <errno.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REAL_LIMIT ((size_t)1 << 20)

static unsigned checks;
#define check(condition) do { checks++; if (!(condition)) { \
    fprintf(stderr, "FAIL at line %d: %s\n", __LINE__, #condition); abort(); \
} } while (0)

static char stand_in[1];
static unsigned name_lookups, uid_lookups, reallocs, fail_realloc_at;
static int fail_strndup, fail_malloc, lookup_error;
static long live;
static size_t erange_below, largest_real;
static const char *name_home, *uid_home;

void *__real_malloc(size_t size);
void *__real_realloc(void *pointer, size_t size);
void __real_free(void *pointer);
char *__real_strndup(const char *text, size_t length);

void *__wrap_malloc(size_t size) {
    if (fail_malloc) return NULL;
    void *block = __real_malloc(size);
    if (block) live++;
    return block;
}

void *__wrap_realloc(void *pointer, size_t size) {
    reallocs++;
    if (fail_realloc_at && reallocs == fail_realloc_at) return NULL;
    if (pointer == stand_in) return size > REAL_LIMIT ? stand_in : NULL;
    if (size > REAL_LIMIT) {
        if (pointer) __real_free(pointer);
        else live++;
        return stand_in;
    }
    void *block = __real_realloc(pointer, size);
    if (block && !pointer) live++;
    if (block && size > largest_real) largest_real = size;
    return block;
}

void __wrap_free(void *pointer) {
    if (!pointer) return;
    live--;
    if (pointer != stand_in) __real_free(pointer);
}

char *__wrap_strndup(const char *text, size_t length) {
    if (fail_strndup) return NULL;
    char *copy = __real_strndup(text, length);
    if (copy) live++;
    return copy;
}

static int answer(const char *home, struct passwd *entry, char *buffer, size_t size, struct passwd **result) {
    *result = NULL;
    if (size < erange_below) return ERANGE;
    if (lookup_error) return lookup_error;
    if (!home) return 0;
    size_t needed = strlen(home) + 1u;
    if (needed > size) return ERANGE;
    memcpy(buffer, home, needed);
    memset(entry, 0, sizeof(*entry));
    entry->pw_dir = buffer;
    *result = entry;
    return 0;
}

int __wrap_getpwnam_r(const char *name, struct passwd *entry, char *buffer, size_t size, struct passwd **result) {
    name_lookups++;
    return answer(strcmp(name, "alice") ? NULL : name_home, entry, buffer, size, result);
}

int __wrap_getpwuid_r(uid_t uid, struct passwd *entry, char *buffer, size_t size, struct passwd **result) {
    (void)uid;
    uid_lookups++;
    return answer(uid_home, entry, buffer, size, result);
}

static void reset(const char *home) {
    name_lookups = uid_lookups = reallocs = fail_realloc_at = 0u;
    fail_strndup = fail_malloc = lookup_error = 0;
    erange_below = largest_real = 0u;
    name_home = "/home/alice//";
    uid_home = "/u/me/";
    if (home) setenv("HOME", home, 1);
    else unsetenv("HOME");
}

/* The status, and for EXPANDED the text; nothing may stay allocated. */
static CettaExpandUserStatus run(const char *path, size_t limit, const char *want) {
    char *out = NULL;
    size_t length = 0u;
    CettaExpandUserStatus status = cetta_expand_user(path, strlen(path), limit, &out, &length);
    if (status == CETTA_EXPAND_USER_EXPANDED) {
        check(want && length == strlen(want) && !memcmp(out, want, length) && out[length] == '\0');
        free(out);
    } else {
        check(!out);
    }
    check(live == 0);
    return status;
}

int main(void) {
    /* Ordinary answers. */
    reset("/home/test/");
    check(run("~/x", SIZE_MAX, "/home/test/x") == CETTA_EXPAND_USER_EXPANDED);
    check(name_lookups == 0u && uid_lookups == 0u);
    reset(NULL);
    check(run("~", SIZE_MAX, "/u/me") == CETTA_EXPAND_USER_EXPANDED);
    check(uid_lookups == 1u && name_lookups == 0u);
    check(run("~alice/p", SIZE_MAX, "/home/alice/p") == CETTA_EXPAND_USER_EXPANDED);
    check(name_lookups == 1u);
    check(run("~bob/p", SIZE_MAX, NULL) == CETTA_EXPAND_USER_UNCHANGED);
    check(run("x/~", SIZE_MAX, NULL) == CETTA_EXPAND_USER_UNCHANGED);
    check(run("", SIZE_MAX, NULL) == CETTA_EXPAND_USER_UNCHANGED);
    reset("");
    check(run("~", SIZE_MAX, "/") == CETTA_EXPAND_USER_EXPANDED);
    reset(NULL);
    name_home = "/";
    check(run("~alice", SIZE_MAX, "/") == CETTA_EXPAND_USER_EXPANDED);

    /* A name that cannot be copied is an allocation failure, never a lookup
     * of the current account in its place. */
    reset(NULL);
    fail_strndup = 1;
    check(run("~alice/p", SIZE_MAX, NULL) == CETTA_EXPAND_USER_NO_MEMORY);
    check(name_lookups == 0u && uid_lookups == 0u);

    /* A buffer that cannot be had, first or while growing. */
    reset(NULL);
    fail_realloc_at = 1u;
    check(run("~alice/p", SIZE_MAX, NULL) == CETTA_EXPAND_USER_NO_MEMORY);
    check(name_lookups == 0u);
    reset(NULL);
    erange_below = 4096u;
    fail_realloc_at = 3u;
    check(run("~/p", SIZE_MAX, NULL) == CETTA_EXPAND_USER_NO_MEMORY);
    check(uid_lookups == 2u);

    /* ERANGE grows the buffer until the entry fits... */
    reset(NULL);
    erange_below = (size_t)1 << 16;
    check(run("~alice/p", SIZE_MAX, "/home/alice/p") == CETTA_EXPAND_USER_EXPANDED);
    check(name_lookups == 7u && largest_real == ((size_t)1 << 16));
    /* ...and past every size is MemoryError, with no large real buffer. */
    reset(NULL);
    erange_below = SIZE_MAX;
    check(run("~alice/p", SIZE_MAX, NULL) == CETTA_EXPAND_USER_NO_MEMORY);
    check(largest_real <= REAL_LIMIT && name_lookups > 50u);

    /* Any other refusal is an absent account, as pwd's KeyError is. */
    reset(NULL);
    lookup_error = EIO;
    check(run("~alice/p", SIZE_MAX, NULL) == CETTA_EXPAND_USER_UNCHANGED);
    check(run("~", SIZE_MAX, NULL) == CETTA_EXPAND_USER_UNCHANGED);

    /* The string bound is checked before the result is allocated. */
    reset(NULL);
    check(run("~alice/abcdef", 18u, "/home/alice/abcdef") == CETTA_EXPAND_USER_EXPANDED);
    check(run("~alice/abcdef", 17u, NULL) == CETTA_EXPAND_USER_TOO_LONG);
    reset("");
    check(run("~", 1u, "/") == CETTA_EXPAND_USER_EXPANDED);
    check(run("~", 0u, NULL) == CETTA_EXPAND_USER_TOO_LONG);
    reset(NULL);
    fail_malloc = 1;
    check(run("~alice/p", SIZE_MAX, NULL) == CETTA_EXPAND_USER_NO_MEMORY);

    printf("expanduser seams: %u checks\n", checks);
    return 0;
}
