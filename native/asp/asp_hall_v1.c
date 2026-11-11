#include "asp_hall_v1.h"

#include <stdlib.h>
#include <string.h>

/* An edge joins a group to a clique that holds some of its items; its count
 * is how many of those items are not excluded. */
typedef struct {
    uint32_t group;
    uint32_t clique;
    uint32_t count;
} Edge;

struct AspHall {
    uint32_t nitems, ngroups, ncliques, nedges;
    uint32_t *item_group;
    uint32_t *item_edge;      /* the item's edge, or NONE outside every clique */
    uint8_t *excluded;
    /* per group: its items and its edges (CSR) */
    uint32_t *group_item_start, *group_items;
    uint32_t *group_edge_start; /* edges are numbered group by group */
    Edge *edges;
    uint32_t *loose;          /* per group: items outside cliques, not excluded */
    uint32_t *match_edge;     /* per group: the edge it holds, or NONE */
    uint32_t *match_group;    /* per clique: the group holding it, or NONE */
    bool dirty;               /* some bound group may be unmatched */
    /* the search */
    uint32_t epoch;
    uint32_t *seen_group, *seen_clique;
    uint32_t *stack_group, *stack_cursor, *stack_edge;
    /* the answer */
    uint32_t *why_groups, *why_cliques, *why_items;
    AspHallMatchStats stats;
};

static void *zalloc(size_t n, size_t size) {
    return calloc(n ? n : 1u, size);
}

void asp_hall_free(AspHall *h) {
    if (!h)
        return;
    free(h->item_group);
    free(h->item_edge);
    free(h->excluded);
    free(h->group_item_start);
    free(h->group_items);
    free(h->group_edge_start);
    free(h->edges);
    free(h->loose);
    free(h->match_edge);
    free(h->match_group);
    free(h->seen_group);
    free(h->seen_clique);
    free(h->stack_group);
    free(h->stack_cursor);
    free(h->stack_edge);
    free(h->why_groups);
    free(h->why_cliques);
    free(h->why_items);
    free(h);
}

static int edge_order(const void *a, const void *b) {
    const uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

AspHall *asp_hall_new(const AspHallShape *shape) {
    AspHall *h = zalloc(1u, sizeof(*h));
    if (!h)
        return NULL;
    uint32_t n = shape->nitems, ng = shape->ngroups, nc = shape->ncliques;
    h->nitems = n;
    h->ngroups = ng;
    h->ncliques = nc;
    h->item_group = zalloc(n, sizeof(uint32_t));
    h->item_edge = zalloc(n, sizeof(uint32_t));
    h->excluded = zalloc(n, 1u);
    h->group_item_start = zalloc((size_t)ng + 1u, sizeof(uint32_t));
    h->group_items = zalloc(n, sizeof(uint32_t));
    h->group_edge_start = zalloc((size_t)ng + 1u, sizeof(uint32_t));
    h->loose = zalloc(ng, sizeof(uint32_t));
    h->match_edge = zalloc(ng, sizeof(uint32_t));
    h->match_group = zalloc(nc, sizeof(uint32_t));
    h->seen_group = zalloc(ng, sizeof(uint32_t));
    h->seen_clique = zalloc(nc, sizeof(uint32_t));
    h->stack_group = zalloc(ng, sizeof(uint32_t));
    h->stack_cursor = zalloc(ng, sizeof(uint32_t));
    h->stack_edge = zalloc(ng, sizeof(uint32_t));
    h->why_groups = zalloc(ng, sizeof(uint32_t));
    h->why_cliques = zalloc(nc, sizeof(uint32_t));
    h->why_items = zalloc(n, sizeof(uint32_t));
    uint64_t *keys = zalloc(n, sizeof(uint64_t));
    bool ok = h->item_group && h->item_edge && h->excluded &&
              h->group_item_start && h->group_items && h->group_edge_start &&
              h->loose && h->match_edge && h->match_group && h->seen_group &&
              h->seen_clique && h->stack_group && h->stack_cursor &&
              h->stack_edge && h->why_groups && h->why_cliques &&
              h->why_items && keys;
    /* items by group; the (group, clique) pairs, sorted, become the edges */
    uint32_t nkeys = 0u;
    for (uint32_t i = 0u; ok && i < n; i++) {
        uint32_t g = shape->group[i], c = shape->clique[i];
        ok = (g == ASP_HALL_NONE || g < ng) && (c == ASP_HALL_NONE || c < nc);
        h->item_group[i] = ok ? g : ASP_HALL_NONE;
        h->item_edge[i] = ASP_HALL_NONE;
        if (ok && g != ASP_HALL_NONE) {
            h->group_item_start[g + 1u]++;
            if (c != ASP_HALL_NONE)
                keys[nkeys++] = ((uint64_t)g << 32) | c;
            else
                h->loose[g]++;
        }
    }
    if (!ok) {
        free(keys);
        asp_hall_free(h);
        return NULL;
    }
    for (uint32_t g = 0u; g < ng; g++)
        h->group_item_start[g + 1u] += h->group_item_start[g];
    uint32_t *fill = zalloc(ng, sizeof(uint32_t));
    qsort(keys, nkeys, sizeof(*keys), edge_order);
    uint32_t ne = 0u;
    for (uint32_t k = 0u; k < nkeys; k++)
        if (k == 0u || keys[k] != keys[k - 1u])
            keys[ne++] = keys[k];
    h->nedges = ne;
    h->edges = zalloc(ne, sizeof(Edge));
    if (!fill || !h->edges) {
        free(fill);
        free(keys);
        asp_hall_free(h);
        return NULL;
    }
    for (uint32_t e = 0u; e < ne; e++) {
        h->edges[e] = (Edge){(uint32_t)(keys[e] >> 32), (uint32_t)keys[e], 0u};
        h->group_edge_start[h->edges[e].group + 1u]++;
    }
    for (uint32_t g = 0u; g < ng; g++)
        h->group_edge_start[g + 1u] += h->group_edge_start[g];
    for (uint32_t i = 0u; i < n; i++) {
        uint32_t g = h->item_group[i], c = shape->clique[i];
        if (g == ASP_HALL_NONE)
            continue;
        h->group_items[h->group_item_start[g] + fill[g]++] = i;
        if (c == ASP_HALL_NONE)
            continue;
        /* binary search for (g, c) among g's edges */
        uint32_t lo = h->group_edge_start[g], hi = h->group_edge_start[g + 1u];
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2u;
            if (h->edges[mid].clique < c)
                lo = mid + 1u;
            else
                hi = mid;
        }
        h->item_edge[i] = lo;
        h->edges[lo].count++;
    }
    free(fill);
    free(keys);
    for (uint32_t g = 0u; g < ng; g++)
        h->match_edge[g] = ASP_HALL_NONE;
    for (uint32_t c = 0u; c < nc; c++)
        h->match_group[c] = ASP_HALL_NONE;
    h->dirty = true;
    return h;
}

static void unmatch(AspHall *h, uint32_t g) {
    uint32_t e = h->match_edge[g];
    if (e == ASP_HALL_NONE)
        return;
    h->match_group[h->edges[e].clique] = ASP_HALL_NONE;
    h->match_edge[g] = ASP_HALL_NONE;
    h->dirty = true;
}

void asp_hall_exclude(AspHall *h, uint32_t item) {
    if (item >= h->nitems || h->excluded[item])
        return;
    h->excluded[item] = 1u;
    uint32_t g = h->item_group[item];
    if (g == ASP_HALL_NONE)
        return;
    uint32_t e = h->item_edge[item];
    if (e == ASP_HALL_NONE) {
        if (--h->loose[g] == 0u)
            h->dirty = true;           /* g becomes bound */
        return;
    }
    if (--h->edges[e].count == 0u && h->match_edge[g] == e)
        unmatch(h, g);
}

void asp_hall_include(AspHall *h, uint32_t item) {
    if (item >= h->nitems || !h->excluded[item])
        return;
    h->excluded[item] = 0u;
    uint32_t g = h->item_group[item];
    if (g == ASP_HALL_NONE)
        return;
    uint32_t e = h->item_edge[item];
    if (e == ASP_HALL_NONE) {
        if (h->loose[g]++ == 0u)
            unmatch(h, g);              /* g is no longer bound */
        return;
    }
    h->edges[e].count++;
}

bool asp_hall_excluded(const AspHall *h, uint32_t item) {
    return item < h->nitems && h->excluded[item];
}

uint32_t asp_hall_match(const AspHall *h, uint32_t group) {
    if (group >= h->ngroups || h->match_edge[group] == ASP_HALL_NONE)
        return ASP_HALL_NONE;
    return h->edges[h->match_edge[group]].clique;
}

AspHallMatchStats asp_hall_stats(const AspHall *h) {
    return h->stats;
}

/* An augmenting path from the unmatched bound group g0, by depth-first
 * search over the edges that hold an item not excluded: from a group to a
 * clique, from a matched clique to the group holding it.  On failure the
 * groups and cliques seen are a violator. */
static bool augment(AspHall *h, uint32_t g0) {
    h->stats.searches++;
    if (++h->epoch == 0u) {            /* wrapped: reset the marks */
        memset(h->seen_group, 0, sizeof(uint32_t) * (h->ngroups ? h->ngroups : 1u));
        memset(h->seen_clique, 0, sizeof(uint32_t) * (h->ncliques ? h->ncliques : 1u));
        h->epoch = 1u;
    }
    uint32_t top = 0u;
    h->stack_group[top] = g0;
    h->stack_cursor[top] = h->group_edge_start[g0];
    h->seen_group[g0] = h->epoch;
    top = 1u;
    while (top > 0u) {
        uint32_t g = h->stack_group[top - 1u];
        uint32_t end = h->group_edge_start[g + 1u];
        uint32_t *cur = &h->stack_cursor[top - 1u];
        while (*cur < end && (h->edges[*cur].count == 0u ||
                              h->seen_clique[h->edges[*cur].clique] == h->epoch))
            (*cur)++;
        if (*cur == end) {
            top--;
            continue;
        }
        uint32_t e = (*cur)++;
        uint32_t c = h->edges[e].clique;
        h->seen_clique[c] = h->epoch;
        h->stack_edge[top - 1u] = e;
        uint32_t holder = h->match_group[c];
        if (holder == ASP_HALL_NONE) {
            /* flip the path: each group on the stack takes its edge */
            for (uint32_t k = top; k-- > 0u;) {
                uint32_t gk = h->stack_group[k], ek = h->stack_edge[k];
                h->match_edge[gk] = ek;
                h->match_group[h->edges[ek].clique] = gk;
            }
            return true;
        }
        if (h->seen_group[holder] != h->epoch) {
            h->seen_group[holder] = h->epoch;
            h->stack_group[top] = holder;
            h->stack_cursor[top] = h->group_edge_start[holder];
            top++;
        }
    }
    return false;
}

bool asp_hall_feasible(AspHall *h, AspHallWhy *why) {
    h->stats.checks++;
    if (!h->dirty)
        return true;
    for (uint32_t g = 0u; g < h->ngroups; g++) {
        if (h->loose[g] != 0u || h->match_edge[g] != ASP_HALL_NONE)
            continue;
        if (augment(h, g))
            continue;
        h->stats.violations++;
        if (why) {
            uint32_t ns = 0u, nn = 0u, ni = 0u;
            for (uint32_t s = 0u; s < h->ngroups; s++)
                if (h->seen_group[s] == h->epoch)
                    h->why_groups[ns++] = s;
            for (uint32_t c = 0u; c < h->ncliques; c++)
                if (h->seen_clique[c] == h->epoch)
                    h->why_cliques[nn++] = c;
            for (uint32_t k = 0u; k < ns; k++) {
                uint32_t s = h->why_groups[k];
                for (uint32_t j = h->group_item_start[s];
                     j < h->group_item_start[s + 1u]; j++) {
                    uint32_t i = h->group_items[j];
                    uint32_t e = h->item_edge[i];
                    if (h->excluded[i] &&
                        (e == ASP_HALL_NONE ||
                         h->seen_clique[h->edges[e].clique] != h->epoch))
                        h->why_items[ni++] = i;
                }
            }
            *why = (AspHallWhy){h->why_groups, ns, h->why_cliques, nn,
                                 h->why_items, ni};
        }
        /* the matching stays as it is; the next call searches again */
        return false;
    }
    h->dirty = false;
    return true;
}
