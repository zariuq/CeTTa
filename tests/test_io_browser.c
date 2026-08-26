#include "atom.h"
#include "library_io.h"
#include "symbol.h"

#include <emscripten.h>
#include <emscripten/eventloop.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SymbolTable symbols;
static Arena arena;
static CettaIoRuntime *runtime;
static int interval_id;
static int ticks;
static int64_t one_id;
static int64_t two_id;
static int64_t slow_id;
static int64_t large_id;
static int64_t exact_id;
static int64_t over_id;
static int64_t status_id;
static int64_t redirect_id;
static int64_t echo_id;
static int64_t drip_id;
static int64_t mid_cancel_id;
static bool saw_one;
static bool saw_two;
static bool saw_large;
static bool saw_exact;
static bool saw_over;
static bool saw_status;
static bool saw_redirect;
static bool saw_echo;
static bool saw_drip;
static bool mid_cancelled;
static unsigned checks;
static unsigned failures;

#define CHECK(condition, label)                                                \
    do {                                                                       \
        checks++;                                                              \
        if (!(condition)) {                                                    \
            fprintf(stderr, "FAIL: %s\n", (label));                           \
            failures++;                                                       \
        }                                                                      \
    } while (0)

EM_JS(void, browser_origin, (char *result, size_t size), {
    const text = window.location.origin;
    stringToUTF8(text, result, size);
});

EM_JS(void, publish_result, (unsigned total, unsigned failed), {
    const passed = total - failed;
    const text = `(IoBrowserSummary ${total} ${passed} ${failed})`;
    const output = document.getElementById('result');
    output.textContent = text;
    output.dataset.status = failed === 0 ? 'pass' : 'fail';
});

static Atom *dispatch(const char *head_name, Atom **args, uint32_t nargs) {
    return cetta_io_dispatch(runtime, &arena, atom_symbol(&arena, head_name),
                             args, nargs);
}

static Atom *http_request_full(const char *method, const char *url,
                               const char *body, int64_t max_bytes) {
    Atom *items[7] = {
        atom_symbol(&arena, "http:request"),
        atom_string(&arena, method),
        atom_string(&arena, url),
        atom_expr(&arena, NULL, 0u),
        atom_string(&arena, body),
        atom_int(&arena, 3000),
        atom_int(&arena, max_bytes),
    };
    return atom_expr(&arena, items, 7u);
}

static Atom *http_request(const char *url, int64_t max_bytes) {
    return http_request_full("GET", url, "", max_bytes);
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

static void finish(void) {
    emscripten_clear_interval(interval_id);
    printf("(IoBrowserSummary %u %u %u)\n",
           checks, checks - failures, failures);
    publish_result(checks, failures);
    cetta_io_runtime_free(runtime);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
}

static void poll_io(void *user_data) {
    (void)user_data;
    ticks++;
    if (ticks == 5 && !mid_cancelled) {
        Atom *cancel_arg = atom_int(&arena, mid_cancel_id);
        Atom *cancelled = dispatch("__cetta_lib_io_cancel", &cancel_arg, 1u);
        CHECK(cancelled && cancelled->kind == ATOM_EXPR &&
                  cancelled->expr.len == 0u,
              "in-flight browser stream can be cancelled");
        mid_cancelled = true;
    }
    for (;;) {
        Atom *event = dispatch("__cetta_lib_io_poll", NULL, 0u);
        if (idle(event)) break;
        int64_t id = 0;
        Atom *result = NULL;
        CHECK(event_parts(event, &id, &result),
              "browser completion has event envelope");
        if (id == one_id) {
            bool valid = !saw_one && http_response(result, 200, "one");
            if (!valid) { atom_print(result, stderr); fputc('\n', stderr); }
            CHECK(valid,
                  "browser first result stays correlated");
            saw_one = true;
        } else if (id == two_id) {
            bool valid = !saw_two && http_response(result, 200, "two");
            if (!valid) { atom_print(result, stderr); fputc('\n', stderr); }
            CHECK(valid,
                  "browser second result stays correlated");
            saw_two = true;
        } else if (id == large_id) {
            bool valid = !saw_large && response_too_large(result, 4);
            if (!valid) { atom_print(result, stderr); fputc('\n', stderr); }
            CHECK(valid,
                  "browser response bound fails explicitly");
            saw_large = true;
        } else if (id == exact_id) {
            CHECK(!saw_exact && http_response(result, 200, "abcd"),
                  "browser exact byte bound succeeds");
            saw_exact = true;
        } else if (id == over_id) {
            CHECK(!saw_over && response_too_large(result, 3),
                  "browser one-byte overflow fails explicitly");
            saw_over = true;
        } else if (id == status_id) {
            CHECK(!saw_status && http_response(result, 418, "status-418"),
                  "browser preserves HTTP error status as a response");
            saw_status = true;
        } else if (id == redirect_id) {
            CHECK(!saw_redirect && http_response(result, 200, "one"),
                  "browser follows the bounded redirect chain");
            saw_redirect = true;
        } else if (id == echo_id) {
            CHECK(!saw_echo &&
                      http_response(result, 200, "portable-body"),
                  "browser POST body round-trips");
            saw_echo = true;
        } else if (id == drip_id) {
            CHECK(!saw_drip && response_too_large(result, 4),
                  "browser streaming bound fails explicitly");
            CHECK(ticks < 40,
                  "browser byte bound aborts the transfer promptly");
            saw_drip = true;
        } else if (id == slow_id) {
            CHECK(false, "cancelled browser request never completes");
        } else if (id == mid_cancel_id) {
            CHECK(false, "mid-stream cancelled request never completes");
        } else {
            CHECK(false, "browser completion ID was issued by this runtime");
        }
    }
    if (saw_one && saw_two && saw_large && saw_exact && saw_over &&
        saw_status && saw_redirect && saw_echo && saw_drip &&
        mid_cancelled && ticks >= 80) {
        CHECK(idle(dispatch("__cetta_lib_io_poll", NULL, 0u)),
              "browser completions cannot replay");
        finish();
    } else if (ticks >= 800) {
        CHECK(false, "browser requests complete before timeout");
        finish();
    }
}

static int64_t submit(const char *base, const char *path, int64_t max_bytes) {
    char url[1024];
    int written = snprintf(url, sizeof(url), "%s%s", base, path);
    if (written < 0 || (size_t)written >= sizeof(url)) return 0;
    Atom *request = http_request(url, max_bytes);
    Atom *pending = dispatch("__cetta_lib_io_submit", &request, 1u);
    int64_t id = 0;
    return pending_id(pending, &id) ? id : 0;
}

static int64_t submit_full(const char *base, const char *path,
                           const char *method, const char *body,
                           int64_t max_bytes) {
    char url[1024];
    int written = snprintf(url, sizeof(url), "%s%s", base, path);
    if (written < 0 || (size_t)written >= sizeof(url)) return 0;
    Atom *request = http_request_full(method, url, body, max_bytes);
    Atom *pending = dispatch("__cetta_lib_io_submit", &request, 1u);
    int64_t id = 0;
    return pending_id(pending, &id) ? id : 0;
}

int main(void) {
    symbol_table_init(&symbols);
    symbol_table_init_builtins(&symbols, &g_builtin_syms);
    g_symbols = &symbols;
    g_hashcons = NULL;
    g_var_intern = NULL;
    arena_init(&arena);
    runtime = cetta_io_runtime_new();

    Atom *capabilities = dispatch("__cetta_lib_io_capabilities", NULL, 0u);
    CHECK(capabilities && capabilities->kind == ATOM_EXPR &&
              capabilities->expr.len == 1u &&
              atom_is_symbol(capabilities->expr.elems[0], "http"),
          "browser provider advertises HTTP");

    char base[512];
    browser_origin(base, sizeof(base));
    one_id = submit(base, "/one", 64);
    two_id = submit(base, "/two", 64);
    slow_id = submit(base, "/slow", 64);
    large_id = submit(base, "/large", 4);
    exact_id = submit(base, "/bytes/4", 4);
    over_id = submit(base, "/bytes/4", 3);
    status_id = submit(base, "/status/418", 64);
    redirect_id = submit(base, "/redirect/2", 64);
    echo_id = submit_full(base, "/echo", "POST", "portable-body", 64);
    drip_id = submit(base, "/drip", 4);
    mid_cancel_id = submit(base, "/drip", 1048576);
    CHECK(one_id > 0 && two_id > one_id && slow_id > two_id &&
              large_id > slow_id && exact_id > large_id &&
              over_id > exact_id && status_id > over_id &&
              redirect_id > status_id && echo_id > redirect_id &&
              drip_id > echo_id && mid_cancel_id > drip_id,
          "browser request IDs are monotone and unique");

    Atom *cancel_arg = atom_int(&arena, slow_id);
    Atom *cancelled = dispatch("__cetta_lib_io_cancel", &cancel_arg, 1u);
    CHECK(cancelled && cancelled->kind == ATOM_EXPR &&
              cancelled->expr.len == 0u,
          "browser cancellation returns unit");

    interval_id = emscripten_set_interval(poll_io, 5.0, NULL);
    emscripten_exit_with_live_runtime();
    return 0;
}
