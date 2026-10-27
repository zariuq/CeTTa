#ifndef CETTA_DENSE_RELATION_H
#define CETTA_DENSE_RELATION_H

#include "relational_plan.h"
#include "space.h"

/*
 * Conjunctions over dense binary relations, by bit operations.
 *
 * Each pattern is a binary relation of one space, (h a b): h a symbol, a and
 * b each a variable or an atom equal only to itself.  Its rows (h x y), x and
 * y such atoms, become a matrix of bits over one numbering of the atoms in
 * the columns the query reads, kept by rows in each direction a pattern is
 * read.  The variables are bound one at a time: a variable's candidates are
 * the AND of the rows its bound neighbours select, and the last variable's
 * are counted by popcount.  A consumer that reads only some variables, and
 * whose instances go into sets, has the others summed out by OR: each
 * distinct assignment of what it reads is visited once.
 *
 * A repeated row declines, so every row holds once, a solution is one
 * combination of rows, and the number of solutions is the size of the
 * answer bag.  A row holding a variable, or a value whose equality is not
 * its identity, declines too: the relational plan or the ordinary path
 * answers.
 */

typedef struct CettaDenseQuery CettaDenseQuery;

typedef struct {
    /* Machine words of rows read, combined or counted. */
    uint64_t words;
    /* Candidates bound at a level other than the last. */
    uint64_t candidates;
    /* Visits made. */
    uint64_t visits;
} CettaDenseWork;

/* Prepare the query over SPACE's rows when every pattern is a binary
 * relation and rows of bits suit the relations: there are enough rows for
 * the numbering, and the matrices stay within a bound set by the rows.
 * False, leaving *OUT unchanged, otherwise. */
bool cetta_dense_query_prepare(Space *space, Arena *scratch,
                               Atom *const *patterns, size_t pattern_count,
                               CettaDenseQuery **out);
size_t cetta_dense_query_variable_count(const CettaDenseQuery *query);
VarId cetta_dense_query_variable_at(const CettaDenseQuery *query, size_t index);

/* Run the query.  KEPT, indexed like the variables, names the variables the
 * visits read; NULL keeps every one.  With every variable kept each solution
 * is visited once, with weight one; with some, each distinct assignment of
 * the kept ones is visited once, the others' values unspecified; with none,
 * nothing is visited, and VISIT may be NULL.  COUNT_OUT receives the number
 * of solutions.  False when a visit fails, the count overflows or memory
 * runs out. */
bool cetta_dense_query_run(CettaDenseQuery *query, const bool *kept,
                           CettaRelationalVisit visit, void *context,
                           uint64_t *count_out, CettaDenseWork *work);
void cetta_dense_query_free(CettaDenseQuery *query);

/* Whether dense queries are enabled: CETTA_DENSE_RELATION_REFERENCE=1
 * leaves every query to the other routes. */
bool cetta_dense_query_enabled(void);

#endif
