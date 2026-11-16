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

/* A state: a bag of rows, kept in a canonical order (the order of printing). */
typedef struct {
    uint32_t start, rows; /* its rows in the state pool */
    uint64_t hash;
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
