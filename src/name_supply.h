#ifndef CETTA_NAME_SUPPLY_H
#define CETTA_NAME_SUPPLY_H

/*
 * Generated names: the name of a space new-space makes, &space(N), and the
 * identity of a callable a PeTTa lambda makes, lambda_N.
 *
 * A generated name is nominal.  Its only observable properties are equality
 * and freshness: two names are equal exactly when they are the same name,
 * and a new name equals no name given before.  Every observation except its
 * printed spelling and its place in the order of terms is invariant under
 * renaming.  Inside one process the spelled text never reaches what the
 * name names (a space is found by its identity, never by its text), so local
 * safety comes from identity, not from the bits of the name; unguessable
 * bits matter only once a name leaves the process.
 *
 * Names are drawn from a supply, and a draw never fails: no depth, width or
 * count of supplies and names is refused.  The supplies form a split tree,
 * the root and the child split(parent, rank) for each rank 0, 1, ..., and a
 * supply's place in it is its pedigree (Leiserson, Schardl and Sukha,
 * PPoPP 2012): it depends on the program's structure and never on thread
 * scheduling.  A supply keeps its place as a block and a path in it.  The
 * path is one word: a start bit followed by the Elias gamma code of rank + 1
 * for each split since the block began.  The process starts in the root
 * block.  When the next step does not fit the word -- a child whose code
 * would overflow it (depth), a child past the 2^32 - 1 that one word can
 * number (width), or a kind whose counter has given 2^64 - 1 names (count)
 * -- the supply moves on to its successor: a new block, with an empty path
 * and new counters, whose key is
 *
 *     key(new) = SipHash-2-4-128(k, 0x01 || key(left) || path left)
 *
 * (the root block's key is zero).  This is splitting by hashing the split
 * path (Claessen and Palka, Haskell 2013), one hash per filled word.  The
 * key stands for everything before the block, so a block keeps no parent:
 * what a run holds for its names is bounded by its live supplies and names,
 * however deep it goes, and a block is made at most once per filled word,
 * never on an ordinary split or draw.  k is the seed in unforgeable mode and
 * a fixed public key, zero, in counter mode.  A block lives while a supply,
 * a queued task or a name kept by a space refers to it.
 *
 * A name is (block, path, counter): the counter counts the names of its kind
 * the supply has given.  Two modes give names:
 *   counter      the name itself.  Equality compares the block's identity,
 *                the path and the counter.  It is spelled as the path's
 *                ranks and the counter, dot-separated, behind the block's
 *                key when the block is not the root block: the root gives
 *                1, 2, ..., its first child 0.1, 0.2, ..., a grandchild
 *                0.0.1, and a supply beyond the root block
 *                <32 hexadecimal digits>.0.1.
 *   unforgeable  SipHash-2-4 with 128-bit output, keyed by the seed, of
 *                kind, path and counter, and of the block's key beyond the
 *                root block: 32 hexadecimal digits.
 * A spelling is at most CETTA_GENERATED_NAME_SPELLING_MAX - 1 bytes.
 * Collision bounds: names of the root block in counter mode are distinct by
 * construction.  A block key and an unforgeable name are 128 bits: two of n
 * collide with probability at most about n^2 / 2^129, so collisions become
 * likely only near 2^64 blocks (two counter-mode names of different blocks
 * would then spell alike) or 2^64 unforgeable names.
 *
 * This header holds the types, the pure formatter and order, the inline
 * counted draw and split, and the references a kept name holds; drawing past
 * a filled word, hashing and configuration live in name_supply.c.
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The supply is the runtime's own: none of it is exported from a binary or
   a shared library, so its globals are reached directly. */
#define CETTA_NAME_SUPPLY_INTERNAL __attribute__((visibility("hidden")))

typedef enum {
    CETTA_NAME_FORM_NONE = 0,   /* no name */
    CETTA_NAME_FORM_COUNTED,    /* counter mode: block, path and counter */
    CETTA_NAME_FORM_HASHED,     /* unforgeable mode: a 128-bit hash */
} CettaNameForm;

/* The low bits of a name's tag that hold its form; a counted name's block
   is the rest. */
#define CETTA_NAME_FORM_MASK ((uintptr_t)3u)

typedef enum {
    CETTA_NAME_KIND_SPACE = 0,  /* &space(N) */
    CETTA_NAME_KIND_CALLABLE,   /* lambda_N */
    CETTA_NAME_KIND_COUNT
} CettaNameKind;

/* A block of the split tree beyond the root block.  Its key is fixed when
   it is made; its references are counted by every supply, queued task and
   kept name in it. */
typedef struct CettaNameBlock {
    _Atomic uint64_t references;
    uint64_t key_hi;
    uint64_t key_lo;
} CettaNameBlock;

/* A generated name.  Counted: hi is the path, lo the counter, and tag the
   block (NULL for the root block) with the form in its low bits.  Hashed:
   hi and lo are the hash and tag the form.  No name is all zero.  A name has
   no padding and moves as three words; a counted name of a block borrows
   the block from its supply, and one kept longer holds it
   (cetta_generated_name_retain). */
typedef struct {
    uint64_t hi;
    uint64_t lo;
    uintptr_t tag;
} CettaGeneratedName;

/* A node of the split tree.  One thread owns a supply while it draws from it
   or splits it; nothing in it is shared but its block. */
typedef struct {
    /* The block, NULL for the root block; held by this supply. */
    CettaNameBlock *block;
    /* The path in the block: its start bit, then the gamma code of each
       rank + 1. */
    uint64_t path;
    /* The names of each kind given so far in this block. */
    uint64_t drawn[CETTA_NAME_KIND_COUNT];
    /* The children split off so far in this block: the next child's
       rank. */
    uint64_t children;
} CettaNameSupply;

/* A child just split off: the supply it starts as, holding its block, before
   any draw or split. */
typedef struct {
    CettaNameBlock *block;
    uint64_t path;
} CettaNameSupplyChild;

/* The longest numeral: a block key (32 hexadecimal digits and a dot), the
   ranks one word can hold (63 of rank 0, each a digit and a dot) and a
   counter (20 digits); one byte more for the NUL. */
#define CETTA_GENERATED_NAME_NUMERAL_MAX 180u
/* The longest spelling: "&space(" numeral ")" or "lambda_" numeral, and the
   NUL. */
#define CETTA_GENERATED_NAME_SPELLING_MAX 188u

static inline bool cetta_generated_name_is_name(CettaGeneratedName name) {
    return (name.tag & CETTA_NAME_FORM_MASK) != CETTA_NAME_FORM_NONE;
}

static inline CettaNameForm cetta_generated_name_form(CettaGeneratedName name) {
    return (CettaNameForm)(name.tag & CETTA_NAME_FORM_MASK);
}

/* A counted name's block, NULL for the root block and for any other name:
   only a counted name has bits above its form. */
static inline const CettaNameBlock *cetta_generated_name_block(
    CettaGeneratedName name) {
    return (const CettaNameBlock *)(name.tag & ~CETTA_NAME_FORM_MASK);
}

/* Equality: O(1), the block by identity for a counted name. */
static inline bool cetta_generated_name_eq(CettaGeneratedName left,
                                           CettaGeneratedName right) {
    return left.tag == right.tag && left.hi == right.hi &&
           left.lo == right.lo;
}

/* The name as a positive int64 when it is a counted name of the root
   supply whose counter fits, as every name the counter mode's main thread
   draws is. */
static inline bool cetta_generated_name_small(CettaGeneratedName name,
                                              int64_t *number) {
    if (name.tag != (uintptr_t)CETTA_NAME_FORM_COUNTED || name.hi != 1u ||
        name.lo == 0u || name.lo > (uint64_t)INT64_MAX)
        return false;
    if (number)
        *number = (int64_t)name.lo;
    return true;
}

/* The decimal digits of VALUE, most significant first; the count written. */
static inline size_t cetta_generated_name_put_u64(char *out, uint64_t value) {
    char digits[20];
    size_t length = 0u;
    do {
        digits[length++] = (char)('0' + (int)(value % 10u));
        value /= 10u;
    } while (value != 0u);
    for (size_t i = 0u; i < length; i++)
        out[i] = digits[length - 1u - i];
    return length;
}

static inline void cetta_generated_name_put_hex64(char *out, uint64_t value) {
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0u; i < 16u; i++)
        out[i] = hex[(value >> (60u - 4u * i)) & 15u];
}

/* The next rank + 1 of PATH, the low *BITS bits of a path's code below its
   start bit, MSB first; false, with nothing read, when none is left or the
   bits do not hold a code. */
static inline bool cetta_name_path_next(uint64_t path, unsigned *bits,
                                        uint64_t *code) {
    if (*bits == 0u)
        return false;
    uint64_t rest = path & ((UINT64_C(1) << *bits) - 1u);
    if (rest == 0u)
        return false;
    unsigned zeros = *bits - 1u - (63u - (unsigned)__builtin_clzll(rest));
    unsigned width = 2u * zeros + 1u;
    if (width > *bits)
        return false;
    *code = rest >> (*bits - width);
    *bits -= width;
    return true;
}

/* The code bits of PATH below its start bit. */
static inline unsigned cetta_name_path_bits(uint64_t path) {
    return path ? 63u - (unsigned)__builtin_clzll(path) : 0u;
}

/* The numeral of NAME into OUT, NUL-terminated; its length, or 0 for no
   name or an OUT shorter than CETTA_GENERATED_NAME_NUMERAL_MAX. */
static inline size_t cetta_generated_name_numeral(CettaGeneratedName name,
                                                  char *out,
                                                  size_t capacity) {
    if (!out || capacity < CETTA_GENERATED_NAME_NUMERAL_MAX)
        return 0u;
    switch (cetta_generated_name_form(name)) {
    case CETTA_NAME_FORM_HASHED:
        cetta_generated_name_put_hex64(out, name.hi);
        cetta_generated_name_put_hex64(out + 16, name.lo);
        out[32] = '\0';
        return 32u;
    case CETTA_NAME_FORM_COUNTED: {
        size_t length = 0u;
        if (name.hi == 1u && name.tag == (uintptr_t)CETTA_NAME_FORM_COUNTED) {
            /* A root's name: its counter alone. */
            if (name.lo == 0u)
                return 0u;
            length = cetta_generated_name_put_u64(out, name.lo);
            out[length] = '\0';
            return length;
        }
        const CettaNameBlock *block = cetta_generated_name_block(name);
        if (block) {
            cetta_generated_name_put_hex64(out, block->key_hi);
            cetta_generated_name_put_hex64(out + 16, block->key_lo);
            out[32] = '.';
            length = 33u;
        }
        unsigned bits = cetta_name_path_bits(name.hi);
        uint64_t code = 0u;
        while (cetta_name_path_next(name.hi, &bits, &code)) {
            length += cetta_generated_name_put_u64(out + length, code - 1u);
            out[length++] = '.';
        }
        if (bits != 0u || name.hi == 0u || name.lo == 0u)
            return 0u;
        length += cetta_generated_name_put_u64(out + length, name.lo);
        out[length] = '\0';
        return length;
    }
    default:
        return 0u;
    }
}

/* The printed spelling of NAME as a name of KIND into OUT, NUL-terminated:
   &space(N) or lambda_N.  Its length, or 0 for no name or an OUT shorter
   than CETTA_GENERATED_NAME_SPELLING_MAX. */
static inline size_t cetta_generated_name_spell(CettaNameKind kind,
                                                CettaGeneratedName name,
                                                char *out, size_t capacity) {
    if (!out || capacity < CETTA_GENERATED_NAME_SPELLING_MAX)
        return 0u;
    const char *prefix = kind == CETTA_NAME_KIND_SPACE ? "&space(" : "lambda_";
    memcpy(out, prefix, 7u);
    size_t length = cetta_generated_name_numeral(
        name, out + 7u, capacity - 7u);
    if (length == 0u)
        return 0u;
    length += 7u;
    if (kind == CETTA_NAME_KIND_SPACE)
        out[length++] = ')';
    out[length] = '\0';
    return length;
}

static inline int cetta_generated_name_compare_u64(uint64_t left,
                                                   uint64_t right) {
    return left < right ? -1 : left > right ? 1 : 0;
}

/* The order of two paths of one block: by their ranks, lexicographically,
   a path before the paths that extend it. */
static inline int cetta_name_path_compare(uint64_t left, uint64_t right) {
    if (left == right)
        return 0;
    unsigned left_bits = cetta_name_path_bits(left);
    unsigned right_bits = cetta_name_path_bits(right);
    for (;;) {
        uint64_t left_code = 0u, right_code = 0u;
        bool left_more = cetta_name_path_next(left, &left_bits, &left_code);
        bool right_more =
            cetta_name_path_next(right, &right_bits, &right_code);
        if (!left_more || !right_more)
            return left_more ? 1 : right_more ? -1 : 0;
        if (left_code != right_code)
            return left_code < right_code ? -1 : 1;
    }
}

/* A total order on names, deterministic: it reads pedigrees and hashes,
   never addresses, but for two blocks whose 128-bit keys collide.  Counted
   names: the root block before every other, other blocks by key, then by
   path (cetta_name_path_compare), then by counter, so the root's names come
   in the order they were drawn.  Hashed names: by their 128 bits.  O(1)
   but for the paths of one block, which it reads a rank at a time. */
static inline int cetta_generated_name_compare(CettaGeneratedName left,
                                               CettaGeneratedName right) {
    CettaNameForm left_form = cetta_generated_name_form(left);
    CettaNameForm right_form = cetta_generated_name_form(right);
    if (left_form != right_form)
        return left_form < right_form ? -1 : 1;
    if (left_form == CETTA_NAME_FORM_HASHED) {
        int high = cetta_generated_name_compare_u64(left.hi, right.hi);
        return high ? high : cetta_generated_name_compare_u64(left.lo, right.lo);
    }
    if (left_form != CETTA_NAME_FORM_COUNTED)
        return 0;
    const CettaNameBlock *left_block = cetta_generated_name_block(left);
    const CettaNameBlock *right_block = cetta_generated_name_block(right);
    if (left_block != right_block) {
        if (!left_block || !right_block)
            return left_block ? 1 : -1;
        int key = cetta_generated_name_compare_u64(left_block->key_hi,
                                                   right_block->key_hi);
        if (!key)
            key = cetta_generated_name_compare_u64(left_block->key_lo,
                                                   right_block->key_lo);
        if (key)
            return key;
        return (uintptr_t)left_block < (uintptr_t)right_block ? -1 : 1;
    }
    int path = cetta_name_path_compare(left.hi, right.hi);
    return path ? path : cetta_generated_name_compare_u64(left.lo, right.lo);
}

/* Blocks (name_supply.c). */

/* Drop a reference to BLOCK; the last frees it.  A supply between tasks
   refers to a block that is never freed, and dropping that is nothing. */
CETTA_NAME_SUPPLY_INTERNAL void cetta_name_block_release(CettaNameBlock *block);

static inline void cetta_name_block_retain(CettaNameBlock *block) {
    atomic_fetch_add_explicit(&block->references, 1u, memory_order_relaxed);
}

/* NAME, holding its block for a holder that keeps it past its supply (a
   space): every name but a counted one beyond the root block holds
   nothing. */
static inline CettaGeneratedName cetta_generated_name_retain(
    CettaGeneratedName name) {
    CettaNameBlock *block = (CettaNameBlock *)cetta_generated_name_block(name);
    if (block)
        cetta_name_block_retain(block);
    return name;
}

static inline void cetta_generated_name_release(CettaGeneratedName name) {
    CettaNameBlock *block = (CettaNameBlock *)cetta_generated_name_block(name);
    if (block)
        cetta_name_block_release(block);
}

/* Drawing and splitting (name_supply.c). */

/* Read the mode and seed: MODE and SEED when given (the command line),
   otherwise CETTA_NAME_SUPPLY and CETTA_NAME_SEED.  Unforgeable mode with
   no seed takes a random one and reports it on stderr, so the run can be
   replayed.  Call before the first name is drawn; cetta does so whenever an
   option or a variable is set, and the embedding API when it makes an
   engine.  False, with ERROR filled, when the mode or the seed cannot be
   read.  A host that configures nothing has the first draw or block read
   the environment, and a value that cannot be read there stops the
   process, since no name can be given under it. */
CETTA_NAME_SUPPLY_INTERNAL bool cetta_name_supply_configure(
    const char *mode, const char *seed, char *error, size_t error_size);
/* Whether names are hashed (unforgeable mode). */
CETTA_NAME_SUPPLY_INTERNAL bool cetta_name_supply_unforgeable(void);

/* A root supply: the process's own, or an embedding engine's. */
CETTA_NAME_SUPPLY_INTERNAL void cetta_name_supply_init_root(CettaNameSupply *supply);
/* A supply outside the split tree, a parallel worker's between tasks: no
   program draws there.  If something does, the supply moves into a fresh
   block, numbered apart from every block a program makes, so its names are
   fresh, though they depend on when they were drawn. */
CETTA_NAME_SUPPLY_INTERNAL void cetta_name_supply_init_unplaced(
    CettaNameSupply *supply);
/* Make SUPPLY the calling thread's (NULL: the process's root); the one it
   replaces, to be installed again. */
CETTA_NAME_SUPPLY_INTERNAL CettaNameSupply *cetta_name_supply_install(
    CettaNameSupply *supply);

/* A fresh name of KIND from SUPPLY: hashed, or the first of a new block. */
CETTA_NAME_SUPPLY_INTERNAL CettaGeneratedName cetta_name_supply_fresh(
    CettaNameSupply *supply, CettaNameKind kind);
/* The next child of PARENT once PARENT has moved on to a new block, because
   the child would not fit in its path. */
CETTA_NAME_SUPPLY_INTERNAL CettaNameSupplyChild cetta_name_supply_split_beyond(
    CettaNameSupply *parent);

/* The calling thread's supply (the process's root until it installs
   another) and whether names are counted (configured, counter mode): the
   state the inline draw below reads. */
extern _Thread_local CettaNameSupply *cetta_name_supply_installed
    CETTA_NAME_SUPPLY_INTERNAL;
extern atomic_bool cetta_name_supply_counting CETTA_NAME_SUPPLY_INTERNAL;

/* The supply the calling thread draws from. */
static inline CettaNameSupply *cetta_name_supply_current(void) {
    return cetta_name_supply_installed;
}

/* A fresh name of KIND from the calling thread's supply.  A counted name
   whose counter has not reached its end is drawn here; every other draw is
   cetta_name_supply_fresh's. */
static inline CettaGeneratedName cetta_name_fresh(CettaNameKind kind) {
    CettaNameSupply *supply = cetta_name_supply_installed;
    uint64_t index = supply->drawn[kind] + 1u;
    if (__builtin_expect(
            atomic_load_explicit(&cetta_name_supply_counting,
                                 memory_order_relaxed) && index != 0u, 1)) {
        supply->drawn[kind] = index;
        return (CettaGeneratedName){
            supply->path, index,
            (uintptr_t)supply->block | (uintptr_t)CETTA_NAME_FORM_COUNTED};
    }
    return cetta_name_supply_fresh(supply, kind);
}

/* The next child of PARENT: split(parent, parent's child count), holding
   PARENT's block.  Its rank's code fits behind PARENT's path here; when it
   does not, PARENT moves on to a new block first. */
static inline CettaNameSupplyChild cetta_name_supply_split(
    CettaNameSupply *parent) {
    uint64_t code = parent->children + 1u;
    uint64_t path = parent->path;
    unsigned width = 2u * (63u - (unsigned)__builtin_clzll(code)) + 1u;
    if (__builtin_expect(width <= (unsigned)__builtin_clzll(path), 1)) {
        CettaNameBlock *block = parent->block;
        parent->children = code;
        if (block)
            cetta_name_block_retain(block);
        return (CettaNameSupplyChild){block, (path << width) | code};
    }
    return cetta_name_supply_split_beyond(parent);
}

/* SUPPLY as CHILD starts, taking over its block. */
static inline void cetta_name_supply_init_child(CettaNameSupply *supply,
                                                CettaNameSupplyChild child) {
    *supply = (CettaNameSupply){.block = child.block, .path = child.path};
}

/* Drop the block a child holds that will never run. */
static inline void cetta_name_supply_child_release(CettaNameSupplyChild child) {
    if (child.block)
        cetta_name_block_release(child.block);
}

/* Drop SUPPLY's block when SUPPLY ends; its names held by spaces keep
   theirs. */
static inline void cetta_name_supply_dispose(CettaNameSupply *supply) {
    CettaNameBlock *block = supply->block;
    supply->block = NULL;
    if (block)
        cetta_name_block_release(block);
}

/* The blocks alive in the process: a diagnostic that tests read. */
CETTA_NAME_SUPPLY_INTERNAL uint64_t cetta_name_supply_live_blocks(void);

/* SipHash-2-4 with 128-bit output (Aumasson and Bernstein), OUT in the
   specification's byte order. */
CETTA_NAME_SUPPLY_INTERNAL void cetta_siphash24_128(
    const uint8_t key[16], const void *input, size_t length, uint8_t out[16]);

#endif /* CETTA_NAME_SUPPLY_H */
