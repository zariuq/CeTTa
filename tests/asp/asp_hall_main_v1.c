/* Differential test of native/asp/asp_hall_v1: random groups and cliques,
 * random sequences of exclusions and inclusions, and after each step the
 * structure's verdict against a brute-force search for distinct cliques.
 * Each violator is checked: fewer cliques than groups, every group bound,
 * every item of its groups that is not excluded inside its cliques, and its
 * items exactly the excluded items of its groups outside its cliques.  Each
 * feasible verdict is checked by its matching.
 *   asp_hall_main_v1 [ROUNDS [SEED]] */
#include "asp_hall_v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t rng_state;
static uint32_t rnd(uint32_t n) {
    rng_state = rng_state * 6364136223846793005ull + 1442695040888963407ull;
    return (uint32_t)(rng_state >> 33) % n;
}

enum { MAXI = 24, MAXG = 7, MAXC = 7 };

typedef struct {
    uint32_t n, ng, nc;
    uint32_t group[MAXI], clique[MAXI];
    uint8_t excluded[MAXI];
} Case;

static bool bound(const Case *k, uint32_t g) {
    for (uint32_t i = 0u; i < k->n; i++)
        if (k->group[i] == g && !k->excluded[i] && k->clique[i] == ASP_HALL_NONE)
            return false;
    return true;
}

/* Can bound groups g.. take distinct cliques, the used ones marked? */
static bool brute(const Case *k, uint32_t g, uint32_t used) {
    if (g == k->ng)
        return true;
    if (!bound(k, g))
        return brute(k, g + 1u, used);
    for (uint32_t i = 0u; i < k->n; i++) {
        uint32_t c = k->clique[i];
        if (k->group[i] == g && !k->excluded[i] && c != ASP_HALL_NONE &&
            !(used & (1u << c)) && brute(k, g + 1u, used | (1u << c)))
            return true;
    }
    return false;
}

static int check(const Case *k, AspHall *h, uint32_t round) {
    AspHallWhy why;
    bool feasible = asp_hall_feasible(h, &why);
    bool expect = brute(k, 0u, 0u);
    if (feasible != expect) {
        printf("FAIL round %u: verdict %d, brute force %d\n", round, feasible, expect);
        return 1;
    }
    if (feasible) {
        uint32_t used = 0u;
        for (uint32_t g = 0u; g < k->ng; g++) {
            if (!bound(k, g))
                continue;
            uint32_t c = asp_hall_match(h, g);
            bool held = false;
            for (uint32_t i = 0u; i < k->n && c != ASP_HALL_NONE; i++)
                held |= k->group[i] == g && k->clique[i] == c && !k->excluded[i];
            if (c == ASP_HALL_NONE || !held || (used & (1u << c))) {
                printf("FAIL round %u: the matching of group %u\n", round, g);
                return 1;
            }
            used |= 1u << c;
        }
        return 0;
    }
    uint32_t in_s = 0u, in_n = 0u;
    for (uint32_t j = 0u; j < why.ngroups; j++)
        in_s |= 1u << why.groups[j];
    for (uint32_t j = 0u; j < why.ncliques; j++)
        in_n |= 1u << why.cliques[j];
    bool ok = why.ncliques < why.ngroups;
    for (uint32_t j = 0u; ok && j < why.ngroups; j++)
        ok = bound(k, why.groups[j]);
    uint8_t named[MAXI] = {0};
    for (uint32_t j = 0u; ok && j < why.nitems; j++)
        named[why.items[j]] = 1u;
    for (uint32_t i = 0u; ok && i < k->n; i++) {
        if (k->group[i] == ASP_HALL_NONE || !(in_s & (1u << k->group[i])))
            continue;
        bool inside = k->clique[i] != ASP_HALL_NONE && (in_n & (1u << k->clique[i]));
        if (!k->excluded[i])
            ok = inside;
        else
            ok = named[i] == !inside;
    }
    if (!ok) {
        printf("FAIL round %u: a malformed violator\n", round);
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    uint32_t rounds = argc > 1 ? (uint32_t)atoi(argv[1]) : 2000u;
    rng_state = argc > 2 ? (uint64_t)atoll(argv[2]) : 1u;
    uint32_t checks = 0u, infeasible = 0u;
    for (uint32_t r = 0u; r < rounds; r++) {
        Case k = {0};
        k.ng = 1u + rnd(MAXG);
        k.nc = 1u + rnd(MAXC);
        k.n = 1u + rnd(MAXI);
        for (uint32_t i = 0u; i < k.n; i++) {
            k.group[i] = rnd(5u) ? rnd(k.ng) : ASP_HALL_NONE;
            k.clique[i] = rnd(6u) ? rnd(k.nc) : ASP_HALL_NONE;
        }
        AspHallShape shape = {k.n, k.group, k.ng, k.clique, k.nc};
        AspHall *h = asp_hall_new(&shape);
        if (!h) {
            printf("FAIL round %u: no structure\n", r);
            return 1;
        }
        for (uint32_t step = 0u; step < 3u * k.n; step++) {
            uint32_t i = rnd(k.n);
            if (rnd(3u)) {
                asp_hall_exclude(h, i);
                k.excluded[i] = 1u;
            } else {
                asp_hall_include(h, i);
                k.excluded[i] = 0u;
            }
            if (asp_hall_excluded(h, i) != (k.excluded[i] != 0u)) {
                printf("FAIL round %u: exclusion state\n", r);
                return 1;
            }
            if (check(&k, h, r))
                return 1;
            checks++;
            infeasible += !brute(&k, 0u, 0u);
        }
        asp_hall_free(h);
    }
    printf("asp_hall: %u rounds, %u checks against brute force, %u infeasible, all agree\n",
           rounds, checks, infeasible);
    return 0;
}
