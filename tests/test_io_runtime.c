#define _POSIX_C_SOURCE 200809L

#include "atom.h"
#include "library_io.h"
#include "symbol.h"
#include "host_notice.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>
#include <fcntl.h>

static void *publish_later(void *unused) {
    (void)unused;
    struct timespec delay = {0, 30000000L};
    nanosleep(&delay, NULL);
    cetta_host_notice_publish();
    return NULL;
}

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

static void test_native_notifications(CettaIoRuntime *runtime, Arena *arena) {
    Atom *timer = atom_expr2(arena, atom_symbol(arena, "timer:after"), atom_int(arena, 0));
    Atom *foreign = dispatch(runtime, arena, "__cetta_lib_io_submit", &timer, 1);
    Atom *notice = atom_expr(arena, (Atom *[]){atom_symbol(arena, "host:notice")}, 1);
    Atom *pending = dispatch(runtime, arena, "__cetta_lib_io_submit", &notice, 1);
    CHECK(!atom_is_error(pending), "native host notification subscription starts");
    Atom *id = pending->expr.elems[1];
    Atom *selected = atom_expr2(arena, atom_symbol(arena, "io:ids"), id);
    Atom *wait_args[] = {selected, atom_int(arena, 5)};
    Atom *ready = dispatch(runtime, arena, "__cetta_lib_io_wait_ready", wait_args, 2);
    CHECK(atom_is_symbol(ready->expr.elems[0], "io:idle"), "unrelated ready timer cannot spin selected wait");
    pthread_t publisher;
    CHECK(!pthread_create(&publisher, NULL, publish_later, NULL), "foreign notifier thread starts");
    wait_args[1] = atom_int(arena, 2000);
    ready = dispatch(runtime, arena, "__cetta_lib_io_wait_ready", wait_args, 2);
    pthread_join(publisher, NULL);
    CHECK(atom_is_symbol(ready->expr.elems[0], "io:ready"), "host notification wakes blocked native wait");
    ready = dispatch(runtime, arena, "__cetta_lib_io_wait_ready", wait_args, 2);
    CHECK(atom_is_symbol(ready->expr.elems[0], "io:ready"), "readiness notification remains unconsumed");
    Atom *event = dispatch(runtime, arena, "__cetta_lib_io_poll", &id, 1);
    CHECK(atom_is_symbol(event->expr.elems[2]->expr.elems[0], "host:noticed"), "owned host notice has a result");
    id = foreign->expr.elems[1];
    event = dispatch(runtime, arena, "__cetta_lib_io_poll", &id, 1);
    CHECK(atom_is_symbol(event->expr.elems[2]->expr.elems[0], "timer:fired"), "foreign completion survives selected wait");
#ifdef __linux__
    char directory[] = "/tmp/cetta-file-notice-XXXXXX";
    CHECK(mkdtemp(directory) != NULL, "file notification directory exists");
    Atom *watch = atom_expr(arena, (Atom *[]){atom_symbol(arena, "fs:changed"),
        atom_string(arena, directory), atom_expr(arena, (Atom *[]){atom_string(arena, "wake")}, 1)}, 3);
    pending = dispatch(runtime, arena, "__cetta_lib_io_submit", &watch, 1);
    CHECK(!atom_is_error(pending), "filtered directory notification starts");
    id = pending->expr.elems[1];
    char file[256], renamed[256];
    snprintf(file, sizeof(file), "%s/progress", directory);
    FILE *f = fopen(file, "w"); if (f) { fputs("heartbeat", f); fclose(f); }
    Atom *file_args[] = {id, atom_int(arena, 5)};
    ready = dispatch(runtime, arena, "__cetta_lib_io_wait_ready", file_args, 2);
    CHECK(atom_is_symbol(ready->expr.elems[0], "io:idle"), "unselected progress writes do not wake control wait");
    snprintf(renamed, sizeof(renamed), "%s/wake", directory); rename(file, renamed);
    file_args[1] = atom_int(arena, 1000);
    ready = dispatch(runtime, arena, "__cetta_lib_io_wait_ready", file_args, 2);
    CHECK(atom_is_symbol(ready->expr.elems[0], "io:ready"), "atomic control rename wakes native wait");
    event = dispatch(runtime, arena, "__cetta_lib_io_poll", &id, 1);
    CHECK(atom_is_symbol(event->expr.elems[2]->expr.elems[0], "fs:changed"), "file notification has explicit result");
    unlink(renamed); rmdir(directory);
#endif
    int pipefd[2]; CHECK(!pipe(pipefd), "notification lifecycle fixture opens");
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    CettaHostNoticeRegistration *registration = cetta_host_notice_register(pipefd[1]);
    cetta_host_notice_unregister(registration);
    cetta_host_notice_publish(); char byte;
    CHECK(read(pipefd[0], &byte, 1) < 0 && errno == EAGAIN, "unregistered descriptor never receives later publication");
    close(pipefd[0]); close(pipefd[1]);
}

static void test_process_owner(CettaIoRuntime *runtime, Arena *arena) {
    char directory[] = "/tmp/cetta-process-owner-XXXXXX";
    CHECK(mkdtemp(directory) != NULL, "process fixture directory exists");
    char output[256];
    snprintf(output, sizeof(output), "%s/output", directory);
    Atom *request = atom_expr(arena, (Atom *[]){
        atom_symbol(arena, "proc:request"),
        atom_expr(arena, (Atom *[]){ atom_string(arena, "/bin/sh"),
            atom_string(arena, "-c"), atom_string(arena, "sleep 5") }, 3),
        atom_string(arena, "/"), atom_symbol(arena, "inherit"), atom_int(arena, 0),
        atom_string(arena, output), atom_string(arena, output) }, 7);
    Atom *timed = atom_expr(arena, (Atom *[]){
        atom_symbol(arena, "proc:request"),
        atom_expr(arena, (Atom *[]){ atom_string(arena, "/bin/sh"),
            atom_string(arena, "-c"), atom_string(arena, "printf early; sleep .15; printf late") }, 3),
        atom_string(arena, "/"), atom_symbol(arena, "inherit"), atom_int(arena, 50),
        atom_string(arena, output), atom_string(arena, output) }, 7);
    Atom *pending = dispatch(runtime, arena, "__cetta_lib_io_submit", &timed, 1);
    CHECK(pending && pending->kind == ATOM_EXPR && pending->expr.len == 2,
          "busy evaluator deadline candidate submitted");
    if (pending && pending->kind == ATOM_EXPR && pending->expr.len == 2) {
        struct timespec pause = {0, 300000000L};
        nanosleep(&pause, NULL);
        Atom *id = pending->expr.elems[1];
        Atom *event = dispatch(runtime, arena, "__cetta_lib_io_poll", &id, 1);
        Atom *result = event && event->kind == ATOM_EXPR && event->expr.len == 3 ? event->expr.elems[2] : NULL;
        CHECK(result && result->kind == ATOM_EXPR && result->expr.len == 6 &&
              atom_is_symbol(result->expr.elems[2], "timed-out"), "deadline survives unavailable evaluator");
        FILE *file = fopen(output, "r"); char bytes[32] = {0};
        size_t size = file ? fread(bytes, 1, sizeof(bytes), file) : 0;
        if (file) fclose(file);
        CHECK(size == 5 && !memcmp(bytes, "early", 5), "deadline retains prefix and prevents late output");
    }
    for (unsigned i = 0; i < 100; ++i) {
        Atom *pending = dispatch(runtime, arena, "__cetta_lib_io_submit", &request, 1);
        CHECK(pending && pending->kind == ATOM_EXPR && pending->expr.len == 2 &&
              atom_is_symbol(pending->expr.elems[0], "io:pending"), "cancel candidate submitted");
        if (!pending || pending->kind != ATOM_EXPR || pending->expr.len != 2) break;
        Atom *id = pending->expr.elems[1];
        Atom *status = dispatch(runtime, arena, "__cetta_lib_io_status", &id, 1);
        CHECK(status && status->kind == ATOM_EXPR && status->expr.len == 5 &&
              atom_is_symbol(status->expr.elems[0], "proc:running"), "candidate is a live child");
        if (!status || status->kind != ATOM_EXPR || status->expr.len != 5) break;
        pid_t pid = (pid_t)status->expr.elems[1]->ground.ival;
        CHECK(!atom_is_error(dispatch(runtime, arena, "__cetta_lib_io_cancel", &id, 1)),
              "cancel returns without consuming internal child ownership");
        for (unsigned tries = 0; tries < 2000 && kill(pid, 0) == 0; ++tries)
            pause_one_ms();
        errno = 0;
        pid_t reaped = waitpid(pid, NULL, WNOHANG);
        CHECK(reaped == -1 && errno == ECHILD, "owner reaped cancellation while evaluator was blocked");
        /* Recover the fixture even if a regression leaves the child running. */
        if (reaped == 0) {
            kill(-pid, SIGKILL); kill(pid, SIGKILL);
            while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
        }
    }
    pid_t foreign = fork();
    if (!foreign) _exit(37);
    if (foreign > 0) {
        struct timespec pause = {0, 50000000L};
        nanosleep(&pause, NULL);
        int status = 0;
        CHECK(waitpid(foreign, &status, 0) == foreign && WIFEXITED(status) && WEXITSTATUS(status) == 37,
              "owner never steals another subsystem's child");
    } else CHECK(false, "foreign child fixture started");
    struct sigaction ignored = {.sa_handler = SIG_IGN}, previous;
    sigemptyset(&ignored.sa_mask);
    if (!sigaction(SIGCHLD, &ignored, &previous)) {
        CHECK(atom_is_error(dispatch(runtime, arena, "__cetta_lib_io_submit", &request, 1)),
              "host that discards exit status gets an explicit startup error");
        sigaction(SIGCHLD, &previous, NULL);
    } else CHECK(false, "SIGCHLD fixture installed");
    unlink(output); rmdir(directory);
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
    if (getenv("CETTA_IO_PROCESS_ONLY")) {
        test_process_owner(runtime, &arena);
        printf("(IoProcessSummary %u %u %u)\n", checks, checks - failures, failures);
        cetta_io_runtime_free(runtime);
        arena_free(&arena); symbol_table_free(&symbols); g_symbols = NULL;
        return failures == 0u ? 0 : 1;
    }

    Atom *capabilities = dispatch(runtime, &arena,
                                  "__cetta_lib_io_capabilities", NULL, 0u);
    CHECK(capabilities && capabilities->kind == ATOM_EXPR &&
              capabilities->expr.len == 7u &&
              atom_is_symbol(capabilities->expr.elems[0], "http") &&
              atom_is_symbol(capabilities->expr.elems[1], "proc") &&
              atom_is_symbol(capabilities->expr.elems[2], "timer"),
          "native providers advertise HTTP, processes and timers");

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

    char owned_slow_url[512];
    make_url(owned_slow_url, sizeof(owned_slow_url), argv[1], "/slow");
    Atom *foreign_request = http_request(&arena, one_url, 64);
    Atom *owned_request = http_request(&arena, owned_slow_url, 64);
    int64_t foreign_id = 0, owned_id = 0;
    CHECK(pending_id(dispatch(runtime, &arena, "__cetta_lib_io_submit",
                             &foreign_request, 1u), &foreign_id),
          "foreign completion candidate is admitted");
    CHECK(pending_id(dispatch(runtime, &arena, "__cetta_lib_io_submit",
                             &owned_request, 1u), &owned_id),
          "owned completion candidate is admitted");
    Atom *owned_wait_args[] = {atom_int(&arena, owned_id), atom_int(&arena, 5000)};
    Atom *owned_event = dispatch(runtime, &arena, "__cetta_lib_io_wait", owned_wait_args, 2u);
    int64_t owned_observed_id = 0;
    Atom *owned_result = NULL;
    CHECK(event_parts(owned_event, &owned_observed_id, &owned_result) &&
          owned_observed_id == owned_id && http_response(owned_result, 200, "slow"),
          "owned wait selects its request despite an earlier foreign completion");
    Atom *foreign_arg = atom_int(&arena, foreign_id);
    Atom *foreign_event = dispatch(runtime, &arena, "__cetta_lib_io_poll", &foreign_arg, 1u);
    int64_t foreign_observed_id = 0;
    Atom *foreign_result = NULL;
    CHECK(event_parts(foreign_event, &foreign_observed_id, &foreign_result) &&
          foreign_observed_id == foreign_id && http_response(foreign_result, 200, "one"),
          "owned wait leaves the foreign completion available to its owner");
    Atom *retired = dispatch(runtime, &arena, "__cetta_lib_io_poll", &foreign_arg, 1u);
    CHECK(retired && retired->kind == ATOM_EXPR && retired->expr.len == 2u &&
          atom_is_symbol(retired->expr.elems[0], "io:unknown"),
          "an already consumed owned handle is distinguishable from idle");
    Atom *invalid_id = atom_int(&arena, 0);
    Atom *invalid_poll = dispatch(runtime, &arena, "__cetta_lib_io_poll", &invalid_id, 1u);
    CHECK(invalid_poll && invalid_poll->kind == ATOM_EXPR &&
          atom_is_symbol(invalid_poll->expr.elems[0], "Error"),
          "owned poll rejects a zero handle");

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
    CHECK(atom_is_error(large_result) &&
              large_result->expr.elems[1]->kind == ATOM_EXPR &&
              large_result->expr.elems[1]->expr.len == 2u &&
              atom_is_symbol(large_result->expr.elems[1]->expr.elems[0], "io:request") &&
              large_result->expr.elems[1]->expr.elems[1]->ground.ival == large_id,
          "asynchronous errors expose only the correlation ID");
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
              http_response(redirect_result, 302, ""),
          "redirects are not followed by default");
    Atom *follow_items[8];
    memcpy(follow_items, redirect_request->expr.elems, 7 * sizeof(Atom *));
    follow_items[7] = atom_bool(&arena, true);
    redirect_request = atom_expr(&arena, follow_items, 8);
    redirect_result = submit_and_poll(runtime, &arena, redirect_request, &redirect_id);
    CHECK(http_response(redirect_result, 200, "one"),
          "explicit redirect opt-in reaches the final response");

    char json_url[512];
    make_url(json_url, sizeof(json_url), argv[1], "/json");
    int64_t json_id = 0;
    Atom *json_request = http_request(&arena, json_url, 64);
    Atom *json_result = submit_and_poll(runtime, &arena, json_request,
                                        &json_id);
    CHECK(json_id > redirect_id &&
              http_response(json_result, 200, "{\"x\":1,\"x\":2}"),
          "JSON response bytes retain duplicate-member source order");

    char echo_url[512];
    make_url(echo_url, sizeof(echo_url), argv[1], "/echo");
    int64_t echo_id = 0;
    Atom *echo_request = http_request_full(
        &arena, "POST", echo_url, "portable-body", 3000, 64);
    Atom *echo_result = submit_and_poll(runtime, &arena, echo_request,
                                        &echo_id);
    CHECK(echo_id > json_id &&
              http_response(echo_result, 200, "portable-body"),
          "POST body round-trips through the native provider");

    CHECK(idle(dispatch(runtime, &arena, "__cetta_lib_io_poll", NULL, 0u)),
          "conformance completions leave the queue empty");

    Atom *secret = http_request_full(&arena, "POST", "file:///FAKE-TOKEN",
                                     "FAKE-BODY", 1000, 64);
    Atom *refused = dispatch(runtime, &arena, "__cetta_lib_io_submit", &secret, 1u);
    CHECK(atom_is_error(refused) && refused->expr.elems[1]->kind == ATOM_EXPR &&
              refused->expr.elems[1]->expr.len == 1u &&
              atom_is_symbol(refused->expr.elems[1]->expr.elems[0], "io:submit"),
          "validation errors omit token-bearing arguments");

    Atom *wait_request = http_request(&arena, slow_url, 64);
    int64_t wait_id = 0;
    CHECK(pending_id(dispatch(runtime, &arena, "__cetta_lib_io_submit", &wait_request, 1u), &wait_id),
          "blocking wait candidate is admitted");
    Atom *timeout = atom_int(&arena, 5000);
    Atom *wait_event = dispatch(runtime, &arena, "__cetta_lib_io_wait", &timeout, 1u);
    Atom *wait_result = NULL;
    int64_t event_id = 0;
    CHECK(event_parts(wait_event, &event_id, &wait_result) && event_id == wait_id &&
              http_response(wait_result, 200, "slow"),
          "blocking wait consumes the independent owner's completion");
    timeout = atom_int(&arena, 0);
    CHECK(idle(dispatch(runtime, &arena, "__cetta_lib_io_wait", &timeout, 1u)),
          "zero-time wait consumes nothing after completion");

    test_native_notifications(runtime, &arena);
    test_process_owner(runtime, &arena);
    printf("(IoRuntimeSummary %u %u %u)\n",
           checks, checks - failures, failures);
    cetta_io_runtime_free(runtime);
    arena_free(&arena);
    symbol_table_free(&symbols);
    g_symbols = NULL;
    return failures == 0u ? 0 : 1;
}
