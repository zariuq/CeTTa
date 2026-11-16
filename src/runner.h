/* runner.h - what a run of the rho and tcalc lanes share: the budget it
 * spends, the schedule by which one run picks its next step, the record of
 * when one run is back where it has been, and the breadth-first exploration
 * of the states a program reaches.  Each lane keeps how it steps and what it
 * observes; these are the machinery around them.  The schedules are declared
 * in lib/runner_axes.data.  Header only. */
#ifndef CETTA_RUNNER_H
#define CETTA_RUNNER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "generated/runner_axes.generated.h"
#include "generated/runner_lists.generated.h"

/* ---- The budget ------------------------------------------------------------
 * Units of work a run may spend.  A run that finds none left stops and says so
 * (tcalc: (finished false); rho: the reduction limit exhausted). */
typedef struct {
    uint64_t limit, spent;
} RunBudget;

/* One unit, if one is left. */
static inline bool run_spend(RunBudget *budget) {
    if (budget->spent >= budget->limit) return false;
    budget->spent++;
    return true;
}

/* ---- The schedule ------------------------------------------------------------
 * Which of n successors, listed in the lane's canonical order, the next step
 * of one run takes.  *turn counts the steps a rotating run has taken. */
static inline size_t run_pick(RunSchedule schedule, size_t n, uint64_t *turn) {
    if (schedule == RUN_SCHEDULE_ROTATING) return (size_t)((*turn)++ % n);
    return 0;
}

/* The period of the schedule's picks once a pick among n successors is met:
 * the turns after which every pick met so far is the same again.  Canonical
 * picks the first whatever the turn, so its period stays 1; rotating picks the
 * turn modulo n, so its period is the least common multiple of the counts
 * met.  0: no period below 2^64. */
static inline uint64_t run_period(RunSchedule schedule, uint64_t period, size_t n) {
    if (schedule != RUN_SCHEDULE_ROTATING || n == 0 || period == 0) return period;
    uint64_t a = period, b = n;
    while (b) { uint64_t r = a % b; a = b; b = r; }   /* a: their greatest common divisor */
    uint64_t q = period / a;
    return q > UINT64_MAX / n ? 0 : q * n;
}

/* ---- One run's configurations ---------------------------------------------
 * A run is at a configuration: its state, and its turn modulo the period of
 * the picks met so far.  A run back at a configuration it has been in repeats
 * the stretch between for ever, since every count met in that stretch divides
 * the turns it took: the run is infinite.  Under canonical (period 1) this is
 * a state visited again.  The configurations kept are those met under the
 * current period; a new period forgets them. */
typedef struct {
    uint64_t period;                    /* start at 1; 0: nothing repeats */
    uint32_t *state; uint64_t *phase;   /* open addressing; state + 1, 0 an empty slot */
    size_t len, slots;
} RunVisits;

static inline size_t run_visit_slot(uint32_t state, uint64_t phase, size_t slots) {
    uint64_t h = (((uint64_t)state << 32) ^ phase) * 0x9E3779B97F4A7C15ull;
    return (size_t)(h >> 32) & (slots - 1);
}

/* Notes the run at `state` at turn `turn`: 1 when it has been at this
 * configuration before, 0 when not, -1 when memory ran out. */
static inline int run_visit(RunVisits *v, uint32_t state, uint64_t turn) {
    if (v->period == 0) return 0;
    uint64_t phase = turn % v->period;
    if (2 * (v->len + 1) > v->slots) {   /* at most half full */
        size_t slots = v->slots ? 2 * v->slots : 64;
        uint32_t *grown = calloc(slots, sizeof *grown);
        uint64_t *phases = calloc(slots, sizeof *phases);
        if (!grown || !phases) { free(grown); free(phases); return -1; }
        if (v->slots) {
            if (!v->state) { free(grown); free(phases); return -1; }
            if (!v->phase) { free(grown); free(phases); return -1; }
            for (size_t k = 0; k < v->slots; k++) {
                if (!v->state[k]) continue;
                size_t j = run_visit_slot(v->state[k] - 1, v->phase[k], slots);
                while (grown[j]) j = (j + 1) & (slots - 1);
                grown[j] = v->state[k];
                phases[j] = v->phase[k];
            }
        }
        free(v->state); free(v->phase);
        v->state = grown; v->phase = phases; v->slots = slots;
        size_t i = run_visit_slot(state, phase, v->slots);
        for (; v->state[i]; i = (i + 1) & (v->slots - 1))
            if (v->state[i] == state + 1 && v->phase[i] == phase) return 1;
        v->state[i] = state + 1;
        v->phase[i] = phase;
        v->len++;
        return 0;
    }
    if (!v->state) return -1;
    if (!v->phase) return -1;
    size_t i = run_visit_slot(state, phase, v->slots);
    for (; v->state[i]; i = (i + 1) & (v->slots - 1))
        if (v->state[i] == state + 1 && v->phase[i] == phase) return 1;
    v->state[i] = state + 1;
    v->phase[i] = phase;
    v->len++;
    return 0;
}

/* The run picks among n successors: under a new period the configurations
 * met are forgotten. */
static inline void run_picks_among(RunVisits *v, RunSchedule schedule, size_t n) {
    uint64_t period = run_period(schedule, v->period, n);
    if (period == v->period) return;
    v->period = period;
    if (v->slots) memset(v->state, 0, v->slots * sizeof *v->state);
    v->len = 0;
}

static inline void run_visits_free(RunVisits *v) { free(v->state); free(v->phase); }

static inline const char *run_schedule_name(RunSchedule schedule) {
    switch (schedule) {
#define RUN_SCHEDULE_NAME(code, spelling) case RUN_SCHEDULE_##code: return spelling;
    RUN_SCHEDULE_ROWS(RUN_SCHEDULE_NAME)
#undef RUN_SCHEDULE_NAME
    default: return "?";
    }
}

static inline bool run_schedule_from_name(const char *name, size_t length, RunSchedule *out) {
#define RUN_SCHEDULE_PARSE(code, spelling) \
    if (length == sizeof(spelling) - 1 && memcmp(spelling, name, length) == 0) { \
        *out = RUN_SCHEDULE_##code; \
        return true; \
    }
    RUN_SCHEDULE_ROWS(RUN_SCHEDULE_PARSE)
#undef RUN_SCHEDULE_PARSE
    return false;
}

/* ---- The exploration ---------------------------------------------------------
 * Breadth first from an initial state, each state reached expanded once.  The
 * lane numbers its states; the graph keeps them in the order reached and, when
 * keep_steps, the steps found from each state expanded, as positions in that
 * order, so that a cycle among them can be found. */
typedef struct {
    bool keep_steps;
    uint32_t *order; size_t len, cap;          /* position -> state                    */
    uint32_t *place; size_t places;            /* state -> position + 1, 0 unreached    */
    uint32_t *edge; size_t edges, edge_cap;    /* the steps of expanded states          */
    uint32_t *first; size_t firsts, first_cap; /* position -> its first step            */
} RunGraph;

/* Whether the state is reached. */
static inline bool run_reached(const RunGraph *g, uint32_t state) {
    return state < g->places && g->place[state] != 0;
}

/* Marks the state reached, after those reached before it; false when memory
 * ran out. */
static inline bool run_reach(RunGraph *g, uint32_t state) {
    if (state >= g->places) {
        size_t places = g->places ? g->places : 64;
        while (places <= state) places *= 2;
        uint32_t *grown = realloc(g->place, places * sizeof *grown);
        if (!grown) return false;
        memset(grown + g->places, 0, (places - g->places) * sizeof *grown);
        g->place = grown;
        g->places = places;
    }
    if (!g->place) return false;
    if (g->place[state]) return true;
    if (!run_push_u32(realloc, &g->order, &g->len, &g->cap, state)) return false;
    g->place[state] = (uint32_t)g->len;
    return true;
}

/* The steps of the state being expanded, to these states, each reached in
 * turn; false when memory ran out. */
static inline bool run_steps(RunGraph *g, const uint32_t *to, size_t n) {
    if (g->keep_steps &&
        !run_push_u32(realloc, &g->first, &g->firsts, &g->first_cap, (uint32_t)g->edges))
        return false;
    if (!n) return true;
    if (!to) return false;
    for (size_t i = 0; i < n; i++) {
        if (!run_reach(g, to[i])) return false;
        if (g->keep_steps) {
            if (!g->place) return false;
            if (!run_push_u32(realloc, &g->edge, &g->edges, &g->edge_cap,
                              g->place[to[i]] - 1)) return false;
        }
    }
    return true;
}

/* Whether the steps among the expanded states contain a cycle (an infinite
 * run).  Iterative depth-first search with colours. */
static inline bool run_cyclic(const RunGraph *g) {
    size_t n = g->firsts;   /* the expanded states are positions 0 .. n-1 */
    uint8_t *colour = calloc(n ? n : 1, 1);   /* 0 new, 1 open, 2 done */
    uint32_t *stack = malloc((n ? n : 1) * 2 * sizeof *stack);
    bool cycle = false;
    if (!colour || !stack) { free(colour); free(stack); return false; }
    for (size_t root = 0; root < n && !cycle; root++) {
        if (colour[root]) continue;
        size_t top = 0;
        stack[top++] = (uint32_t)root; stack[top++] = g->first[root];
        colour[root] = 1;
        while (top && !cycle) {
            uint32_t k = stack[top - 2], at = stack[top - 1];
            uint32_t end = k + 1 < n ? g->first[k + 1] : (uint32_t)g->edges;
            if (at == end) { colour[k] = 2; top -= 2; continue; }
            stack[top - 1] = at + 1;
            uint32_t t = g->edge[at];
            if (t >= n) continue;                       /* reached, not expanded */
            if (colour[t] == 1) cycle = true;
            else if (colour[t] == 0) {
                colour[t] = 1;
                stack[top++] = t; stack[top++] = g->first[t];
            }
        }
    }
    free(colour); free(stack);
    return cycle;
}

static inline void run_graph_free(RunGraph *g) {
    free(g->order); free(g->place); free(g->edge); free(g->first);
}

/* What a lane says of the state it expanded. */
typedef enum {
    RUN_EXPANDED,   /* its steps are reported (run_steps), or it has none */
    RUN_STOP,       /* the exploration ends here, unfinished              */
    RUN_FAILED,     /* memory ran out                                     */
} RunExpansion;

typedef RunExpansion (*RunExpand)(void *lane, uint32_t state, RunGraph *g);

/* Explores breadth first from `initial`.  The lane owns any work-budget
 * accounting because its expansion may have a more precise unit of work.
 * *finished: every state reached was expanded.  False when memory ran out. */
static inline bool run_breadth_first(RunGraph *g, uint32_t initial,
                                     RunExpand expand, void *lane, bool *finished) {
    *finished = false;
    size_t k = 0;
    if (!run_reach(g, initial)) return false;
    for (; k < g->len; k++) {
        if (!g->order) return false;
        RunExpansion e = expand(lane, g->order[k], g);
        if (e == RUN_FAILED) return false;
        if (e == RUN_STOP) return true;
    }
    *finished = true;
    return true;
}

#endif
