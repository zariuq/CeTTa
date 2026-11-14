/* rc_intern.h - an interning store for the values of sorts declared (rep SORT intern) or
 * (rep SORT bytes): hash-consed constructor nodes and byte strings.  Generic: a node is a
 * constructor id and its children, and nothing in this component depends on a particular module.
 *
 * Contract.
 *   - rc_intern_node(c, n, kids) returns the node of constructor c with exactly these children;
 *     rc_intern_bytes(b, n) the node of exactly these bytes.  A second call with equal arguments
 *     returns the same node, so two values of an interned sort are equal as module data exactly
 *     when they are the same pointer (the store invariant of RuleModuleInterning.lean: children are
 *     earlier nodes, equal contents are one node; theorem interning_eq_iff).
 *   - A child is a value of the module: an interned node (the pointer tagged RC_INTERN_TAG), an
 *     immediate, or a static nullary constructor.  It is never a term of the moving heap; rulec
 *     checks that every child sort of an interned sort is interned, immediate or nullary-only.
 *   - Nodes do not move.  rc_intern_collect frees the nodes not reachable from the module's live
 *     values (rc_walk_live: the roots at a safepoint, the old space after a full copy, the static
 *     constants) and from the store's own nullary nodes; it runs only where the host calls it (the
 *     module's safepoint hook, or between runs).
 *   - Memo tables (rc_memo.h) are cleared before a collection: they are caches, not roots.
 * Label: the invariant is proved (RuleModuleInterning.lean) for the model of this store; that this
 * C code is the model (lookup by contents, append on a miss) is trusted.
 */
#ifndef RC_INTERN_H
#define RC_INTERN_H

#include "../rule_runtime/rc_rt.h"

typedef struct RcINode {
    uint32_t hash;
    uint16_t ctor;      /* the constructor id, or RC_INTERN_BYTES */
    uint8_t mark;
    uint8_t nslots;     /* memo slots after the children (rc_ctor_slots[ctor]) */
    uint32_t n;         /* number of children, or of bytes */
    uint32_t pad2;
    T *kid[];           /* the children, then nslots memo slots; for a byte string, the bytes */
} RcINode;


#define RC_INTERN_TAG 2u
#define RC_INTERN_BYTES 0xFFFFu
#define RC_INODE(x) ((RcINode *)RC_FOREIGN_PTR(x))
#define RC_IVAL(p) RC_FOREIGN((p), RC_INTERN_TAG)
#define RC_IS_INODE(x) (RC_IS_FOREIGN(x) && RC_FOREIGN_TAG(x) == RC_INTERN_TAG)
/* memo slots: a memoised judgment of one interned argument keeps its result in a slot of the
 * argument's node (rulec numbers the slots per sort: rc_ctor_slots); NULL is "not computed",
 * RC_SLOT_NONE a kept failure; the slots are cleared when the store collects */
extern const uint8_t rc_ctor_slots[];
#define RC_SLOT_NONE ((T *)(uintptr_t)8)
static inline T **rc_intern_slot(T *x, int i) { RcINode *p = RC_INODE(x); return &p->kid[p->n + (uint32_t)i]; }

T *rc_intern_node(uint16_t ctor, uint32_t n, T *const *kids);
T *rc_intern_bytes(const uint8_t *b, size_t n);
static inline const uint8_t *rc_ibytes(T *x) { return (const uint8_t *)RC_INODE(x)->kid; }
static inline size_t rc_ibytes_len(T *x) { return RC_INODE(x)->n; }

/* views of a sort declared (rep SORT intern): the generated code reads and builds its values only
 * through these */
static inline uint16_t RC_TAG_intern(T *x) { return RC_INODE(x)->ctor; }
static inline T *RC_KID_intern(T *x, int i) { return RC_INODE(x)->kid[i]; }
T *rc_intern_nullary(uint16_t ctor);   /* rc_intern_node(ctor, 0, NULL), kept per constructor */
#define RC_NUL_intern(c) rc_intern_nullary((uint16_t)(c))
static inline T *RC_MK1_intern(uint16_t c, T *a) { T *k[1] = {a}; return rc_intern_node(c, 1, k); }
static inline T *RC_MK2_intern(uint16_t c, T *a, T *b) { T *k[2] = {a, b}; return rc_intern_node(c, 2, k); }
static inline T *RC_MK3_intern(uint16_t c, T *a, T *b, T *d) { T *k[3] = {a, b, d}; return rc_intern_node(c, 3, k); }
static inline T *RC_MK4_intern(uint16_t c, T *a, T *b, T *d, T *e) { T *k[4] = {a, b, d, e}; return rc_intern_node(c, 4, k); }
static inline T *RC_MK5_intern(uint16_t c, T *a, T *b, T *d, T *e, T *f) { T *k[5] = {a, b, d, e, f}; return rc_intern_node(c, 5, k); }
static inline T *RC_MK6_intern(uint16_t c, T *a, T *b, T *d, T *e, T *f, T *g) { T *k[6] = {a, b, d, e, f, g}; return rc_intern_node(c, 6, k); }

/* the store's life: begin a run (a store is made if there is none); end a run (a store above
 * RC_INTERN_KEEP_BYTES is collected from the static constants and the host's held values, and freed if
 * still above and nothing is held); free it.  rc_intern_fresh(true) makes every run begin with a new
 * store. */
void rc_intern_begin(void);
void rc_intern_end(void);
void rc_intern_free(void);
void rc_intern_fresh(bool fresh);
bool rc_intern_active(void);
/* values a host keeps across runs (handles): a collection marks every value the hook reports (it
 * calls mark once per held value and returns how many it reported; with mark NULL it only counts);
 * the epoch changes whenever the store is freed, after which no value of an earlier epoch may be used */
extern size_t (*rc_intern_held_hook)(void (*mark)(T *v));
uint64_t rc_intern_epoch(void);
/* the module's safepoint: a collection when the store has grown by its live size (at least 64 MiB)
 * since the last one; the heap is copied first so that it holds exactly the live terms */
void rc_intern_safepoint(int nroots);
void rc_intern_collect(int nroots);
typedef struct { uint64_t nodes, bytes_live, collections, freed; } RcInternStats;
RcInternStats rc_intern_stats(void);
/* hooks of the components that keep caches keyed by nodes (rc_memo): called before a collection
 * and when the store is freed */
void rc_intern_on_collect(void (*fn)(void));

#endif
