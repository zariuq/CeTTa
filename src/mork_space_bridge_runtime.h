#ifndef CETTA_MORK_SPACE_BRIDGE_RUNTIME_H
#define CETTA_MORK_SPACE_BRIDGE_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct CettaMorkSpaceHandle CettaMorkSpaceHandle;
typedef struct CettaMorkProgramHandle CettaMorkProgramHandle;
typedef struct CettaMorkContextHandle CettaMorkContextHandle;
typedef struct CettaMorkCursorHandle CettaMorkCursorHandle;
typedef struct CettaMorkProductCursorHandle CettaMorkProductCursorHandle;
typedef struct CettaMorkOverlayCursorHandle CettaMorkOverlayCursorHandle;

/*
Current MM2 bridge mirror:

- space handles cover storage/query on one live MORK space
- program/context handles mirror the present host-side MM2 bridge seam

Raw MM2 itself is still one live space with facts and execs together. The
program/context split here is therefore an implementation boundary, not MM2's
intended long-term public model.

Primary CeTTa bridge surface:

- cetta_mork_bridge_space_add_expr_bytes()/remove_expr_bytes() accept one
  stable bridge expr-byte span directly and bypass UTF-8 parsing on the
  mutation path
- cetta_mork_bridge_space_add_expr_bytes_batch() accepts a packed sequence of
  length-prefixed stable bridge expr-byte spans so bulk mutation stays on the
  same low ABI as singular mutation
- cetta_mork_bridge_space_unique_size() reports unique structural support
- query results cross back as binding/debug packets, not mirrored row ids

Compatibility names remain only for text aliases:

- cetta_mork_bridge_space_add_sexpr() == cetta_mork_bridge_space_add_text()
- cetta_mork_bridge_space_size() == cetta_mork_bridge_space_unique_size()

The packet-binding exports below are the durable query seams. Future
structured query entry points should stay semantic and never reintroduce row
slot provenance.
*/

bool cetta_mork_bridge_is_available(void);
const char *cetta_mork_bridge_last_error(void);

CettaMorkSpaceHandle *cetta_mork_bridge_space_new(void);
/* CeTTa-owned counted PathMap storage for generic `(new-space pathmap)`.
   This is intentionally distinct from the raw MORK/lib_mork `mork:` lane. */
CettaMorkSpaceHandle *cetta_mork_bridge_space_new_pathmap(void);
void cetta_mork_bridge_space_free(CettaMorkSpaceHandle *space);

bool cetta_mork_bridge_space_clear(CettaMorkSpaceHandle *space);
bool cetta_mork_bridge_space_add_text(CettaMorkSpaceHandle *space,
                                      const char *text,
                                      uint64_t *out_added);
bool cetta_mork_bridge_space_add_expr_bytes(CettaMorkSpaceHandle *space,
                                            const uint8_t *expr_bytes,
                                            size_t len,
                                            uint64_t *out_added);
bool cetta_mork_bridge_space_add_expr_bytes_batch(CettaMorkSpaceHandle *space,
                                                  const uint8_t *packet,
                                                  size_t len,
                                                  uint64_t *out_added);
bool cetta_mork_bridge_space_add_sexpr(CettaMorkSpaceHandle *space,
                                       const uint8_t *text,
                                       size_t len,
                                       uint64_t *out_added);
bool cetta_mork_bridge_space_remove_text(CettaMorkSpaceHandle *space,
                                         const char *text,
                                         uint64_t *out_removed);
bool cetta_mork_bridge_space_remove_expr_bytes(CettaMorkSpaceHandle *space,
                                               const uint8_t *expr_bytes,
                                               size_t len,
                                               uint64_t *out_removed);
bool cetta_mork_bridge_space_contains_expr_bytes(const CettaMorkSpaceHandle *space,
                                                 const uint8_t *expr_bytes,
                                                 size_t len,
                                                 bool *out_found);
bool cetta_mork_bridge_space_remove_sexpr(CettaMorkSpaceHandle *space,
                                          const uint8_t *text,
                                          size_t len,
                                          uint64_t *out_removed);
bool cetta_mork_bridge_space_unique_size(const CettaMorkSpaceHandle *space,
                                         uint64_t *out_unique_size);
bool cetta_mork_bridge_space_size(const CettaMorkSpaceHandle *space,
                                  uint64_t *out_size);
bool cetta_mork_bridge_space_step(CettaMorkSpaceHandle *space,
                                  uint64_t steps,
                                  uint64_t *out_performed);
bool cetta_mork_bridge_space_dump(CettaMorkSpaceHandle *space,
                                  uint8_t **out_packet,
                                  size_t *out_len,
                                  uint32_t *out_rows);
bool cetta_mork_bridge_space_dump_expr_rows(CettaMorkSpaceHandle *space,
                                            uint8_t **out_packet,
                                            size_t *out_len,
                                            uint32_t *out_rows);
bool cetta_mork_bridge_space_join_into(CettaMorkSpaceHandle *dst,
                                       const CettaMorkSpaceHandle *src);
CettaMorkSpaceHandle *cetta_mork_bridge_space_join(
    const CettaMorkSpaceHandle *lhs,
    const CettaMorkSpaceHandle *rhs);
CettaMorkSpaceHandle *cetta_mork_bridge_space_clone(
    const CettaMorkSpaceHandle *space);
bool cetta_mork_bridge_space_meet_into(CettaMorkSpaceHandle *dst,
                                       const CettaMorkSpaceHandle *src);
CettaMorkSpaceHandle *cetta_mork_bridge_space_meet(
    const CettaMorkSpaceHandle *lhs,
    const CettaMorkSpaceHandle *rhs);
bool cetta_mork_bridge_space_subtract_into(CettaMorkSpaceHandle *dst,
                                           const CettaMorkSpaceHandle *src);
CettaMorkSpaceHandle *cetta_mork_bridge_space_subtract(
    const CettaMorkSpaceHandle *lhs,
    const CettaMorkSpaceHandle *rhs);
bool cetta_mork_bridge_space_restrict_into(CettaMorkSpaceHandle *dst,
                                           const CettaMorkSpaceHandle *src);
CettaMorkSpaceHandle *cetta_mork_bridge_space_restrict(
    const CettaMorkSpaceHandle *lhs,
    const CettaMorkSpaceHandle *rhs);
CettaMorkCursorHandle *cetta_mork_bridge_cursor_new(
    const CettaMorkSpaceHandle *space);
void cetta_mork_bridge_cursor_free(CettaMorkCursorHandle *cursor);
bool cetta_mork_bridge_cursor_path_exists(const CettaMorkCursorHandle *cursor,
                                          bool *out_exists);
bool cetta_mork_bridge_cursor_is_val(const CettaMorkCursorHandle *cursor,
                                     bool *out_is_val);
bool cetta_mork_bridge_cursor_child_count(const CettaMorkCursorHandle *cursor,
                                          uint64_t *out_child_count);
bool cetta_mork_bridge_cursor_path_bytes(const CettaMorkCursorHandle *cursor,
                                         uint8_t **out_bytes,
                                         size_t *out_len);
bool cetta_mork_bridge_cursor_child_bytes(const CettaMorkCursorHandle *cursor,
                                          uint8_t **out_bytes,
                                          size_t *out_len);
bool cetta_mork_bridge_cursor_val_count(const CettaMorkCursorHandle *cursor,
                                        uint64_t *out_val_count);
bool cetta_mork_bridge_cursor_depth(const CettaMorkCursorHandle *cursor,
                                    uint64_t *out_depth);
bool cetta_mork_bridge_cursor_reset(CettaMorkCursorHandle *cursor);
bool cetta_mork_bridge_cursor_ascend(CettaMorkCursorHandle *cursor,
                                     uint64_t steps,
                                     bool *out_moved);
bool cetta_mork_bridge_cursor_descend_byte(CettaMorkCursorHandle *cursor,
                                           uint32_t byte,
                                           bool *out_moved);
bool cetta_mork_bridge_cursor_descend_index(CettaMorkCursorHandle *cursor,
                                            uint64_t index,
                                            bool *out_moved);
bool cetta_mork_bridge_cursor_descend_first(CettaMorkCursorHandle *cursor,
                                            bool *out_moved);
bool cetta_mork_bridge_cursor_descend_last(CettaMorkCursorHandle *cursor,
                                           bool *out_moved);
bool cetta_mork_bridge_cursor_descend_until(CettaMorkCursorHandle *cursor,
                                            bool *out_moved);
bool cetta_mork_bridge_cursor_descend_until_max_bytes(
    CettaMorkCursorHandle *cursor,
    uint64_t max_bytes,
    bool *out_moved);
bool cetta_mork_bridge_cursor_ascend_until(CettaMorkCursorHandle *cursor,
                                           bool *out_moved);
bool cetta_mork_bridge_cursor_ascend_until_branch(CettaMorkCursorHandle *cursor,
                                                  bool *out_moved);
bool cetta_mork_bridge_cursor_next_sibling_byte(CettaMorkCursorHandle *cursor,
                                                bool *out_moved);
bool cetta_mork_bridge_cursor_prev_sibling_byte(CettaMorkCursorHandle *cursor,
                                                bool *out_moved);
bool cetta_mork_bridge_cursor_next_step(CettaMorkCursorHandle *cursor,
                                        bool *out_moved);
bool cetta_mork_bridge_cursor_next_val(CettaMorkCursorHandle *cursor,
                                       bool *out_moved);
CettaMorkCursorHandle *cetta_mork_bridge_cursor_fork(
    const CettaMorkCursorHandle *cursor);
CettaMorkSpaceHandle *cetta_mork_bridge_cursor_make_map(
    const CettaMorkCursorHandle *cursor);
CettaMorkSpaceHandle *cetta_mork_bridge_cursor_make_snapshot_map(
    const CettaMorkCursorHandle *cursor);
CettaMorkProductCursorHandle *cetta_mork_bridge_product_cursor_new(
    const CettaMorkSpaceHandle *const *spaces,
    size_t count);
void cetta_mork_bridge_product_cursor_free(CettaMorkProductCursorHandle *cursor);
bool cetta_mork_bridge_product_cursor_path_exists(
    const CettaMorkProductCursorHandle *cursor,
    bool *out_exists);
bool cetta_mork_bridge_product_cursor_is_val(
    const CettaMorkProductCursorHandle *cursor,
    bool *out_is_val);
bool cetta_mork_bridge_product_cursor_child_count(
    const CettaMorkProductCursorHandle *cursor,
    uint64_t *out_child_count);
bool cetta_mork_bridge_product_cursor_path_bytes(
    const CettaMorkProductCursorHandle *cursor,
    uint8_t **out_bytes,
    size_t *out_len);
bool cetta_mork_bridge_product_cursor_child_bytes(
    const CettaMorkProductCursorHandle *cursor,
    uint8_t **out_bytes,
    size_t *out_len);
bool cetta_mork_bridge_product_cursor_val_count(
    const CettaMorkProductCursorHandle *cursor,
    uint64_t *out_val_count);
bool cetta_mork_bridge_product_cursor_depth(
    const CettaMorkProductCursorHandle *cursor,
    uint64_t *out_depth);
bool cetta_mork_bridge_product_cursor_factor_count(
    const CettaMorkProductCursorHandle *cursor,
    uint64_t *out_factor_count);
bool cetta_mork_bridge_product_cursor_focus_factor(
    const CettaMorkProductCursorHandle *cursor,
    uint64_t *out_focus_factor);
bool cetta_mork_bridge_product_cursor_path_indices(
    const CettaMorkProductCursorHandle *cursor,
    uint8_t **out_bytes,
    size_t *out_len,
    uint32_t *out_count);
bool cetta_mork_bridge_product_cursor_reset(CettaMorkProductCursorHandle *cursor);
bool cetta_mork_bridge_product_cursor_ascend(
    CettaMorkProductCursorHandle *cursor,
    uint64_t steps,
    bool *out_moved);
bool cetta_mork_bridge_product_cursor_descend_byte(
    CettaMorkProductCursorHandle *cursor,
    uint32_t byte,
    bool *out_moved);
bool cetta_mork_bridge_product_cursor_descend_index(
    CettaMorkProductCursorHandle *cursor,
    uint64_t index,
    bool *out_moved);
bool cetta_mork_bridge_product_cursor_descend_first(
    CettaMorkProductCursorHandle *cursor,
    bool *out_moved);
bool cetta_mork_bridge_product_cursor_descend_last(
    CettaMorkProductCursorHandle *cursor,
    bool *out_moved);
bool cetta_mork_bridge_product_cursor_descend_until(
    CettaMorkProductCursorHandle *cursor,
    bool *out_moved);
bool cetta_mork_bridge_product_cursor_descend_until_max_bytes(
    CettaMorkProductCursorHandle *cursor,
    uint64_t max_bytes,
    bool *out_moved);
bool cetta_mork_bridge_product_cursor_ascend_until(
    CettaMorkProductCursorHandle *cursor,
    bool *out_moved);
bool cetta_mork_bridge_product_cursor_ascend_until_branch(
    CettaMorkProductCursorHandle *cursor,
    bool *out_moved);
bool cetta_mork_bridge_product_cursor_next_sibling_byte(
    CettaMorkProductCursorHandle *cursor,
    bool *out_moved);
bool cetta_mork_bridge_product_cursor_prev_sibling_byte(
    CettaMorkProductCursorHandle *cursor,
    bool *out_moved);
bool cetta_mork_bridge_product_cursor_next_step(
    CettaMorkProductCursorHandle *cursor,
    bool *out_moved);
bool cetta_mork_bridge_product_cursor_next_val(
    CettaMorkProductCursorHandle *cursor,
    bool *out_moved);
CettaMorkOverlayCursorHandle *cetta_mork_bridge_overlay_cursor_new(
    const CettaMorkSpaceHandle *base,
    const CettaMorkSpaceHandle *overlay);
void cetta_mork_bridge_overlay_cursor_free(CettaMorkOverlayCursorHandle *cursor);
bool cetta_mork_bridge_overlay_cursor_path_exists(
    const CettaMorkOverlayCursorHandle *cursor,
    bool *out_exists);
bool cetta_mork_bridge_overlay_cursor_is_val(
    const CettaMorkOverlayCursorHandle *cursor,
    bool *out_is_val);
bool cetta_mork_bridge_overlay_cursor_child_count(
    const CettaMorkOverlayCursorHandle *cursor,
    uint64_t *out_child_count);
bool cetta_mork_bridge_overlay_cursor_path_bytes(
    const CettaMorkOverlayCursorHandle *cursor,
    uint8_t **out_bytes,
    size_t *out_len);
bool cetta_mork_bridge_overlay_cursor_child_bytes(
    const CettaMorkOverlayCursorHandle *cursor,
    uint8_t **out_bytes,
    size_t *out_len);
bool cetta_mork_bridge_overlay_cursor_depth(
    const CettaMorkOverlayCursorHandle *cursor,
    uint64_t *out_depth);
bool cetta_mork_bridge_overlay_cursor_reset(CettaMorkOverlayCursorHandle *cursor);
bool cetta_mork_bridge_overlay_cursor_ascend(
    CettaMorkOverlayCursorHandle *cursor,
    uint64_t steps,
    bool *out_moved);
bool cetta_mork_bridge_overlay_cursor_descend_byte(
    CettaMorkOverlayCursorHandle *cursor,
    uint32_t byte,
    bool *out_moved);
bool cetta_mork_bridge_overlay_cursor_descend_index(
    CettaMorkOverlayCursorHandle *cursor,
    uint64_t index,
    bool *out_moved);
bool cetta_mork_bridge_overlay_cursor_descend_first(
    CettaMorkOverlayCursorHandle *cursor,
    bool *out_moved);
bool cetta_mork_bridge_overlay_cursor_descend_last(
    CettaMorkOverlayCursorHandle *cursor,
    bool *out_moved);
bool cetta_mork_bridge_overlay_cursor_descend_until(
    CettaMorkOverlayCursorHandle *cursor,
    bool *out_moved);
bool cetta_mork_bridge_overlay_cursor_descend_until_max_bytes(
    CettaMorkOverlayCursorHandle *cursor,
    uint64_t max_bytes,
    bool *out_moved);
bool cetta_mork_bridge_overlay_cursor_ascend_until(
    CettaMorkOverlayCursorHandle *cursor,
    bool *out_moved);
bool cetta_mork_bridge_overlay_cursor_ascend_until_branch(
    CettaMorkOverlayCursorHandle *cursor,
    bool *out_moved);
bool cetta_mork_bridge_overlay_cursor_next_sibling_byte(
    CettaMorkOverlayCursorHandle *cursor,
    bool *out_moved);
bool cetta_mork_bridge_overlay_cursor_prev_sibling_byte(
    CettaMorkOverlayCursorHandle *cursor,
    bool *out_moved);
bool cetta_mork_bridge_overlay_cursor_next_step(
    CettaMorkOverlayCursorHandle *cursor,
    bool *out_moved);
bool cetta_mork_bridge_space_dump_act_file(CettaMorkSpaceHandle *space,
                                           const uint8_t *path,
                                           size_t len,
                                           uint64_t *out_saved);
bool cetta_mork_bridge_space_load_act_file(CettaMorkSpaceHandle *space,
                                           const uint8_t *path,
                                           size_t len,
                                           uint64_t *out_loaded);
bool cetta_mork_bridge_space_query_bindings(CettaMorkSpaceHandle *space,
                                           const uint8_t *pattern,
                                           size_t len,
                                           uint8_t **out_packet,
                                           size_t *out_len,
                                           uint32_t *out_rows);
bool cetta_mork_bridge_space_query_bindings_text(CettaMorkSpaceHandle *space,
                                                 const char *pattern_text,
                                                 uint8_t **out_packet,
                                                 size_t *out_len,
                                                 uint32_t *out_rows);
bool cetta_mork_bridge_space_query_bindings_query_only_v2(CettaMorkSpaceHandle *space,
                                                          const uint8_t *pattern,
                                                          size_t len,
                                                          uint8_t **out_packet,
                                                          size_t *out_len,
                                                          uint32_t *out_rows);
bool cetta_mork_bridge_space_query_bindings_multi_ref_v3(CettaMorkSpaceHandle *space,
                                                         const uint8_t *pattern,
                                                         size_t len,
                                                         uint8_t **out_packet,
                                                         size_t *out_len,
                                                         uint32_t *out_rows);

CettaMorkProgramHandle *cetta_mork_bridge_program_new(void);
void cetta_mork_bridge_program_free(CettaMorkProgramHandle *program);
bool cetta_mork_bridge_program_clear(CettaMorkProgramHandle *program);
bool cetta_mork_bridge_program_add_sexpr(CettaMorkProgramHandle *program,
                                         const uint8_t *text, size_t len,
                                         uint64_t *out_added);
bool cetta_mork_bridge_program_size(const CettaMorkProgramHandle *program,
                                    uint64_t *out_size);
bool cetta_mork_bridge_program_dump(CettaMorkProgramHandle *program,
                                    uint8_t **out_packet, size_t *out_len,
                                    uint32_t *out_rows);

CettaMorkContextHandle *cetta_mork_bridge_context_new(void);
void cetta_mork_bridge_context_free(CettaMorkContextHandle *context);
bool cetta_mork_bridge_context_clear(CettaMorkContextHandle *context);
bool cetta_mork_bridge_context_load_program(CettaMorkContextHandle *context,
                                            const CettaMorkProgramHandle *program,
                                            uint64_t *out_added);
bool cetta_mork_bridge_context_add_sexpr(CettaMorkContextHandle *context,
                                         const uint8_t *text, size_t len,
                                         uint64_t *out_added);
bool cetta_mork_bridge_context_remove_sexpr(CettaMorkContextHandle *context,
                                            const uint8_t *text, size_t len,
                                            uint64_t *out_removed);
bool cetta_mork_bridge_context_size(const CettaMorkContextHandle *context,
                                    uint64_t *out_size);
bool cetta_mork_bridge_context_run(CettaMorkContextHandle *context,
                                   uint64_t steps, uint64_t *out_performed);
bool cetta_mork_bridge_context_dump(CettaMorkContextHandle *context,
                                    uint8_t **out_packet, size_t *out_len,
                                    uint32_t *out_rows);

void cetta_mork_bridge_bytes_free(uint8_t *data, size_t len);

#endif /* CETTA_MORK_SPACE_BRIDGE_RUNTIME_H */
