#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Emit structured collector inputs and independently constructed expected
 * observations. This is not an implementation of the tested index. */
static uint32_t ordered_key(uint32_t position, uint32_t count, const char *order) {
    if (strcmp(order, "descending") == 0)
        return count - 1u - position;
    if (strcmp(order, "alternating") == 0)
        return position % 2u == 0u
            ? position / 2u : count - 1u - position / 2u;
    return position;
}

static void key(uint32_t value) {
    /* Shared prefix, then decimal digits; exercise lexicographic prefix keys. */
    char digits[16];
    int length = snprintf(digits, sizeof(digits), "%u", value);
    fputs("(bnf-v1:text-cons 114 ", stdout);
    for (int i = 0; i < length; i++)
        printf("(bnf-v1:text-cons %u ", (unsigned char)digits[i]);
    fputs("(bnf-v1:text-nil)", stdout);
    for (int i = 0; i <= length; i++) putchar(')');
}

static void definition(uint32_t value, uint32_t occurrence) {
    fputs("(BNFDefinitionV1 ", stdout);
    key(value);
    printf(" (IndexExpression %u) (IndexSpan %u))", value, occurrence);
}

static void entries(uint32_t count, const char *order) {
    for (uint32_t i = 0; i < count; i++) {
        uint32_t value = ordered_key(i, count, order);
        fputs("(bnf-v1:entries-cons (bnf-v1:rule ", stdout);
        key(value);
        printf(" (IndexExpression %u) (IndexSpan %u)) ", value, i);
    }
    fputs("(bnf-v1:entries-nil)", stdout);
    for (uint32_t i = 0; i < count; i++) putchar(')');
}

static void expected(uint32_t count, const char *order) {
    for (uint32_t i = 0; i < count; i++) {
        fputs("(BNFDefinitionsConsV1 ", stdout);
        definition(ordered_key(i, count, order), i);
        putchar(' ');
    }
    fputs("BNFDefinitionsNilV1", stdout);
    for (uint32_t i = 0; i < count; i++) putchar(')');
}

int main(int argc, char **argv) {
    char *end = NULL;
    unsigned long parsed;
    uint32_t count;
    const char *order;
    const char *mode;
    const char *relation;
    if (argc != 4) {
        fprintf(stderr, "usage: %s COUNT ascending|descending|alternating indexed|reference|compare\n", argv[0]);
        return 2;
    }
    errno = 0;
    parsed = strtoul(argv[1], &end, 10);
    if (errno || !argv[1][0] || argv[1][0] == '-' || *end ||
        parsed == 0u || parsed > UINT32_MAX) {
        fputs("COUNT must be a positive 32-bit integer\n", stderr);
        return 2;
    }
    count = (uint32_t)parsed;
    order = argv[2];
    mode = argv[3];
    if (strcmp(order, "ascending") && strcmp(order, "descending") &&
        strcmp(order, "alternating")) {
        fputs("unknown ordering\n", stderr);
        return 2;
    }
    if (strcmp(mode, "indexed") && strcmp(mode, "reference") &&
        strcmp(mode, "compare")) {
        fputs("unknown collector mode\n", stderr);
        return 2;
    }
    relation = strcmp(mode, "reference") == 0
        ? "BNFCollectDefinitionsV1" : "BNFIndexedCollectDefinitionsV1";
    puts("; Structured collector benchmark: no grammar parser or graph closure.");
    puts("; Load exactly one generated comparison composition first.");
    fputs("(= (index-bench:entries) ", stdout);
    entries(count, order);
    puts(")");
    fputs("(= (index-bench:expected) (IndexCollected ", stdout);
    expected(count, order);
    puts(" BNFDiagnosticsNilV1))");
    printf("(= (index-bench:run $entries)\n"
           "   (let (gslt:result:%s:1100 $definitions $diagnostics)\n"
           "        (gslt:entry:%s:1100 $entries BNFDefinitionsNilV1)\n"
           "     (IndexCollected $definitions $diagnostics)))\n",
           relation, relation);
    if (strcmp(mode, "compare") == 0) {
        puts("(= (index-bench:reference $entries)\n"
             "   (let (gslt:result:BNFCollectDefinitionsV1:1100 $definitions $diagnostics)\n"
             "        (gslt:entry:BNFCollectDefinitionsV1:1100 $entries BNFDefinitionsNilV1)\n"
             "     (IndexCollected $definitions $diagnostics)))");
    }
    puts("!(let $results (collapse (index-bench:run (index-bench:entries)))");
    puts("   (case $results");
    printf("     ((($actual) (if (== $actual (index-bench:expected))\n");
    if (strcmp(mode, "compare") == 0) {
        printf("                    (if (== $results (collapse (index-bench:reference (index-bench:entries))))\n"
               "                      (IndexCollectorAccepted %u %s %s)\n"
               "                      (IndexCollectorReferenceMismatch %u))\n", count, order, mode, count);
    } else {
        printf("                    (IndexCollectorAccepted %u %s %s)\n", count, order, mode);
    }
    puts("                    (IndexCollectorValueMismatch $actual)))");
    puts("      ($other (IndexCollectorMultiplicityMismatch $other)))))");
    return ferror(stdout) ? 1 : 0;
}
