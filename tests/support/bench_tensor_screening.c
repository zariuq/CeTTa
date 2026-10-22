#define _POSIX_C_SOURCE 200809L
#include "tensor_screening.h"
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <time.h>

static uint64_t now(clockid_t clock) {
    struct timespec t;
    if (clock_gettime(clock, &t)) abort();
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static size_t natural(const char *text) {
    errno = 0; char *end;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno || !*text || *text == '-' || *end || value > SIZE_MAX) abort();
    return (size_t)value;
}
static void require(CettaTensorStatus status) {
    if (status != CETTA_TENSOR_OK) {
        fprintf(stderr, "screening benchmark: %s\n", cetta_tensor_status_name(status)); exit(1);
    }
}
static double coefficient(size_t rows, size_t row, size_t col) {
    return (double)(rows - row) / 65536. +
        ((int)((row * 17 + col * 13) % 127) - 63) / 1024. +
        (double)((row + col) % 7) * 0x1p-45;
}
static void reference(CettaTensor *matrix, CettaTensor *query, size_t k,
    CettaTensor **indices, CettaTensor **ranked, CettaTensor **all) {
    require(cetta_tensor_matvec(matrix, query, CETTA_TENSOR_SEQUENTIAL, all));
    require(cetta_tensor_top_k(*all, NULL, k, indices));
    require(cetta_tensor_gather(*all, *indices, ranked));
}
static void write_tensor(CettaTensor *tensor, const char *path) {
    unsigned char *bytes = NULL; size_t size;
    require(cetta_tensor_encode(tensor, &bytes, &size));
    FILE *file = fopen(path, "wb");
    if (!file || fwrite(bytes, 1, size, file) != size || fclose(file)) abort();
    free(bytes);
}
static int order_u64(const void *left, const void *right) {
    uint64_t a = *(const uint64_t *)left, b = *(const uint64_t *)right;
    return (a > b) - (a < b);
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "usage: bench_tensor_screening rows columns k repetitions full-scores.ctn\n");
        return 2;
    }
    size_t rows = natural(argv[1]), columns = natural(argv[2]), k = natural(argv[3]);
    size_t repetitions = natural(argv[4]), shape[] = {rows, columns};
    if (!rows || !columns || !repetitions || repetitions > SIZE_MAX / sizeof(uint64_t)) return 2;
    size_t count, bytes;
    require(cetta_tensor_byte_count(CETTA_TENSOR_F64, shape, 2, &count, &bytes));
    if (columns > SIZE_MAX / sizeof(double)) return 2;
    uint64_t begin = now(CLOCK_MONOTONIC);
    double *matrix_values = malloc(bytes), *query_values = malloc(columns * sizeof(double));
    if (!matrix_values || !query_values) return 1;
    for (size_t row = 0; row < rows; row++)
        for (size_t col = 0; col < columns; col++)
            matrix_values[row * columns + col] = coefficient(rows, row, col);
    for (size_t col = 0; col < columns; col++) query_values[col] = 1. / (double)columns;
    CettaTensor *matrix = NULL, *query = NULL;
    require(cetta_tensor_new_take(CETTA_TENSOR_F64, shape, 2, matrix_values, bytes, &matrix));
    require(cetta_tensor_new_take(CETTA_TENSOR_F64, &columns, 1, query_values,
        columns * sizeof(*query_values), &query));
    uint64_t loaded = now(CLOCK_MONOTONIC);
    CettaTensorScreenIndex *index = NULL;
    require(cetta_tensor_screen_index(matrix, &index));
    uint64_t prepared = now(CLOCK_MONOTONIC);
    CettaTensor *expected_ids = NULL, *expected_scores = NULL, *all_scores = NULL;
    reference(matrix, query, k, &expected_ids, &expected_scores, &all_scores);
    write_tensor(all_scores, argv[5]);
    cetta_tensor_release(all_scores);
    uint64_t *full_times = malloc(repetitions * sizeof(*full_times));
    uint64_t *screen_times = malloc(repetitions * sizeof(*screen_times));
    uint64_t *refine_times = malloc(repetitions * sizeof(*refine_times));
    uint64_t *combined_times = malloc(repetitions * sizeof(*combined_times));
    uint64_t *screen_cpu = malloc(repetitions * sizeof(*screen_cpu));
    if (!full_times || !screen_times || !refine_times || !combined_times || !screen_cpu) return 1;
    size_t candidates = 0, receipt_bound = 0;
    for (size_t trial = 0; trial < repetitions; trial++) {
        for (unsigned step = 0; step < 2; step++) {
            bool full = (step == 0) == (trial % 2 == 0);
            CettaTensor *ids = NULL, *scores = NULL, *all = NULL;
            uint64_t started = now(CLOCK_MONOTONIC), cpu_started = now(CLOCK_PROCESS_CPUTIME_ID);
            if (full) {
                reference(matrix, query, k, &ids, &scores, &all);
                full_times[trial] = now(CLOCK_MONOTONIC) - started;
            } else {
                CettaTensorScreenReceipt *receipt = NULL;
                require(cetta_tensor_screen(index, query, NULL, k, &receipt));
                uint64_t screened = now(CLOCK_MONOTONIC);
                require(cetta_tensor_screen_rerank(receipt, &ids, &scores));
                screen_times[trial] = screened - started;
                refine_times[trial] = now(CLOCK_MONOTONIC) - screened;
                combined_times[trial] = now(CLOCK_MONOTONIC) - started;
                screen_cpu[trial] = now(CLOCK_PROCESS_CPUTIME_ID) - cpu_started;
                candidates = cetta_tensor_count(cetta_tensor_screen_candidates(receipt));
                receipt_bound = cetta_tensor_screen_retained_bound(receipt);
                cetta_tensor_screen_receipt_release(receipt);
            }
            if (!cetta_tensor_equal(ids, expected_ids) || !cetta_tensor_equal(scores, expected_scores)) {
                fprintf(stderr, "unmatched screening/reranking observation\n"); return 1;
            }
            cetta_tensor_release(ids); cetta_tensor_release(scores); cetta_tensor_release(all);
        }
    }
    qsort(full_times, repetitions, sizeof(*full_times), order_u64);
    qsort(screen_times, repetitions, sizeof(*screen_times), order_u64);
    qsort(refine_times, repetitions, sizeof(*refine_times), order_u64);
    qsort(combined_times, repetitions, sizeof(*combined_times), order_u64);
    qsort(screen_cpu, repetitions, sizeof(*screen_cpu), order_u64);
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage)) abort();
    printf("{\"rows\":%zu,\"columns\":%zu,\"k\":%zu,\"repetitions\":%zu,"
        "\"load_ns\":%" PRIu64 ",\"prepare_ns\":%" PRIu64 ",\"full_ns\":%" PRIu64 ","
        "\"screen_ns\":%" PRIu64 ",\"rerank_ns\":%" PRIu64 ",\"screen_rerank_ns\":%" PRIu64 ",\"screen_rerank_cpu_ns\":%" PRIu64 ","
        "\"candidates\":%zu,\"index_retained_upper_bound\":%zu,"
        "\"receipt_retained_upper_bound\":%zu,\"peak_rss_kib\":%ld,\"top_k\":[",
        rows, columns, k, repetitions, loaded - begin, prepared - loaded,
        full_times[repetitions / 2], screen_times[repetitions / 2], refine_times[repetitions / 2],
        combined_times[repetitions / 2], screen_cpu[repetitions / 2], candidates, cetta_tensor_screen_index_retained_bound(index),
        receipt_bound, usage.ru_maxrss);
    for (size_t i = 0; i < cetta_tensor_count(expected_ids); i++) {
        bool present; CettaTensorScalar value;
        require(cetta_tensor_get(expected_ids, i, &present, &value));
        if (!present) abort();
        printf("%s%" PRIu64, i ? "," : "", value.index);
    }
    puts("]}");
    cetta_tensor_release(expected_ids); cetta_tensor_release(expected_scores);
    cetta_tensor_screen_index_release(index); cetta_tensor_release(matrix); cetta_tensor_release(query);
    free(full_times); free(screen_times); free(refine_times); free(combined_times); free(screen_cpu);
    return 0;
}
