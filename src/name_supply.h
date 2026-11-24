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
 * Names are drawn from supplies in the style of interval tree clocks
 * (Almeida, Baquero and Fonte, OPODIS 2008).  A supply holds a share of the
 * unit interval and, for each kind of name, a clock: the number of names of
 * that kind it has given.  Its next name is its share with its clock, and
 * drawing advances the clock.  A fork in two hands each half of the share
 * (Shares.split) on at the same clocks.  A spawn of k components hands
 * component i its balanced piece (Mettapedia: Machines/NameSupplies/
 * BalancedPieces, balancedPiece): the first ceil(k/2) components share the
 * left half and the others the right half, each half parted again in the
 * same way, so every piece lies at most ceil(log2 k) halvings below the
 * share (balancedPiece_depth_le).  A supply that queues tasks one at a time,
 * not knowing how many, counts them (SiblingPushes, countedChild): at its
 * first push it keeps the left half of its share, which it draws from and
 * never pushes from, and the right half becomes its pool; push n hands the
 * task the part of the pool at countedPath n: floor(log2(n+1)) = j right
 * halvings, a left halving into the block of the 2^j pushes n = 2^j - 1 ..
 * 2^(j+1) - 2, and the balanced path of n + 1 - 2^j among them, so the
 * pushed tasks' shares are disjoint and push n lies 2j + 2 halvings below
 * the share (countedChild_depth_le): a million pushes from the whole
 * interval lie within 40.  No count-oblivious way of pushing keeps push n
 * within ceil(log2(n+1)) + c halvings for any c (no_balanced_pushes).  A
 * join -- the continuation of a rho COMM, or a parallel episode handing its
 * supplies back to the supply that started it -- takes the union of the
 * shares at the later clocks.  The shares of live supplies are disjoint, so
 * their names are distinct and never repeat (SharedSupplies, shares_exact,
 * and balanced_exact and counted_exact for the pieces and the pushes); a
 * supply forked in two and joined again is the supply it was (join_pieces),
 * so balanced fork and join keep shares bounded.  The process starts with
 * the whole interval.
 *
 * A share is kept in one word.  A single dyadic interval (the whole
 * interval among them) is its start bit followed by its path, a bit per
 * halving, 0 for the left half: the whole interval is 1, its left half 2,
 * its right half 3, at most 62 halvings deep.  Any other share is its
 * prefix code, a node 1 followed by its halves, an empty half 00 and a full
 * half 01, behind a start bit, with the top bit set, at most 62 bits.  The
 * pushes of one block are consecutive words, so a supply that pushes keeps
 * the word of its last push and the count: the next push of the block is
 * that word plus one, and the first of the next block lies two halvings
 * below that.  When a fork's or a join's share, or a push's, would not fit
 * the word, the supply moves on to a block of its own: it keeps its clocks
 * and continues with the whole interval of a new block, whose key is
 *
 *     key(new) = SipHash-2-4-128(k, 0x01 || key(left) || clocks || share)
 *
 * of the block it leaves (the root block's key is zero) and of the share it
 * leaves there (its word, or the prefix code of a union too wide for one),
 * so the block stands for that share from those clocks on (Claessen and
 * Palka's splitting by hashing, Haskell 2013).  A supply whose pool is spent
 * takes a block keyed by its place, its share and what is left of its pool:
 * key(left), its clocks, its share, its last push and the count (0x04); a
 * join of supplies of two different blocks takes a new block keyed in the
 * same way by both (0x02).  k is the seed in unforgeable mode and a fixed
 * public key, zero, in counter mode.  A block keeps no parent: what a run
 * holds for its names is bounded by its live supplies and names.  A block
 * lives while a supply, a queued task or a name kept by a space refers to
 * it.  Names of one block are exact by construction; names of two blocks
 * are told apart by the blocks themselves, and their spellings by the
 * keys.
 *
 * A name is (block, share, clock).  Two modes give names:
 *   counter      the name itself.  Equality compares the block's identity,
 *                the share and the clock.  The root's names, of the whole
 *                interval, spell their clock alone: 1, 2, ....  Any other
 *                name spells its share's prefix code, 0 for an empty half,
 *                1 for a full one and 2 for a node followed by its halves,
 *                a dot and its clock, behind its block's key when the block
 *                is not the root block: 201.1 is the right half's first,
 *                22010.3 the second quarter's third, 20210.2 a name at
 *                clock 2 of the root's first pushed task, and
 *                <32 hexadecimal digits>.1.1 the first of a new block.
 *   unforgeable  SipHash-2-4 with 128-bit output, keyed by the seed, of
 *                kind, share and clock, and of the block's key beyond the
 *                root block: 32 hexadecimal digits.
 * A spelling is at most CETTA_GENERATED_NAME_SPELLING_MAX - 1 bytes.
 * Collision bounds: names of one block in counter mode are distinct by
 * construction.  A block key and an unforgeable name are 128 bits: two of n
 * collide with probability at most about n^2 / 2^129, so collisions become
 * likely only near 2^64 blocks (two counter-mode names of different blocks
 * would then spell alike) or 2^64 unforgeable names.
 *
 * This header holds the types, the pure formatter and order, the inline
 * draw, fork and join for shares that fit, and the references a kept name
 * holds; everything else, hashing and configuration live in name_supply.c.
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
    CETTA_NAME_FORM_COUNTED,    /* counter mode: block, share and clock */
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

/* A share word: the whole interval, and the mark of a share kept as its
   prefix code.  A word without the mark is a dyadic interval: its start
   bit, then a bit per halving.  Zero is no share. */
#define CETTA_NAME_SHARE_WHOLE UINT64_C(1)
#define CETTA_NAME_SHARE_CODED (UINT64_C(1) << 63)

/* A block beyond the root block.  Its key is fixed when it is made; its
   references are counted by every supply, queued task and kept name in
   it. */
typedef struct CettaNameBlock {
    _Atomic uint64_t references;
    uint64_t key_hi;
    uint64_t key_lo;
} CettaNameBlock;

/* A generated name.  Counted: hi is the share word, lo the clock, and tag
   the block (NULL for the root block) with the form in its low bits.
   Hashed: hi and lo are the hash and tag the form.  No name is all zero.  A
   name has no padding and moves as three words; a counted name of a block
   borrows the block from its supply, and one kept longer holds it
   (cetta_generated_name_retain). */
typedef struct {
    uint64_t hi;
    uint64_t lo;
    uintptr_t tag;
} CettaGeneratedName;

/* A supply.  One thread owns a supply while it draws from it, forks it,
   pushes from it or joins it; nothing in it is shared but its block. */
typedef struct {
    /* The block, NULL for the root block; held by this supply. */
    CettaNameBlock *block;
    /* The share of the block's interval it draws from: a share word. */
    uint64_t share;
    /* The clocks: the names of each kind given so far. */
    uint64_t drawn[CETTA_NAME_KIND_COUNT];
    /* Its pushes: the share word of the last task it pushed, an interval of
       its pool, and how many it has pushed from that pool; both zero before
       its first push. */
    uint64_t pushed;
    uint64_t pushes;
} CettaNameSupply;

/* The longest numeral: a block key (32 hexadecimal digits and a dot), the
   prefix code of an interval 62 halvings deep (125 digits) and a dot, and a
   clock (20 digits); one byte more for the NUL. */
#define CETTA_GENERATED_NAME_NUMERAL_MAX 180u
/* The longest spelling: "&space(" numeral ")" or "lambda_" numeral, and the
   NUL. */
#define CETTA_GENERATED_NAME_SPELLING_MAX 188u
/* The longest prefix code of a share word, in digits. */
#define CETTA_NAME_SHARE_SPELLING_MAX 125u

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

/* The name as a positive int64 when it is a counted name of the whole
   interval of the root block whose clock fits, as every name the counter
   mode's main thread draws is. */
static inline bool cetta_generated_name_small(CettaGeneratedName name,
                                              int64_t *number) {
    if (name.tag != (uintptr_t)CETTA_NAME_FORM_COUNTED ||
        name.hi != CETTA_NAME_SHARE_WHOLE || name.lo == 0u ||
        name.lo > (uint64_t)INT64_MAX)
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

/* The prefix code of SHARE into OUT (at least CETTA_NAME_SHARE_SPELLING_MAX
   bytes), as digits: 0 an empty half, 1 a full one, 2 a node followed by
   its halves.  Its length, or 0 when SHARE is no share word.  This is the
   share's serialization in Mettapedia's PortableNames. */
static inline size_t cetta_name_share_spell(uint64_t share, char *out) {
    if (share == 0u)
        return 0u;
    size_t length = 0u;
    if (!(share & CETTA_NAME_SHARE_CODED)) {
        /* A path: each halving a node, the empty half before the path when
           it turns right and after it when it turns left. */
        unsigned depth = 63u - (unsigned)__builtin_clzll(share);
        for (unsigned bit = depth; bit-- > 0u;) {
            out[length++] = '2';
            if ((share >> bit) & 1u)
                out[length++] = '0';
        }
        out[length++] = '1';
        for (unsigned bit = 0u; bit < depth; bit++)
            if (!((share >> bit) & 1u))
                out[length++] = '0';
        return length;
    }
    uint64_t code = share & ~CETTA_NAME_SHARE_CODED;
    if (code == 0u)
        return 0u;
    unsigned bits = 63u - (unsigned)__builtin_clzll(code);
    unsigned open = 1u;
    while (open != 0u) {
        if (bits == 0u)
            return 0u;
        if ((code >> --bits) & 1u) {
            out[length++] = '2';
            open++;
            continue;
        }
        if (bits == 0u)
            return 0u;
        out[length++] = (code >> --bits) & 1u ? '1' : '0';
        open--;
    }
    return bits == 0u ? length : 0u;
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
        if (name.lo == 0u)
            return 0u;
        size_t length = 0u;
        if (name.hi == CETTA_NAME_SHARE_WHOLE &&
            name.tag == (uintptr_t)CETTA_NAME_FORM_COUNTED) {
            /* A root's name: its clock alone. */
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
        size_t share = cetta_name_share_spell(name.hi, out + length);
        if (share == 0u)
            return 0u;
        length += share;
        out[length++] = '.';
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

/* A total order on names, deterministic: it reads shares, clocks and
   hashes, never addresses, but for two blocks whose 128-bit keys collide.
   Counted names: the root block before every other, other blocks by key,
   then by share word (the whole interval first, so the root's names come
   in the order they were drawn), then by clock.  Hashed names: by their
   128 bits.  O(1). */
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
    }
    int share = cetta_generated_name_compare_u64(left.hi, right.hi);
    if (!share)
        share = cetta_generated_name_compare_u64(left.lo, right.lo);
    if (share || left_block == right_block)
        return share;
    return (uintptr_t)left_block < (uintptr_t)right_block ? -1 : 1;
}

/* Blocks (name_supply.c). */

/* Drop a reference to BLOCK; the last frees it.  A supply outside the
   split tree refers to a block that is never freed, and dropping that is
   nothing. */
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

/* Drawing, forking and joining (name_supply.c). */

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

/* A root supply, the whole interval of the root block: the process's own,
   or an embedding engine's. */
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

/* A fresh name of KIND from SUPPLY: hashed, or the first of a fresh block
   when the clock of KIND has given every value. */
CETTA_NAME_SUPPLY_INTERNAL CettaGeneratedName cetta_name_supply_fresh(
    CettaNameSupply *supply, CettaNameKind kind);
/* The forks of a supply whose share does not halve in the word: a coded
   share, an interval 62 halvings deep, or a supply outside the split tree.
   When a half would not fit the word, the supply moves on to a block of its
   own first.  As cetta_name_supply_fork_into. */
CETTA_NAME_SUPPLY_INTERNAL void cetta_name_supply_fork_beyond(
    CettaNameSupply *parent, CettaNameSupply *child);
/* The pushes cetta_name_supply_push_into does not make in the word.  A
   first push from a share that is not an interval at most 60 halvings deep:
   a coded share whose right half is one interval at most 61 deep pushes
   from it; any other supply moves on to a block of its own first (one
   outside the split tree to a fresh one) and pushes from the right half of
   its whole interval there.  The first push of a block that would not fit
   the word: the pool is spent, so the supply moves on with its share and
   what is left of its pool, and pushes from the right half of the new
   block's whole interval.  As cetta_name_supply_push_into. */
CETTA_NAME_SUPPLY_INTERNAL void cetta_name_supply_push_beyond(
    CettaNameSupply *pusher, CettaNameSupply *child);
/* The join of FIRST and SECOND when it is not the parent of two sibling
   intervals of one block: the union of their shares in their block, or in
   a block of its own when the union does not fit or their blocks differ,
   at the later clocks, with nothing pushed.  It holds its block; FIRST and
   SECOND are left as they are. */
CETTA_NAME_SUPPLY_INTERNAL CettaNameSupply cetta_name_supply_join_beyond(
    const CettaNameSupply *first, const CettaNameSupply *second);

/* The calling thread's supply (the process's root until it installs
   another), whether names are counted (configured, counter mode), and the
   clocks that ending supplies hand back on this thread, if any: the state
   the inline functions below read. */
extern _Thread_local CettaNameSupply *cetta_name_supply_installed
    CETTA_NAME_SUPPLY_INTERNAL;
extern atomic_bool cetta_name_supply_counting CETTA_NAME_SUPPLY_INTERNAL;
extern _Thread_local uint64_t *cetta_name_supply_ending_clocks
    CETTA_NAME_SUPPLY_INTERNAL;
/* The block of supplies outside the split tree. */
extern CettaNameBlock cetta_name_block_unplaced CETTA_NAME_SUPPLY_INTERNAL;

/* The supply the calling thread draws from. */
static inline CettaNameSupply *cetta_name_supply_current(void) {
    return cetta_name_supply_installed;
}

/* Make CLOCKS (CETTA_NAME_KIND_COUNT words, or NULL) where the supplies
   that end on the calling thread hand their clocks: each clock there
   becomes at least theirs.  A parallel episode collects the clocks of its
   tasks so, and hands them to the supply that started it.  The previous
   one, to be installed again. */
static inline uint64_t *cetta_name_supply_ending_clocks_install(
    uint64_t *clocks) {
    uint64_t *previous = cetta_name_supply_ending_clocks;
    cetta_name_supply_ending_clocks = clocks;
    return previous;
}

/* Raise each clock of CLOCKS to SUPPLY's. */
static inline void cetta_name_supply_absorb(uint64_t *clocks,
                                            const CettaNameSupply *supply) {
    for (unsigned kind = 0u; kind < CETTA_NAME_KIND_COUNT; kind++)
        if (supply->drawn[kind] > clocks[kind])
            clocks[kind] = supply->drawn[kind];
}

/* A fresh name of KIND from the calling thread's supply.  A counted name
   whose clock has not reached its end is drawn here; every other draw is
   cetta_name_supply_fresh's. */
static inline CettaGeneratedName cetta_name_fresh(CettaNameKind kind) {
    CettaNameSupply *supply = cetta_name_supply_installed;
    uint64_t index = supply->drawn[kind] + 1u;
    if (__builtin_expect(
            atomic_load_explicit(&cetta_name_supply_counting,
                                 memory_order_relaxed) && index != 0u, 1)) {
        supply->drawn[kind] = index;
        return (CettaGeneratedName){
            supply->share, index,
            (uintptr_t)supply->block | (uintptr_t)CETTA_NAME_FORM_COUNTED};
    }
    return cetta_name_supply_fresh(supply, kind);
}

/* SUPPLY again, holding its block for a second holder. */
static inline CettaNameSupply cetta_name_supply_copy(
    const CettaNameSupply *supply) {
    if (supply->block)
        cetta_name_block_retain(supply->block);
    return *supply;
}

/* Whether SHARE is an interval less than 62 halvings deep, whose halves
   are its word shifted. */
static inline bool cetta_name_share_halves_in_word(uint64_t share) {
    return share - 1u < (UINT64_C(1) << 62) - 1u;
}

/* Fork PARENT in two (Shares.split): PARENT keeps the left half of its
   share, and its pool, and *CHILD gets the right half, at PARENT's clocks,
   holding its own reference to the block, with nothing pushed. */
static inline void cetta_name_supply_fork_into(CettaNameSupply *parent,
                                               CettaNameSupply *child) {
    uint64_t share = parent->share;
    if (__builtin_expect(cetta_name_share_halves_in_word(share), 1)) {
        CettaNameBlock *block = parent->block;
        child->block = block;
        child->share = (share << 1) | 1u;
        for (unsigned kind = 0u; kind < CETTA_NAME_KIND_COUNT; kind++)
            child->drawn[kind] = parent->drawn[kind];
        child->pushed = 0u;
        child->pushes = 0u;
        parent->share = share << 1;
        if (block)
            cetta_name_block_retain(block);
        return;
    }
    cetta_name_supply_fork_beyond(parent, child);
}

static inline CettaNameSupply cetta_name_supply_fork(CettaNameSupply *parent) {
    CettaNameSupply child;
    cetta_name_supply_fork_into(parent, &child);
    return child;
}

/* Push a task from PUSHER, PUSHER's push n: *CHILD gets the part of
   PUSHER's pool at countedPath n (SiblingPushes.countedChild), at PUSHER's
   clocks, holding its own reference to the block, with nothing pushed.  At
   its first push PUSHER keeps the left half of its share to draw from and
   pools the right half, whose left half is push 0.  The pushes of one
   block are consecutive words, so push n is the word after push n - 1,
   unless it is the first of its block (n + 1 a power of two), which lies
   two halvings below that word.  The rest is
   cetta_name_supply_push_beyond's. */
static inline void cetta_name_supply_push_into(CettaNameSupply *pusher,
                                               CettaNameSupply *child) {
    uint64_t n = pusher->pushes;
    uint64_t word = pusher->pushed + 1u;
    if (((n + 1u) & n) == 0u) {
        if (n != 0u) {
            if (__builtin_expect(word >= (UINT64_C(1) << 61), 0)) {
                cetta_name_supply_push_beyond(pusher, child);
                return;
            }
            word <<= 2;
        } else {
            /* An interval at most 60 halvings deep: its halves, and the
               right one's left half, fit the word. */
            uint64_t share = pusher->share;
            if (__builtin_expect(share - 1u >= (UINT64_C(1) << 61) - 1u, 0)) {
                cetta_name_supply_push_beyond(pusher, child);
                return;
            }
            pusher->share = share << 1;
            word = (share << 2) | 2u;
        }
    }
    CettaNameBlock *block = pusher->block;
    pusher->pushed = word;
    pusher->pushes = n + 1u;
    child->block = block;
    child->share = word;
    for (unsigned kind = 0u; kind < CETTA_NAME_KIND_COUNT; kind++)
        child->drawn[kind] = pusher->drawn[kind];
    child->pushed = 0u;
    child->pushes = 0u;
    if (block)
        cetta_name_block_retain(block);
}

/* Split SUPPLY into its halves LEFT and RIGHT (Shares.split), each holding
   its block, neither with anything pushed; SUPPLY's reference goes with
   them, and what is left of its pool is given up. */
static inline void cetta_name_supply_split(CettaNameSupply *supply,
                                           CettaNameSupply *left,
                                           CettaNameSupply *right) {
    *right = cetta_name_supply_fork(supply);
    *left = *supply;
    left->pushed = 0u;
    left->pushes = 0u;
}

/* The join of FIRST and SECOND, holding its block: the union of their
   shares at the later clocks, with nothing pushed (what is left of their
   pools is given up).  Two sibling intervals of one block join in the
   word to their parent; everything else is
   cetta_name_supply_join_beyond's.  FIRST and SECOND are left as they
   are. */
static inline CettaNameSupply cetta_name_supply_join(
    const CettaNameSupply *first, const CettaNameSupply *second) {
    uint64_t a = first->share, b = second->share;
    if (__builtin_expect(first->block == second->block &&
                             ((a | b) & CETTA_NAME_SHARE_CODED) == 0u &&
                             (a ^ b) == 1u && a > 1u, 1)) {
        CettaNameSupply joined = *first;
        joined.share = a >> 1;
        joined.pushed = 0u;
        joined.pushes = 0u;
        cetta_name_supply_absorb(joined.drawn, second);
        if (joined.block)
            cetta_name_block_retain(joined.block);
        return joined;
    }
    return cetta_name_supply_join_beyond(first, second);
}

/* End SUPPLY: hand its clocks to the thread's ending clocks, if any, and
   drop its block; its names held by spaces keep theirs. */
static inline void cetta_name_supply_dispose(CettaNameSupply *supply) {
    CettaNameBlock *block = supply->block;
    uint64_t *clocks = cetta_name_supply_ending_clocks;
    if (clocks && block != &cetta_name_block_unplaced)
        cetta_name_supply_absorb(clocks, supply);
    supply->block = NULL;
    if (block)
        cetta_name_block_release(block);
}

/* The pieces of a spawn of parts components, taken in order: component i
   draws from the part of the spawner's share along balancedPath parts i
   (BalancedPieces.balancedPiece), at the spawner's clocks, with nothing
   pushed.  Taking them forks each share on the way down once, in the
   word or through cetta_name_supply_fork_beyond; the right halves met on
   the way wait for the components after, at most ceil(log2 parts) at a
   time, each with its block (every piece has the spawner's clocks). */
#define CETTA_NAME_SUPPLY_PIECES_WAITING 33u

typedef struct {
    uint64_t drawn[CETTA_NAME_KIND_COUNT];
    uint32_t waiting;
    struct {
        CettaNameBlock *block;   /* held */
        uint64_t share;
        uint32_t parts;          /* the components it is for */
    } half[CETTA_NAME_SUPPLY_PIECES_WAITING];
} CettaNameSupplyPieces;

/* Start the pieces of a spawn of PARTS components from SUPPLY, which is
   left as it is. */
static inline void cetta_name_supply_pieces_begin(
    CettaNameSupplyPieces *pieces, const CettaNameSupply *supply,
    uint32_t parts) {
    for (unsigned kind = 0u; kind < CETTA_NAME_KIND_COUNT; kind++)
        pieces->drawn[kind] = supply->drawn[kind];
    pieces->waiting = 0u;
    if (parts == 0u)
        return;
    pieces->waiting = 1u;
    pieces->half[0].block = supply->block;
    pieces->half[0].share = supply->share;
    pieces->half[0].parts = parts;
    if (supply->block)
        cetta_name_block_retain(supply->block);
}

/* The next component's piece, its taker's to end.  A spawn takes exactly
   as many as it has components. */
static inline CettaNameSupply cetta_name_supply_pieces_next(
    CettaNameSupplyPieces *pieces) {
    uint32_t top = pieces->waiting - 1u;
    CettaNameBlock *block = pieces->half[top].block;
    uint64_t share = pieces->half[top].share;
    uint32_t parts = pieces->half[top].parts;
    while (parts > 1u) {
        /* The first ceil(parts / 2) take the left half, the rest wait with
           the right half. */
        if (__builtin_expect(cetta_name_share_halves_in_word(share), 1)) {
            pieces->half[top].block = block;
            pieces->half[top].share = (share << 1) | 1u;
            if (block)
                cetta_name_block_retain(block);
            share <<= 1;
        } else {
            CettaNameSupply left = {block, share, {0}, 0u, 0u}, right;
            for (unsigned kind = 0u; kind < CETTA_NAME_KIND_COUNT; kind++)
                left.drawn[kind] = pieces->drawn[kind];
            cetta_name_supply_fork_beyond(&left, &right);
            pieces->half[top].block = right.block;
            pieces->half[top].share = right.share;
            block = left.block;
            share = left.share;
        }
        pieces->half[top++].parts = parts / 2u;
        parts = (parts + 1u) / 2u;
    }
    pieces->waiting = top;
    CettaNameSupply piece = {block, share, {0}, 0u, 0u};
    for (unsigned kind = 0u; kind < CETTA_NAME_KIND_COUNT; kind++)
        piece.drawn[kind] = pieces->drawn[kind];
    return piece;
}

/* End the pieces a spawn that stopped early will not take. */
static inline void cetta_name_supply_pieces_end(CettaNameSupplyPieces *pieces) {
    while (pieces->waiting != 0u) {
        uint32_t at = --pieces->waiting;
        CettaNameSupply rest = {pieces->half[at].block, pieces->half[at].share,
                                {0}, 0u, 0u};
        for (unsigned kind = 0u; kind < CETTA_NAME_KIND_COUNT; kind++)
            rest.drawn[kind] = pieces->drawn[kind];
        cetta_name_supply_dispose(&rest);
    }
}

/* The blocks alive in the process: a diagnostic that tests read. */
CETTA_NAME_SUPPLY_INTERNAL uint64_t cetta_name_supply_live_blocks(void);

/* SipHash-2-4 with 128-bit output (Aumasson and Bernstein), OUT in the
   specification's byte order. */
CETTA_NAME_SUPPLY_INTERNAL void cetta_siphash24_128(
    const uint8_t key[16], const void *input, size_t length, uint8_t out[16]);

#endif /* CETTA_NAME_SUPPLY_H */
