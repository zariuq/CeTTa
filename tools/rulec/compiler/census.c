/* census.c - see rulec.h */
#include "rulec.h"

/* ---------------------------------------------------------------------- census */

char *layer_name[MAXLAYERS]; char *layer_first[MAXLAYERS]; int nlayers;

void assign_layers(void) {
    int cur = -1;
    for (int i = 0; i < nrules; i++) {
        for (int l = 0; l < nlayers; l++) if (!strcmp(rules[i].label, layer_first[l])) cur = l;
        rules[i].layer = cur;
    }
}

int cmp_group_desc(const void *a, const void *b) {
    const Ctor *x = &ctors[*(const int *)a], *y = &ctors[*(const int *)b];
    if (x->nrules != y->nrules) return y->nrules - x->nrules;
    return strcmp(x->name, y->name);
}

int pat_size(const Pat *p) { int s = 1; if (p->var == -1) for (int i = 0; i < p->n; i++) s += pat_size(p->k[i]); return s; }
int pat_depth(const Pat *p) { int d = 0; if (p->var == -1) for (int i = 0; i < p->n; i++) { int e = pat_depth(p->k[i]); if (e > d) d = e; } return p->var == -1 ? d + 1 : 0; }


void write_census(const char *path, const char *tsv) {
    FILE *o = fopen(path, "w");
    if (!o) die("cannot write %s", path);
    int nj = 0, ndata = 0, ndet = 0, ndetrules = 0, ncomplete = 0, nrec = 0, ntail = 0, nprem = 0, nrefut = 0, nrulesemi = 0;
    int nrelq = 0, nrelbind = 0, neqtest = 0, nrelational = 0;
    for (int c = 0; c < nctors; c++) {
        if (ctors[c].defined) {
            nj++;
            if (ctors[c].det) { ndet++; ndetrules += ctors[c].nrules; } else nrulesemi += ctors[c].nrules;
            if (ctors[c].complete) ncomplete++;
            if (ctors[c].recursive) nrec++;
        } else ndata++;
    }
    for (int i = 0; i < nrules; i++) {
        if (rules[i].tail) ntail++;
        nprem += rules[i].np;
        for (int j = 0; j < rules[i].np; j++) {
            if (rules[i].p[j].relation) { nrelq++; if (rules[i].p[j].binds) nrelbind++; continue; }
            if (!prem_irrefutable(&rules[i].p[j])) nrefut++;
            if (pat_tests_bound(rules[i].p[j].tgt, rules[i].p[j].eqv)) neqtest++;
        }
    }
    fprintf(o, "rule census of %s, theory %s\n\n", module_name, theory_name);
    fprintf(o, "sorts %d; constructors %d (data %d, judgments %d); rules %d; premises %d\n", nsorts, nctors, ndata, nj, nrules, nprem);
    fprintf(o, "mode: every judgment (in, ..., in) -> out; every premise source has its inputs bound (checked)\n");
    int nnondet = 0, nnondetrules = 0;
    for (int c = 0; c < nctors; c++) if (ctors[c].defined && ctors[c].nondet) { nnondet++; nnondetrules += ctors[c].nrules; }
    if (nnondet)
        fprintf(o, "judgments det %d (%d rules), semidet %d (%d rules), nondet %d (%d rules); groups covering every input %d of %d\n",
                ndet, ndetrules, nj - ndet - nnondet, nrulesemi - nnondetrules, nnondet, nnondetrules, ncomplete, nj);
    else
        fprintf(o, "judgments det %d (%d rules), semidet %d (%d rules); groups covering every input %d of %d\n",
                ndet, ndetrules, nj - ndet, nrulesemi, ncomplete, nj);
    int nopen = 0, nopenrules = 0;
    for (int c = 0; c < nctors; c++) if (ctors[c].defined && ctors[c].overlap_open) { nopen++; nopenrules += ctors[c].nrules; }
    fprintf(o, "share of rules in det judgments: %.1f%%; judgments whose determinism is not established (an overlap not shown exclusive): %d (%d rules)\n",
            100.0 * ndetrules / nrules, nopen, nopenrules);
    for (int c = 0; c < nctors; c++) {
        if (!ctors[c].defined) continue;
        for (int i = 0; i < ctors[c].nrules; i++) if (relational_rule(&rules[ctors[c].rules[i]])) { nrelational++; break; }
    }
    fprintf(o, "premises with a refutable target (a constructor pattern or a bound variable that can fail): %d of %d\n", nrefut, nprem);
    fprintf(o, "premise targets that test a variable already bound (an equality test): %d\n", neqtest);
    fprintf(o, "overlapping left sides in one judgment: %d pairs; shown exclusive by their premises: %d (by lockstep targets %d, by evaluated guards %d); not shown: %d\n",
            n_overlaps, n_overlaps_exclusive, n_overlaps_class[OV_TARGETS], n_overlaps_class[OV_EVALUATED], n_overlaps_unresolved);
    fprintf(o, "relation premises (queries of the module's relations): %d, of which %d bind a variable; judgments with one that binds (possibly several answers): %d\n",
            nrelq, nrelbind, nrelational);
    fprintf(o, "rules whose last premise is a tail call: %d; recursive judgments (self or mutual): %d\n", ntail, nrec);
    fprintf(o, "rules in functional form (calls nested in a right side or a premise's arguments, read call by value): %d, their calls made premises: %d\n\n",
            n_flattened_rules, n_flattened_calls);
    /* layers */
    if (nlayers) {
        fprintf(o, "layer        rules  judgments  det-judgments\n");
        for (int l = 0; l < nlayers; l++) {
            int nr = 0, njl = 0, nd = 0;
            for (int i = 0; i < nrules; i++) if (rules[i].layer == l) nr++;
            for (int c = 0; c < nctors; c++) {
                if (!ctors[c].defined) continue;
                if (rules[ctors[c].rules[0]].layer != l) continue;
                njl++; if (ctors[c].det) nd++;
            }
            fprintf(o, "%-12s %5d  %9d  %13d\n", layer_name[l], nr, njl, nd);
        }
        fputc('\n', o);
    }
    /* group sizes */
    int *order = xmalloc((size_t)nctors * sizeof(int)), no = 0;
    for (int c = 0; c < nctors; c++) if (ctors[c].defined) order[no++] = c;
    qsort(order, (size_t)no, sizeof(int), cmp_group_desc);
    int hist[8] = {0};
    for (int i = 0; i < no; i++) {
        int s = ctors[order[i]].nrules;
        hist[s == 1 ? 0 : s == 2 ? 1 : s == 3 ? 2 : s <= 5 ? 3 : s <= 9 ? 4 : s <= 16 ? 5 : s <= 32 ? 6 : 7]++;
    }
    fprintf(o, "dispatch-group sizes (rules per judgment): 1:%d 2:%d 3:%d 4-5:%d 6-9:%d 10-16:%d 17-32:%d >32:%d\n\n",
            hist[0], hist[1], hist[2], hist[3], hist[4], hist[5], hist[6], hist[7]);
    fprintf(o, "largest dispatch groups:\n");
    fprintf(o, "  %-32s %5s %5s %8s %6s %6s %8s %s\n", "judgment", "rules", "arity", "class", "nodes", "tests", "max-test", "argument sorts");
    for (int i = 0; i < no && i < 25; i++) {
        Ctor *k = &ctors[order[i]];
        int sw, depth, sz = dag_size(k->dag_root, &sw, &depth);
        fprintf(o, "  %-32s %5d %5d %8s %6d %6d %8d ", k->name, k->nrules, k->arity, k->det ? "det" : k->nondet ? "nondet" : "semidet", sz, sw, depth);
        for (int a = 0; a < k->arity; a++) fprintf(o, "%s%s", a ? "," : "", sorts[k->asort[a]].name);
        fputc('\n', o);
    }
    /* premise dependency structure */
    int chain = 0, indep = 0, maxp = 0;
    for (int i = 0; i < nrules; i++) {
        Rule *r = &rules[i];
        if (r->np > maxp) maxp = r->np;
        for (int j = 1; j < r->np; j++) {
            /* does premise j read a variable bound by an earlier target? */
            int *cnt = xcalloc((size_t)r->nv, sizeof(int)), *tc = xcalloc((size_t)r->nv, sizeof(int));
            if (r->p[j].relation) { for (int a = 0; a < r->p[j].nrel; a++) pat_vars(r->p[j].rel[a], cnt); }
            else pat_vars(r->p[j].src, cnt);
            for (int q = 0; q < j; q++) {
                if (r->p[q].relation) { for (int a = 0; a < r->p[q].nrel; a++) pat_vars(r->p[q].rel[a], tc); }
                else pat_vars(r->p[q].tgt, tc);
            }
            bool dep = false;
            for (int v = 0; v < r->nv; v++) if (cnt[v] && tc[v]) dep = true;
            if (dep) chain++; else indep++;
            free(cnt); free(tc);
        }
    }
    fprintf(o, "\npremise data flow: %d later premises read an earlier premise's output, %d read only the left side; at most %d premises in a rule\n",
            chain, indep, maxp);
    /* primitive hot spots: judgments over the data layer's sorts */
    fprintf(o, "\njudgments whose arguments are names or numerals only (candidates for native primitives):\n");
    for (int i = 0; i < no; i++) {
        Ctor *k = &ctors[order[i]];
        const char *kind = sort_kind_of_judgment(k);
        if (kind) fprintf(o, "  %-28s %-9s %3d rules  (%s)\n", k->name, kind, k->nrules, k->det ? "det" : k->nondet ? "nondet" : "semidet");
    }
    /* the determinism of every judgment, when some may give several results */
    if (nnondet) {
        fprintf(o, "\ndeterminism of every judgment (det: one result for every input; semidet: at most one; nondet: any number):\n");
        for (int c = 0; c < nctors; c++)
            if (ctors[c].defined) fprintf(o, "  %-48s %-7s %3d rules\n", ctors[c].name, ctors[c].det ? "det" : ctors[c].nondet ? "nondet" : "semidet", ctors[c].nrules);
    }
    /* every overlapping pair and how it is resolved */
    if (noverlap_pairs) {
        fprintf(o, "\noverlapping pairs (left sides that unify) and their class:\n");
        for (int i = 0; i < noverlap_pairs; i++)
            fprintf(o, "  %-24s %-26s %-26s %s\n", ctors[overlap_pairs[i].judgment].name, rules[overlap_pairs[i].a].label,
                    rules[overlap_pairs[i].b].label, overlap_class_name[overlap_pairs[i].cls]);
    }
    fclose(o);
    if (tsv) {
        FILE *t = fopen(tsv, "w");
        if (!t) die("cannot write %s", tsv);
        fprintf(t, "judgment\tlayer\trules\tarity\tclass\tcomplete\trecursive\tdag_nodes\tdag_tests\tdag_depth\trule_ids\n");
        for (int c = 0; c < nctors; c++) {
            Ctor *k = &ctors[c];
            if (!k->defined) continue;
            int sw, depth, sz = dag_size(k->dag_root, &sw, &depth);
            int l = rules[k->rules[0]].layer;
            fprintf(t, "%s\t%s\t%d\t%d\t%s\t%s\t%s\t%d\t%d\t%d\t", k->name, l >= 0 ? layer_name[l] : "-", k->nrules, k->arity,
                    k->det ? "det" : k->nondet ? "nondet" : "semidet", k->complete ? "yes" : "no", k->recursive ? "yes" : "no", sz, sw, depth);
            for (int i = 0; i < k->nrules; i++) fprintf(t, "%s%d", i ? "," : "", k->rules[i]);
            fputc('\n', t);
        }
        fprintf(t, "#rule\tid\tlabel\tjudgment\tlayer\tpremises\trefutable\ttail\tlhs_size\tlhs_depth\n");
        for (int i = 0; i < nrules; i++) {
            Rule *r = &rules[i];
            int rf = 0;
            for (int j = 0; j < r->np; j++) if (!prem_irrefutable(&r->p[j])) rf++;
            fprintf(t, "rule\t%d\t%s\t%s\t%s\t%d\t%d\t%s\t%d\t%d\n", r->id, r->label, ctors[r->head].name,
                    r->layer >= 0 ? layer_name[r->layer] : "-", r->np, rf, r->tail ? "yes" : "no", pat_size(r->lhs), pat_depth(r->lhs));
        }
        fprintf(t, "#overlap\tjudgment\trule_a\trule_b\tclass\n");
        for (int i = 0; i < noverlap_pairs; i++)
            fprintf(t, "overlap\t%s\t%s\t%s\t%s\n", ctors[overlap_pairs[i].judgment].name, rules[overlap_pairs[i].a].label,
                    rules[overlap_pairs[i].b].label, overlap_class_name[overlap_pairs[i].cls]);
        fclose(t);
    }
    free(order);
}

const char *sort_kind_of_judgment(const Ctor *k) {
    /* names: Str, Byte, Nib; numerals: Nat; with Ord/Flag/Bits4 as the only other sorts */
    bool names = false, nums = false, other = false;
    for (int a = 0; a <= k->arity; a++) {
        const char *s = sorts[a < k->arity ? k->asort[a] : k->sort].name;
        if (!strcmp(s, "Str") || !strcmp(s, "Byte") || !strcmp(s, "Nib")) names = true;
        else if (!strcmp(s, "Nat")) nums = true;
        else if (!strcmp(s, "Ord") || !strcmp(s, "Flag") || !strcmp(s, "Bits4")) {}
        else other = true;
    }
    if (other || (!names && !nums)) return NULL;
    return names && nums ? "names+nums" : names ? "names" : "numerals";
}

