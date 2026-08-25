#define _POSIX_C_SOURCE 200809L

#include "atom.h"
#include "library_io.h"
#include "symbol.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned checks;
static unsigned failures;

#define CHECK(condition, label)                                                \
    do {                                                                       \
        checks++;                                                              \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: %s\n", (label));                          \
            failures++;                                                       \
        }                                                                      \
    } while (0)

static void pause_one_ms(void) {
    struct timespec duration = {.tv_sec = 0, .tv_nsec = 1000000L};
    (void)nanosleep(&duration, NULL);
}

static Atom *dispatch(CettaIoRuntime *runtime, Arena *arena,
                      const char *head_name, Atom **args, uint32_t nargs) {
    return cetta_io_dispatch(runtime, arena, atom_symbol(arena, head_name),
                             args, nargs);
}

static Atom *http_request_full(Arena *arena, const char *method,
                               const char *url, const char *body,
                               int64_t timeout_ms, int64_t max_bytes) {
    Atom *items[7] = {
        atom_symbol(arena, "http:request"),
        atom_string(arena, method),
        atom_string(arena, url),
        atom_expr(arena, NULL, 0u),
        atom_string(arena, body),
        atom_int(arena, timeout_ms),
        atom_int(arena, max_bytes),
    };
    return atom_expr(arena, items, 7u);
}

static Atom *http_request(Arena *arena, const char *url,
                          int64_t max_bytes) {
    return http_request_full(arena, "GET", url, "", 3000, max_bytes);
}

static bool pending_id(Atom *atom, int64_t *id_out) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 2u ||
        !atom_is_symbol(atom->expr.elems[0], "io:pending") ||
        atom->expr.elems[1]->kind != ATOM_GROUNDED ||
        atom->expr.elems[1]->ground.gkind != GV_INT)
        return false;
    *id_out = atom->expr.elems[1]->ground.ival;
    return true;
}

static bool idle(Atom *atom) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == 1u &&
           atom_is_symbol(atom->expr.elems[0], "io:idle");
}

static bool event_parts(Atom *atom, int64_t *id_out, Atom **result_out) {
    if (!atom || atom->kind != ATOM_EXPR || atom->expr.len != 3u ||
        !atom_is_symbol(atom->expr.elems[0], "io:event") ||
        atom->expr.elems[1]->kind != ATOM_GROUNDED ||
        atom->expr.elems[1]->ground.gkind != GV_INT)
        return false;
    *id_out = atom->expr.elems[1]->ground.ival;
    *result_out = atom->expr.elems[2];
    return true;
}

static bool http_response(Atom *atom, long status, const char *body) {
    return atom && atom->kind == ATOM_EXPR && atom->expr.len == 3u &&
           atom_is_symbol(atom->expr.elems[0], "http:response") &&
           atom->expr.elems[1]->kind == ATOM_GROUNDED &&
           atom->expr.elems[1]->ground.gkind == GV_INT &&
           atom->expr.elems[1]->ground.ival == status &&
           atom->expr.elems[2]->kind == ATOM_GROUNDED &&
           atom->expr.elems[2]->ground.gkind == GV_STRING &&
           strcmp(atom->expr.elems[2]->ground.sval, body) == 0;
}

static bool response_too_large(Atom *atom, int64_t bound) {
    if (!atom_is_error(atom)) return false;
    Atom *reason = atom->expr.elems[2];
    return reason && reason->kind == ATOM_EXPR && reason->expr.len == 3u &&
           atom_is_symbol(reason->expr.elems[0], "http:error") &&
           atom_is_symbol(reason->expr.elems[1], "response-too-large") &&
           reason->expr.elems[2]->kind == ATOM_GROUNDED &&
           reason->expr.elems[2]->ground.gkind == GV_INT &&
           reason->expr.elems[2]->ground.ival == bound;
}

static Atom *poll_until_event(CettaIoRuntime *runtime, Arena *arena,
                              int attempts) {
    for (int i = 0; i < attempts; i++) {
        Atom *value = dispatch(runtime, arena, "__cetta_lib_io_poll", NULL, 0u);
        if (!idle(value)) return value;
        pause_one_ms();
    }
    return NULL;
}

static Atom *submit_and_poll(CettaIoRuntime *runtime, Arena *arena,
                             Atom *request, int64_t *id_out) {
    Atom *pending = dispatch(runtime, arena, "__cetta_lib_io_submit",
                             &request, 1u);
    if (!pending_id(pending, id_out)) return NULL;
    Atom *event = poll_until_event(runtime, arena, 5000);
    int64_t observed_id = 0;
    Atom *result = NULL;
    if (!event_parts(event, &observed_id, &result) ||
        observed_id != *id_out)
        return NULL;
    return result;
}

static void make_url(char *out, size_t out_size, const char *base,
                     const char *path) {
    int written = snprintf(out, out_size, "%s%s", base, path);
    if (written < 0 || (size_t)written >= out_size) {
        fprintf(stderr, "base URL too long\n");
        exit(2);
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s BASE_URL\n", argv[0]);
        return 2;
    }
    SymbolTable symbols;
    Arena arena;
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    CettaIoRuntime *runtime = cetta_io_runtime_new();

    Atom *capabilities = dispatch(runtime, &arena,
                                  "__cetta_lib_io_capabilities", NULL, 0u);
    CHECK(capabilities && capabilities->kind == ATOM_EXPR &&
              capabilities->expr.len == 1u &&
              atom_is_symbol(capabilities->expr.elems[0], "http"),
          "native provider advertises HTTP");

    if (getenv("CETTA_IO_REPLAY_ONLY")) {
        char replay_url[512];
        make_url(replay_url, sizeof(replay_url), argv[1], "/one");
        Atom *replay_arg = http_request(&arena, replay_url, 64);
        Atom *pending = dispatch(runtime, &arena, "__cetta_lib_io_submit",
                                 &replay_arg, 1u);
        int64_t replay_id = 0;
        CHECK(pending_id(pending, &replay_id) && replay_id > 0,
              "replay witness receives an ID");
        Atom *event = poll_until_event(runtime, &arena, 5000);
        int64_t observed_id = 0;
        Atom *result = NULL;
        CHECK(event_parts(event, &observed_id, &result) &&
                  observed_id == replay_id && http_response(result, 200, "one"),
              "replay witness completes once");
        CHECK(idle(dispatch(runtime, &arena, "__cetta_lib_io_poll", NULL, 0u)),
              "consumed completions cannot replay");
        printf("(IoRuntimeSummary %u %u %u)\n",
               checks, checks - failures, failures);
        cetta_io_runtime_free(runtime);
        arena_free(&arena);
        symbol_table_free(&symbols);
        g_symbols = NULL;
        return failures == 0u ? 0 : 1;
    }

    char one_url[512];
    char two_url[512];
    make_url(one_url, sizeof(one_url), argv[1], "/one");
    make_url(two_url, sizeof(two_url), argv[1], "/two");
    Atom *one_arg = http_request(&arena, one_url, 64);
    Atom *two_arg = http_request(&arena, two_url, 64);
    Atom *one_pending = dispatch(runtime, &arena, "__cetta_lib_io_submit",
                                 &one_arg, 1u);
    Atom *two_pending = dispatch(runtime, &arena, "__cetta_lib_io_submit",
                                 &two_arg, 1u);
    int64_t one_id = 0;
    int64_t two_id = 0;
    CHECK(pending_id(one_pending, &one_id), "first request receives an ID");
    CHECK(pending_id(two_pending, &two_id), "second request receives an ID");
    CHECK(one_id > 0 && two_id > one_id, "request IDs are monotone and unique");

    bool saw_one = false;
    bool saw_two = false;
    for (int events = 0; events < 2; events++) {
        Atom *event = poll_until_event(runtime, &arena, 5000);
        int64_t id = 0;
        Atom *result = NULL;
        CHECK(event_parts(event, &id, &result), "completion has event envelope");
        if (id == one_id) {
            CHECK(http_response(result, 200, "one"),
                  "first result stays correlated with first request");
            saw_one = true;
        } else if (id == two_id) {
            CHECK(http_response(result, 200, "two"),
                  "second result stays correlated with second request");
            saw_two = true;
        } else {
            CHECK(false, "completion ID was issued by this runtime");
        }
    }
    CHECK(saw_one && saw_two, "both concurrent completions are observed");
    CHECK(idle(dispatch(runtime, &arena, "__cetta_lib_io_poll", NULL, 0u)),
          "consumed completions cannot replay");

    char slow_url[512];
    make_url(slow_url, sizeof(slow_url), argv[1], "/slow");
    Atom *slow_arg = http_request(&arena, slow_url, 64);
    Atom *slow_pending = dispatch(runtime, &arena, "__cetta_lib_io_submit",
                                  &slow_arg, 1u);
    int64_t slow_id = 0;
    CHECK(pending_id(slow_pending, &slow_id), "cancel candidate receives an ID");
    Atom *cancel_arg = atom_int(&arena, slow_id);
    Atom *cancelled = dispatch(runtime, &arena, "__cetta_lib_io_cancel",
                               &cancel_arg, 1u);
    CHECK(cancelled && cancelled->kind == ATOM_EXPR &&
              cancelled->expr.len == 0u,
          "cancel returns unit");
    bool cancelled_stayed_idle = true;
    for (int i = 0; i < 300; i++) {
        if (!idle(dispatch(runtime, &arena, "__cetta_lib_io_poll", NULL, 0u)))
            cancelled_stayed_idle = false;
        pause_one_ms();
    }
    CHECK(cancelled_stayed_idle, "cancelled request never emits a completion");

    char large_url[512];
    make_url(large_url, sizeof(large_url), argv[1], "/large");
    Atom *large_arg = http_request(&arena, large_url, 4);
    Atom *large_pending = dispatch(runtime, &arena, "__cetta_lib_io_submit",
                                   &large_arg, 1u);
    int64_t large_id = 0;
    CHECK(pending_id(large_pending, &large_id), "bounded request receives an ID");
    Atom *large_event = poll_until_event(runtime, &arena, 5000);
    int64_t observed_large_id = 0;
    Atom *large_result = NULL;
    CHECK(event_parts(large_event, &observed_large_id, &large_result) &&
              observed_large_id == large_id,
          "bounded failure retains its correlation ID");
    CHECK(response_too_large(large_result, 4),
          "response bound fails explicitly instead of truncating");
    CHECK(idle(dispatch(runtime, &arena, "__cetta_lib_io_poll", NULL, 0u)),
          "bounded failure is also consumed at most once");

    char exact_url[512];
    make_url(exact_url, sizeof(exact_url), argv[1], "/bytes/4");
    int64_t exact_id = 0;
    Atom *exact_request = http_request(&arena, exact_url, 4);
    Atom *exact_result = submit_and_poll(runtime, &arena, exact_request,
                                         &exact_id);
    CHECK(exact_id > large_id && http_response(exact_result, 200, "abcd"),
          "response exactly at the byte bound succeeds");

    int64_t over_id = 0;
    Atom *over_request = http_request(&arena, exact_url, 3);
    Atom *over_result = submit_and_poll(runtime, &arena, over_request,
                                        &over_id);
    CHECK(over_id > exact_id && response_too_large(over_result, 3),
          "one byte over the bound fails explicitly");

    char status_url[512];
    make_url(status_url, sizeof(status_url), argv[1], "/status/418");
    int64_t status_id = 0;
    Atom *status_request = http_request(&arena, status_url, 64);
    Atom *status_result = submit_and_poll(runtime, &arena, status_request,
                                          &status_id);
    CHECK(status_id > over_id &&
              http_response(status_result, 418, "status-418"),
          "HTTP error status remains a correlated response");

    char redirect_url[512];
    make_url(redirect_url, sizeof(redirect_url), argv[1], "/redirect/2");
    int64_t redirect_id = 0;
    Atom *redirect_request = http_request(&arena, redirect_url, 64);
    Atom *redirect_result = submit_and_poll(runtime, &arena,
                                             redirect_request, &redirect_id);
    CHECK(redirect_id > status_id &&
              http_response(redirect_result, 200, "one"),
          "bounded redirect chain reaches its final response");

    char echo_url[512];
    make_url(echo_url, sizeof(echo_url), argv[1], "/echo");
    int64_t echo_id = 0;
    Atom *echo_request = http_request_full(
        &arena, "POST", echo_url, "portable-body", 3000, 64);
    Atom *echo_result = submit_and_poll(runtime, &arena, echo_request,
                                        &echo_id);
    CHECK(echo_id > redirect_id &&
              http_response(echo_result, 200, "portable-body"),
          "POST body round-trips through the native provider");

    CHECK(idle(dispatch(runtime, &arena, "__cetta_lib_io_poll", NULL, 0u)),
          "conformance completions leave the queue empty");

    printf("(IoRuntimeSummary %u %u %u)\n",
           checks, checks - failures, failures);
    cetta_io_runtime_free(runtime);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return failures == 0u ? 0 : 1;
}
