/* Counting over groups: Hall's condition, kept incrementally.
 *
 * Items are numbered 0 .. nitems-1.  Some items belong to a group, a set of
 * which at least one item must be chosen; some belong to a clique, a set of
 * which at most one item may be chosen.  Groups are pairwise disjoint, and so
 * are cliques.  An item is excluded (it will not be chosen) or not.
 *
 * A group is bound when each of its items that is not excluded lies in a
 * clique.  Every bound group needs a clique of its own: a chosen item of the
 * group lies in a clique, and two chosen items never share one.  So when some
 * set S of bound groups can only use the cliques of a set N with |N| < |S|,
 * no choice satisfies all the groups and cliques: S and N are a Hall
 * violator, and the excluded items of S's groups outside N's cliques are why.
 *
 * The structure keeps a matching of bound groups to cliques, each matched
 * group holding one clique through one of its items that is not excluded.
 * Excluding an item breaks at most one match; including it again breaks none,
 * unless the item is outside every clique and its group stops being bound.
 * asp_hall_feasible repairs the matching by augmenting paths, and answers a
 * violator when a bound group cannot be matched.  The answer is exact: the
 * groups can all be matched if and only if no violator exists (Hall). */
#ifndef CETTA_ASP_HALL_V1_H
#define CETTA_ASP_HALL_V1_H

#include <stdbool.h>
#include <stdint.h>

#define ASP_HALL_NONE UINT32_MAX

typedef struct AspHall AspHall;

typedef struct {
    uint32_t nitems;
    const uint32_t *group;  /* per item: its group, or ASP_HALL_NONE */
    uint32_t ngroups;
    const uint32_t *clique; /* per item: its clique, or ASP_HALL_NONE */
    uint32_t ncliques;
} AspHallShape;

/* A Hall violator: |cliques| < |groups|, every item of the groups that is not
 * excluded lies in one of the cliques, and items are the excluded items of
 * the groups that lie in no clique of the violator. */
typedef struct {
    uint32_t *groups;
    uint32_t ngroups;
    uint32_t *cliques;
    uint32_t ncliques;
    uint32_t *items;
    uint32_t nitems;
} AspHallWhy;

/* Every item starts not excluded.  NULL when out of memory or when the shape
 * is malformed (an index out of range). */
AspHall *asp_hall_new(const AspHallShape *shape);
void asp_hall_free(AspHall *h);

void asp_hall_exclude(AspHall *h, uint32_t item);
void asp_hall_include(AspHall *h, uint32_t item);
bool asp_hall_excluded(const AspHall *h, uint32_t item);

/* Whether every bound group can be matched to a clique of its own.  When
 * not, and why is not NULL, *why is a violator owned by h, valid until the
 * next call. */
bool asp_hall_feasible(AspHall *h, AspHallWhy *why);

/* The clique a group holds in the matching, or ASP_HALL_NONE.  After
 * asp_hall_feasible answers true, every bound group holds one, and no two
 * the same: a system of distinct representatives. */
uint32_t asp_hall_match(const AspHall *h, uint32_t group);

/* Counters, for measurement. */
typedef struct {
    uint64_t checks;      /* asp_hall_feasible calls */
    uint64_t searches;    /* augmenting-path searches */
    uint64_t violations;  /* violators answered */
} AspHallMatchStats;
AspHallMatchStats asp_hall_stats(const AspHall *h);

#endif
