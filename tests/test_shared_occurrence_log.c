#include "occurrence_log.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned fail_malloc, fail_calloc;
static void count_release(void *context);
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__wrap_malloc(size_t size) {
    if (fail_malloc && !--fail_malloc)
        return NULL;
    return __real_malloc(size);
}
void *__wrap_calloc(size_t count, size_t size) {
    if (fail_calloc && !--fail_calloc)
        return NULL;
    return __real_calloc(count, size);
}

static bool publish_constant(void *context, uint64_t position, const void *value) {
    (void)context;
    (void)position;
    (void)value;
    return true;
}

static bool refuse_publication(void *context, uint64_t position, const void *value) {
    (void)context;
    (void)position;
    (void)value;
    return false;
}

typedef struct {
    unsigned references;
    unsigned *reclaimed;
    int values[2];
} AnswerGraph;

static void release_graph(void *owner) {
    AnswerGraph *graph = owner;
    assert(graph->references);
    if (!--graph->references) {
        ++*graph->reclaimed;
        free(graph);
    }
}

static bool publish_graph(void *context, uint64_t position, const void *value) {
    AnswerGraph *graph = (AnswerGraph *)value;
    AnswerGraph **caller = context;
    (void)position;
    assert(graph->values[0] == 7 && graph->values[1] == 11);
    ++graph->references;
    *caller = graph;
    return true;
}

static void test_independent_ownership(void) {
    unsigned reclaimed = 0u;
    AnswerGraph *graph = malloc(sizeof(*graph));
    assert(graph);
    *graph = (AnswerGraph){.references = 2u, .reclaimed = &reclaimed, .values = {7, 11}};
    CettaOccurrenceLog *log = cetta_occurrence_log_new(2u, 2u);
    assert(log);
    CettaOccurrenceRow row = {.value = graph, .owner = graph, .release = release_graph};
    assert(cetta_occurrence_log_append(log, row) == CETTA_OCCURRENCE_APPENDED);
    assert(cetta_occurrence_log_append(log, row) == CETTA_OCCURRENCE_APPENDED);
    CettaOccurrenceReply refused = cetta_occurrence_log_take(log, 0u, refuse_publication, NULL);
    assert(refused.kind == CETTA_OCCURRENCE_PUBLICATION_REFUSED);
    assert(cetta_occurrence_log_peek(log, 0u).position == 0u);
    assert(cetta_occurrence_log_peek(log, 1u).position == 0u);
    AnswerGraph *first = NULL, *second = NULL, *third = NULL;
    CettaOccurrenceReply a = cetta_occurrence_log_take(log, 0u, publish_graph, &first);
    assert(a.kind == CETTA_OCCURRENCE_ANSWER && a.position == 0u);
    assert(cetta_occurrence_log_peek(log, 1u).position == 0u);
    assert(cetta_occurrence_log_cancel(log, 0u));
    assert(cetta_occurrence_log_trim(log) == 0u);
    CettaOccurrenceReply b = cetta_occurrence_log_take(log, 1u, publish_graph, &second);
    assert(b.kind == CETTA_OCCURRENCE_ANSWER && b.position == 0u && b.value == a.value);
    assert(cetta_occurrence_log_trim(log) == 1u);
    b = cetta_occurrence_log_take(log, 1u, publish_graph, &third);
    assert(b.kind == CETTA_OCCURRENCE_ANSWER && b.position == 1u && b.value == a.value);
    assert(cetta_occurrence_log_trim(log) == 1u);
    assert(cetta_occurrence_log_cancel(log, 1u));
    cetta_occurrence_log_free(log);
    assert(!reclaimed && first->values[0] == 7 && third->values[1] == 11);
    release_graph(first);
    release_graph(second);
    assert(!reclaimed);
    release_graph(third);
    assert(reclaimed == 1u);
}

static void test_retention_and_wrap(void) {
    CettaOccurrenceLog *log = cetta_occurrence_log_new(2u, 3u);
    assert(log && cetta_occurrence_log_valid(log));
    for (uintptr_t i = 0u; i < 3u; ++i) {
        assert(cetta_occurrence_log_append(log, (CettaOccurrenceRow){.value = (void *)(i % 2u)}) ==
               CETTA_OCCURRENCE_APPENDED);
    }
    for (uint64_t i = 0u; i < 3u; ++i) {
        CettaOccurrenceReply received = cetta_occurrence_log_take(log, 0u, publish_constant, NULL);
        assert(received.kind == CETTA_OCCURRENCE_ANSWER && received.position == i);
    }
    assert(cetta_occurrence_log_peek(log, 0u).kind == CETTA_OCCURRENCE_PAUSED);
    assert(cetta_occurrence_log_trim(log) == 0u);
    assert(cetta_occurrence_log_append(log, (CettaOccurrenceRow){.value = (void *)9}) ==
           CETTA_OCCURRENCE_BACKPRESSURE);
    assert(cetta_occurrence_log_cancel(log, 1u));
    assert(cetta_occurrence_log_trim(log) == 3u);
    for (uint64_t i = 3u; i < 10000u; ++i) {
        assert(cetta_occurrence_log_append(log, (CettaOccurrenceRow){.value = (void *)7}) ==
               CETTA_OCCURRENCE_APPENDED);
        CettaOccurrenceReply received = cetta_occurrence_log_take(log, 0u, publish_constant, NULL);
        assert(received.kind == CETTA_OCCURRENCE_ANSWER && received.position == i);
        assert(cetta_occurrence_log_trim(log) == 1u);
        assert(cetta_occurrence_log_valid(log));
    }
    cetta_occurrence_log_complete(log);
    assert(cetta_occurrence_log_peek(log, 0u).kind == CETTA_OCCURRENCE_COMPLETED);
    assert(cetta_occurrence_log_peek(log, 1u).kind == CETTA_OCCURRENCE_CANCELLED);
    assert(cetta_occurrence_log_append(log, (CettaOccurrenceRow){0}) == CETTA_OCCURRENCE_APPEND_COMPLETED);
    cetta_occurrence_log_free(log);
    log = cetta_occurrence_log_new(0u, 2u);
    assert(log);
    assert(cetta_occurrence_log_append(log, (CettaOccurrenceRow){0}) == CETTA_OCCURRENCE_APPENDED);
    assert(cetta_occurrence_log_trim(log) == 1u);
    assert(cetta_occurrence_log_valid(log));
    cetta_occurrence_log_free(log);
    log = cetta_occurrence_log_new(1u, 0u);
    assert(log);
    assert(cetta_occurrence_log_append(log, (CettaOccurrenceRow){0}) == CETTA_OCCURRENCE_BACKPRESSURE);
    assert(cetta_occurrence_log_peek(log, 1u).kind == CETTA_OCCURRENCE_INVALID);
    assert(!cetta_occurrence_log_cancel(log, 1u));
    cetta_occurrence_log_free(log);
    assert(!cetta_occurrence_log_new(SIZE_MAX, 1u));
    assert(!cetta_occurrence_log_valid(NULL));
}

typedef struct {
    CettaOccurrenceLog *log;
    uint64_t base;
    size_t rows;
    unsigned calls;
    bool completed;
} ReleaseView;

static bool inspect_publication(void *context, uint64_t position, const void *value) {
    CettaOccurrenceLog *log = context;
    uint64_t before;
    assert(cetta_occurrence_log_position(log, 0u, &before) && before == position);
    CettaOccurrenceReply unread = cetta_occurrence_log_peek(log, 0u);
    assert(unread.kind == CETTA_OCCURRENCE_ANSWER && unread.position == position && unread.value == value);
    return true;
}

static void inspect_release(void *context) {
    ReleaseView *view = context;
    CettaOccurrenceLogInfo info;
    assert(cetta_occurrence_log_valid(view->log));
    assert(cetta_occurrence_log_info(view->log, &info));
    assert(info.base == view->base && info.rows == view->rows && info.completed == view->completed);
    for (size_t i = 0u; i < info.rows; ++i) {
        CettaOccurrenceRow row;
        assert(cetta_occurrence_log_row(view->log, i, &row));
        assert((uintptr_t)row.value >= 11u);
    }
    ++view->calls;
}

static void test_callback_views(void) {
    CettaOccurrenceLog *log = cetta_occurrence_log_new(2u, 3u);
    assert(log);
    ReleaseView view = {.log = log, .base = 1u, .rows = 2u};
    for (uintptr_t value = 10u; value < 13u; ++value)
        assert(cetta_occurrence_log_append(log, (CettaOccurrenceRow){
            .value = (void *)value, .owner = &view, .release = inspect_release}) == CETTA_OCCURRENCE_APPENDED);
    assert(cetta_occurrence_log_take(log, 0u, inspect_publication, log).kind == CETTA_OCCURRENCE_ANSWER);
    assert(cetta_occurrence_log_take(log, 0u, publish_constant, NULL).kind == CETTA_OCCURRENCE_ANSWER);
    assert(cetta_occurrence_log_take(log, 1u, publish_constant, NULL).kind == CETTA_OCCURRENCE_ANSWER);
    assert(cetta_occurrence_log_trim(log) == 1u && view.calls == 1u);
    view.base = 3u;
    view.rows = 0u;
    view.completed = true;
    cetta_occurrence_log_free(log);
    assert(view.calls == 3u);
}

static void test_allocation_refusals(void) {
    fail_calloc = 1u;
    assert(!cetta_occurrence_log_new(2u, 8u));
    fail_calloc = 2u;
    assert(!cetta_occurrence_log_new(2u, 8u));
    CettaOccurrenceLog *log = cetta_occurrence_log_new(1u, 20u);
    assert(log);
    uint64_t releases = 0u;
    CettaOccurrenceRow pending = {.value = (void *)7, .owner = &releases, .release = count_release};
    fail_malloc = 1u;
    assert(cetta_occurrence_log_append(log, pending) == CETTA_OCCURRENCE_CAPACITY);
    assert(!releases && cetta_occurrence_log_peek(log, 0u).kind == CETTA_OCCURRENCE_PAUSED);
    assert(cetta_occurrence_log_valid(log));
    assert(cetta_occurrence_log_append(log, pending) == CETTA_OCCURRENCE_APPENDED);
    cetta_occurrence_log_free(log);
    assert(releases == 1u);
    log = cetta_occurrence_log_new(1u, 20u);
    assert(log);
    for (uintptr_t i = 0u; i < 8u; ++i)
        assert(cetta_occurrence_log_append(log, (CettaOccurrenceRow){.value = (void *)i}) == CETTA_OCCURRENCE_APPENDED);
    for (size_t i = 0u; i < 5u; ++i)
        assert(cetta_occurrence_log_take(log, 0u, publish_constant, NULL).kind == CETTA_OCCURRENCE_ANSWER);
    assert(cetta_occurrence_log_trim(log) == 5u);
    for (uintptr_t i = 8u; i < 13u; ++i)
        assert(cetta_occurrence_log_append(log, (CettaOccurrenceRow){.value = (void *)i}) == CETTA_OCCURRENCE_APPENDED);
    fail_malloc = 1u;
    assert(cetta_occurrence_log_append(log, (CettaOccurrenceRow){.value = (void *)13}) == CETTA_OCCURRENCE_CAPACITY);
    assert(cetta_occurrence_log_valid(log));
    CettaOccurrenceLogInfo info;
    assert(cetta_occurrence_log_info(log, &info));
    assert(info.base == 5u && info.rows == 8u && info.reserved_rows == 8u);
    for (size_t i = 0u; i < 8u; ++i) {
        CettaOccurrenceRow row;
        assert(cetta_occurrence_log_row(log, i, &row) && (uintptr_t)row.value == i + 5u);
    }
    assert(cetta_occurrence_log_append(log, (CettaOccurrenceRow){.value = (void *)13}) == CETTA_OCCURRENCE_APPENDED);
    for (uint64_t i = 5u; i <= 13u; ++i) {
        CettaOccurrenceReply received = cetta_occurrence_log_take(log, 0u, publish_constant, NULL);
        assert(received.kind == CETTA_OCCURRENCE_ANSWER && received.position == i && (uintptr_t)received.value == i);
    }
    cetta_occurrence_log_free(log);
}

static void print_state(const CettaOccurrenceLog *log) {
    CettaOccurrenceLogInfo info;
    assert(cetta_occurrence_log_info(log, &info));
    printf("{\"base\":%" PRIu64 ",\"rows\":[", info.base);
    for (size_t i = 0u; i < info.rows; ++i) {
        CettaOccurrenceRow row;
        assert(cetta_occurrence_log_row(log, i, &row));
        printf("%s%" PRIuPTR, i ? "," : "", (uintptr_t)row.value);
    }
    fputs("],\"positions\":[", stdout);
    for (size_t i = 0u; i < info.consumers; ++i) {
        uint64_t position;
        if (i)
            putchar(',');
        if (cetta_occurrence_log_position(log, i, &position))
            printf("%" PRIu64, position);
        else
            fputs("null", stdout);
    }
    printf("],\"completed\":%s,\"limit\":%zu}", info.completed ? "true" : "false", info.limit);
}

static const char *reply_name(CettaOccurrenceReplyKind kind) {
    switch (kind) {
    case CETTA_OCCURRENCE_ANSWER: return "answer";
    case CETTA_OCCURRENCE_PAUSED: return "paused";
    case CETTA_OCCURRENCE_COMPLETED: return "completed";
    case CETTA_OCCURRENCE_CANCELLED: return "cancelled";
    case CETTA_OCCURRENCE_INVALID: return "invalid";
    case CETTA_OCCURRENCE_PUBLICATION_REFUSED: return "publication-refused";
    }
    abort();
}

static bool parse_number(const char *text, uint64_t *value) {
    if (!text || !isdigit((unsigned char)text[0]))
        return false;
    errno = 0;
    char *end = NULL;
    uintmax_t parsed = strtoumax(text, &end, 10);
    if (errno || !end || *end || parsed > UINT64_MAX)
        return false;
    *value = (uint64_t)parsed;
    return true;
}

static int replay(void) {
    char line[256];
    CettaOccurrenceLog *log = NULL;
    size_t line_number = 0u;
    while (fgets(line, sizeof(line), stdin)) {
        ++line_number;
        if (!strchr(line, '\n') && !feof(stdin))
            goto invalid;
        char *op = strtok(line, " \t\r\n");
        char *first = strtok(NULL, " \t\r\n");
        char *second = strtok(NULL, " \t\r\n");
        char *extra = strtok(NULL, " \t\r\n");
        uint64_t a = 0u, b = 0u;
        const char *kind = NULL;
        CettaOccurrenceReply received = {0};
        bool answer = false;
        if (!op || extra || (first && !parse_number(first, &a)) ||
            (second && !parse_number(second, &b)))
            goto invalid;
        if (!strcmp(op, "init")) {
            if (log || !first || !second || a > SIZE_MAX || b > SIZE_MAX)
                goto invalid;
            log = cetta_occurrence_log_new((size_t)a, (size_t)b);
            if (!log)
                goto invalid;
            kind = "initialized";
        } else {
            if (!log || second)
                goto invalid;
            if (!strcmp(op, "append")) {
                if (!first || a > UINTPTR_MAX)
                    goto invalid;
                CettaOccurrenceAdmission added = cetta_occurrence_log_append(
                    log, (CettaOccurrenceRow){.value = (void *)(uintptr_t)a});
                switch (added) {
                case CETTA_OCCURRENCE_APPENDED: kind = "appended"; break;
                case CETTA_OCCURRENCE_BACKPRESSURE: kind = "backpressure"; break;
                case CETTA_OCCURRENCE_APPEND_COMPLETED: kind = "completed"; break;
                case CETTA_OCCURRENCE_CAPACITY: kind = "capacity"; break;
                case CETTA_OCCURRENCE_APPEND_INVALID: goto invalid;
                }
            } else if (!strcmp(op, "peek") || !strcmp(op, "pull") || !strcmp(op, "cancel")) {
                CettaOccurrenceLogInfo info;
                assert(cetta_occurrence_log_info(log, &info));
                if (!first || a >= info.consumers)
                    goto invalid;
                if (!strcmp(op, "cancel")) {
                    assert(cetta_occurrence_log_cancel(log, (size_t)a));
                    kind = "cancelled";
                } else {
                    received = !strcmp(op, "peek") ? cetta_occurrence_log_peek(log, (size_t)a)
                        : cetta_occurrence_log_take(log, (size_t)a, publish_constant, NULL);
                    kind = reply_name(received.kind);
                    answer = received.kind == CETTA_OCCURRENCE_ANSWER;
                }
            } else if (!strcmp(op, "trim")) {
                if (first)
                    goto invalid;
                (void)cetta_occurrence_log_trim(log);
                kind = "trimmed";
            } else if (!strcmp(op, "complete")) {
                if (first)
                    goto invalid;
                cetta_occurrence_log_complete(log);
                kind = "completed";
            } else {
                goto invalid;
            }
        }
        assert(cetta_occurrence_log_valid(log));
        printf("{\"reply\":{\"kind\":\"%s\"", kind);
        if (answer)
            printf(",\"position\":%" PRIu64 ",\"value\":%" PRIuPTR, received.position, (uintptr_t)received.value);
        fputs("},\"state\":", stdout);
        print_state(log);
        fputs("}\n", stdout);
    }
    if (!log || ferror(stdin))
        goto invalid;
    cetta_occurrence_log_free(log);
    return 0;
invalid:
    fprintf(stderr, "invalid occurrence-log command at line %zu\n", line_number);
    cetta_occurrence_log_free(log);
    return 2;
}

typedef struct { uint64_t next; } DeliveryCount;
static bool count_delivery(void *context, uint64_t position, const void *value) {
    DeliveryCount *count = context;
    assert(position == count->next);
    assert((uintptr_t)value == position % 5u);
    ++count->next;
    return true;
}
static void count_release(void *context) {
    ++*(uint64_t *)context;
}

static int bench(uint64_t count) {
    CettaOccurrenceLog *log = cetta_occurrence_log_new(2u, 128u);
    assert(log);
    DeliveryCount first = {0}, second = {0};
    uint64_t releases = 0u;
    size_t maximum = 0u, reserved = 0u;
    clock_t started = clock();
    for (uint64_t i = 0u; i < count; ++i) {
        assert(cetta_occurrence_log_append(log, (CettaOccurrenceRow){
            .value = (void *)(uintptr_t)(i % 5u), .owner = &releases, .release = count_release}) ==
            CETTA_OCCURRENCE_APPENDED);
        CettaOccurrenceLogInfo info;
        assert(cetta_occurrence_log_info(log, &info));
        if (info.rows > maximum) maximum = info.rows;
        if (info.reserved_rows > reserved) reserved = info.reserved_rows;
        assert(cetta_occurrence_log_take(log, 0u, count_delivery, &first).kind == CETTA_OCCURRENCE_ANSWER);
        assert(cetta_occurrence_log_take(log, 1u, count_delivery, &second).kind == CETTA_OCCURRENCE_ANSWER);
        assert(cetta_occurrence_log_trim(log) == 1u);
    }
    double seconds = (double)(clock() - started) / CLOCKS_PER_SEC;
    assert(first.next == count && second.next == count && releases == count);
    printf("{\"rows\":%" PRIu64 ",\"delivered\":%" PRIu64 ",\"released\":%" PRIu64
           ",\"maximum_retained\":%zu,\"maximum_reserved\":%zu,\"cpu_seconds\":%.6f}\n",
           count, first.next + second.next, releases, maximum, reserved, seconds);
    cetta_occurrence_log_free(log);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--replay"))
        return replay();
    uint64_t count;
    if (argc == 3 && !strcmp(argv[1], "--bench") && parse_number(argv[2], &count) &&
        count <= UINT64_MAX / 2u)
        return bench(count);
    if (argc != 1)
        return 2;
    test_independent_ownership();
    test_retention_and_wrap();
    test_callback_views();
    test_allocation_refusals();
    puts("shared occurrence log: independent delivery, ownership, refusal and retention controls passed");
    return 0;
}
