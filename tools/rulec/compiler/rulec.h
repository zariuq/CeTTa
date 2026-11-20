/* rulec.h - the rule compiler's shared declarations.
 *
 * rulec compiles a rule module (the core rows CeTTa's module reader prints: TypeDecl,
 * GrammarRuleV2, ScopedRewriteRuleV1) into moded procedures with decision DAGs.  The module stays the
 * specification.  Stages, one file each (see docs/rulec.txt):
 *   util.c      utilities, S-expressions
 *   ir.c        the rule IR: signature (sorts, constructors) and procedures (judgments, rules); the
 *               wire reader
 *   check.c     checks (declarations, sorts, constructor discipline, linearity, binding, overlap
 *               with guard exclusivity), modes and determinism, the call graph, the safepoint
 *   dag.c       decision DAGs (pattern-matrix compilation, hash-consed nodes), the DAG as data
 *   census.c    the census (text and TSV)
 *   bindings.c  representations of sorts and the binding table (components, blocks, laws)
 *   emit_c.c    the C back end
 *   main.c      options
 */
#ifndef RULEC_H
#define RULEC_H
#define _GNU_SOURCE
#include <ctype.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VEC_PUSH(arr, len, cap, val) do { \
    if ((len) == (cap)) { (cap) = (cap) ? 2 * (cap) : 16; (arr) = xrealloc((arr), (size_t)(cap) * sizeof(*(arr))); } \
    (arr)[(len)++] = (val); } while (0)

/* string -> int map (open addressing) */
typedef struct { char **keys; int *vals; size_t cap, len; } Map;
enum { SX_SYM, SX_STR, SX_LIST };
typedef struct Sx { int k; char *s; int n; struct Sx **v; } Sx;
typedef struct { char *name; int ndata; int *data; int datacap; int native; char *repname; int *extctors; int nextctors;
                 int rp[4]; int width; int src[3]; bool source; } Sort;   /* src: an atoms sort's source list CONS CP NIL */   /* rp, width: a per-sort representation's constructors and word width
                                                    (atoms: the constructors standing for the other integers,
                                                    strings, symbols and expressions) */
typedef struct {
    char *name; int sort; int arity; int *asort; char **aname;
    bool defined; int nrules; int *rules; int rcap;
    int det;          /* 1 det, 0 semidet or nondet (judgments) */
    bool nondet;      /* may give several results: answers are passed to a success continuation */
    bool complete;    /* the group covers every input */
    int dag_root;
    int native_op;    /* index into native_ops or -1 */
    bool memo;        /* results kept per arguments (rc_memo) */
    int memo_slot;    /* a memo of one interned argument: its slot in the argument's node, else -1 */
    int scc;          /* call-graph component */
    bool overlap_open;  /* two of its left sides overlap and their premises are not shown exclusive */
    bool recursive;
    int lit;          /* a nullary constructor of an atoms sort that stands for a literal: LIT_INT, LIT_STRING */
    int64_t ival;     /* LIT_INT: the integer */
    char *sval; int slen;   /* LIT_STRING: the string's bytes */
} Ctor;
enum { LIT_NONE = 0, LIT_INT, LIT_STRING };
typedef struct Pat { int var; int ctor; int n; struct Pat **k; int sort; int ground; } Pat;  /* var >= 0: variable; var == -1: app; var == -2: wildcard;
                                                                                ground: 1 + the index of a kept constant (emit_c) */
/* a premise is a call J(args) ~> target, or a query of a relation of the module (no target): the
 * relation's rows are host data, or a test component the binding table names (test >= 0); a query
 * that binds a variable is counted by the census but not compiled */
typedef struct { Pat *src, *tgt; bool *eqv; char *relation; int nrel; Pat **rel; bool binds; int test; } Prem;
typedef struct {
    int id; char *label; int nv; char **vn; int *vs;
    Pat *lhs, *rhs; int np; Prem *p; int head; int layer;
    bool tail;        /* the last premise is a tail call */
} Rule;
typedef struct { int len; int *p; int *s; } Occ;
enum { N_FAIL, N_LEAF, N_SWITCH };
typedef struct { int kind; int rule; int occ; int sort; int ncase; int *cctor; int *cchild; int dflt; int ncand; int *cand; int nexcl; int *excl; } Node;
typedef struct { int rule; Pat **cols; } Row;
/* how an overlapping pair of rules (left sides that unify) is resolved; see check.c */
enum { OV_TARGETS, OV_EVALUATED, OV_UNRESOLVED, OV_COUNT };
typedef struct { int judgment, a, b, cls; } OverlapPair;
#define MAXLAYERS 16
enum { REP_SYMBOLIC = 0, REP_RECORDS, REP_NAT, REP_STR, REP_EXT, REP_INTERN, REP_BYTES, REP_TABLE, REP_UNARY, REP_WORD, REP_BINARY,
       REP_SLICES, REP_ATOMS, REP_COUNT };
/* representations with parameters of their own per sort (Sort.rp, Sort.width; views named by sort) */
#define REP_PERSORT(r) ((r) == REP_WORD || (r) == REP_BINARY || (r) == REP_SLICES || (r) == REP_ATOMS)
/* naturals held as immediates: binary numerals (nat) or unary ones (unary) */
#define REP_IMMNAT(r) ((r) == REP_NAT || (r) == REP_UNARY)
/* a block: judgment ctor answered by fn (a C function the back end binds) or by a generic component
 * (comp: the component's expression in the binding table; fn is then the generated glue) */
typedef struct { int ctor; char *fn; char *why; char *name; char *label; Sx *comp; } NativeOp;
typedef struct { int ctor; int result; char *why; char *name; char *label; } Law;
typedef struct { bool law; char *name, *judgment, *fn, *label, *why; Sx *comp; } BlockRow;
/* a memo row: judgment ctor keeps its results per arguments */
typedef struct { int ctor; char *name; char *label; char *why; } MemoRow;
/* a test row: the relation's queries with every argument bound are answered by a test component */
enum { TEST_EQUAL, TEST_DIFFERENT, TEST_INT_LESS, TEST_INT_NOT_LESS, TEST_INT_GAP, TEST_INT_NO_GAP, TEST_SCALAR, TEST_NON_SCALAR,
       TEST_KINDS };
typedef struct { char *relation; int kind; int arity; int sort; char *name; char *label; char *why; } TestRow;

/* ---- shared state ---- */
/* ir.c */
extern Sort *sorts; extern int nsorts, sortcap;
extern Ctor *ctors; extern int nctors, ctorcap;
extern Rule *rules; extern int nrules, rulecap;
extern Map sort_map, ctor_map;
extern char **declared_judgments; extern int ndeclared_judgments;   /* (Judgment NAME) rows */
extern int nerrors;
extern char *module_name, *theory_name;
/* check.c */
extern bool census_only;
extern int n_overlaps, n_overlaps_exclusive, n_overlaps_unresolved;
extern int n_overlaps_class[OV_COUNT];
extern OverlapPair *overlap_pairs; extern int noverlap_pairs;
extern const char *overlap_class_name[OV_COUNT];
extern int *tj_index, *tj_low, *tj_stack, tj_sp, tj_counter, nscc;
extern bool *tj_on;
/* dag.c */
extern Occ *occs; extern int noccs, occcap;
extern Map occ_map;
extern Node *nodes; extern int nnodes, nodecap;
extern Map node_map;
extern Pat wildcard;
/* census.c */
extern char *layer_name[MAXLAYERS]; extern char *layer_first[MAXLAYERS]; extern int nlayers;
/* bindings.c */
extern const char *rep_name[];
extern const char *rep_header;
extern int rep_ctor[REP_COUNT][4];
extern int table_ctor[4];             /* TABLE LEAF NODE NONE of an array-table representation */
extern MemoRow memo_rows[64]; extern int nmemo_rows;
extern int *sorts_memo_slots;
extern const char *component_prefix; /* where the generated code finds the component headers */
extern int str_nib[16];
extern Law laws[64]; extern int nlaws;
extern TestRow test_rows[64]; extern int ntest_rows;
extern const char *const test_kind_name[TEST_KINDS];
extern bool functional_only;
extern NativeOp native_ops[64]; extern int nnative_ops;
extern char *safepoint_name;
extern int safepoint_ctor;
extern bool no_musttail;
extern bool cache_constants;
extern const char *blocks_file;
extern BlockRow *block_rows; extern int nblock_rows, block_row_cap;
extern const char *include_rt;
extern const char *source_digest;
/* emit_c.c */
extern bool *var_used;
extern int tmpno;
extern const bool *cur_eqv;

/* ---- functions ---- */
void die(const char *fmt, ...);
extern jmp_buf *rulec_die_jump;   /* set by rulec_main: die returns there instead of ending the process */
void fput_quoted(FILE *o, const char *s);
void fput_cstring(FILE *o, const char *s);
void *xmalloc(size_t n);
void *xcalloc(size_t a, size_t b);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
uint64_t hash_str(const char *s);
int map_get(const Map *m, const char *k);
void map_put(Map *m, const char *k, int v);
Sx *sx_read(const char **pp);
bool sx_is_sym(const Sx *x, const char *s);
bool sx_head(const Sx *x, const char *s);
const char *sx_str(const Sx *x, const char *what);
int sx_items(const Sx *x, Sx ***out);
void err(const Rule *r, const char *fmt, ...);
int sort_id(const char *s);
Pat *pat_from(Sx *x, Rule *r);
void load_wire(const char *path);
void flatten_calls(void);
extern int n_flattened_rules, n_flattened_calls;
bool pat_has_judgment(const Pat *p);
void pat_vars(const Pat *p, int *count);
bool pat_tests_bound(const Pat *p, const bool *eqv);
bool pat_unify(const Pat *a, const Pat *b);
bool pat_equal(const Pat *a, const Pat *b);
int ut_from(const Pat *p, int off);
int u_resolve(int t);
bool u_occurs(int v, int t);
bool u_unify(int a, int b);
bool u_equal(int a, int b);
int overlap_class(const Rule *a, const Rule *b);
void u_reset(void);
int u_fresh(int n);
void u_undo(int mark);
int ut_push(int ctor, int n, const int *kids);
int ut_var(void);
bool rule_excluded_at(const Rule *r, int input);
int test_of(const char *relation, int arity);
int test_complement(int kind);
bool test_holds_on(int kind, const int64_t *v);
void check_heads(void);
void check_rules(void);
bool irrefutable(const Pat *p);
bool prem_irrefutable(const Prem *p);
bool relational_rule(const Rule *r);
void analyse_determinism(void);
void tarjan(int v);
void analyse_calls(void);
void check_safepoint(void);
int occ_intern(const int *p, const int *s, int len);
int occ_child(int occ, int k, int sort);
int node_intern(Node *n);
int dag_compile(int ncol, const int *occ, const int *osort, Row *rows, int nrow);
void build_dags(void);
void dag_reach(int n, bool *seen, int *order, int *norder);
bool dag_has_fail(int root);
int dag_size(int root, int *switches, int *depth);
void write_dag(const char *path);
void assign_layers(void);
int cmp_group_desc(const void *a, const void *b);
int pat_size(const Pat *p);
int pat_depth(const Pat *p);
void write_census(const char *path, const char *tsv);
const char *sort_kind_of_judgment(const Ctor *k);
const char *sort_rep(int s);
void read_blocks(const char *path);
int native_op_of(int c);
void write_rule_labels(FILE *o, int c);
void write_census_blocks(const char *path);
void write_block_table(const char *path, const char *mode);
int law_of(int c);
void parse_native_sort(const char *spec);
bool pat_has_var(const Pat *p);
void occ_expr(char *out, size_t cap, int occ);
void emit_build(FILE *o, const Pat *p);
bool pat_has_used_var(const Pat *p);
void mark_used(const Pat *p);
void emit_lhs_bind(FILE *o, const Pat *p, const char *e);
void emit_tgt_match(FILE *o, const Pat *p, const char *e, int judgment);
const char *target_label(int child, char *buf, size_t cap);
void emit_params(FILE *o, int arity);
void emit_params_k(FILE *o, int arity);
void emit_rules_body(FILE *o, int c);
void emit_proc(FILE *o, int c);
void c_ident(char *out, size_t cap, const char *name);
void emit_views(FILE *o);
void emit_c(const char *path, const char *hpath, const char *wire_path);
const char *sort_kind_of_judgment(const Ctor *k);
const char *sx_atom(const Sx *x, const char *what);
/* bindings.c */
void set_rep(int sort, const char *kind, char **args, int na, const char *where);
void check_reps(void);
void check_component(NativeOp *b);
void apply_blocks(void);
void apply_laws(void);
void check_memo_safepoint(const bool *reaches);
void apply_tests(void);
/* emit_c.c */
void emit_component_glue(FILE *o);
void emit_relation_table(FILE *o);
/* main.c */
int rulec_main(int argc, char **argv);

#endif
