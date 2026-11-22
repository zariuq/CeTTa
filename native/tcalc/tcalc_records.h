/* tcalc_records.h - the records the tcalc core keeps.  Private to the core
 * (tcalc_core.c) and to the generated list functions (tcalc_lists.generated.h). */
#ifndef CETTA_TCALC_RECORDS_H
#define CETTA_TCALC_RECORDS_H

#include <stdbool.h>
#include <stdint.h>

/* One term of the table.  The kind is a TcKind (tcalc_core.h). */
typedef struct {
    uint8_t kind;
    uint8_t ground;   /* mentions no local and no unknown                       */
    uint32_t length;  /* expression: items; symbol, string: bytes              */
    uint32_t start;   /* expression: first item; symbol, string: first byte    */
    int64_t value;    /* integer: the value; local, unknown: the index         */
    uint32_t head;    /* expression starting with a symbol: that symbol, else 0 */
    uint32_t hash;
} TcNode;

/* A rule as the program keeps it: its terms are consecutive in the program's
 * term list, in the order take, read, tell (two per equation), make, and its
 * checks are consecutive in the program's check list.  Its locals are numbered
 * by first appearance, so two rules equal up to renaming have equal terms. */
typedef struct {
    uint32_t name;    /* offset of the name in the program's name bytes */
    uint32_t terms;   /* first term */
    uint32_t checks;  /* first check */
    uint32_t takes, reads, tells, check_count, makes, locals;
    uint32_t same;    /* the first rule equal to this one up to renaming      */
} TcStoredRule;

/* A state: a bag of rows (tcalc_store.c, the table of states). */
typedef struct {
    uint64_t hash;        /* its identity hash                                      */
    uint64_t sum;         /* the sum of its rows of fixed terms' hashes, a copy
                             counted each time                                       */
    uint32_t rows;        /* its rows of fixed terms: a map of (run, row) -> copies */
    uint32_t size;        /* its rows, copies counted                               */
    uint32_t loose, looses;  /* its rows with unknowns, canonical: in the loose pool */
    uint32_t unknowns;    /* the unknowns they mention: 0 .. unknowns-1             */
    uint32_t parent;      /* the state it was made from, UINT32_MAX none            */
    uint32_t diff, diffs; /* its rows of fixed terms changed from the parent's: the
                             diffs (row, copies added) pairs from word diff of the
                             diff pool                                              */
} TcState;

/* A match at a state: a rule and the rows it selects for its take and read
 * patterns; whether it is admitted on its own, and then the state its firing
 * reaches on its own.  Each match is met once, as the interpretation counts
 * matches (TcMatches). */
typedef struct {
    uint32_t rule;
    uint32_t start;       /* its selection in the selection pool: takes, then reads */
    uint32_t successor;   /* state index, when alone */
    uint32_t firing;      /* its entry among the firings met (step one), or UINT32_MAX */
    bool alone;           /* admitted on its own: its checks hold at its own information */
} TcMember;

#endif
