#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Source and independent printed-value expectation for a deep inert list.
 * No grammar loader or parser-plan representation participates in this test. */
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    char *end = NULL;
    errno = 0;
    unsigned long depth = strtoul(argv[1], &end, 10);
    if (errno || !*argv[1] || *end || argv[1][0] == '-') return 2;
    int source = strcmp(argv[2], "source") == 0;
    int bracketed = strcmp(argv[2], "bracketed") == 0;
    if (!source && !bracketed && strcmp(argv[2], "value") != 0) return 2;
    if (source) putchar('!');
    else if (bracketed) putchar('[');
    fputs("(list-value:box ", stdout);
    for (unsigned long i = 0; i < depth; ++i)
        printf("(LCons %lu ", i % 7);
    fputs("LNil", stdout);
    for (unsigned long i = 0; i < depth; ++i) putchar(')');
    putchar(')');
    if (bracketed) putchar(']');
    putchar('\n');
    return ferror(stdout) ? 1 : 0;
}
