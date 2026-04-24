#ifndef MORK_SPACE_BRIDGE_H
#define MORK_SPACE_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MorkSpace MorkSpace;
typedef struct MorkProgram MorkProgram;
typedef struct MorkContext MorkContext;

typedef enum {
    MORK_STATUS_OK = 0,
    MORK_STATUS_NULL = 1,
    MORK_STATUS_PARSE = 2,
    MORK_STATUS_PANIC = 3,
    MORK_STATUS_INTERNAL = 4
} MorkStatusCode;

typedef struct {
    int32_t code;
    uint64_t value;
    uint8_t *message;
    size_t message_len;
} MorkStatus;

typedef struct {
    int32_t code;
    uint8_t *data;
    size_t len;
    uint32_t count;
    uint8_t *message;
    size_t message_len;
} MorkBuffer;

/*
Primary storage/query surface:

- mork_space_add_text()/mork_space_remove_text() are UTF-8 transport helpers.
- mork_space_add_expr_bytes()/mork_space_remove_expr_bytes() accept one stable
  bridge expr-byte span directly and bypass UTF-8 parsing.
- mork_space_add_expr_bytes_batch() accepts a packed length-prefixed sequence
  of stable bridge expr-byte spans and keeps bulk mutation on the same low ABI.
- mork_space_unique_size() reports unique structural support count.
- query traffic crosses the bridge as binding/debug packets, not mirrored row
  ids or candidate-slot packets.

Compatibility names are kept only for text aliases:

- mork_space_add_sexpr() == mork_space_add_text()
- mork_space_remove_sexpr() == mork_space_remove_text()
- mork_space_size() == mork_space_unique_size()

Long-term direction:

- keep storage structural
- keep query results semantic
- add tighter structured query entry points only when they preserve that line

Packet/debug exports below are the durable public query seams.
*/

/*
Packet format for mork_space_query_bindings():

  u32 rows_be
  repeated rows:
    u32 ref_count_be
    repeated refs:
      u32 atom_idx_be
    u32 binding_count_be
    repeated bindings:
      u8  key_side   // 0 = query side, 1 = matched atom side
      u8  key_index  // first-occurrence variable index within that side
      u32 expr_len_be
      u8  expr_text_bytes[expr_len]

The value text is ordinary S-expression text with synthetic bridge variable
names like $__mork_b1_0 so the C side can remap them onto its own VarId space
without learning MORK's raw expression encoding.

For human inspection, use mork_space_query_debug().
*/

MorkSpace *mork_space_new(void);
void mork_space_free(MorkSpace *space);

MorkStatus mork_space_clear(MorkSpace *space);
MorkStatus mork_space_add_text(MorkSpace *space, const uint8_t *text, size_t len);
MorkStatus mork_space_remove_text(MorkSpace *space, const uint8_t *text, size_t len);
MorkStatus mork_space_add_expr_bytes(MorkSpace *space,
                                      const uint8_t *expr_bytes,
                                      size_t len);
MorkStatus mork_space_add_contextual_exact_expr_bytes(MorkSpace *space,
                                                      const uint8_t *expr_bytes,
                                                      size_t len,
                                                      const uint8_t *context_bytes,
                                                      size_t context_len);
MorkStatus mork_space_add_expr_bytes_batch(MorkSpace *space,
                                           const uint8_t *packet,
                                           size_t len);
MorkStatus mork_space_add_logical_rows_from(MorkSpace *dst,
                                            const MorkSpace *src);
MorkStatus mork_space_remove_expr_bytes(MorkSpace *space,
                                        const uint8_t *expr_bytes,
                                        size_t len);
MorkStatus mork_space_remove_contextual_exact_expr_bytes(MorkSpace *space,
                                                         const uint8_t *expr_bytes,
                                                         size_t len,
                                                         const uint8_t *context_bytes,
                                                         size_t context_len);
MorkStatus mork_space_contains_expr_bytes(const MorkSpace *space,
                                          const uint8_t *expr_bytes,
                                          size_t len);
MorkStatus mork_space_add_sexpr(MorkSpace *space, const uint8_t *text, size_t len);
MorkStatus mork_space_remove_sexpr(MorkSpace *space, const uint8_t *text, size_t len);
MorkStatus mork_space_unique_size(const MorkSpace *space);
MorkStatus mork_space_size(const MorkSpace *space);
MorkStatus mork_space_step(MorkSpace *space, uint64_t steps);
MorkStatus mork_space_dump_act_file(MorkSpace *space, const uint8_t *path, size_t len);
MorkStatus mork_space_load_act_file(MorkSpace *space, const uint8_t *path, size_t len);
MorkBuffer mork_space_dump(MorkSpace *space);
/*
Packet format for mork_space_dump_expr_rows():

  repeated rows:
    u32 expr_len_be
    u8[expr_len] stable bridge expr packet bytes

count = logical row count with multiplicities expanded.
*/
MorkBuffer mork_space_dump_expr_rows(MorkSpace *space);

/*
Contextual exact-row bridge packet (wire version 4):

  u32 magic_be      // 0x43544252 = "CTBR"
  u16 version_be    // 4
  u16 flags_be      // 0 for exact-row packets
  u32 rows_be       // number of row specs, not expanded logical rows
  u32 contexts_be
  repeated contexts:
    u32 context_id_be
    u32 entry_count_be
    repeated entries:
      u16 slot_be
      u8  ref_kind       // 0 = exact VarId/spelling, 1 = query-slot ref
      u8  reserved
      ... ref payload ...
  repeated rows:
    u32 context_id_be
    u32 multiplicity_be
    u32 expr_len_be
    u8  expr_bytes[expr_len]

Ground rows use empty opening contexts. Variable-bearing rows are emitted only
when the bridge has an explicit exact opening context for the row; rows imported
through structural-only APIs are rejected here instead of silently synthesizing
identities.
*/
MorkBuffer mork_space_dump_contextual_exact_rows(MorkSpace *space);

/* Compatibility v1 text packet export. Prefer the v2/v3 raw-byte packet
   surfaces for new CeTTa integration work. */
MorkBuffer mork_space_query_bindings(MorkSpace *space, const uint8_t *pattern, size_t len);

/*
Packet format for mork_space_query_bindings_query_only_v2():

  u32 magic_be      // 0x43544252 = "CTBR"
  u16 version_be    // 2
  u16 flags_be      // bit0=query-side keys only, bit1=stable bridge expr payload,
                    // bit4=wide token lengths in the expr payload
  u32 rows_be
  repeated rows:
    u32 ref_count_be
    repeated refs:
      u32 atom_idx_be
    u32 binding_count_be
    repeated bindings:
      u16 query_var_slot_be
      u8  value_env        // source ExprEnv side for the encoded value
      u8  value_flags      // bit0 = value expr is ground
      u32 expr_len_be
      u8  expr_bytes[expr_len]

This export is intentionally stricter than v1:
- binding keys must be query-side only
- values may be matched-side only if they are ground
- matched-side variables in values cause the whole call to fail
- explicit unary wrapper text `(, pattern)` is accepted
- explicit multi-factor conjunction text `(, p1 p2 ...)` is currently rejected
  until a future multi-ref packet can report every mirrored matched atom, not
  just the primary match

The expr bytes use the same compact tag structure as MORK expressions, but
symbol payloads are exported as stable symbol text bytes rather than MORK-
internal symbol handles. That makes the packet meaningful to a C consumer.

This makes it safe as a future fast-path packet for CeTTa's VarId/SymbolId
decoder while preserving the existing v1 text packet for current integration.
*/
MorkBuffer mork_space_query_bindings_query_only_v2(MorkSpace *space,
                                                   const uint8_t *pattern,
                                                   size_t len);

/*
Packet format for mork_space_query_bindings_multi_ref_v3():

  u32 magic_be      // 0x43544252 = "CTBR"
  u16 version_be    // 3
  u16 flags_be      // bit0=query-side keys only, bit1=stable bridge expr payload,
                    // bit2=per-factor matched-atom ref groups,
                    // bit4=wide token lengths in the expr payload
  u32 factor_count_be
  u32 rows_be
  repeated rows:
    repeated factor groups (factor_count of them):
      u32 factor_ref_count_be
      repeated refs:
        u32 atom_idx_be
    u32 binding_count_be
    repeated bindings:
      u16 query_var_slot_be
      u8  value_env
      u8  value_flags
      u32 expr_len_be
      u8  expr_bytes[expr_len]

This is the conjunction-capable sibling of v2. It keeps the same query-only,
stable-bridge-byte binding discipline, but now reports every matched factor's
mirrored atom-index group so CeTTa can preserve joined-query multiplicity.
*/
MorkBuffer mork_space_query_bindings_multi_ref_v3(MorkSpace *space,
                                                  const uint8_t *pattern,
                                                  size_t len);
/*
Packet format for mork_space_query_contextual_rows():

  u32 magic_be      // 0x43544252 = "CTBR"
  u16 version_be    // contextual rows wire version
  u16 flags_be      // 0 for contextual query packets
  u32 rows_be
  u32 contexts_be
  repeated contexts:
    u32 context_id_be
    u32 entry_count_be
    repeated entries:
      u16 slot_be
      u8  ref_kind       // 0 = exact VarId/spelling, 1 = query-slot ref
      u8  reserved
      if ref_kind == 0:
        u64 var_id_be
        u32 spelling_len_be
        u8  spelling_bytes[spelling_len]
      if ref_kind == 1:
        u16 query_slot_be
  repeated rows:
    u32 binding_count_be
    repeated bindings:
      u16 query_slot_be
      u32 value_context_id_be
      u32 value_flags_be
      u32 expr_len_be
      u8  expr_bytes[expr_len]

This is the origin-aware query sibling of contextual exact rows. Values are
opened with either exact stored identities or references back to the query
variable context owned by CeTTa.
*/
MorkBuffer mork_space_query_contextual_rows(MorkSpace *space,
                                            const uint8_t *pattern,
                                            size_t len);
MorkBuffer mork_space_query_debug(MorkSpace *space, const uint8_t *pattern, size_t len);

/*
Current MM2 host bridge ABI:

- The present bridge exposes a program store plus a live context handle.
- Loading copies exec expressions from the program store into the live context.
- Running a context delegates to MORK's metta_calculus kernel.

This is the current host-side bridge shape, not MM2's public ontology.
Raw MM2 itself works over one live space where facts and execs coexist, and
exec rules may match, remove, and generate other exec rules during execution.
Future public ABI work should therefore grow a space-level stepping entry
instead of teaching program/context as if they were the language model.
*/
MorkProgram *mork_program_new(void);
void mork_program_free(MorkProgram *program);
MorkStatus mork_program_clear(MorkProgram *program);
MorkStatus mork_program_add_sexpr(MorkProgram *program, const uint8_t *text, size_t len);
MorkStatus mork_program_size(const MorkProgram *program);
MorkBuffer mork_program_dump(MorkProgram *program);

MorkContext *mork_context_new(void);
void mork_context_free(MorkContext *context);
MorkStatus mork_context_clear(MorkContext *context);
MorkStatus mork_context_load_program(MorkContext *context, const MorkProgram *program);
MorkStatus mork_context_add_sexpr(MorkContext *context, const uint8_t *text, size_t len);
MorkStatus mork_context_remove_sexpr(MorkContext *context, const uint8_t *text, size_t len);
MorkStatus mork_context_size(const MorkContext *context);
MorkStatus mork_context_run(MorkContext *context, uint64_t steps);
MorkBuffer mork_context_dump(MorkContext *context);

void mork_bytes_free(uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* MORK_SPACE_BRIDGE_H */
