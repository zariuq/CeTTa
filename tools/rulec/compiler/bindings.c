/* bindings.c - representations of sorts and the binding table.
 *
 * The binding table (rulec --bindings FILE; --blocks is the same reader) is part of the IR: it
 * says how a back end may represent sorts and which judgments it may answer by a component or a
 * bound function instead of running their rules; the judgments' rules stay the contract.  Rows:
 *
 *   (rep SORT records NIL CONS)                  the record cursor (decoded records, read as a list)
 *   (rep SORT nat ZERO B0 B1)                    binary numerals as immediates
 *   (rep SORT str NIL CONS BYTE NIBBLE)          byte strings on the heap
 *   (rep SORT bytes NIL CONS BYTE NIBBLE)        interned byte strings (rc_bytes.h)
 *   (rep SORT intern)                            hash-consed constructor nodes (rc_intern.h)
 *   (rep SORT array-table TABLE LEAF NODE NONE)  id tables as arrays (rc_table.h)
 *   (rep SORT ext NAME CTOR...)                  views from --rep-header, the constructors in order
 *   (rep SORT word CTOR BIT0 BIT1)               words of bits as immediates: the sort's one constructor
 *                                                CTOR of w children (1 <= w <= 62) of a bit sort whose
 *                                                constructors are exactly the nullary BIT0, BIT1; the
 *                                                first child is the most significant bit
 *   (rep SORT binary ZERO BIT BIT0 BIT1)         binary numerals least significant bit first (ZERO; BIT b
 *                                                n is 2n + b) as natural numbers of any size (rc_bignat.h);
 *                                                BIT BIT0 ZERO is not canonical and stops the run
 *   (rep SORT slices NIL CONS)                   byte strings of a word sort of 8 bits as slices of the
 *                                                run's input or interned strings (rc_slices.h)
 *   (rep SORT atoms INT STRING SYMBOL EXPRESSION)
 *                                                MeTTa atoms (rc_atoms.h): constructor applications as heap
 *                                                terms, integers as immediates, strings and symbols as byte
 *                                                strings; a nullary constructor named by a decimal integer
 *                                                or by a quoted string stands for that literal; the four
 *                                                nullary constructors named stand for the integers, strings,
 *                                                symbols and expressions that are no constructor of the sort
 *   (atoms-source SORT CONS CP NIL)              the run's source (rc_atoms_set_source) as a value of the
 *                                                atoms sort: the list (CONS (CP n) ...) ending in NIL of
 *                                                its scalars, read through cursors (rc_atoms.h)
 *   (block NAME JUDGMENT IMPLEMENTATION LABEL WHY)
 *       IMPLEMENTATION is a component, (KIND PARAM...), or a string naming a C function:
 *         (equal YES NO)                 identity of two values of an interned or immediate sort
 *         (bytes-order LT EQ GT)         rc_bytes_cmp
 *         (bytes-snoc HEX)               append the bytes HEX
 *         (nat-order LT EQ GT)           rc_nat_cmp
 *         (nat-succ)                     rc_nat_succ (rc_bn_succ for a binary sort)
 *         (nat-add)                      rc_nat_add (rc_bn_add for a binary sort)
 *       components of a binary sort (rc_bignat.h), a word sort and a slices sort (rc_slices.h):
 *         (nat-push-bit)                 J(b, n) = 2n + b
 *         (nat-pred)                     J(n) = n - 1, no result for 0
 *         (nat-xor) (nat-multiply)       bitwise exclusive or; product
 *         (nat-subtract)                 J(a, b) = a - b when b <= a, else 0
 *         (nat-trim)                     J(w, n) = n mod 2^w
 *         (word-pack)                    J(word, n) = n * 2^width + word
 *         (bytes-length)                 J(s) = the number of bytes of s
 *         (bytes-take TAKEN NEED)        J(n, s) = TAKEN (first n bytes) (the rest) when n <= length, else NEED
 *         (bytes-equal YES NO)           YES when the two contents are equal, else NO
 *         (fnv-hash BASIS PRIME BITS)    J(s) = BASIS for no bytes; else h(i+1) = ((h(i) xor byte) * PRIME)
 *                                        mod 2^BITS from h(0) = BASIS (BITS <= 64, BASIS < 2^BITS)
 *         (uleb DECODED NEED BAD MAXBYTES LASTBITS)
 *                                        J(s): a minimal unsigned LEB128 number at the start of s, of at
 *                                        most MAXBYTES bytes (7 bits each, the high bit "more"), whose
 *                                        byte number MAXBYTES has at most LASTBITS bits: DECODED n rest;
 *                                        NEED when s ends inside the number; BAD when the number is not
 *                                        minimal (two or more bytes, the last one 0) or too long or
 *                                        large (1 <= MAXBYTES <= 9, 7 * (MAXBYTES - 1) + LASTBITS <= 64)
 *         (structural-order LT EQ GT)    rc_order_cmp on the arguments' sort
 *         (set-union CMP LT EQ GT)       rc_set_union over the comparator judgment CMP
 *         (set-remove CMP LT EQ GT)      rc_set_remove
 *         (table-get) (table-put) (table-set) (table-next) (table-empty)
 *   (memo NAME JUDGMENT LABEL WHY)               keep the judgment's results per arguments
 *   (test NAME RELATION COMPONENT LABEL WHY)     the relation's queries (every argument bound) answered by a
 *                                                test component on an atoms sort (rc_atoms.h):
 *         (term-equal) (term-different)  two values equal, or different, as module data
 *         (int-less) (int-not-less)      a < b; b <= a (integers; no other value passes either)
 *         (int-gap) (int-no-gap)         a + 1 < b; b <= a + 1
 *         (unicode-scalar) (non-unicode-scalar)
 *                                        an integer that is (is not) a Unicode scalar value
 *   (law NAME JUDGMENT RESULT LABEL WHY)         J(x, x) gives the constant RESULT
 * LABEL is proved (WHY names the theorems), checked (every call compared with the rules in check
 * mode) or trusted (WHY gives the reason).
 */
#include "rulec.h"
#include <errno.h>

const char *rep_name[] = {"symbolic", "records", "nat", "str", "ext", "intern", "bytes", "table", "unary", "word", "binary", "slices", "atoms"};
const char *rep_header;
const char *component_prefix = "";
const char *sort_rep(int s) { return sorts[s].native == REP_EXT || REP_PERSORT(sorts[s].native) ? sorts[s].repname : rep_name[sorts[s].native]; }
int rep_ctor[REP_COUNT][4];   /* constructor ids a native representation views */
int str_nib[16];              /* the sixteen nibble constructors of a byte string's view */
int table_ctor[4];            /* TABLE LEAF NODE NONE */
Law laws[64]; int nlaws;
TestRow test_rows[64]; int ntest_rows;
bool functional_only;
NativeOp native_ops[64]; int nnative_ops;
MemoRow memo_rows[64]; int nmemo_rows;
int *sorts_memo_slots;     /* memo slots per sort (apply_blocks) */
char *safepoint_name;
int safepoint_ctor = -1;
bool no_musttail;
bool cache_constants;
const char *blocks_file;
BlockRow *block_rows; int nblock_rows, block_row_cap;
const char *include_rt = "rc_rt.h";
const char *source_digest;

/* ---- representations ---- */
static int ctor_named(const char *name, const char *where) {
    int c = map_get(&ctor_map, name);
    if (c < 0) die("%s: unknown constructor %s", where, name);
    return c;
}
/* the byte-list shape: NIL; CONS BYTE rest; BYTE nibble nibble; sixteen nullary nibbles PREFIX0..f */
static void byte_list_shape(int sort, int *rc, const char *nibprefix, const char *where) {
    int by = rc[2];
    if (!(sorts[sort].ndata == 2 && ctors[rc[0]].arity == 0 && ctors[rc[1]].arity == 2 && ctors[rc[1]].asort[1] == sort &&
          ctors[rc[1]].asort[0] == ctors[by].sort && ctors[by].arity == 2 && ctors[by].asort[0] == ctors[by].asort[1] &&
          sorts[ctors[by].sort].ndata == 1 && sorts[ctors[by].asort[0]].ndata == 16))
        die("%s: not a byte-list sort", where);
    for (int i = 0; i < 16; i++) {
        char nm[256];
        snprintf(nm, sizeof nm, "%s%x", nibprefix, i);
        str_nib[i] = map_get(&ctor_map, nm);
        if (str_nib[i] < 0 || ctors[str_nib[i]].arity != 0 || ctors[str_nib[i]].sort != ctors[by].asort[0])
            die("%s: nibble constructor %s missing", where, nm);
    }
}
void set_rep(int sort, const char *kind, char **args, int na, const char *where) {
    if (sorts[sort].native) die("%s: sort %s already has a representation", where, sorts[sort].name);
    if (!strcmp(kind, "intern")) {
        if (na) die("%s: (rep SORT intern) has no parameters", where);
        sorts[sort].native = REP_INTERN;
        return;
    }
    if (!strcmp(kind, "array-table")) {
        if (na != 4) die("%s: (rep SORT array-table TABLE LEAF NODE NONE)", where);
        for (int i = 0; i < 4; i++) table_ctor[i] = ctor_named(args[i], where);
        int tb = table_ctor[0], lf = table_ctor[1], nd = table_ctor[2], none = table_ctor[3];
        if (!(sorts[sort].ndata == 1 && ctors[tb].sort == sort && ctors[tb].arity == 2 && ctors[lf].arity == 0 &&
              ctors[nd].arity == 3 && ctors[nd].sort == ctors[lf].sort && ctors[tb].asort[1] == ctors[lf].sort &&
              ctors[nd].asort[1] == ctors[lf].sort && ctors[nd].asort[2] == ctors[lf].sort && ctors[none].arity == 0 &&
              ctors[none].sort == ctors[nd].asort[0]))
            die("%s: not TABLE next trie with LEAF / NODE cell t0 t1 and a nullary cell NONE", where);
        sorts[sort].native = REP_TABLE;
        return;
    }
    if (!strncmp(kind, "ext:", 4) || !strcmp(kind, "ext")) {
        /* ext:NAME(c1,...,cn), or (rep SORT ext NAME c1 ... cn): every data constructor, in the
         * order the header numbers them */
        const char *name = kind[3] == ':' ? kind + 4 : (na ? args[0] : NULL);
        if (!name) die("%s: ext needs a name", where);
        if (kind[3] != ':') { args++; na--; }
        sorts[sort].native = REP_EXT;
        sorts[sort].repname = xstrdup(name);
        sorts[sort].extctors = xcalloc((size_t)(na ? na : 1), sizeof(int));
        for (int i = 0; i < na; i++) {
            int c = ctor_named(args[i], where);
            if (ctors[c].sort != sort || ctors[c].defined) die("%s: %s is not a data constructor of %s", where, args[i], sorts[sort].name);
            sorts[sort].extctors[i] = c;
        }
        if (na != sorts[sort].ndata) die("%s: names %d of the %d data constructors", where, na, sorts[sort].ndata);
        sorts[sort].nextctors = na;
        return;
    }
    if (!strcmp(kind, "atoms")) {
        if (na != 4) die("%s: (rep %s atoms INT STRING SYMBOL EXPRESSION)", where, sorts[sort].name);
        Sort *so = &sorts[sort];
        for (int i = 0; i < 4; i++) {
            so->rp[i] = ctor_named(args[i], where);
            if (ctors[so->rp[i]].sort != sort || ctors[so->rp[i]].defined || ctors[so->rp[i]].arity)
                die("%s: %s is not a nullary data constructor of %s", where, args[i], so->name);
        }
        for (int c = 0; c < nctors; c++) {
            Ctor *k = &ctors[c];
            if (k->sort != sort || k->defined || k->arity) continue;
            const char *nm = k->name;
            size_t n = strlen(nm);
            if (n >= 2 && nm[0] == '"' && nm[n - 1] == '"') {
                k->lit = LIT_STRING; k->sval = xstrdup(nm + 1); k->slen = (int)(n - 2); k->sval[n - 2] = 0;
                continue;
            }
            char *end;
            errno = 0;
            long long v = strtoll(nm, &end, 10);
            if (n && !*end && !errno && (isdigit((unsigned char)nm[0]) || (nm[0] == '-' && isdigit((unsigned char)nm[1])))) {
                char back[32];
                snprintf(back, sizeof back, "%lld", v);
                if (strcmp(back, nm)) die("%s: %s is not an integer in its canonical spelling", where, nm);
                k->lit = LIT_INT; k->ival = v;
            }
        }
        for (int s2 = 0; s2 < nsorts; s2++)
            if (s2 != sort && sorts[s2].native == REP_ATOMS) die("%s: one atoms sort per module", where);
        char nm[64];
        snprintf(nm, sizeof nm, "atoms_%d", sort);
        so->repname = xstrdup(nm);
        so->native = REP_ATOMS;
        return;
    }
    if (!strcmp(kind, "word") || !strcmp(kind, "binary") || !strcmp(kind, "slices")) {
        int r = kind[0] == 'w' ? REP_WORD : kind[0] == 'b' ? REP_BINARY : REP_SLICES;
        int want = r == REP_WORD ? 3 : r == REP_BINARY ? 4 : 2;
        if (na != want) die("%s: (rep %s %s) takes %d constructors", where, sorts[sort].name, kind, want);
        Sort *so = &sorts[sort];
        for (int i = 0; i < na; i++) so->rp[i] = ctor_named(args[i], where);
        /* a bit sort: exactly the two nullary constructors BIT0, BIT1, represented symbolically */
        #define BIT_SORT(bs, c0, c1) ((bs) >= 0 && sorts[(bs)].ndata == 2 && !sorts[(bs)].native && ctors[(c0)].sort == (bs) && \
            ctors[(c1)].sort == (bs) && !ctors[(c0)].defined && !ctors[(c1)].defined && ctors[(c0)].arity == 0 && \
            ctors[(c1)].arity == 0 && (c0) != (c1))
        if (r == REP_WORD) {
            const Ctor *w = &ctors[so->rp[0]];
            int bs = w->arity ? w->asort[0] : -1;
            bool same = true;
            for (int i = 0; i < w->arity; i++) if (w->asort[i] != bs) same = false;
            if (!(so->ndata == 1 && w->sort == sort && !w->defined && w->arity >= 1 && w->arity <= 62 && same && BIT_SORT(bs, so->rp[1], so->rp[2])))
                die("%s: (rep %s word CTOR BIT0 BIT1): not one constructor of 1 to 62 children of one bit sort with exactly BIT0, BIT1", where, so->name);
            so->width = w->arity;
        } else if (r == REP_BINARY) {
            const Ctor *z = &ctors[so->rp[0]], *b = &ctors[so->rp[1]];
            if (!(so->ndata == 2 && z->sort == sort && !z->defined && z->arity == 0 && b->sort == sort && !b->defined && b->arity == 2 &&
                  b->asort[1] == sort && BIT_SORT(b->asort[0], so->rp[2], so->rp[3])))
                die("%s: (rep %s binary ZERO BIT BIT0 BIT1): not ZERO and BIT (a bit, the rest) with a bit sort of exactly BIT0, BIT1", where, so->name);
        } else {
            const Ctor *nl = &ctors[so->rp[0]], *cs = &ctors[so->rp[1]];
            if (!(so->ndata == 2 && nl->sort == sort && !nl->defined && nl->arity == 0 && cs->sort == sort && !cs->defined && cs->arity == 2 &&
                  cs->asort[1] == sort && cs->asort[0] != sort))
                die("%s: (rep %s slices NIL CONS): not NIL and CONS (a byte, the rest)", where, so->name);
            for (int s2 = 0; s2 < nsorts; s2++)
                if (s2 != sort && sorts[s2].native == REP_SLICES) die("%s: one slices sort per module (the run's input is one)", where);
        }
        #undef BIT_SORT
        char nm[64];
        snprintf(nm, sizeof nm, "%s_%d", kind, sort);
        so->repname = xstrdup(nm);
        so->native = r;
        return;
    }
    int rep = !strcmp(kind, "records") ? REP_RECORDS : !strcmp(kind, "nat") ? REP_NAT : !strcmp(kind, "str") ? REP_STR
            : !strcmp(kind, "bytes") ? REP_BYTES : !strcmp(kind, "unary") ? REP_UNARY : -1;
    int want = rep == REP_RECORDS ? 2 : rep == REP_NAT ? 3 : rep == REP_STR || rep == REP_BYTES ? 4 : rep == REP_UNARY ? 2 : -1;
    if (rep < 0 || na != want) die("%s: unknown representation or wrong parameters", where);
    for (int i = 0; i < ((rep == REP_STR || rep == REP_BYTES) ? 3 : na); i++) rep_ctor[rep][i] = ctor_named(args[i], where);
    int *rc = rep_ctor[rep];
    if (rep == REP_RECORDS && !(ctors[rc[0]].sort == sort && ctors[rc[0]].arity == 0 && ctors[rc[1]].sort == sort &&
                                ctors[rc[1]].arity == 2 && ctors[rc[1]].asort[1] == sort && sorts[sort].ndata == 2))
        die("%s: not a list sort with exactly these two constructors", where);
    if (rep == REP_NAT && !(sorts[sort].ndata == 3 && ctors[rc[0]].sort == sort && ctors[rc[0]].arity == 0 &&
                            ctors[rc[1]].arity == 1 && ctors[rc[1]].asort[0] == sort && ctors[rc[2]].arity == 1 &&
                            ctors[rc[2]].asort[0] == sort && ctors[rc[1]].sort == sort && ctors[rc[2]].sort == sort))
        die("%s: not a binary-numeral sort with exactly these three constructors", where);
    if (rep == REP_UNARY && !(sorts[sort].ndata == 2 && ctors[rc[0]].sort == sort && ctors[rc[0]].arity == 0 &&
                              ctors[rc[1]].sort == sort && ctors[rc[1]].arity == 1 && ctors[rc[1]].asort[0] == sort))
        die("%s: not a unary-numeral sort with exactly these two constructors (zero, successor)", where);
    if (rep == REP_STR || rep == REP_BYTES) {
        for (int s2 = 0; s2 < nsorts; s2++)
            if (s2 != sort && (sorts[s2].native == REP_STR || sorts[s2].native == REP_BYTES)) die("%s: one byte-string sort per module", where);
        byte_list_shape(sort, rc, args[3], where);
    }
    sorts[sort].native = rep;
}
void parse_native_sort(const char *spec) {
    /* SORT=KIND(args) */
    char *s = xstrdup(spec), *eq = strchr(s, '='), *lp = NULL, *rp = NULL;
    if (eq) lp = strchr(eq, '(');
    if (lp) rp = strrchr(lp, ')');
    if (!eq || !lp || !rp) die("bad --native-sort %s", spec);
    *eq = 0; *lp = 0; *rp = 0;
    int sort = sort_id(s);
    if (sort < 0) die("--native-sort: unknown sort %s", s);
    char *args[32]; int na = 0;
    for (char *t = strtok(lp + 1, ","); t && na < 32; t = strtok(NULL, ",")) args[na++] = t;
    set_rep(sort, eq + 1, args, na, spec);
}
/* an interned sort's nodes may hold only values that never move: interned or immediate values, or
 * nullary constructors of a symbolic sort (static terms) */
static bool identity_sort(int s) {
    int r = sorts[s].native;
    if (r == REP_INTERN || r == REP_BYTES || REP_IMMNAT(r) || r == REP_WORD || r == REP_BINARY) return true;
    if (r != REP_SYMBOLIC) return false;
    for (int c = 0; c < nctors; c++) if (ctors[c].sort == s && !ctors[c].defined && ctors[c].arity) return false;
    return true;
}
void check_reps(void) {
    for (int s = 0; s < nsorts; s++) {
        if (sorts[s].native != REP_SLICES) continue;
        int bs = ctors[sorts[s].rp[1]].asort[0];
        if (sorts[bs].native != REP_WORD || sorts[bs].width != 8)
            die("(rep %s slices ...): the byte sort %s is not (rep %s word ...) of eight bits", sorts[s].name, sorts[bs].name, sorts[bs].name);
    }
    for (int c = 0; c < nctors; c++) {
        if (ctors[c].defined || sorts[ctors[c].sort].native != REP_INTERN) continue;
        if (ctors[c].arity > 6) die("(rep %s intern): constructor %s has more than 6 children", sorts[ctors[c].sort].name, ctors[c].name);
        for (int i = 0; i < ctors[c].arity; i++)
            if (!identity_sort(ctors[c].asort[i]))
                die("(rep %s intern): child %d of %s is of sort %s, which is neither interned, immediate nor nullary",
                    sorts[ctors[c].sort].name, i, ctors[c].name, sorts[ctors[c].asort[i]].name);
    }
}

/* ---- the binding table ---- */
static char *sx_text(const Sx *x) {
    if (x->k != SX_LIST) return xstrdup(x->s);
    size_t cap = 256, n = 0;
    char *b = xmalloc(cap);
    b[n++] = '(';
    for (int i = 0; i < x->n; i++) {
        char *t = sx_text(x->v[i]);
        size_t lt = strlen(t);
        while (n + lt + 4 > cap) { cap *= 2; b = xrealloc(b, cap); }
        if (i) b[n++] = ' ';
        memcpy(b + n, t, lt);
        n += lt;
        free(t);
    }
    b[n++] = ')';
    b[n] = 0;
    return b;
}
static void check_label(const char *path, const char *name, const char *label) {
    if (strcmp(label, "proved") && strcmp(label, "checked") && strcmp(label, "trusted"))
        die("%s: %s: label is proved, checked or trusted", path, name);
}
void read_blocks(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) die("cannot open %s", path);
    char *line = NULL; size_t cap = 0;
    while (getline(&line, &cap, f) > 0) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '(') continue;
        Sx *x = sx_read(&p);
        if (!x) continue;
        if (sx_head(x, "rep")) {
            if (x->n < 3) die("%s: (rep SORT KIND PARAM...)", path);
            const char *sname = sx_str(x->v[1], "sort");
            int sort = sort_id(sname);
            if (sort < 0) die("%s: unknown sort %s", path, sname);
            char *args[32]; int na = 0;
            for (int i = 3; i < x->n && na < 32; i++) args[na++] = xstrdup(sx_atom(x->v[i], "parameter"));
            set_rep(sort, sx_atom(x->v[2], "kind"), args, na, path);
            continue;
        }
        if (sx_head(x, "memo")) {
            if (x->n != 5) die("%s: (memo NAME JUDGMENT LABEL WHY)", path);
            const char *jn = sx_str(x->v[2], "judgment");
            int c = map_get(&ctor_map, jn);
            if (c < 0 || !ctors[c].defined) die("%s: memo: %s is not a judgment", path, jn);
            MemoRow m = {c, xstrdup(sx_str(x->v[1], "name")), xstrdup(sx_str(x->v[3], "label")), xstrdup(sx_str(x->v[4], "justification"))};
            check_label(path, m.name, m.label);
            if (nmemo_rows == 64) die("%s: too many memo rows", path);
            memo_rows[nmemo_rows++] = m;
            continue;
        }
        if (sx_head(x, "atoms-source")) {
            if (x->n != 5) die("%s: (atoms-source SORT CONS CP NIL)", path);
            int sort = sort_id(sx_str(x->v[1], "sort"));
            if (sort < 0 || sorts[sort].native != REP_ATOMS) die("%s: atoms-source: %s is not an atoms sort (its rep row first)", path, x->v[1]->s);
            static const int want[3] = {2, 1, 0};
            for (int i = 0; i < 3; i++) {
                int c = ctor_named(sx_atom(x->v[2 + i], "constructor"), path);
                if (ctors[c].sort != sort || ctors[c].defined || ctors[c].arity != want[i] || ctors[c].lit)
                    die("%s: atoms-source: %s is not a data constructor of %s of %d arguments", path, ctors[c].name, sorts[sort].name, want[i]);
                for (int a = 0; a < ctors[c].arity; a++)
                    if (ctors[c].asort[a] != sort) die("%s: atoms-source: %s takes an argument of another sort", path, ctors[c].name);
                sorts[sort].src[i] = c;
            }
            sorts[sort].source = true;
            continue;
        }
        if (sx_head(x, "test")) {
            if (x->n != 6 || x->v[3]->k != SX_LIST || x->v[3]->n != 1) die("%s: (test NAME RELATION (COMPONENT) LABEL WHY)", path);
            const char *kn = sx_atom(x->v[3]->v[0], "test component");
            int kind = -1;
            for (int i = 0; i < TEST_KINDS; i++) if (!strcmp(test_kind_name[i], kn)) kind = i;
            if (kind < 0) die("%s: unknown test component %s", path, kn);
            TestRow t = {xstrdup(sx_str(x->v[2], "relation")), kind, kind == TEST_SCALAR || kind == TEST_NON_SCALAR ? 1 : 2, -1,
                         xstrdup(sx_str(x->v[1], "name")), xstrdup(sx_str(x->v[4], "label")), xstrdup(sx_str(x->v[5], "justification"))};
            check_label(path, t.name, t.label);
            for (int i = 0; i < ntest_rows; i++)
                if (!strcmp(test_rows[i].relation, t.relation)) die("%s: relation %s has two test rows", path, t.relation);
            if (ntest_rows == 64) die("%s: too many test rows", path);
            test_rows[ntest_rows++] = t;
            continue;
        }
        bool law = sx_head(x, "law");
        if (!law && !sx_head(x, "block")) die("%s: unknown row", path);
        if (x->n != 6) die("%s: a %s row has NAME JUDGMENT %s LABEL WHY", path, law ? "law" : "block", law ? "RESULT" : "IMPLEMENTATION");
        BlockRow r = {law, xstrdup(sx_str(x->v[1], "block name")), xstrdup(sx_str(x->v[2], "judgment")), NULL,
                      xstrdup(sx_str(x->v[4], "label")), xstrdup(sx_str(x->v[5], "justification")), NULL};
        if (!law && x->v[3]->k == SX_LIST) { r.comp = x->v[3]; r.fn = sx_text(x->v[3]); }
        else r.fn = xstrdup(sx_str(x->v[3], law ? "result" : "implementation"));
        check_label(path, r.name, r.label);
        VEC_PUSH(block_rows, nblock_rows, block_row_cap, r);
    }
    free(line);
    fclose(f);
}

/* ---- components: the shape each needs of its judgment ---- */
static int comp_ctor(const NativeOp *b, const Sx *x, int i, int sort, int arity) {
    if (i >= x->n) die("block %s: %s needs more parameters", b->name, b->fn);
    const char *nm = sx_atom(x->v[i], "constructor");
    int c = map_get(&ctor_map, nm);
    if (c < 0 || ctors[c].defined || (sort >= 0 && ctors[c].sort != sort) || (arity >= 0 && ctors[c].arity != arity))
        die("block %s: %s is not a constructor of the right sort and arity", b->name, nm);
    return c;
}
static int list_sort_ctors(int s, int *nil, int *cons) {
    *nil = *cons = -1;
    for (int c = 0; c < nctors; c++) {
        if (ctors[c].defined || ctors[c].sort != s) continue;
        if (ctors[c].arity == 0) *nil = c;
        else if (ctors[c].arity == 2 && ctors[c].asort[1] == s) *cons = c;
    }
    return sorts[s].ndata == 2 && *nil >= 0 && *cons >= 0;
}
void check_component(NativeOp *b) {
    const Sx *x = b->comp;
    const Ctor *k = &ctors[b->ctor];
    const char *kind = sx_atom(x->v[0], "component");
    int r = k->sort;
    #define NEED(cond, msg) do { if (!(cond)) die("block %s: %s: %s", b->name, kind, msg); } while (0)
    if (!strcmp(kind, "equal")) {
        NEED(k->arity == 2 && k->asort[0] == k->asort[1] && identity_sort(k->asort[0]) && sorts[k->asort[0]].native,
             "two arguments of one interned or immediate sort");
        comp_ctor(b, x, 1, r, 0); comp_ctor(b, x, 2, r, 0);
    } else if (!strcmp(kind, "bytes-order") || !strcmp(kind, "nat-order") || !strcmp(kind, "structural-order")) {
        int want = !strcmp(kind, "bytes-order") ? REP_BYTES : !strcmp(kind, "nat-order") ? REP_NAT : -1;
        NEED(k->arity == 2 && k->asort[0] == k->asort[1], "two arguments of one sort");
        int rep = sorts[k->asort[0]].native;
        NEED(want < 0 ? (rep == REP_INTERN || rep == REP_BYTES || REP_IMMNAT(rep) || rep == REP_WORD)
                      : want == REP_NAT ? (REP_IMMNAT(rep) || rep == REP_BINARY) : rep == want,
             "arguments of the component's representation");
        for (int i = 1; i <= 3; i++) comp_ctor(b, x, i, r, 0);
    } else if (!strcmp(kind, "bytes-snoc")) {
        NEED(k->arity == 1 && k->asort[0] == r && sorts[r].native == REP_BYTES && x->n == 2, "a byte string to a byte string, one hex parameter");
        const char *hex = sx_atom(x->v[1], "bytes");
        NEED(strlen(hex) % 2 == 0 && strspn(hex, "0123456789abcdefABCDEF") == strlen(hex), "bytes as hex digits");
    } else if (!strcmp(kind, "nat-succ")) {
        NEED(k->arity == 1 && k->asort[0] == r && (REP_IMMNAT(sorts[r].native) || sorts[r].native == REP_BINARY), "a natural to a natural");
    } else if (!strcmp(kind, "nat-add")) {
        NEED(k->arity == 2 && k->asort[0] == r && k->asort[1] == r && (REP_IMMNAT(sorts[r].native) || sorts[r].native == REP_BINARY),
             "two naturals to a natural");
    } else if (!strcmp(kind, "nat-pred")) {
        NEED(x->n == 1 && k->arity == 1 && k->asort[0] == r && sorts[r].native == REP_BINARY, "a binary natural to a binary natural");
    } else if (!strcmp(kind, "nat-xor") || !strcmp(kind, "nat-multiply") || !strcmp(kind, "nat-subtract") || !strcmp(kind, "nat-trim")) {
        NEED(x->n == 1 && k->arity == 2 && k->asort[0] == r && k->asort[1] == r && sorts[r].native == REP_BINARY,
             "two binary naturals to a binary natural");
    } else if (!strcmp(kind, "nat-push-bit")) {
        NEED(x->n == 1 && sorts[r].native == REP_BINARY && k->arity == 2 && k->asort[1] == r && k->asort[0] == ctors[sorts[r].rp[1]].asort[0],
             "a bit and a binary natural to a binary natural");
    } else if (!strcmp(kind, "word-pack")) {
        NEED(x->n == 1 && sorts[r].native == REP_BINARY && k->arity == 2 && sorts[k->asort[0]].native == REP_WORD && k->asort[1] == r,
             "a word and a binary natural to a binary natural");
    } else if (!strcmp(kind, "bytes-length")) {
        NEED(x->n == 1 && sorts[r].native == REP_BINARY && k->arity == 1 && sorts[k->asort[0]].native == REP_SLICES,
             "a slices byte string to a binary natural");
    } else if (!strcmp(kind, "bytes-take")) {
        NEED(k->arity == 2 && sorts[k->asort[0]].native == REP_BINARY && sorts[k->asort[1]].native == REP_SLICES,
             "a binary natural and a slices byte string");
        int tk = comp_ctor(b, x, 1, r, 2);
        comp_ctor(b, x, 2, r, 0);
        NEED(ctors[tk].asort[0] == k->asort[1] && ctors[tk].asort[1] == k->asort[1], "TAKEN holds two byte strings of the argument's sort");
        NEED(x->n == 3, "TAKEN NEED");
    } else if (!strcmp(kind, "bytes-equal")) {
        NEED(k->arity == 2 && k->asort[0] == k->asort[1] && sorts[k->asort[0]].native == REP_SLICES, "two slices byte strings");
        comp_ctor(b, x, 1, r, 0); comp_ctor(b, x, 2, r, 0);
        NEED(x->n == 3, "YES NO");
    } else if (!strcmp(kind, "uleb")) {
        NEED(x->n == 6 && k->arity == 1 && sorts[k->asort[0]].native == REP_SLICES, "a slices byte string; DECODED NEED BAD MAXBYTES LASTBITS");
        int dc = comp_ctor(b, x, 1, r, 2);
        comp_ctor(b, x, 2, r, 0); comp_ctor(b, x, 3, r, 0);
        NEED(sorts[ctors[dc].asort[0]].native == REP_BINARY && ctors[dc].asort[1] == k->asort[0], "DECODED holds a binary natural and the rest");
        char *e1, *e2;
        long mb = strtol(sx_atom(x->v[4], "bytes"), &e1, 10), lb = strtol(sx_atom(x->v[5], "bits"), &e2, 10);
        NEED(!*e1 && !*e2 && mb >= 1 && mb <= 9 && lb >= 1 && lb <= 7 && 7 * (mb - 1) + lb <= 64, "1 <= MAXBYTES <= 9, 1 <= LASTBITS <= 7, at most 64 bits");
    } else if (!strcmp(kind, "fnv-hash")) {
        NEED(x->n == 4 && sorts[r].native == REP_BINARY && k->arity == 1 && sorts[k->asort[0]].native == REP_SLICES,
             "a slices byte string to a binary natural, BASIS PRIME BITS");
        char *e1, *e2, *e3;
        errno = 0;
        unsigned long long basis = strtoull(sx_atom(x->v[1], "basis"), &e1, 0), prime = strtoull(sx_atom(x->v[2], "prime"), &e2, 0),
                           bits = strtoull(sx_atom(x->v[3], "bits"), &e3, 0);
        (void)prime;
        NEED(!errno && !*e1 && !*e2 && !*e3 && bits >= 1 && bits <= 64 && (bits == 64 || basis < (1ull << bits)), "numbers, 1 <= BITS <= 64, BASIS < 2^BITS");
    } else if (!strcmp(kind, "set-union") || !strcmp(kind, "set-remove")) {
        bool uni = !strcmp(kind, "set-union");
        int ls = uni ? k->asort[0] : k->asort[1], nil = -1, cons = -1;
        NEED(k->arity == 2 && r == ls && sorts[ls].native == REP_INTERN && list_sort_ctors(ls, &nil, &cons), "an interned list sort");
        NEED(uni ? k->asort[1] == ls : k->asort[0] == ctors[cons].asort[0], "the arguments of the operation");
        const char *cn = sx_atom(x->v[1], "comparator");
        int cmp = map_get(&ctor_map, cn);
        NEED(cmp >= 0 && ctors[cmp].defined && ctors[cmp].arity == 2 && ctors[cmp].asort[0] == ctors[cons].asort[0] &&
             ctors[cmp].asort[1] == ctors[cons].asort[0], "a comparator judgment on the elements");
        for (int i = 2; i <= 4; i++) comp_ctor(b, x, i, ctors[cmp].sort, 0);
    } else if (!strncmp(kind, "table-", 6)) {
        int ts = -1;
        for (int s = 0; s < nsorts; s++) if (sorts[s].native == REP_TABLE) ts = s;
        NEED(ts >= 0, "an array-table representation");
        int nat = ctors[table_ctor[0]].asort[0], cell = ctors[table_ctor[2]].asort[0];
        if (!strcmp(kind, "table-get")) NEED(k->arity == 2 && k->asort[0] == ts && k->asort[1] == nat && r == cell, "Table Nat -> Cell");
        else if (!strcmp(kind, "table-put")) NEED(k->arity == 2 && k->asort[0] == ts && k->asort[1] == cell && r == ts, "Table Cell -> Table");
        else if (!strcmp(kind, "table-set")) NEED(k->arity == 3 && k->asort[0] == ts && k->asort[1] == nat && k->asort[2] == cell && r == ts, "Table Nat Cell -> Table");
        else if (!strcmp(kind, "table-next")) NEED(k->arity == 1 && k->asort[0] == ts && r == nat, "Table -> Nat");
        else if (!strcmp(kind, "table-empty")) NEED(k->arity == 0 && r == ts, "-> Table");
        else die("block %s: unknown component %s", b->name, kind);
        NEED(REP_IMMNAT(sorts[nat].native), "naturals as immediates");
    } else {
        die("block %s: unknown component %s", b->name, kind);
    }
    #undef NEED
}
/* the rows as blocks, memo judgments, laws (laws need the coverage analysis: apply_laws) */
void apply_blocks(void) {
    for (int i = 0; i < nblock_rows; i++) {
        BlockRow *b = &block_rows[i];
        if (b->law) continue;
        int c = map_get(&ctor_map, b->judgment);
        if (c < 0 || !ctors[c].defined) die("block %s: %s is not a judgment", b->name, b->judgment);
        for (int j = 0; j < nnative_ops; j++) if (native_ops[j].ctor == c) die("block %s: %s is bound twice", b->name, b->judgment);
        if (nnative_ops == 64) die("too many blocks");
        NativeOp op = {c, b->fn, b->why, b->name, b->label, b->comp};
        if (op.comp) {
            check_component(&op);
            char nm[32];
            snprintf(nm, sizeof nm, "rc_bind_%d", nnative_ops);
            op.fn = xstrdup(nm);
        }
        native_ops[nnative_ops++] = op;
    }
    int *slots_of_sort = xcalloc((size_t)nsorts, sizeof(int));
    for (int c = 0; c < nctors; c++) ctors[c].memo_slot = -1;
    for (int i = 0; i < nmemo_rows; i++) {
        int c = memo_rows[i].ctor;
        const Ctor *k = &ctors[c];
        if (native_op_of(c) >= 0) die("memo %s: %s has a block", memo_rows[i].name, k->name);
        if (k->arity < 1 || k->arity > 4) die("memo %s: %s needs 1 to 4 arguments", memo_rows[i].name, k->name);
        for (int a = 0; a < k->arity; a++)
            if (!identity_sort(k->asort[a])) die("memo %s: argument %d of %s is not interned, immediate or nullary", memo_rows[i].name, a, k->name);
        if (!identity_sort(k->sort)) die("memo %s: the result sort of %s is not interned, immediate or nullary", memo_rows[i].name, k->name);
        ctors[c].memo = true;
        /* one interned argument: the result is kept in a slot of the argument's node */
        if (k->arity == 1 && sorts[k->asort[0]].native == REP_INTERN) ctors[c].memo_slot = slots_of_sort[k->asort[0]]++;
    }
    for (int s = 0; s < nsorts; s++) if (slots_of_sort[s] > 8) die("more than 8 memo slots on sort %s", sorts[s].name);
    sorts_memo_slots = slots_of_sort;
}
void apply_laws(void) {
    for (int i = 0; i < nblock_rows; i++) {
        BlockRow *b = &block_rows[i];
        if (!b->law) continue;
        int c = map_get(&ctor_map, b->judgment), r = map_get(&ctor_map, b->fn);
        if (c < 0 || !ctors[c].defined || ctors[c].arity != 2 || ctors[c].asort[0] != ctors[c].asort[1])
            die("law %s: %s is not a judgment of two arguments of one sort", b->name, b->judgment);
        if (r < 0 || ctors[r].defined || ctors[r].arity != 0 || ctors[r].sort != ctors[c].sort)
            die("law %s: %s is not a nullary constructor of %s's sort", b->name, b->fn, b->judgment);
        if (!ctors[c].complete) die("law %s: %s does not cover every input", b->name, b->judgment);
        for (int j = 0; j < nnative_ops; j++) if (native_ops[j].ctor == c) die("law %s: %s already has a block", b->name, b->judgment);
        if (ctors[c].memo) die("law %s: %s is memoised", b->name, b->judgment);
        laws[nlaws++] = (Law){c, r, b->why, b->name, b->label};
    }
}
/* a memoised judgment is called, not tail-called: it must not reach the safepoint */
void check_memo_safepoint(const bool *reaches) {
    for (int i = 0; i < nmemo_rows; i++)
        if (reaches && reaches[memo_rows[i].ctor]) die("memo %s: %s reaches the safepoint", memo_rows[i].name, ctors[memo_rows[i].ctor].name);
}

int native_op_of(int c) { for (int i = 0; i < nnative_ops; i++) if (native_ops[i].ctor == c) return i; return -1; }
int law_of(int c) { for (int i = 0; i < nlaws; i++) if (laws[i].ctor == c) return i; return -1; }
void write_rule_labels(FILE *o, int c) {
    fputs(" (rules", o);
    for (int i = 0; i < ctors[c].nrules; i++) fprintf(o, " \"%s\"", rules[ctors[c].rules[i]].label);
    fputc(')', o);
}
void write_census_blocks(const char *path) {
    FILE *o = fopen(path, "a");
    if (!o) die("cannot write %s", path);
    fprintf(o, "\njustified blocks (the judgment's rules are the contract):\n");
    fprintf(o, "  %-22s %-26s %5s  %-8s %-30s %s\n", "block", "judgment", "rules", "label", "implementation", "justification");
    for (int i = 0; i < nnative_ops; i++)
        fprintf(o, "  %-22s %-26s %5d  %-8s %-30s %s\n", native_ops[i].name ? native_ops[i].name : native_ops[i].fn, ctors[native_ops[i].ctor].name,
                ctors[native_ops[i].ctor].nrules, native_ops[i].label ? native_ops[i].label : "",
                native_ops[i].comp ? sx_text(native_ops[i].comp) : native_ops[i].fn, native_ops[i].why ? native_ops[i].why : "");
    for (int i = 0; i < nmemo_rows; i++)
        fprintf(o, "  %-22s %-26s %5d  %-8s %-30s %s\n", memo_rows[i].name, ctors[memo_rows[i].ctor].name, ctors[memo_rows[i].ctor].nrules,
                memo_rows[i].label, "(memo)", memo_rows[i].why);
    for (int i = 0; i < nlaws; i++)
        fprintf(o, "  %-22s %-26s %5d  %-8s law %s(x, x) = %s; %s\n", laws[i].name ? laws[i].name : "", ctors[laws[i].ctor].name,
                ctors[laws[i].ctor].nrules, laws[i].label ? laws[i].label : "", ctors[laws[i].ctor].name, ctors[laws[i].result].name,
                laws[i].why ? laws[i].why : "");
    for (int i = 0; i < ntest_rows; i++)
        fprintf(o, "  %-22s %-26s %5s  %-8s (%s) %s\n", test_rows[i].name, test_rows[i].relation, "test", test_rows[i].label,
                test_kind_name[test_rows[i].kind], test_rows[i].why);
    int nsym = 0;
    for (int s2 = 0; s2 < nsorts; s2++) if (sorts[s2].native) nsym++;
    fprintf(o, "sorts with a native representation: %d\n", nsym);
    for (int s2 = 0; s2 < nsorts; s2++)
        if (sorts[s2].native) fprintf(o, "  %-10s %s\n", sorts[s2].name, sort_rep(s2));
    fclose(o);
}
void write_block_table(const char *path, const char *mode) {
    FILE *o = fopen(path, mode);
    if (!o) die("cannot write %s", path);
    for (int s = 0; s < nsorts; s++)
        if (sorts[s].native) fprintf(o, "(rep \"%s\" \"%s\")\n", sorts[s].name, sort_rep(s));
    for (int s = 0; s < nsorts; s++)
        if (sorts[s].source) {
            fprintf(o, "(atoms-source \"%s\"", sorts[s].name);
            for (int i = 0; i < 3; i++) { fputc(' ', o); fput_quoted(o, ctors[sorts[s].src[i]].name); }
            fputs(")\n", o);
        }
    for (int i = 0; i < nnative_ops; i++) {
        char *impl = native_ops[i].comp ? sx_text(native_ops[i].comp) : NULL;
        fprintf(o, "(block \"%s\" \"%s\" ", native_ops[i].name ? native_ops[i].name : "", ctors[native_ops[i].ctor].name);
        if (impl) fputs(impl, o); else fprintf(o, "\"%s\"", native_ops[i].fn);
        fprintf(o, " \"%s\" \"%s\"", native_ops[i].label ? native_ops[i].label : "", native_ops[i].why ? native_ops[i].why : "");
        write_rule_labels(o, native_ops[i].ctor);
        fputs(")\n", o);
        free(impl);
    }
    for (int i = 0; i < nmemo_rows; i++) {
        fprintf(o, "(memo \"%s\" \"%s\" \"%s\" \"%s\"", memo_rows[i].name, ctors[memo_rows[i].ctor].name, memo_rows[i].label, memo_rows[i].why);
        write_rule_labels(o, memo_rows[i].ctor);
        fputs(")\n", o);
    }
    for (int i = 0; i < ntest_rows; i++)
        fprintf(o, "(test \"%s\" \"%s\" (%s) \"%s\" \"%s\")\n", test_rows[i].name, test_rows[i].relation, test_kind_name[test_rows[i].kind],
                test_rows[i].label, test_rows[i].why);
    for (int i = 0; i < nlaws; i++) {
        fprintf(o, "(law \"%s\" \"%s\" \"%s\" \"%s\" \"%s\"", laws[i].name ? laws[i].name : "", ctors[laws[i].ctor].name,
                ctors[laws[i].result].name, laws[i].label ? laws[i].label : "", laws[i].why ? laws[i].why : "");
        write_rule_labels(o, laws[i].ctor);
        fputs(")\n", o);
    }
    fclose(o);
}

/* ---- test components ----
 * A test answers the queries of a relation whose every argument is bound, on an atoms sort.  The
 * analysis (check.c) knows each test's algebra: term-equal is unification, term-different fails on
 * two identical terms, a test and its complement never both hold on the same arguments, and an
 * integer test on literals is decided. */
const char *const test_kind_name[TEST_KINDS] = {"term-equal", "term-different", "int-less", "int-not-less", "int-gap", "int-no-gap",
                                                "unicode-scalar", "non-unicode-scalar"};
int test_of(const char *relation, int arity) {
    for (int i = 0; i < ntest_rows; i++) if (!strcmp(test_rows[i].relation, relation) && test_rows[i].arity == arity) return i;
    return -1;
}
int test_complement(int kind) {
    static const int comp[TEST_KINDS] = {TEST_DIFFERENT, TEST_EQUAL, TEST_INT_NOT_LESS, TEST_INT_LESS, TEST_INT_NO_GAP, TEST_INT_GAP,
                                         TEST_NON_SCALAR, TEST_SCALAR};
    return comp[kind];
}
/* an integer test on integers v[0], v[1] (the equality tests are decided by the analysis on terms) */
bool test_holds_on(int kind, const int64_t *v) {
    switch (kind) {
    case TEST_INT_LESS: return v[0] < v[1];
    case TEST_INT_NOT_LESS: return v[1] <= v[0];
    case TEST_INT_GAP: return v[0] < INT64_MAX && v[0] + 1 < v[1];
    case TEST_INT_NO_GAP: return v[0] == INT64_MAX || v[1] <= v[0] + 1;
    case TEST_SCALAR: return (v[0] >= 0 && v[0] < 0xD800) || (v[0] > 0xDFFF && v[0] <= 0x10FFFF);
    case TEST_NON_SCALAR: return !((v[0] >= 0 && v[0] < 0xD800) || (v[0] > 0xDFFF && v[0] <= 0x10FFFF));
    default: return true;
    }
}
/* every test row names a relation the rules query, of the test's arity, on the module's atoms sort */
void apply_tests(void) {
    int atoms = -1;
    for (int s = 0; s < nsorts; s++) if (sorts[s].native == REP_ATOMS) atoms = s;
    for (int i = 0; i < ntest_rows; i++) {
        TestRow *t = &test_rows[i];
        if (atoms < 0) die("test %s: the module has no atoms sort", t->name);
        t->sort = atoms;
        bool used = false;
        for (int r = 0; r < nrules; r++)
            for (int j = 0; j < rules[r].np; j++) {
                const Prem *p = &rules[r].p[j];
                if (!p->relation || strcmp(p->relation, t->relation)) continue;
                if (p->nrel != t->arity) die("test %s: %s is queried with %d arguments, the test takes %d", t->name, t->relation, p->nrel, t->arity);
                for (int a = 0; a < p->nrel; a++)
                    if (p->rel[a]->sort != atoms) die("test %s: argument %d of %s in rule %s is not of the atoms sort", t->name, a, t->relation, rules[r].label);
                used = true;
            }
        if (!used) die("test %s: no rule queries %s", t->name, t->relation);
    }
}
