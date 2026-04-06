#define _GNU_SOURCE
#include "mork_space_bridge_runtime.h"

#include "atom.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#if !CETTA_BUILD_WITH_MORK_STATIC
#include <dlfcn.h>
#endif

typedef struct {
    int32_t code;
    uint64_t value;
    uint8_t *message;
    size_t message_len;
} CettaMorkStatus;

typedef struct {
    int32_t code;
    uint8_t *data;
    size_t len;
    uint32_t count;
    uint8_t *message;
    size_t message_len;
} CettaMorkBuffer;

static char g_mork_bridge_error[512];

static void bridge_set_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_mork_bridge_error, sizeof(g_mork_bridge_error), fmt, ap);
    va_end(ap);
}

static void bridge_set_error_bytes(const char *prefix, const uint8_t *bytes, size_t len) {
    size_t usable = len;
    if (usable > sizeof(g_mork_bridge_error) - 1)
        usable = sizeof(g_mork_bridge_error) - 1;
    if (usable == 0) {
        bridge_set_error("%s", prefix);
        return;
    }
    snprintf(g_mork_bridge_error, sizeof(g_mork_bridge_error),
             "%s%.*s", prefix, (int)usable, (const char *)bytes);
}

static uint32_t read_u32_be(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static bool bridge_take_status_value(const char *ctx, CettaMorkStatus st,
                                     uint64_t *out_value,
                                     void (*free_bytes)(uint8_t *, size_t)) {
    if (out_value)
        *out_value = 0;
    if (st.code == 0) {
        if (out_value)
            *out_value = st.value;
        if (free_bytes)
            free_bytes(st.message, st.message_len);
        return true;
    }
    if (st.message && st.message_len > 0)
        bridge_set_error_bytes(ctx, st.message, st.message_len);
    else
        bridge_set_error("%sstatus code %d", ctx, st.code);
    if (free_bytes)
        free_bytes(st.message, st.message_len);
    return false;
}

static bool bridge_take_status_bool(const char *ctx, CettaMorkStatus st,
                                    bool *out_bool,
                                    void (*free_bytes)(uint8_t *, size_t)) {
    uint64_t value = 0;
    if (out_bool)
        *out_bool = false;
    if (!bridge_take_status_value(ctx, st, &value, free_bytes))
        return false;
    if (out_bool)
        *out_bool = (value != 0);
    return true;
}

static bool bridge_take_buffer(const char *ctx, CettaMorkBuffer buf,
                               uint8_t **out_packet, size_t *out_len,
                               uint32_t *out_rows,
                               void (*free_bytes)(uint8_t *, size_t)) {
    *out_packet = NULL;
    *out_len = 0;
    *out_rows = 0;
    if (buf.code != 0) {
        if (buf.message && buf.message_len > 0)
            bridge_set_error_bytes(ctx, buf.message, buf.message_len);
        else
            bridge_set_error("%scode %d", ctx, buf.code);
        if (free_bytes) {
            free_bytes(buf.message, buf.message_len);
            free_bytes(buf.data, buf.len);
        }
        return false;
    }
    if (free_bytes)
        free_bytes(buf.message, buf.message_len);
    *out_packet = buf.data;
    *out_len = buf.len;
    *out_rows = buf.count;
    return true;
}

#if CETTA_BUILD_WITH_MORK_STATIC

extern CettaMorkSpaceHandle *mork_space_new(void);
extern void mork_space_free(CettaMorkSpaceHandle *space);
extern CettaMorkStatus mork_space_clear(CettaMorkSpaceHandle *space);
extern CettaMorkStatus mork_space_add_sexpr(CettaMorkSpaceHandle *space,
                                            const uint8_t *text,
                                            size_t len);
extern CettaMorkStatus mork_space_remove_sexpr(CettaMorkSpaceHandle *space,
                                               const uint8_t *text,
                                               size_t len);
extern CettaMorkStatus mork_space_add_indexed_sexpr(CettaMorkSpaceHandle *space,
                                                    uint32_t atom_idx,
                                                    const uint8_t *text,
                                                    size_t len);
extern CettaMorkStatus mork_space_logical_size(const CettaMorkSpaceHandle *space);
extern CettaMorkStatus mork_space_size(const CettaMorkSpaceHandle *space);
extern CettaMorkStatus mork_space_step(CettaMorkSpaceHandle *space,
                                       uint64_t steps);
extern CettaMorkStatus mork_space_dump_act_file(CettaMorkSpaceHandle *space,
                                                const uint8_t *path,
                                                size_t len);
extern CettaMorkStatus mork_space_load_act_file(CettaMorkSpaceHandle *space,
                                                const uint8_t *path,
                                                size_t len);
extern CettaMorkBuffer mork_space_dump(CettaMorkSpaceHandle *space);
extern CettaMorkStatus mork_space_join_into(CettaMorkSpaceHandle *dst,
                                            const CettaMorkSpaceHandle *src);
extern CettaMorkSpaceHandle *mork_space_clone(const CettaMorkSpaceHandle *space);
extern CettaMorkSpaceHandle *mork_space_join(const CettaMorkSpaceHandle *lhs,
                                             const CettaMorkSpaceHandle *rhs);
extern CettaMorkStatus mork_space_meet_into(CettaMorkSpaceHandle *dst,
                                            const CettaMorkSpaceHandle *src);
extern CettaMorkSpaceHandle *mork_space_meet(const CettaMorkSpaceHandle *lhs,
                                             const CettaMorkSpaceHandle *rhs);
extern CettaMorkStatus mork_space_subtract_into(CettaMorkSpaceHandle *dst,
                                                const CettaMorkSpaceHandle *src);
extern CettaMorkSpaceHandle *mork_space_subtract(const CettaMorkSpaceHandle *lhs,
                                                 const CettaMorkSpaceHandle *rhs);
extern CettaMorkStatus mork_space_restrict_into(CettaMorkSpaceHandle *dst,
                                                const CettaMorkSpaceHandle *src);
extern CettaMorkSpaceHandle *mork_space_restrict(const CettaMorkSpaceHandle *lhs,
                                                 const CettaMorkSpaceHandle *rhs);
extern CettaMorkCursorHandle *mork_cursor_new(const CettaMorkSpaceHandle *space);
extern void mork_cursor_free(CettaMorkCursorHandle *cursor);
extern CettaMorkStatus mork_cursor_path_exists(const CettaMorkCursorHandle *cursor);
extern CettaMorkStatus mork_cursor_is_val(const CettaMorkCursorHandle *cursor);
extern CettaMorkStatus mork_cursor_child_count(const CettaMorkCursorHandle *cursor);
extern CettaMorkBuffer mork_cursor_path_bytes(const CettaMorkCursorHandle *cursor);
extern CettaMorkBuffer mork_cursor_child_bytes(const CettaMorkCursorHandle *cursor);
extern CettaMorkStatus mork_cursor_val_count(const CettaMorkCursorHandle *cursor);
extern CettaMorkStatus mork_cursor_depth(const CettaMorkCursorHandle *cursor);
extern CettaMorkStatus mork_cursor_reset(CettaMorkCursorHandle *cursor);
extern CettaMorkStatus mork_cursor_ascend(CettaMorkCursorHandle *cursor,
                                          uint64_t steps);
extern CettaMorkStatus mork_cursor_descend_byte(CettaMorkCursorHandle *cursor,
                                                uint32_t byte);
extern CettaMorkStatus mork_cursor_descend_index(CettaMorkCursorHandle *cursor,
                                                 uint64_t index);
extern CettaMorkStatus mork_cursor_descend_first(CettaMorkCursorHandle *cursor);
extern CettaMorkStatus mork_cursor_descend_last(CettaMorkCursorHandle *cursor);
extern CettaMorkStatus mork_cursor_descend_until(CettaMorkCursorHandle *cursor);
extern CettaMorkStatus mork_cursor_descend_until_max_bytes(CettaMorkCursorHandle *cursor,
                                                           uint64_t max_bytes);
extern CettaMorkStatus mork_cursor_ascend_until(CettaMorkCursorHandle *cursor);
extern CettaMorkStatus mork_cursor_ascend_until_branch(CettaMorkCursorHandle *cursor);
extern CettaMorkStatus mork_cursor_next_sibling_byte(CettaMorkCursorHandle *cursor);
extern CettaMorkStatus mork_cursor_prev_sibling_byte(CettaMorkCursorHandle *cursor);
extern CettaMorkStatus mork_cursor_next_step(CettaMorkCursorHandle *cursor);
extern CettaMorkStatus mork_cursor_next_val(CettaMorkCursorHandle *cursor);
extern CettaMorkCursorHandle *mork_cursor_fork(const CettaMorkCursorHandle *cursor);
extern CettaMorkSpaceHandle *mork_cursor_make_map(const CettaMorkCursorHandle *cursor);
extern CettaMorkSpaceHandle *mork_cursor_make_snapshot_map(const CettaMorkCursorHandle *cursor);
extern CettaMorkProductCursorHandle *mork_product_cursor_new(
    const CettaMorkSpaceHandle *const *spaces,
    size_t count);
extern void mork_product_cursor_free(CettaMorkProductCursorHandle *cursor);
extern CettaMorkStatus mork_product_cursor_path_exists(
    const CettaMorkProductCursorHandle *cursor);
extern CettaMorkStatus mork_product_cursor_is_val(
    const CettaMorkProductCursorHandle *cursor);
extern CettaMorkStatus mork_product_cursor_child_count(
    const CettaMorkProductCursorHandle *cursor);
extern CettaMorkBuffer mork_product_cursor_path_bytes(
    const CettaMorkProductCursorHandle *cursor);
extern CettaMorkBuffer mork_product_cursor_child_bytes(
    const CettaMorkProductCursorHandle *cursor);
extern CettaMorkStatus mork_product_cursor_val_count(
    const CettaMorkProductCursorHandle *cursor);
extern CettaMorkStatus mork_product_cursor_depth(
    const CettaMorkProductCursorHandle *cursor);
extern CettaMorkStatus mork_product_cursor_factor_count(
    const CettaMorkProductCursorHandle *cursor);
extern CettaMorkStatus mork_product_cursor_focus_factor(
    const CettaMorkProductCursorHandle *cursor);
extern CettaMorkBuffer mork_product_cursor_path_indices(
    const CettaMorkProductCursorHandle *cursor);
extern CettaMorkStatus mork_product_cursor_reset(CettaMorkProductCursorHandle *cursor);
extern CettaMorkStatus mork_product_cursor_ascend(CettaMorkProductCursorHandle *cursor,
                                                  uint64_t steps);
extern CettaMorkStatus mork_product_cursor_descend_byte(
    CettaMorkProductCursorHandle *cursor,
    uint32_t byte);
extern CettaMorkStatus mork_product_cursor_descend_index(
    CettaMorkProductCursorHandle *cursor,
    uint64_t index);
extern CettaMorkStatus mork_product_cursor_descend_first(
    CettaMorkProductCursorHandle *cursor);
extern CettaMorkStatus mork_product_cursor_descend_last(
    CettaMorkProductCursorHandle *cursor);
extern CettaMorkStatus mork_product_cursor_descend_until(
    CettaMorkProductCursorHandle *cursor);
extern CettaMorkStatus mork_product_cursor_descend_until_max_bytes(
    CettaMorkProductCursorHandle *cursor,
    uint64_t max_bytes);
extern CettaMorkStatus mork_product_cursor_ascend_until(
    CettaMorkProductCursorHandle *cursor);
extern CettaMorkStatus mork_product_cursor_ascend_until_branch(
    CettaMorkProductCursorHandle *cursor);
extern CettaMorkStatus mork_product_cursor_next_sibling_byte(
    CettaMorkProductCursorHandle *cursor);
extern CettaMorkStatus mork_product_cursor_prev_sibling_byte(
    CettaMorkProductCursorHandle *cursor);
extern CettaMorkStatus mork_product_cursor_next_step(
    CettaMorkProductCursorHandle *cursor);
extern CettaMorkStatus mork_product_cursor_next_val(
    CettaMorkProductCursorHandle *cursor);
extern CettaMorkOverlayCursorHandle *mork_overlay_cursor_new(
    const CettaMorkSpaceHandle *base,
    const CettaMorkSpaceHandle *overlay);
extern void mork_overlay_cursor_free(CettaMorkOverlayCursorHandle *cursor);
extern CettaMorkStatus mork_overlay_cursor_path_exists(
    const CettaMorkOverlayCursorHandle *cursor);
extern CettaMorkStatus mork_overlay_cursor_is_val(
    const CettaMorkOverlayCursorHandle *cursor);
extern CettaMorkStatus mork_overlay_cursor_child_count(
    const CettaMorkOverlayCursorHandle *cursor);
extern CettaMorkBuffer mork_overlay_cursor_path_bytes(
    const CettaMorkOverlayCursorHandle *cursor);
extern CettaMorkBuffer mork_overlay_cursor_child_bytes(
    const CettaMorkOverlayCursorHandle *cursor);
extern CettaMorkStatus mork_overlay_cursor_depth(
    const CettaMorkOverlayCursorHandle *cursor);
extern CettaMorkStatus mork_overlay_cursor_reset(CettaMorkOverlayCursorHandle *cursor);
extern CettaMorkStatus mork_overlay_cursor_ascend(CettaMorkOverlayCursorHandle *cursor,
                                                  uint64_t steps);
extern CettaMorkStatus mork_overlay_cursor_descend_byte(
    CettaMorkOverlayCursorHandle *cursor,
    uint32_t byte);
extern CettaMorkStatus mork_overlay_cursor_descend_index(
    CettaMorkOverlayCursorHandle *cursor,
    uint64_t index);
extern CettaMorkStatus mork_overlay_cursor_descend_first(
    CettaMorkOverlayCursorHandle *cursor);
extern CettaMorkStatus mork_overlay_cursor_descend_last(
    CettaMorkOverlayCursorHandle *cursor);
extern CettaMorkStatus mork_overlay_cursor_descend_until(
    CettaMorkOverlayCursorHandle *cursor);
extern CettaMorkStatus mork_overlay_cursor_descend_until_max_bytes(
    CettaMorkOverlayCursorHandle *cursor,
    uint64_t max_bytes);
extern CettaMorkStatus mork_overlay_cursor_ascend_until(
    CettaMorkOverlayCursorHandle *cursor);
extern CettaMorkStatus mork_overlay_cursor_ascend_until_branch(
    CettaMorkOverlayCursorHandle *cursor);
extern CettaMorkStatus mork_overlay_cursor_next_sibling_byte(
    CettaMorkOverlayCursorHandle *cursor);
extern CettaMorkStatus mork_overlay_cursor_prev_sibling_byte(
    CettaMorkOverlayCursorHandle *cursor);
extern CettaMorkStatus mork_overlay_cursor_next_step(
    CettaMorkOverlayCursorHandle *cursor);
extern CettaMorkBuffer mork_space_query_indices(CettaMorkSpaceHandle *space,
                                                const uint8_t *pattern,
                                                size_t len);
extern CettaMorkBuffer mork_space_compile_query_expr_text(CettaMorkSpaceHandle *space,
                                                          const uint8_t *pattern,
                                                          size_t len)
    __attribute__((weak));
extern CettaMorkBuffer mork_space_query_candidates_prefix_expr_bytes(
    CettaMorkSpaceHandle *space,
    const uint8_t *pattern_expr,
    size_t len)
    __attribute__((weak));
extern CettaMorkBuffer mork_space_query_candidates_expr_bytes(CettaMorkSpaceHandle *space,
                                                              const uint8_t *pattern_expr,
                                                              size_t len)
    __attribute__((weak));
extern CettaMorkBuffer mork_space_query_bindings(CettaMorkSpaceHandle *space,
                                                 const uint8_t *pattern,
                                                 size_t len);
extern CettaMorkBuffer mork_space_query_bindings_query_only_v2(CettaMorkSpaceHandle *space,
                                                               const uint8_t *pattern,
                                                               size_t len)
    __attribute__((weak));
extern CettaMorkBuffer mork_space_query_bindings_multi_ref_v3(CettaMorkSpaceHandle *space,
                                                              const uint8_t *pattern,
                                                              size_t len)
    __attribute__((weak));
extern CettaMorkProgramHandle *mork_program_new(void);
extern void mork_program_free(CettaMorkProgramHandle *program);
extern CettaMorkStatus mork_program_clear(CettaMorkProgramHandle *program);
extern CettaMorkStatus mork_program_add_sexpr(CettaMorkProgramHandle *program,
                                              const uint8_t *text, size_t len);
extern CettaMorkStatus mork_program_size(const CettaMorkProgramHandle *program);
extern CettaMorkBuffer mork_program_dump(CettaMorkProgramHandle *program);
extern CettaMorkContextHandle *mork_context_new(void);
extern void mork_context_free(CettaMorkContextHandle *context);
extern CettaMorkStatus mork_context_clear(CettaMorkContextHandle *context);
extern CettaMorkStatus mork_context_load_program(CettaMorkContextHandle *context,
                                                 const CettaMorkProgramHandle *program);
extern CettaMorkStatus mork_context_add_sexpr(CettaMorkContextHandle *context,
                                              const uint8_t *text, size_t len);
extern CettaMorkStatus mork_context_remove_sexpr(CettaMorkContextHandle *context,
                                                 const uint8_t *text, size_t len);
extern CettaMorkStatus mork_context_size(const CettaMorkContextHandle *context);
extern CettaMorkStatus mork_context_run(CettaMorkContextHandle *context,
                                        uint64_t steps);
extern CettaMorkBuffer mork_context_dump(CettaMorkContextHandle *context);
extern void mork_bytes_free(uint8_t *data, size_t len);

static void bridge_free_bytes(uint8_t *data, size_t len) {
    if (data)
        mork_bytes_free(data, len);
}

#define BRIDGE_PROGRAM_CONTEXT_AVAILABLE() (true)
#define BRIDGE_PROGRAM_NEW() mork_program_new()
#define BRIDGE_PROGRAM_FREE(program) mork_program_free(program)
#define BRIDGE_PROGRAM_CLEAR(program) mork_program_clear(program)
#define BRIDGE_PROGRAM_ADD(program, text, len) mork_program_add_sexpr(program, text, len)
#define BRIDGE_PROGRAM_SIZE(program) mork_program_size(program)
#define BRIDGE_PROGRAM_DUMP(program) mork_program_dump(program)
#define BRIDGE_CONTEXT_NEW() mork_context_new()
#define BRIDGE_CONTEXT_FREE(context) mork_context_free(context)
#define BRIDGE_CONTEXT_CLEAR(context) mork_context_clear(context)
#define BRIDGE_CONTEXT_LOAD_PROGRAM(context, program) mork_context_load_program(context, program)
#define BRIDGE_CONTEXT_ADD(context, text, len) mork_context_add_sexpr(context, text, len)
#define BRIDGE_CONTEXT_REMOVE(context, text, len) mork_context_remove_sexpr(context, text, len)
#define BRIDGE_CONTEXT_SIZE(context) mork_context_size(context)
#define BRIDGE_CONTEXT_RUN(context, steps) mork_context_run(context, steps)
#define BRIDGE_CONTEXT_DUMP(context) mork_context_dump(context)

static bool bridge_status_ok(const char *ctx, CettaMorkStatus st) {
    if (st.code == 0) {
        bridge_free_bytes(st.message, st.message_len);
        return true;
    }
    if (st.message && st.message_len > 0)
        bridge_set_error_bytes(ctx, st.message, st.message_len);
    else
        bridge_set_error("%sstatus code %d", ctx, st.code);
    bridge_free_bytes(st.message, st.message_len);
    return false;
}

bool cetta_mork_bridge_is_available(void) {
    return true;
}

const char *cetta_mork_bridge_last_error(void) {
    if (!g_mork_bridge_error[0])
        return "no MORK bridge error";
    return g_mork_bridge_error;
}

CettaMorkSpaceHandle *cetta_mork_bridge_space_new(void) {
    CettaMorkSpaceHandle *space = mork_space_new();
    if (!space)
        bridge_set_error("mork_space_new returned null");
    return space;
}

void cetta_mork_bridge_space_free(CettaMorkSpaceHandle *space) {
    if (space)
        mork_space_free(space);
}

bool cetta_mork_bridge_space_clear(CettaMorkSpaceHandle *space) {
    if (!space) {
        bridge_set_error("cannot clear null MORK bridge space");
        return false;
    }
    return bridge_status_ok("mork_space_clear failed: ", mork_space_clear(space));
}

bool cetta_mork_bridge_space_add_text(CettaMorkSpaceHandle *space,
                                      const char *text,
                                      uint64_t *out_added) {
    if (!text) {
        bridge_set_error("cannot add null text to MORK bridge space");
        return false;
    }
    return cetta_mork_bridge_space_add_sexpr(space,
                                             (const uint8_t *)text,
                                             strlen(text),
                                             out_added);
}

bool cetta_mork_bridge_space_add_sexpr(CettaMorkSpaceHandle *space,
                                       const uint8_t *text,
                                       size_t len,
                                       uint64_t *out_added) {
    if (!space) {
        bridge_set_error("cannot add to null MORK bridge space");
        return false;
    }
    return bridge_take_status_value("mork_space_add_sexpr failed: ",
                                    mork_space_add_sexpr(space, text, len),
                                    out_added,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_space_remove_text(CettaMorkSpaceHandle *space,
                                         const char *text,
                                         uint64_t *out_removed) {
    if (!text) {
        bridge_set_error("cannot remove null text from MORK bridge space");
        return false;
    }
    return cetta_mork_bridge_space_remove_sexpr(space,
                                                (const uint8_t *)text,
                                                strlen(text),
                                                out_removed);
}

bool cetta_mork_bridge_space_remove_sexpr(CettaMorkSpaceHandle *space,
                                          const uint8_t *text,
                                          size_t len,
                                          uint64_t *out_removed) {
    if (!space) {
        bridge_set_error("cannot remove from null MORK bridge space");
        return false;
    }
    return bridge_take_status_value("mork_space_remove_sexpr failed: ",
                                    mork_space_remove_sexpr(space, text, len),
                                    out_removed,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_space_add_indexed_text(CettaMorkSpaceHandle *space,
                                              uint32_t atom_idx,
                                              const char *text) {
    if (!text) {
        bridge_set_error("cannot add null indexed text to MORK bridge space");
        return false;
    }
    return cetta_mork_bridge_space_add_indexed_sexpr(space,
                                                     atom_idx,
                                                     (const uint8_t *)text,
                                                     strlen(text));
}

bool cetta_mork_bridge_space_add_indexed_sexpr(CettaMorkSpaceHandle *space,
                                               uint32_t atom_idx,
                                               const uint8_t *text,
                                               size_t len) {
    if (!space) {
        bridge_set_error("cannot add to null MORK bridge space");
        return false;
    }
    return bridge_status_ok("mork_space_add_indexed_sexpr failed: ",
                            mork_space_add_indexed_sexpr(space, atom_idx, text, len));
}

bool cetta_mork_bridge_space_size(const CettaMorkSpaceHandle *space,
                                  uint64_t *out_size) {
    if (!space) {
        bridge_set_error("cannot size null MORK bridge space");
        return false;
    }
    return bridge_take_status_value("mork_space_size failed: ",
                                    mork_space_size(space),
                                    out_size,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_space_unique_size(const CettaMorkSpaceHandle *space,
                                         uint64_t *out_unique_size) {
    return cetta_mork_bridge_space_size(space, out_unique_size);
}

bool cetta_mork_bridge_space_logical_size(const CettaMorkSpaceHandle *space,
                                          uint64_t *out_logical_size) {
    if (!space) {
        bridge_set_error("cannot size null MORK bridge space");
        return false;
    }
    return bridge_take_status_value("mork_space_logical_size failed: ",
                                    mork_space_logical_size(space),
                                    out_logical_size,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_space_step(CettaMorkSpaceHandle *space,
                                  uint64_t steps,
                                  uint64_t *out_performed) {
    if (!space) {
        bridge_set_error("cannot step null MORK bridge space");
        return false;
    }
    return bridge_take_status_value("mork_space_step failed: ",
                                    mork_space_step(space, steps),
                                    out_performed,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_space_dump(CettaMorkSpaceHandle *space,
                                  uint8_t **out_packet,
                                  size_t *out_len,
                                  uint32_t *out_rows) {
    if (!space) {
        bridge_set_error("cannot dump null MORK bridge space");
        return false;
    }
    return bridge_take_buffer("mork_space_dump failed: ",
                              mork_space_dump(space),
                              out_packet, out_len, out_rows,
                              bridge_free_bytes);
}

bool cetta_mork_bridge_space_join_into(CettaMorkSpaceHandle *dst,
                                       const CettaMorkSpaceHandle *src) {
    if (!dst || !src) {
        bridge_set_error("cannot join into null MORK bridge space");
        return false;
    }
    return bridge_status_ok("mork_space_join_into failed: ",
                            mork_space_join_into(dst, src));
}

CettaMorkSpaceHandle *cetta_mork_bridge_space_join(
    const CettaMorkSpaceHandle *lhs,
    const CettaMorkSpaceHandle *rhs) {
    CettaMorkSpaceHandle *joined;
    if (!lhs || !rhs) {
        bridge_set_error("cannot join null MORK bridge space");
        return NULL;
    }
    joined = mork_space_join(lhs, rhs);
    if (!joined)
        bridge_set_error("mork_space_join returned null");
    return joined;
}

CettaMorkSpaceHandle *cetta_mork_bridge_space_clone(
    const CettaMorkSpaceHandle *space) {
    CettaMorkSpaceHandle *clone;
    if (!space) {
        bridge_set_error("cannot clone null MORK bridge space");
        return NULL;
    }
    clone = mork_space_clone(space);
    if (!clone)
        bridge_set_error("mork_space_clone returned null");
    return clone;
}

bool cetta_mork_bridge_space_meet_into(CettaMorkSpaceHandle *dst,
                                       const CettaMorkSpaceHandle *src) {
    if (!dst || !src) {
        bridge_set_error("cannot meet into null MORK bridge space");
        return false;
    }
    return bridge_status_ok("mork_space_meet_into failed: ",
                            mork_space_meet_into(dst, src));
}

CettaMorkSpaceHandle *cetta_mork_bridge_space_meet(
    const CettaMorkSpaceHandle *lhs,
    const CettaMorkSpaceHandle *rhs) {
    CettaMorkSpaceHandle *met;
    if (!lhs || !rhs) {
        bridge_set_error("cannot meet null MORK bridge space");
        return NULL;
    }
    met = mork_space_meet(lhs, rhs);
    if (!met)
        bridge_set_error("mork_space_meet returned null");
    return met;
}

bool cetta_mork_bridge_space_subtract_into(CettaMorkSpaceHandle *dst,
                                           const CettaMorkSpaceHandle *src) {
    if (!dst || !src) {
        bridge_set_error("cannot subtract into null MORK bridge space");
        return false;
    }
    return bridge_status_ok("mork_space_subtract_into failed: ",
                            mork_space_subtract_into(dst, src));
}

CettaMorkSpaceHandle *cetta_mork_bridge_space_subtract(
    const CettaMorkSpaceHandle *lhs,
    const CettaMorkSpaceHandle *rhs) {
    CettaMorkSpaceHandle *subtracted;
    if (!lhs || !rhs) {
        bridge_set_error("cannot subtract null MORK bridge space");
        return NULL;
    }
    subtracted = mork_space_subtract(lhs, rhs);
    if (!subtracted)
        bridge_set_error("mork_space_subtract returned null");
    return subtracted;
}

bool cetta_mork_bridge_space_restrict_into(CettaMorkSpaceHandle *dst,
                                           const CettaMorkSpaceHandle *src) {
    if (!dst || !src) {
        bridge_set_error("cannot restrict into null MORK bridge space");
        return false;
    }
    return bridge_status_ok("mork_space_restrict_into failed: ",
                            mork_space_restrict_into(dst, src));
}

CettaMorkSpaceHandle *cetta_mork_bridge_space_restrict(
    const CettaMorkSpaceHandle *lhs,
    const CettaMorkSpaceHandle *rhs) {
    CettaMorkSpaceHandle *restricted;
    if (!lhs || !rhs) {
        bridge_set_error("cannot restrict null MORK bridge space");
        return NULL;
    }
    restricted = mork_space_restrict(lhs, rhs);
    if (!restricted)
        bridge_set_error("mork_space_restrict returned null");
    return restricted;
}

CettaMorkCursorHandle *cetta_mork_bridge_cursor_new(
    const CettaMorkSpaceHandle *space) {
    CettaMorkCursorHandle *cursor;
    if (!space) {
        bridge_set_error("cannot create cursor from null MORK bridge space");
        return NULL;
    }
    cursor = mork_cursor_new(space);
    if (!cursor)
        bridge_set_error("mork_cursor_new returned null");
    return cursor;
}

void cetta_mork_bridge_cursor_free(CettaMorkCursorHandle *cursor) {
    if (cursor)
        mork_cursor_free(cursor);
}

bool cetta_mork_bridge_cursor_path_exists(const CettaMorkCursorHandle *cursor,
                                          bool *out_exists) {
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK cursor");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_path_exists failed: ",
                                   mork_cursor_path_exists(cursor),
                                   out_exists,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_is_val(const CettaMorkCursorHandle *cursor,
                                     bool *out_is_val) {
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK cursor");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_is_val failed: ",
                                   mork_cursor_is_val(cursor),
                                   out_is_val,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_child_count(const CettaMorkCursorHandle *cursor,
                                          uint64_t *out_child_count) {
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK cursor");
        return false;
    }
    return bridge_take_status_value("mork_cursor_child_count failed: ",
                                    mork_cursor_child_count(cursor),
                                    out_child_count,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_path_bytes(const CettaMorkCursorHandle *cursor,
                                         uint8_t **out_bytes,
                                         size_t *out_len) {
    uint32_t ignored = 0;
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK cursor");
        return false;
    }
    return bridge_take_buffer("mork_cursor_path_bytes failed: ",
                              mork_cursor_path_bytes(cursor),
                              out_bytes, out_len, &ignored,
                              bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_child_bytes(const CettaMorkCursorHandle *cursor,
                                          uint8_t **out_bytes,
                                          size_t *out_len) {
    uint32_t ignored = 0;
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK cursor");
        return false;
    }
    return bridge_take_buffer("mork_cursor_child_bytes failed: ",
                              mork_cursor_child_bytes(cursor),
                              out_bytes, out_len, &ignored,
                              bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_val_count(const CettaMorkCursorHandle *cursor,
                                        uint64_t *out_val_count) {
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK cursor");
        return false;
    }
    return bridge_take_status_value("mork_cursor_val_count failed: ",
                                    mork_cursor_val_count(cursor),
                                    out_val_count,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_depth(const CettaMorkCursorHandle *cursor,
                                    uint64_t *out_depth) {
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK cursor");
        return false;
    }
    return bridge_take_status_value("mork_cursor_depth failed: ",
                                    mork_cursor_depth(cursor),
                                    out_depth,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_reset(CettaMorkCursorHandle *cursor) {
    if (!cursor) {
        bridge_set_error("cannot reset null MORK cursor");
        return false;
    }
    return bridge_status_ok("mork_cursor_reset failed: ",
                            mork_cursor_reset(cursor));
}

bool cetta_mork_bridge_cursor_ascend(CettaMorkCursorHandle *cursor,
                                     uint64_t steps,
                                     bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot ascend null MORK cursor");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_ascend failed: ",
                                   mork_cursor_ascend(cursor, steps),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_descend_byte(CettaMorkCursorHandle *cursor,
                                           uint32_t byte,
                                           bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot descend null MORK cursor");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_descend_byte failed: ",
                                   mork_cursor_descend_byte(cursor, byte),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_descend_index(CettaMorkCursorHandle *cursor,
                                            uint64_t index,
                                            bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot descend null MORK cursor");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_descend_index failed: ",
                                   mork_cursor_descend_index(cursor, index),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_descend_first(CettaMorkCursorHandle *cursor,
                                            bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot descend null MORK cursor");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_descend_first failed: ",
                                   mork_cursor_descend_first(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_descend_last(CettaMorkCursorHandle *cursor,
                                           bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot descend null MORK cursor");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_descend_last failed: ",
                                   mork_cursor_descend_last(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_descend_until(CettaMorkCursorHandle *cursor,
                                            bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot descend null MORK cursor");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_descend_until failed: ",
                                   mork_cursor_descend_until(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_descend_until_max_bytes(CettaMorkCursorHandle *cursor,
                                                      uint64_t max_bytes,
                                                      bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot descend null MORK cursor");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_descend_until_max_bytes failed: ",
                                   mork_cursor_descend_until_max_bytes(cursor, max_bytes),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_ascend_until(CettaMorkCursorHandle *cursor,
                                           bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot ascend null MORK cursor");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_ascend_until failed: ",
                                   mork_cursor_ascend_until(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_ascend_until_branch(CettaMorkCursorHandle *cursor,
                                                  bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot ascend null MORK cursor");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_ascend_until_branch failed: ",
                                   mork_cursor_ascend_until_branch(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_next_sibling_byte(CettaMorkCursorHandle *cursor,
                                                bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot move null MORK cursor to next sibling");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_next_sibling_byte failed: ",
                                   mork_cursor_next_sibling_byte(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_prev_sibling_byte(CettaMorkCursorHandle *cursor,
                                                bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot move null MORK cursor to previous sibling");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_prev_sibling_byte failed: ",
                                   mork_cursor_prev_sibling_byte(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_next_step(CettaMorkCursorHandle *cursor,
                                        bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot step null MORK cursor");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_next_step failed: ",
                                   mork_cursor_next_step(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_next_val(CettaMorkCursorHandle *cursor,
                                       bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot advance null MORK cursor to next value");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_next_val failed: ",
                                   mork_cursor_next_val(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

CettaMorkCursorHandle *cetta_mork_bridge_cursor_fork(
    const CettaMorkCursorHandle *cursor) {
    CettaMorkCursorHandle *forked;
    if (!cursor) {
        bridge_set_error("cannot fork null MORK cursor");
        return NULL;
    }
    forked = mork_cursor_fork(cursor);
    if (!forked)
        bridge_set_error("mork_cursor_fork returned null");
    return forked;
}

CettaMorkSpaceHandle *cetta_mork_bridge_cursor_make_map(
    const CettaMorkCursorHandle *cursor) {
    CettaMorkSpaceHandle *subspace;
    if (!cursor) {
        bridge_set_error("cannot materialize structural map from null MORK cursor");
        return NULL;
    }
    subspace = mork_cursor_make_map(cursor);
    if (!subspace)
        bridge_set_error("mork_cursor_make_map returned null");
    return subspace;
}

CettaMorkSpaceHandle *cetta_mork_bridge_cursor_make_snapshot_map(
    const CettaMorkCursorHandle *cursor) {
    CettaMorkSpaceHandle *subspace;
    if (!cursor) {
        bridge_set_error("cannot materialize snapshot map from null MORK cursor");
        return NULL;
    }
    subspace = mork_cursor_make_snapshot_map(cursor);
    if (!subspace)
        bridge_set_error("mork_cursor_make_snapshot_map returned null");
    return subspace;
}

CettaMorkProductCursorHandle *cetta_mork_bridge_product_cursor_new(
    const CettaMorkSpaceHandle *const *spaces,
    size_t count) {
    CettaMorkProductCursorHandle *cursor;
    if (!spaces || count < 2) {
        bridge_set_error("cannot create MORK product cursor with fewer than two spaces");
        return NULL;
    }
    cursor = mork_product_cursor_new(spaces, count);
    if (!cursor)
        bridge_set_error("mork_product_cursor_new returned null");
    return cursor;
}

void cetta_mork_bridge_product_cursor_free(CettaMorkProductCursorHandle *cursor) {
    if (cursor)
        mork_product_cursor_free(cursor);
}

bool cetta_mork_bridge_product_cursor_path_exists(
    const CettaMorkProductCursorHandle *cursor,
    bool *out_exists) {
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK product cursor");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_path_exists failed: ",
                                   mork_product_cursor_path_exists(cursor),
                                   out_exists,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_is_val(
    const CettaMorkProductCursorHandle *cursor,
    bool *out_is_val) {
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK product cursor");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_is_val failed: ",
                                   mork_product_cursor_is_val(cursor),
                                   out_is_val,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_child_count(
    const CettaMorkProductCursorHandle *cursor,
    uint64_t *out_child_count) {
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK product cursor");
        return false;
    }
    return bridge_take_status_value("mork_product_cursor_child_count failed: ",
                                    mork_product_cursor_child_count(cursor),
                                    out_child_count,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_path_bytes(
    const CettaMorkProductCursorHandle *cursor,
    uint8_t **out_bytes,
    size_t *out_len) {
    uint32_t ignored = 0;
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK product cursor");
        return false;
    }
    return bridge_take_buffer("mork_product_cursor_path_bytes failed: ",
                              mork_product_cursor_path_bytes(cursor),
                              out_bytes, out_len, &ignored,
                              bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_child_bytes(
    const CettaMorkProductCursorHandle *cursor,
    uint8_t **out_bytes,
    size_t *out_len) {
    uint32_t ignored = 0;
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK product cursor");
        return false;
    }
    return bridge_take_buffer("mork_product_cursor_child_bytes failed: ",
                              mork_product_cursor_child_bytes(cursor),
                              out_bytes, out_len, &ignored,
                              bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_val_count(
    const CettaMorkProductCursorHandle *cursor,
    uint64_t *out_val_count) {
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK product cursor");
        return false;
    }
    return bridge_take_status_value("mork_product_cursor_val_count failed: ",
                                    mork_product_cursor_val_count(cursor),
                                    out_val_count,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_depth(
    const CettaMorkProductCursorHandle *cursor,
    uint64_t *out_depth) {
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK product cursor");
        return false;
    }
    return bridge_take_status_value("mork_product_cursor_depth failed: ",
                                    mork_product_cursor_depth(cursor),
                                    out_depth,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_factor_count(
    const CettaMorkProductCursorHandle *cursor,
    uint64_t *out_factor_count) {
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK product cursor");
        return false;
    }
    return bridge_take_status_value("mork_product_cursor_factor_count failed: ",
                                    mork_product_cursor_factor_count(cursor),
                                    out_factor_count,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_focus_factor(
    const CettaMorkProductCursorHandle *cursor,
    uint64_t *out_focus_factor) {
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK product cursor");
        return false;
    }
    return bridge_take_status_value("mork_product_cursor_focus_factor failed: ",
                                    mork_product_cursor_focus_factor(cursor),
                                    out_focus_factor,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_path_indices(
    const CettaMorkProductCursorHandle *cursor,
    uint8_t **out_bytes,
    size_t *out_len,
    uint32_t *out_count) {
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK product cursor");
        return false;
    }
    return bridge_take_buffer("mork_product_cursor_path_indices failed: ",
                              mork_product_cursor_path_indices(cursor),
                              out_bytes, out_len, out_count,
                              bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_reset(CettaMorkProductCursorHandle *cursor) {
    if (!cursor) {
        bridge_set_error("cannot reset null MORK product cursor");
        return false;
    }
    return bridge_status_ok("mork_product_cursor_reset failed: ",
                            mork_product_cursor_reset(cursor));
}

bool cetta_mork_bridge_product_cursor_ascend(CettaMorkProductCursorHandle *cursor,
                                             uint64_t steps,
                                             bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot ascend null MORK product cursor");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_ascend failed: ",
                                   mork_product_cursor_ascend(cursor, steps),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_descend_byte(CettaMorkProductCursorHandle *cursor,
                                                   uint32_t byte,
                                                   bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot descend null MORK product cursor");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_descend_byte failed: ",
                                   mork_product_cursor_descend_byte(cursor, byte),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_descend_index(CettaMorkProductCursorHandle *cursor,
                                                    uint64_t index,
                                                    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot descend null MORK product cursor");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_descend_index failed: ",
                                   mork_product_cursor_descend_index(cursor, index),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_descend_first(CettaMorkProductCursorHandle *cursor,
                                                    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot descend null MORK product cursor");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_descend_first failed: ",
                                   mork_product_cursor_descend_first(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_descend_last(CettaMorkProductCursorHandle *cursor,
                                                   bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot descend null MORK product cursor");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_descend_last failed: ",
                                   mork_product_cursor_descend_last(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_descend_until(CettaMorkProductCursorHandle *cursor,
                                                    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot descend null MORK product cursor");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_descend_until failed: ",
                                   mork_product_cursor_descend_until(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_descend_until_max_bytes(
    CettaMorkProductCursorHandle *cursor,
    uint64_t max_bytes,
    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot descend null MORK product cursor");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_descend_until_max_bytes failed: ",
                                   mork_product_cursor_descend_until_max_bytes(cursor, max_bytes),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_ascend_until(
    CettaMorkProductCursorHandle *cursor,
    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot ascend null MORK product cursor");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_ascend_until failed: ",
                                   mork_product_cursor_ascend_until(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_ascend_until_branch(
    CettaMorkProductCursorHandle *cursor,
    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot ascend null MORK product cursor");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_ascend_until_branch failed: ",
                                   mork_product_cursor_ascend_until_branch(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_next_sibling_byte(
    CettaMorkProductCursorHandle *cursor,
    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot move null MORK product cursor to next sibling");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_next_sibling_byte failed: ",
                                   mork_product_cursor_next_sibling_byte(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_prev_sibling_byte(
    CettaMorkProductCursorHandle *cursor,
    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot move null MORK product cursor to previous sibling");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_prev_sibling_byte failed: ",
                                   mork_product_cursor_prev_sibling_byte(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_next_step(
    CettaMorkProductCursorHandle *cursor,
    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot step null MORK product cursor");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_next_step failed: ",
                                   mork_product_cursor_next_step(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_next_val(
    CettaMorkProductCursorHandle *cursor,
    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot advance null MORK product cursor to next value");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_next_val failed: ",
                                   mork_product_cursor_next_val(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

CettaMorkOverlayCursorHandle *cetta_mork_bridge_overlay_cursor_new(
    const CettaMorkSpaceHandle *base,
    const CettaMorkSpaceHandle *overlay) {
    CettaMorkOverlayCursorHandle *cursor;
    if (!base || !overlay) {
        bridge_set_error("cannot create MORK overlay cursor with null spaces");
        return NULL;
    }
    cursor = mork_overlay_cursor_new(base, overlay);
    if (!cursor)
        bridge_set_error("mork_overlay_cursor_new returned null");
    return cursor;
}

void cetta_mork_bridge_overlay_cursor_free(CettaMorkOverlayCursorHandle *cursor) {
    if (cursor)
        mork_overlay_cursor_free(cursor);
}

bool cetta_mork_bridge_overlay_cursor_path_exists(
    const CettaMorkOverlayCursorHandle *cursor,
    bool *out_exists) {
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK overlay cursor");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_path_exists failed: ",
                                   mork_overlay_cursor_path_exists(cursor),
                                   out_exists,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_is_val(
    const CettaMorkOverlayCursorHandle *cursor,
    bool *out_is_val) {
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK overlay cursor");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_is_val failed: ",
                                   mork_overlay_cursor_is_val(cursor),
                                   out_is_val,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_child_count(
    const CettaMorkOverlayCursorHandle *cursor,
    uint64_t *out_child_count) {
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK overlay cursor");
        return false;
    }
    return bridge_take_status_value("mork_overlay_cursor_child_count failed: ",
                                    mork_overlay_cursor_child_count(cursor),
                                    out_child_count,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_path_bytes(
    const CettaMorkOverlayCursorHandle *cursor,
    uint8_t **out_bytes,
    size_t *out_len) {
    uint32_t ignored = 0;
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK overlay cursor");
        return false;
    }
    return bridge_take_buffer("mork_overlay_cursor_path_bytes failed: ",
                              mork_overlay_cursor_path_bytes(cursor),
                              out_bytes, out_len, &ignored,
                              bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_child_bytes(
    const CettaMorkOverlayCursorHandle *cursor,
    uint8_t **out_bytes,
    size_t *out_len) {
    uint32_t ignored = 0;
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK overlay cursor");
        return false;
    }
    return bridge_take_buffer("mork_overlay_cursor_child_bytes failed: ",
                              mork_overlay_cursor_child_bytes(cursor),
                              out_bytes, out_len, &ignored,
                              bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_depth(
    const CettaMorkOverlayCursorHandle *cursor,
    uint64_t *out_depth) {
    if (!cursor) {
        bridge_set_error("cannot inspect null MORK overlay cursor");
        return false;
    }
    return bridge_take_status_value("mork_overlay_cursor_depth failed: ",
                                    mork_overlay_cursor_depth(cursor),
                                    out_depth,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_reset(CettaMorkOverlayCursorHandle *cursor) {
    if (!cursor) {
        bridge_set_error("cannot reset null MORK overlay cursor");
        return false;
    }
    return bridge_status_ok("mork_overlay_cursor_reset failed: ",
                            mork_overlay_cursor_reset(cursor));
}

bool cetta_mork_bridge_overlay_cursor_ascend(CettaMorkOverlayCursorHandle *cursor,
                                             uint64_t steps,
                                             bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot ascend null MORK overlay cursor");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_ascend failed: ",
                                   mork_overlay_cursor_ascend(cursor, steps),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_descend_byte(CettaMorkOverlayCursorHandle *cursor,
                                                   uint32_t byte,
                                                   bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot descend null MORK overlay cursor");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_descend_byte failed: ",
                                   mork_overlay_cursor_descend_byte(cursor, byte),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_descend_index(CettaMorkOverlayCursorHandle *cursor,
                                                    uint64_t index,
                                                    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot descend null MORK overlay cursor");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_descend_index failed: ",
                                   mork_overlay_cursor_descend_index(cursor, index),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_descend_first(CettaMorkOverlayCursorHandle *cursor,
                                                    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot descend null MORK overlay cursor");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_descend_first failed: ",
                                   mork_overlay_cursor_descend_first(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_descend_last(CettaMorkOverlayCursorHandle *cursor,
                                                   bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot descend null MORK overlay cursor");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_descend_last failed: ",
                                   mork_overlay_cursor_descend_last(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_descend_until(CettaMorkOverlayCursorHandle *cursor,
                                                    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot descend null MORK overlay cursor");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_descend_until failed: ",
                                   mork_overlay_cursor_descend_until(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_descend_until_max_bytes(
    CettaMorkOverlayCursorHandle *cursor,
    uint64_t max_bytes,
    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot descend null MORK overlay cursor");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_descend_until_max_bytes failed: ",
                                   mork_overlay_cursor_descend_until_max_bytes(cursor, max_bytes),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_ascend_until(
    CettaMorkOverlayCursorHandle *cursor,
    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot ascend null MORK overlay cursor");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_ascend_until failed: ",
                                   mork_overlay_cursor_ascend_until(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_ascend_until_branch(
    CettaMorkOverlayCursorHandle *cursor,
    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot ascend null MORK overlay cursor");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_ascend_until_branch failed: ",
                                   mork_overlay_cursor_ascend_until_branch(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_next_sibling_byte(
    CettaMorkOverlayCursorHandle *cursor,
    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot move null MORK overlay cursor to next sibling");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_next_sibling_byte failed: ",
                                   mork_overlay_cursor_next_sibling_byte(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_prev_sibling_byte(
    CettaMorkOverlayCursorHandle *cursor,
    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot move null MORK overlay cursor to previous sibling");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_prev_sibling_byte failed: ",
                                   mork_overlay_cursor_prev_sibling_byte(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_next_step(
    CettaMorkOverlayCursorHandle *cursor,
    bool *out_moved) {
    if (!cursor) {
        bridge_set_error("cannot step null MORK overlay cursor");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_next_step failed: ",
                                   mork_overlay_cursor_next_step(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_space_dump_act_file(CettaMorkSpaceHandle *space,
                                           const uint8_t *path,
                                           size_t len,
                                           uint64_t *out_saved) {
    if (!space) {
        bridge_set_error("cannot dump ACT from null MORK bridge space");
        return false;
    }
    return bridge_take_status_value("mork_space_dump_act_file failed: ",
                                    mork_space_dump_act_file(space, path, len),
                                    out_saved,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_space_load_act_file(CettaMorkSpaceHandle *space,
                                           const uint8_t *path,
                                           size_t len,
                                           uint64_t *out_loaded) {
    if (!space) {
        bridge_set_error("cannot load ACT into null MORK bridge space");
        return false;
    }
    return bridge_take_status_value("mork_space_load_act_file failed: ",
                                    mork_space_load_act_file(space, path, len),
                                    out_loaded,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_space_query_candidates_text(CettaMorkSpaceHandle *space,
                                                   const char *pattern_text,
                                                   uint32_t **out_indices,
                                                   uint32_t *out_count) {
    if (!pattern_text) {
        bridge_set_error("cannot query null pattern text in MORK bridge space");
        return false;
    }
    return cetta_mork_bridge_space_query_candidates(space,
                                                    (const uint8_t *)pattern_text,
                                                    strlen(pattern_text),
                                                    out_indices,
                                                    out_count);
}

bool cetta_mork_bridge_space_compile_query_expr_text(CettaMorkSpaceHandle *space,
                                                     const char *pattern_text,
                                                     uint8_t **out_expr,
                                                     size_t *out_len) {
    uint32_t ignored = 0;
    if (!pattern_text) {
        bridge_set_error("cannot compile null pattern text in MORK bridge space");
        return false;
    }
    if (!space) {
        bridge_set_error("cannot compile query expr for null MORK bridge space");
        return false;
    }
    if (!mork_space_compile_query_expr_text) {
        bridge_set_error("mork_space_compile_query_expr_text is unavailable in the linked MORK bridge");
        return false;
    }
    return bridge_take_buffer("mork_space_compile_query_expr_text failed: ",
                              mork_space_compile_query_expr_text(space,
                                                                 (const uint8_t *)pattern_text,
                                                                 strlen(pattern_text)),
                              out_expr, out_len, &ignored, bridge_free_bytes);
}

bool cetta_mork_bridge_space_query_candidates_expr_bytes(
    CettaMorkSpaceHandle *space,
    const uint8_t *pattern_expr,
    size_t len,
    uint32_t **out_indices,
    uint32_t *out_count) {
    *out_indices = NULL;
    *out_count = 0;
    if (!space) {
        bridge_set_error("cannot query null MORK bridge space");
        return false;
    }
    if (!mork_space_query_candidates_expr_bytes) {
        bridge_set_error("mork_space_query_candidates_expr_bytes is unavailable in the linked MORK bridge");
        return false;
    }

    CettaMorkBuffer buf = mork_space_query_candidates_expr_bytes(space, pattern_expr, len);
    if (buf.code != 0) {
        if (buf.message && buf.message_len > 0)
            bridge_set_error_bytes("mork_space_query_candidates_expr_bytes failed: ", buf.message, buf.message_len);
        else
            bridge_set_error("mork_space_query_candidates_expr_bytes failed with code %d", buf.code);
        bridge_free_bytes(buf.message, buf.message_len);
        bridge_free_bytes(buf.data, buf.len);
        return false;
    }

    bridge_free_bytes(buf.message, buf.message_len);
    if (buf.len == 0 || buf.count == 0) {
        bridge_free_bytes(buf.data, buf.len);
        return true;
    }
    if (buf.len != (size_t)buf.count * 4u || (buf.len % 4u) != 0u) {
        bridge_set_error("mork_space_query_candidates_expr_bytes returned malformed packet (%zu bytes for %u indices)",
                         buf.len, buf.count);
        bridge_free_bytes(buf.data, buf.len);
        return false;
    }

    uint32_t *indices = cetta_malloc(sizeof(uint32_t) * buf.count);
    for (uint32_t i = 0; i < buf.count; i++)
        indices[i] = read_u32_be(buf.data + (size_t)i * 4u);
    bridge_free_bytes(buf.data, buf.len);
    *out_indices = indices;
    *out_count = buf.count;
    return true;
}

bool cetta_mork_bridge_space_query_candidates_prefix_expr_bytes(
    CettaMorkSpaceHandle *space,
    const uint8_t *pattern_expr,
    size_t len,
    uint32_t **out_indices,
    uint32_t *out_count) {
    *out_indices = NULL;
    *out_count = 0;
    if (!space) {
        bridge_set_error("cannot query null MORK bridge space");
        return false;
    }
    if (!mork_space_query_candidates_prefix_expr_bytes) {
        bridge_set_error("mork_space_query_candidates_prefix_expr_bytes is unavailable in the linked MORK bridge");
        return false;
    }

    CettaMorkBuffer buf = mork_space_query_candidates_prefix_expr_bytes(space, pattern_expr, len);
    if (buf.code != 0) {
        if (buf.message && buf.message_len > 0)
            bridge_set_error_bytes("mork_space_query_candidates_prefix_expr_bytes failed: ", buf.message, buf.message_len);
        else
            bridge_set_error("mork_space_query_candidates_prefix_expr_bytes failed with code %d", buf.code);
        bridge_free_bytes(buf.message, buf.message_len);
        bridge_free_bytes(buf.data, buf.len);
        return false;
    }

    bridge_free_bytes(buf.message, buf.message_len);
    if (buf.len == 0 || buf.count == 0) {
        bridge_free_bytes(buf.data, buf.len);
        return true;
    }
    if (buf.len != (size_t)buf.count * 4u || (buf.len % 4u) != 0u) {
        bridge_set_error("mork_space_query_candidates_prefix_expr_bytes returned malformed packet (%zu bytes for %u indices)",
                         buf.len, buf.count);
        bridge_free_bytes(buf.data, buf.len);
        return false;
    }

    uint32_t *indices = cetta_malloc(sizeof(uint32_t) * buf.count);
    for (uint32_t i = 0; i < buf.count; i++)
        indices[i] = read_u32_be(buf.data + (size_t)i * 4u);
    bridge_free_bytes(buf.data, buf.len);
    *out_indices = indices;
    *out_count = buf.count;
    return true;
}

bool cetta_mork_bridge_space_query_indices(CettaMorkSpaceHandle *space,
                                           const uint8_t *pattern,
                                           size_t len,
                                           uint32_t **out_indices,
                                           uint32_t *out_count) {
    *out_indices = NULL;
    *out_count = 0;
    if (!space) {
        bridge_set_error("cannot query null MORK bridge space");
        return false;
    }

    CettaMorkBuffer buf = mork_space_query_indices(space, pattern, len);
    if (buf.code != 0) {
        if (buf.message && buf.message_len > 0)
            bridge_set_error_bytes("mork_space_query_indices failed: ", buf.message, buf.message_len);
        else
            bridge_set_error("mork_space_query_indices failed with code %d", buf.code);
        bridge_free_bytes(buf.message, buf.message_len);
        bridge_free_bytes(buf.data, buf.len);
        return false;
    }

    bridge_free_bytes(buf.message, buf.message_len);
    if (buf.len == 0 || buf.count == 0) {
        bridge_free_bytes(buf.data, buf.len);
        return true;
    }
    if (buf.len != (size_t)buf.count * 4u || (buf.len % 4u) != 0u) {
        bridge_set_error("mork_space_query_indices returned malformed packet (%zu bytes for %u indices)",
                         buf.len, buf.count);
        bridge_free_bytes(buf.data, buf.len);
        return false;
    }

    uint32_t *indices = cetta_malloc(sizeof(uint32_t) * buf.count);
    for (uint32_t i = 0; i < buf.count; i++)
        indices[i] = read_u32_be(buf.data + (size_t)i * 4u);
    bridge_free_bytes(buf.data, buf.len);
    *out_indices = indices;
    *out_count = buf.count;
    return true;
}

bool cetta_mork_bridge_space_query_candidates(CettaMorkSpaceHandle *space,
                                              const uint8_t *pattern,
                                              size_t len,
                                              uint32_t **out_indices,
                                              uint32_t *out_count) {
    return cetta_mork_bridge_space_query_indices(space, pattern, len,
                                                 out_indices, out_count);
}

bool cetta_mork_bridge_space_query_bindings_text(CettaMorkSpaceHandle *space,
                                                 const char *pattern_text,
                                                 uint8_t **out_packet,
                                                 size_t *out_len,
                                                 uint32_t *out_rows) {
    if (!pattern_text) {
        bridge_set_error("cannot query null pattern text in MORK bridge space");
        return false;
    }
    return cetta_mork_bridge_space_query_bindings(space,
                                                  (const uint8_t *)pattern_text,
                                                  strlen(pattern_text),
                                                  out_packet,
                                                  out_len,
                                                  out_rows);
}

bool cetta_mork_bridge_space_query_bindings(CettaMorkSpaceHandle *space,
                                            const uint8_t *pattern,
                                            size_t len,
                                            uint8_t **out_packet,
                                            size_t *out_len,
                                            uint32_t *out_rows) {
    *out_packet = NULL;
    *out_len = 0;
    *out_rows = 0;
    if (!space) {
        bridge_set_error("cannot query null MORK bridge space");
        return false;
    }

    CettaMorkBuffer buf = mork_space_query_bindings(space, pattern, len);
    if (buf.code != 0) {
        if (buf.message && buf.message_len > 0)
            bridge_set_error_bytes("mork_space_query_bindings failed: ", buf.message, buf.message_len);
        else
            bridge_set_error("mork_space_query_bindings failed with code %d", buf.code);
        bridge_free_bytes(buf.message, buf.message_len);
        bridge_free_bytes(buf.data, buf.len);
        return false;
    }

    bridge_free_bytes(buf.message, buf.message_len);
    *out_packet = buf.data;
    *out_len = buf.len;
    *out_rows = buf.count;
    return true;
}

bool cetta_mork_bridge_space_query_bindings_query_only_v2(CettaMorkSpaceHandle *space,
                                                          const uint8_t *pattern,
                                                          size_t len,
                                                          uint8_t **out_packet,
                                                          size_t *out_len,
                                                          uint32_t *out_rows) {
    *out_packet = NULL;
    *out_len = 0;
    *out_rows = 0;
    if (!space) {
        bridge_set_error("cannot query null MORK bridge space");
        return false;
    }
    if (!mork_space_query_bindings_query_only_v2) {
        bridge_set_error("mork_space_query_bindings_query_only_v2 is unavailable in the linked MORK bridge");
        return false;
    }

    CettaMorkBuffer buf = mork_space_query_bindings_query_only_v2(space, pattern, len);
    if (buf.code != 0) {
        if (buf.message && buf.message_len > 0)
            bridge_set_error_bytes("mork_space_query_bindings_query_only_v2 failed: ",
                                   buf.message, buf.message_len);
        else
            bridge_set_error("mork_space_query_bindings_query_only_v2 failed with code %d",
                             buf.code);
        bridge_free_bytes(buf.message, buf.message_len);
        bridge_free_bytes(buf.data, buf.len);
        return false;
    }

    bridge_free_bytes(buf.message, buf.message_len);
    *out_packet = buf.data;
    *out_len = buf.len;
    *out_rows = buf.count;
    return true;
}

bool cetta_mork_bridge_space_query_bindings_multi_ref_v3(CettaMorkSpaceHandle *space,
                                                         const uint8_t *pattern,
                                                         size_t len,
                                                         uint8_t **out_packet,
                                                         size_t *out_len,
                                                         uint32_t *out_rows) {
    *out_packet = NULL;
    *out_len = 0;
    *out_rows = 0;
    if (!space) {
        bridge_set_error("cannot query null MORK bridge space");
        return false;
    }
    if (!mork_space_query_bindings_multi_ref_v3) {
        bridge_set_error("mork_space_query_bindings_multi_ref_v3 is unavailable in the linked MORK bridge");
        return false;
    }

    CettaMorkBuffer buf = mork_space_query_bindings_multi_ref_v3(space, pattern, len);
    if (buf.code != 0) {
        if (buf.message && buf.message_len > 0)
            bridge_set_error_bytes("mork_space_query_bindings_multi_ref_v3 failed: ",
                                   buf.message, buf.message_len);
        else
            bridge_set_error("mork_space_query_bindings_multi_ref_v3 failed with code %d",
                             buf.code);
        bridge_free_bytes(buf.message, buf.message_len);
        bridge_free_bytes(buf.data, buf.len);
        return false;
    }

    bridge_free_bytes(buf.message, buf.message_len);
    *out_packet = buf.data;
    *out_len = buf.len;
    *out_rows = buf.count;
    return true;
}

#else

typedef struct CettaMorkBridgeApi {
    bool attempted;
    void *handle;
    CettaMorkSpaceHandle *(*space_new)(void);
    void (*space_free)(CettaMorkSpaceHandle *space);
    CettaMorkStatus (*space_clear)(CettaMorkSpaceHandle *space);
    CettaMorkStatus (*space_add_sexpr)(CettaMorkSpaceHandle *space,
                                       const uint8_t *text,
                                       size_t len);
    CettaMorkStatus (*space_remove_sexpr)(CettaMorkSpaceHandle *space,
                                          const uint8_t *text,
                                          size_t len);
    CettaMorkStatus (*space_add_indexed_sexpr)(CettaMorkSpaceHandle *space,
                                               uint32_t atom_idx,
                                               const uint8_t *text,
                                               size_t len);
    CettaMorkStatus (*space_logical_size)(const CettaMorkSpaceHandle *space);
    CettaMorkStatus (*space_size)(const CettaMorkSpaceHandle *space);
    CettaMorkStatus (*space_step)(CettaMorkSpaceHandle *space, uint64_t steps);
    CettaMorkStatus (*space_dump_act_file)(CettaMorkSpaceHandle *space,
                                           const uint8_t *path,
                                           size_t len);
    CettaMorkStatus (*space_load_act_file)(CettaMorkSpaceHandle *space,
                                           const uint8_t *path,
                                           size_t len);
    CettaMorkBuffer (*space_dump)(CettaMorkSpaceHandle *space);
    CettaMorkStatus (*space_join_into)(CettaMorkSpaceHandle *dst,
                                       const CettaMorkSpaceHandle *src);
    CettaMorkSpaceHandle *(*space_clone)(const CettaMorkSpaceHandle *space);
    CettaMorkSpaceHandle *(*space_join)(const CettaMorkSpaceHandle *lhs,
                                        const CettaMorkSpaceHandle *rhs);
    CettaMorkStatus (*space_meet_into)(CettaMorkSpaceHandle *dst,
                                       const CettaMorkSpaceHandle *src);
    CettaMorkSpaceHandle *(*space_meet)(const CettaMorkSpaceHandle *lhs,
                                        const CettaMorkSpaceHandle *rhs);
    CettaMorkStatus (*space_subtract_into)(CettaMorkSpaceHandle *dst,
                                           const CettaMorkSpaceHandle *src);
    CettaMorkSpaceHandle *(*space_subtract)(const CettaMorkSpaceHandle *lhs,
                                            const CettaMorkSpaceHandle *rhs);
    CettaMorkStatus (*space_restrict_into)(CettaMorkSpaceHandle *dst,
                                           const CettaMorkSpaceHandle *src);
    CettaMorkSpaceHandle *(*space_restrict)(const CettaMorkSpaceHandle *lhs,
                                            const CettaMorkSpaceHandle *rhs);
    CettaMorkCursorHandle *(*cursor_new)(const CettaMorkSpaceHandle *space);
    void (*cursor_free)(CettaMorkCursorHandle *cursor);
    CettaMorkStatus (*cursor_path_exists)(const CettaMorkCursorHandle *cursor);
    CettaMorkStatus (*cursor_is_val)(const CettaMorkCursorHandle *cursor);
    CettaMorkStatus (*cursor_child_count)(const CettaMorkCursorHandle *cursor);
    CettaMorkBuffer (*cursor_path_bytes)(const CettaMorkCursorHandle *cursor);
    CettaMorkBuffer (*cursor_child_bytes)(const CettaMorkCursorHandle *cursor);
    CettaMorkStatus (*cursor_val_count)(const CettaMorkCursorHandle *cursor);
    CettaMorkStatus (*cursor_depth)(const CettaMorkCursorHandle *cursor);
    CettaMorkStatus (*cursor_reset)(CettaMorkCursorHandle *cursor);
    CettaMorkStatus (*cursor_ascend)(CettaMorkCursorHandle *cursor, uint64_t steps);
    CettaMorkStatus (*cursor_descend_byte)(CettaMorkCursorHandle *cursor, uint32_t byte);
    CettaMorkStatus (*cursor_descend_index)(CettaMorkCursorHandle *cursor, uint64_t index);
    CettaMorkStatus (*cursor_descend_first)(CettaMorkCursorHandle *cursor);
    CettaMorkStatus (*cursor_descend_last)(CettaMorkCursorHandle *cursor);
    CettaMorkStatus (*cursor_descend_until)(CettaMorkCursorHandle *cursor);
    CettaMorkStatus (*cursor_descend_until_max_bytes)(CettaMorkCursorHandle *cursor,
                                                      uint64_t max_bytes);
    CettaMorkStatus (*cursor_ascend_until)(CettaMorkCursorHandle *cursor);
    CettaMorkStatus (*cursor_ascend_until_branch)(CettaMorkCursorHandle *cursor);
    CettaMorkStatus (*cursor_next_sibling_byte)(CettaMorkCursorHandle *cursor);
    CettaMorkStatus (*cursor_prev_sibling_byte)(CettaMorkCursorHandle *cursor);
    CettaMorkStatus (*cursor_next_step)(CettaMorkCursorHandle *cursor);
    CettaMorkStatus (*cursor_next_val)(CettaMorkCursorHandle *cursor);
    CettaMorkCursorHandle *(*cursor_fork)(const CettaMorkCursorHandle *cursor);
    CettaMorkSpaceHandle *(*cursor_make_map)(const CettaMorkCursorHandle *cursor);
    CettaMorkSpaceHandle *(*cursor_make_snapshot_map)(const CettaMorkCursorHandle *cursor);
    CettaMorkProductCursorHandle *(*product_cursor_new)(
        const CettaMorkSpaceHandle *const *spaces,
        size_t count);
    void (*product_cursor_free)(CettaMorkProductCursorHandle *cursor);
    CettaMorkStatus (*product_cursor_path_exists)(
        const CettaMorkProductCursorHandle *cursor);
    CettaMorkStatus (*product_cursor_is_val)(
        const CettaMorkProductCursorHandle *cursor);
    CettaMorkStatus (*product_cursor_child_count)(
        const CettaMorkProductCursorHandle *cursor);
    CettaMorkBuffer (*product_cursor_path_bytes)(
        const CettaMorkProductCursorHandle *cursor);
    CettaMorkBuffer (*product_cursor_child_bytes)(
        const CettaMorkProductCursorHandle *cursor);
    CettaMorkStatus (*product_cursor_val_count)(
        const CettaMorkProductCursorHandle *cursor);
    CettaMorkStatus (*product_cursor_depth)(
        const CettaMorkProductCursorHandle *cursor);
    CettaMorkStatus (*product_cursor_factor_count)(
        const CettaMorkProductCursorHandle *cursor);
    CettaMorkStatus (*product_cursor_focus_factor)(
        const CettaMorkProductCursorHandle *cursor);
    CettaMorkBuffer (*product_cursor_path_indices)(
        const CettaMorkProductCursorHandle *cursor);
    CettaMorkStatus (*product_cursor_reset)(CettaMorkProductCursorHandle *cursor);
    CettaMorkStatus (*product_cursor_ascend)(CettaMorkProductCursorHandle *cursor,
                                             uint64_t steps);
    CettaMorkStatus (*product_cursor_descend_byte)(
        CettaMorkProductCursorHandle *cursor,
        uint32_t byte);
    CettaMorkStatus (*product_cursor_descend_index)(
        CettaMorkProductCursorHandle *cursor,
        uint64_t index);
    CettaMorkStatus (*product_cursor_descend_first)(CettaMorkProductCursorHandle *cursor);
    CettaMorkStatus (*product_cursor_descend_last)(CettaMorkProductCursorHandle *cursor);
    CettaMorkStatus (*product_cursor_descend_until)(CettaMorkProductCursorHandle *cursor);
    CettaMorkStatus (*product_cursor_descend_until_max_bytes)(
        CettaMorkProductCursorHandle *cursor,
        uint64_t max_bytes);
    CettaMorkStatus (*product_cursor_ascend_until)(CettaMorkProductCursorHandle *cursor);
    CettaMorkStatus (*product_cursor_ascend_until_branch)(CettaMorkProductCursorHandle *cursor);
    CettaMorkStatus (*product_cursor_next_sibling_byte)(CettaMorkProductCursorHandle *cursor);
    CettaMorkStatus (*product_cursor_prev_sibling_byte)(CettaMorkProductCursorHandle *cursor);
    CettaMorkStatus (*product_cursor_next_step)(CettaMorkProductCursorHandle *cursor);
    CettaMorkStatus (*product_cursor_next_val)(CettaMorkProductCursorHandle *cursor);
    CettaMorkOverlayCursorHandle *(*overlay_cursor_new)(
        const CettaMorkSpaceHandle *base,
        const CettaMorkSpaceHandle *overlay);
    void (*overlay_cursor_free)(CettaMorkOverlayCursorHandle *cursor);
    CettaMorkStatus (*overlay_cursor_path_exists)(
        const CettaMorkOverlayCursorHandle *cursor);
    CettaMorkStatus (*overlay_cursor_is_val)(
        const CettaMorkOverlayCursorHandle *cursor);
    CettaMorkStatus (*overlay_cursor_child_count)(
        const CettaMorkOverlayCursorHandle *cursor);
    CettaMorkBuffer (*overlay_cursor_path_bytes)(
        const CettaMorkOverlayCursorHandle *cursor);
    CettaMorkBuffer (*overlay_cursor_child_bytes)(
        const CettaMorkOverlayCursorHandle *cursor);
    CettaMorkStatus (*overlay_cursor_depth)(
        const CettaMorkOverlayCursorHandle *cursor);
    CettaMorkStatus (*overlay_cursor_reset)(CettaMorkOverlayCursorHandle *cursor);
    CettaMorkStatus (*overlay_cursor_ascend)(CettaMorkOverlayCursorHandle *cursor,
                                             uint64_t steps);
    CettaMorkStatus (*overlay_cursor_descend_byte)(
        CettaMorkOverlayCursorHandle *cursor,
        uint32_t byte);
    CettaMorkStatus (*overlay_cursor_descend_index)(
        CettaMorkOverlayCursorHandle *cursor,
        uint64_t index);
    CettaMorkStatus (*overlay_cursor_descend_first)(CettaMorkOverlayCursorHandle *cursor);
    CettaMorkStatus (*overlay_cursor_descend_last)(CettaMorkOverlayCursorHandle *cursor);
    CettaMorkStatus (*overlay_cursor_descend_until)(CettaMorkOverlayCursorHandle *cursor);
    CettaMorkStatus (*overlay_cursor_descend_until_max_bytes)(
        CettaMorkOverlayCursorHandle *cursor,
        uint64_t max_bytes);
    CettaMorkStatus (*overlay_cursor_ascend_until)(CettaMorkOverlayCursorHandle *cursor);
    CettaMorkStatus (*overlay_cursor_ascend_until_branch)(CettaMorkOverlayCursorHandle *cursor);
    CettaMorkStatus (*overlay_cursor_next_sibling_byte)(CettaMorkOverlayCursorHandle *cursor);
    CettaMorkStatus (*overlay_cursor_prev_sibling_byte)(CettaMorkOverlayCursorHandle *cursor);
    CettaMorkStatus (*overlay_cursor_next_step)(CettaMorkOverlayCursorHandle *cursor);
    CettaMorkBuffer (*space_query_indices)(CettaMorkSpaceHandle *space,
                                           const uint8_t *pattern,
                                           size_t len);
    CettaMorkBuffer (*space_compile_query_expr_text)(CettaMorkSpaceHandle *space,
                                                     const uint8_t *pattern,
                                                     size_t len);
    CettaMorkBuffer (*space_query_candidates_prefix_expr_bytes)(CettaMorkSpaceHandle *space,
                                                                const uint8_t *pattern_expr,
                                                                size_t len);
    CettaMorkBuffer (*space_query_candidates_expr_bytes)(CettaMorkSpaceHandle *space,
                                                         const uint8_t *pattern_expr,
                                                         size_t len);
    CettaMorkBuffer (*space_query_bindings)(CettaMorkSpaceHandle *space,
                                            const uint8_t *pattern,
                                            size_t len);
    CettaMorkBuffer (*space_query_bindings_query_only_v2)(CettaMorkSpaceHandle *space,
                                                          const uint8_t *pattern,
                                                          size_t len);
    CettaMorkBuffer (*space_query_bindings_multi_ref_v3)(CettaMorkSpaceHandle *space,
                                                         const uint8_t *pattern,
                                                         size_t len);
    CettaMorkProgramHandle *(*program_new)(void);
    void (*program_free)(CettaMorkProgramHandle *program);
    CettaMorkStatus (*program_clear)(CettaMorkProgramHandle *program);
    CettaMorkStatus (*program_add_sexpr)(CettaMorkProgramHandle *program,
                                         const uint8_t *text, size_t len);
    CettaMorkStatus (*program_size)(const CettaMorkProgramHandle *program);
    CettaMorkBuffer (*program_dump)(CettaMorkProgramHandle *program);
    CettaMorkContextHandle *(*context_new)(void);
    void (*context_free)(CettaMorkContextHandle *context);
    CettaMorkStatus (*context_clear)(CettaMorkContextHandle *context);
    CettaMorkStatus (*context_load_program)(CettaMorkContextHandle *context,
                                            const CettaMorkProgramHandle *program);
    CettaMorkStatus (*context_add_sexpr)(CettaMorkContextHandle *context,
                                         const uint8_t *text, size_t len);
    CettaMorkStatus (*context_remove_sexpr)(CettaMorkContextHandle *context,
                                            const uint8_t *text, size_t len);
    CettaMorkStatus (*context_size)(const CettaMorkContextHandle *context);
    CettaMorkStatus (*context_run)(CettaMorkContextHandle *context, uint64_t steps);
    CettaMorkBuffer (*context_dump)(CettaMorkContextHandle *context);
    void (*bytes_free)(uint8_t *data, size_t len);
} CettaMorkBridgeApi;

static CettaMorkBridgeApi g_mork_bridge_api = {0};
static bool bridge_load_api(void);
static void bridge_free_bytes(uint8_t *data, size_t len);

#define BRIDGE_PROGRAM_CONTEXT_AVAILABLE() bridge_load_api()
#define BRIDGE_PROGRAM_NEW() g_mork_bridge_api.program_new()
#define BRIDGE_PROGRAM_FREE(program) g_mork_bridge_api.program_free(program)
#define BRIDGE_PROGRAM_CLEAR(program) g_mork_bridge_api.program_clear(program)
#define BRIDGE_PROGRAM_ADD(program, text, len) g_mork_bridge_api.program_add_sexpr(program, text, len)
#define BRIDGE_PROGRAM_SIZE(program) g_mork_bridge_api.program_size(program)
#define BRIDGE_PROGRAM_DUMP(program) g_mork_bridge_api.program_dump(program)
#define BRIDGE_CONTEXT_NEW() g_mork_bridge_api.context_new()
#define BRIDGE_CONTEXT_FREE(context) g_mork_bridge_api.context_free(context)
#define BRIDGE_CONTEXT_CLEAR(context) g_mork_bridge_api.context_clear(context)
#define BRIDGE_CONTEXT_LOAD_PROGRAM(context, program) g_mork_bridge_api.context_load_program(context, program)
#define BRIDGE_CONTEXT_ADD(context, text, len) g_mork_bridge_api.context_add_sexpr(context, text, len)
#define BRIDGE_CONTEXT_REMOVE(context, text, len) g_mork_bridge_api.context_remove_sexpr(context, text, len)
#define BRIDGE_CONTEXT_SIZE(context) g_mork_bridge_api.context_size(context)
#define BRIDGE_CONTEXT_RUN(context, steps) g_mork_bridge_api.context_run(context, steps)
#define BRIDGE_CONTEXT_DUMP(context) g_mork_bridge_api.context_dump(context)
static bool bridge_resolve_symbol(void **slot, const char *name) {
    dlerror();
    *slot = dlsym(g_mork_bridge_api.handle, name);
    if (!*slot) {
        const char *err = dlerror();
        bridge_set_error("failed to resolve %s: %s",
                         name, err ? err : "unknown dlsym error");
        return false;
    }
    return true;
}

static void bridge_resolve_symbol_optional(void **slot, const char *name) {
    dlerror();
    *slot = dlsym(g_mork_bridge_api.handle, name);
    (void)dlerror();
}

static uint32_t bridge_append_workspace_candidates(const char **candidates,
                                                   char storage[][PATH_MAX],
                                                   uint32_t ncandidates,
                                                   uint32_t storage_cap) {
    if (storage_cap < 2)
        return ncandidates;

    char exe_path[PATH_MAX];
    ssize_t nread = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (nread <= 0)
        return ncandidates;
    exe_path[nread] = '\0';

    char *slash = strrchr(exe_path, '/');
    if (!slash)
        return ncandidates;
    *slash = '\0';

    const char *suffixes[] = {
        "/../../hyperon/MORK/target/release/libcetta_space_bridge.so",
        "/../../hyperon/MORK/target/debug/libcetta_space_bridge.so"
    };
    const uint32_t nsuffixes = (uint32_t)(sizeof(suffixes) / sizeof(suffixes[0]));
    for (uint32_t i = 0; i < nsuffixes && i < storage_cap; i++) {
        size_t exe_len = strlen(exe_path);
        size_t suffix_len = strlen(suffixes[i]);
        if (exe_len + suffix_len + 1 > PATH_MAX)
            continue;
        memcpy(storage[i], exe_path, exe_len);
        memcpy(storage[i] + exe_len, suffixes[i], suffix_len + 1);
        candidates[ncandidates++] = storage[i];
    }
    return ncandidates;
}

static bool bridge_load_api(void) {
    if (g_mork_bridge_api.attempted)
        return g_mork_bridge_api.handle != NULL;

    g_mork_bridge_api.attempted = true;

    const char *env = getenv("CETTA_MORK_SPACE_BRIDGE_LIB");
    const char *candidates[4];
    char candidate_storage[2][PATH_MAX];
    uint32_t ncandidates = 0;
    if (env && *env)
        candidates[ncandidates++] = env;
    candidates[ncandidates++] = "libcetta_space_bridge.so";
    ncandidates = bridge_append_workspace_candidates(candidates,
                                                     candidate_storage,
                                                     ncandidates,
                                                     2);

    for (uint32_t i = 0; i < ncandidates; i++) {
        g_mork_bridge_api.handle = dlopen(candidates[i], RTLD_NOW | RTLD_LOCAL);
        if (g_mork_bridge_api.handle)
            break;
    }

    if (!g_mork_bridge_api.handle) {
        const char *err = dlerror();
        if (env && *env) {
            bridge_set_error("failed to load MORK bridge from %s: %s",
                             env, err ? err : "unknown dlopen error");
        } else {
            bridge_set_error("failed to load MORK bridge; set CETTA_MORK_SPACE_BRIDGE_LIB or install libcetta_space_bridge.so");
        }
        return false;
    }

    if (!bridge_resolve_symbol((void **)&g_mork_bridge_api.space_new, "mork_space_new") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.space_free, "mork_space_free") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.space_clear, "mork_space_clear") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.space_add_sexpr, "mork_space_add_sexpr") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.space_remove_sexpr, "mork_space_remove_sexpr") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.space_add_indexed_sexpr, "mork_space_add_indexed_sexpr") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.space_size, "mork_space_size") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.space_step, "mork_space_step") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.space_dump_act_file, "mork_space_dump_act_file") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.space_load_act_file, "mork_space_load_act_file") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.space_dump, "mork_space_dump") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.space_query_indices, "mork_space_query_indices") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.space_query_bindings, "mork_space_query_bindings") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.program_new, "mork_program_new") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.program_free, "mork_program_free") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.program_clear, "mork_program_clear") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.program_add_sexpr, "mork_program_add_sexpr") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.program_size, "mork_program_size") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.program_dump, "mork_program_dump") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.context_new, "mork_context_new") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.context_free, "mork_context_free") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.context_clear, "mork_context_clear") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.context_load_program, "mork_context_load_program") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.context_add_sexpr, "mork_context_add_sexpr") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.context_remove_sexpr, "mork_context_remove_sexpr") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.context_size, "mork_context_size") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.context_run, "mork_context_run") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.context_dump, "mork_context_dump") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.bytes_free, "mork_bytes_free")) {
        dlclose(g_mork_bridge_api.handle);
        g_mork_bridge_api.handle = NULL;
        return false;
    }
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.space_logical_size,
        "mork_space_logical_size");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.space_join_into,
        "mork_space_join_into");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.space_clone,
        "mork_space_clone");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.space_join,
        "mork_space_join");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.space_meet_into,
        "mork_space_meet_into");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.space_meet,
        "mork_space_meet");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.space_subtract_into,
        "mork_space_subtract_into");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.space_subtract,
        "mork_space_subtract");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.space_restrict_into,
        "mork_space_restrict_into");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.space_restrict,
        "mork_space_restrict");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_new,
        "mork_cursor_new");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_free,
        "mork_cursor_free");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_path_exists,
        "mork_cursor_path_exists");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_is_val,
        "mork_cursor_is_val");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_child_count,
        "mork_cursor_child_count");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_path_bytes,
        "mork_cursor_path_bytes");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_child_bytes,
        "mork_cursor_child_bytes");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_val_count,
        "mork_cursor_val_count");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_depth,
        "mork_cursor_depth");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_reset,
        "mork_cursor_reset");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_ascend,
        "mork_cursor_ascend");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_descend_byte,
        "mork_cursor_descend_byte");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_descend_index,
        "mork_cursor_descend_index");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_descend_first,
        "mork_cursor_descend_first");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_descend_last,
        "mork_cursor_descend_last");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_descend_until,
        "mork_cursor_descend_until");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_descend_until_max_bytes,
        "mork_cursor_descend_until_max_bytes");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_ascend_until,
        "mork_cursor_ascend_until");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_ascend_until_branch,
        "mork_cursor_ascend_until_branch");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_next_sibling_byte,
        "mork_cursor_next_sibling_byte");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_prev_sibling_byte,
        "mork_cursor_prev_sibling_byte");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_next_step,
        "mork_cursor_next_step");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_next_val,
        "mork_cursor_next_val");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_fork,
        "mork_cursor_fork");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_make_map,
        "mork_cursor_make_map");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.cursor_make_snapshot_map,
        "mork_cursor_make_snapshot_map");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_new,
        "mork_product_cursor_new");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_free,
        "mork_product_cursor_free");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_path_exists,
        "mork_product_cursor_path_exists");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_is_val,
        "mork_product_cursor_is_val");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_child_count,
        "mork_product_cursor_child_count");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_path_bytes,
        "mork_product_cursor_path_bytes");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_child_bytes,
        "mork_product_cursor_child_bytes");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_val_count,
        "mork_product_cursor_val_count");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_depth,
        "mork_product_cursor_depth");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_factor_count,
        "mork_product_cursor_factor_count");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_focus_factor,
        "mork_product_cursor_focus_factor");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_path_indices,
        "mork_product_cursor_path_indices");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_reset,
        "mork_product_cursor_reset");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_ascend,
        "mork_product_cursor_ascend");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_descend_byte,
        "mork_product_cursor_descend_byte");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_descend_index,
        "mork_product_cursor_descend_index");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_descend_first,
        "mork_product_cursor_descend_first");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_descend_last,
        "mork_product_cursor_descend_last");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_descend_until,
        "mork_product_cursor_descend_until");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_descend_until_max_bytes,
        "mork_product_cursor_descend_until_max_bytes");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_ascend_until,
        "mork_product_cursor_ascend_until");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_ascend_until_branch,
        "mork_product_cursor_ascend_until_branch");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_next_sibling_byte,
        "mork_product_cursor_next_sibling_byte");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_prev_sibling_byte,
        "mork_product_cursor_prev_sibling_byte");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_next_step,
        "mork_product_cursor_next_step");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.product_cursor_next_val,
        "mork_product_cursor_next_val");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_new,
        "mork_overlay_cursor_new");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_free,
        "mork_overlay_cursor_free");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_path_exists,
        "mork_overlay_cursor_path_exists");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_is_val,
        "mork_overlay_cursor_is_val");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_child_count,
        "mork_overlay_cursor_child_count");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_path_bytes,
        "mork_overlay_cursor_path_bytes");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_child_bytes,
        "mork_overlay_cursor_child_bytes");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_depth,
        "mork_overlay_cursor_depth");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_reset,
        "mork_overlay_cursor_reset");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_ascend,
        "mork_overlay_cursor_ascend");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_descend_byte,
        "mork_overlay_cursor_descend_byte");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_descend_index,
        "mork_overlay_cursor_descend_index");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_descend_first,
        "mork_overlay_cursor_descend_first");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_descend_last,
        "mork_overlay_cursor_descend_last");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_descend_until,
        "mork_overlay_cursor_descend_until");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_descend_until_max_bytes,
        "mork_overlay_cursor_descend_until_max_bytes");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_ascend_until,
        "mork_overlay_cursor_ascend_until");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_ascend_until_branch,
        "mork_overlay_cursor_ascend_until_branch");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_next_sibling_byte,
        "mork_overlay_cursor_next_sibling_byte");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_prev_sibling_byte,
        "mork_overlay_cursor_prev_sibling_byte");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.overlay_cursor_next_step,
        "mork_overlay_cursor_next_step");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.space_compile_query_expr_text,
        "mork_space_compile_query_expr_text");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.space_query_candidates_prefix_expr_bytes,
        "mork_space_query_candidates_prefix_expr_bytes");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.space_query_candidates_expr_bytes,
        "mork_space_query_candidates_expr_bytes");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.space_query_bindings_query_only_v2,
        "mork_space_query_bindings_query_only_v2");
    bridge_resolve_symbol_optional(
        (void **)&g_mork_bridge_api.space_query_bindings_multi_ref_v3,
        "mork_space_query_bindings_multi_ref_v3");

    g_mork_bridge_error[0] = '\0';
    return true;
}

static void bridge_free_bytes(uint8_t *data, size_t len) {
    if (data && g_mork_bridge_api.bytes_free)
        g_mork_bridge_api.bytes_free(data, len);
}

static bool bridge_status_ok(const char *ctx, CettaMorkStatus st) {
    if (st.code == 0) {
        bridge_free_bytes(st.message, st.message_len);
        return true;
    }
    if (st.message && st.message_len > 0)
        bridge_set_error_bytes(ctx, st.message, st.message_len);
    else
        bridge_set_error("%sstatus code %d", ctx, st.code);
    bridge_free_bytes(st.message, st.message_len);
    return false;
}

bool cetta_mork_bridge_is_available(void) {
    return bridge_load_api();
}

const char *cetta_mork_bridge_last_error(void) {
    if (!g_mork_bridge_error[0])
        return "no MORK bridge error";
    return g_mork_bridge_error;
}

CettaMorkSpaceHandle *cetta_mork_bridge_space_new(void) {
    if (!bridge_load_api())
        return NULL;
    CettaMorkSpaceHandle *space = g_mork_bridge_api.space_new();
    if (!space)
        bridge_set_error("mork_space_new returned null");
    return space;
}

void cetta_mork_bridge_space_free(CettaMorkSpaceHandle *space) {
    if (!space || !bridge_load_api())
        return;
    g_mork_bridge_api.space_free(space);
}

bool cetta_mork_bridge_space_clear(CettaMorkSpaceHandle *space) {
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot clear null or unavailable MORK bridge space");
        return false;
    }
    return bridge_status_ok("mork_space_clear failed: ",
                            g_mork_bridge_api.space_clear(space));
}

bool cetta_mork_bridge_space_add_text(CettaMorkSpaceHandle *space,
                                      const char *text,
                                      uint64_t *out_added) {
    if (!text) {
        bridge_set_error("cannot add null text to MORK bridge space");
        return false;
    }
    return cetta_mork_bridge_space_add_sexpr(space,
                                             (const uint8_t *)text,
                                             strlen(text),
                                             out_added);
}

bool cetta_mork_bridge_space_add_sexpr(CettaMorkSpaceHandle *space,
                                       const uint8_t *text,
                                       size_t len,
                                       uint64_t *out_added) {
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot add to null or unavailable MORK bridge space");
        return false;
    }
    return bridge_take_status_value("mork_space_add_sexpr failed: ",
                                    g_mork_bridge_api.space_add_sexpr(space, text, len),
                                    out_added,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_space_remove_text(CettaMorkSpaceHandle *space,
                                         const char *text,
                                         uint64_t *out_removed) {
    if (!text) {
        bridge_set_error("cannot remove null text from MORK bridge space");
        return false;
    }
    return cetta_mork_bridge_space_remove_sexpr(space,
                                                (const uint8_t *)text,
                                                strlen(text),
                                                out_removed);
}

bool cetta_mork_bridge_space_remove_sexpr(CettaMorkSpaceHandle *space,
                                          const uint8_t *text,
                                          size_t len,
                                          uint64_t *out_removed) {
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot remove from null or unavailable MORK bridge space");
        return false;
    }
    return bridge_take_status_value("mork_space_remove_sexpr failed: ",
                                    g_mork_bridge_api.space_remove_sexpr(space, text, len),
                                    out_removed,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_space_add_indexed_text(CettaMorkSpaceHandle *space,
                                              uint32_t atom_idx,
                                              const char *text) {
    if (!text) {
        bridge_set_error("cannot add null indexed text to MORK bridge space");
        return false;
    }
    return cetta_mork_bridge_space_add_indexed_sexpr(space,
                                                     atom_idx,
                                                     (const uint8_t *)text,
                                                     strlen(text));
}

bool cetta_mork_bridge_space_add_indexed_sexpr(CettaMorkSpaceHandle *space,
                                               uint32_t atom_idx,
                                               const uint8_t *text,
                                               size_t len) {
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot add to null or unavailable MORK bridge space");
        return false;
    }
    return bridge_status_ok("mork_space_add_indexed_sexpr failed: ",
                            g_mork_bridge_api.space_add_indexed_sexpr(space, atom_idx,
                                                                      text, len));
}

bool cetta_mork_bridge_space_size(const CettaMorkSpaceHandle *space,
                                  uint64_t *out_size) {
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot size null or unavailable MORK bridge space");
        return false;
    }
    return bridge_take_status_value("mork_space_size failed: ",
                                    g_mork_bridge_api.space_size(space),
                                    out_size,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_space_unique_size(const CettaMorkSpaceHandle *space,
                                         uint64_t *out_unique_size) {
    return cetta_mork_bridge_space_size(space, out_unique_size);
}

bool cetta_mork_bridge_space_logical_size(const CettaMorkSpaceHandle *space,
                                          uint64_t *out_logical_size) {
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot size null or unavailable MORK bridge space");
        return false;
    }
    if (!g_mork_bridge_api.space_logical_size) {
        bridge_set_error("mork_space_logical_size is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_value("mork_space_logical_size failed: ",
                                    g_mork_bridge_api.space_logical_size(space),
                                    out_logical_size,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_space_step(CettaMorkSpaceHandle *space,
                                  uint64_t steps,
                                  uint64_t *out_performed) {
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot step null or unavailable MORK bridge space");
        return false;
    }
    return bridge_take_status_value("mork_space_step failed: ",
                                    g_mork_bridge_api.space_step(space, steps),
                                    out_performed,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_space_dump(CettaMorkSpaceHandle *space,
                                  uint8_t **out_packet,
                                  size_t *out_len,
                                  uint32_t *out_rows) {
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot dump null or unavailable MORK bridge space");
        return false;
    }
    return bridge_take_buffer("mork_space_dump failed: ",
                              g_mork_bridge_api.space_dump(space),
                              out_packet, out_len, out_rows,
                              bridge_free_bytes);
}

bool cetta_mork_bridge_space_join_into(CettaMorkSpaceHandle *dst,
                                       const CettaMorkSpaceHandle *src) {
    if (!dst || !src || !bridge_load_api()) {
        bridge_set_error("cannot join into null or unavailable MORK bridge space");
        return false;
    }
    if (!g_mork_bridge_api.space_join_into) {
        bridge_set_error("mork_space_join_into is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_status_ok("mork_space_join_into failed: ",
                            g_mork_bridge_api.space_join_into(dst, src));
}

CettaMorkSpaceHandle *cetta_mork_bridge_space_join(
    const CettaMorkSpaceHandle *lhs,
    const CettaMorkSpaceHandle *rhs) {
    CettaMorkSpaceHandle *joined;
    if (!lhs || !rhs || !bridge_load_api()) {
        bridge_set_error("cannot join null or unavailable MORK bridge space");
        return NULL;
    }
    if (!g_mork_bridge_api.space_join) {
        bridge_set_error("mork_space_join is unavailable in the loaded MORK bridge");
        return NULL;
    }
    joined = g_mork_bridge_api.space_join(lhs, rhs);
    if (!joined)
        bridge_set_error("mork_space_join returned null");
    return joined;
}

CettaMorkSpaceHandle *cetta_mork_bridge_space_clone(
    const CettaMorkSpaceHandle *space) {
    CettaMorkSpaceHandle *clone;
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot clone null or unavailable MORK bridge space");
        return NULL;
    }
    if (!g_mork_bridge_api.space_clone) {
        bridge_set_error("mork_space_clone is unavailable in the loaded MORK bridge");
        return NULL;
    }
    clone = g_mork_bridge_api.space_clone(space);
    if (!clone)
        bridge_set_error("mork_space_clone returned null");
    return clone;
}

bool cetta_mork_bridge_space_meet_into(CettaMorkSpaceHandle *dst,
                                       const CettaMorkSpaceHandle *src) {
    if (!dst || !src || !bridge_load_api()) {
        bridge_set_error("cannot meet into null or unavailable MORK bridge space");
        return false;
    }
    if (!g_mork_bridge_api.space_meet_into) {
        bridge_set_error("mork_space_meet_into is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_status_ok("mork_space_meet_into failed: ",
                            g_mork_bridge_api.space_meet_into(dst, src));
}

CettaMorkSpaceHandle *cetta_mork_bridge_space_meet(
    const CettaMorkSpaceHandle *lhs,
    const CettaMorkSpaceHandle *rhs) {
    CettaMorkSpaceHandle *met;
    if (!lhs || !rhs || !bridge_load_api()) {
        bridge_set_error("cannot meet null or unavailable MORK bridge space");
        return NULL;
    }
    if (!g_mork_bridge_api.space_meet) {
        bridge_set_error("mork_space_meet is unavailable in the loaded MORK bridge");
        return NULL;
    }
    met = g_mork_bridge_api.space_meet(lhs, rhs);
    if (!met)
        bridge_set_error("mork_space_meet returned null");
    return met;
}

bool cetta_mork_bridge_space_subtract_into(CettaMorkSpaceHandle *dst,
                                           const CettaMorkSpaceHandle *src) {
    if (!dst || !src || !bridge_load_api()) {
        bridge_set_error("cannot subtract into null or unavailable MORK bridge space");
        return false;
    }
    if (!g_mork_bridge_api.space_subtract_into) {
        bridge_set_error("mork_space_subtract_into is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_status_ok("mork_space_subtract_into failed: ",
                            g_mork_bridge_api.space_subtract_into(dst, src));
}

CettaMorkSpaceHandle *cetta_mork_bridge_space_subtract(
    const CettaMorkSpaceHandle *lhs,
    const CettaMorkSpaceHandle *rhs) {
    CettaMorkSpaceHandle *subtracted;
    if (!lhs || !rhs || !bridge_load_api()) {
        bridge_set_error("cannot subtract null or unavailable MORK bridge space");
        return NULL;
    }
    if (!g_mork_bridge_api.space_subtract) {
        bridge_set_error("mork_space_subtract is unavailable in the loaded MORK bridge");
        return NULL;
    }
    subtracted = g_mork_bridge_api.space_subtract(lhs, rhs);
    if (!subtracted)
        bridge_set_error("mork_space_subtract returned null");
    return subtracted;
}

bool cetta_mork_bridge_space_restrict_into(CettaMorkSpaceHandle *dst,
                                           const CettaMorkSpaceHandle *src) {
    if (!dst || !src || !bridge_load_api()) {
        bridge_set_error("cannot restrict into null or unavailable MORK bridge space");
        return false;
    }
    if (!g_mork_bridge_api.space_restrict_into) {
        bridge_set_error("mork_space_restrict_into is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_status_ok("mork_space_restrict_into failed: ",
                            g_mork_bridge_api.space_restrict_into(dst, src));
}

CettaMorkSpaceHandle *cetta_mork_bridge_space_restrict(
    const CettaMorkSpaceHandle *lhs,
    const CettaMorkSpaceHandle *rhs) {
    CettaMorkSpaceHandle *restricted;
    if (!lhs || !rhs || !bridge_load_api()) {
        bridge_set_error("cannot restrict null or unavailable MORK bridge space");
        return NULL;
    }
    if (!g_mork_bridge_api.space_restrict) {
        bridge_set_error("mork_space_restrict is unavailable in the loaded MORK bridge");
        return NULL;
    }
    restricted = g_mork_bridge_api.space_restrict(lhs, rhs);
    if (!restricted)
        bridge_set_error("mork_space_restrict returned null");
    return restricted;
}

CettaMorkCursorHandle *cetta_mork_bridge_cursor_new(
    const CettaMorkSpaceHandle *space) {
    CettaMorkCursorHandle *cursor;
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot create cursor from null or unavailable MORK bridge space");
        return NULL;
    }
    if (!g_mork_bridge_api.cursor_new) {
        bridge_set_error("mork_cursor_new is unavailable in the loaded MORK bridge");
        return NULL;
    }
    cursor = g_mork_bridge_api.cursor_new(space);
    if (!cursor)
        bridge_set_error("mork_cursor_new returned null");
    return cursor;
}

void cetta_mork_bridge_cursor_free(CettaMorkCursorHandle *cursor) {
    if (!cursor || !bridge_load_api() || !g_mork_bridge_api.cursor_free)
        return;
    g_mork_bridge_api.cursor_free(cursor);
}

bool cetta_mork_bridge_cursor_path_exists(const CettaMorkCursorHandle *cursor,
                                          bool *out_exists) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK cursor");
        return false;
    }
    if (!g_mork_bridge_api.cursor_path_exists) {
        bridge_set_error("mork_cursor_path_exists is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_path_exists failed: ",
                                   g_mork_bridge_api.cursor_path_exists(cursor),
                                   out_exists,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_is_val(const CettaMorkCursorHandle *cursor,
                                     bool *out_is_val) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK cursor");
        return false;
    }
    if (!g_mork_bridge_api.cursor_is_val) {
        bridge_set_error("mork_cursor_is_val is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_is_val failed: ",
                                   g_mork_bridge_api.cursor_is_val(cursor),
                                   out_is_val,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_child_count(const CettaMorkCursorHandle *cursor,
                                          uint64_t *out_child_count) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK cursor");
        return false;
    }
    if (!g_mork_bridge_api.cursor_child_count) {
        bridge_set_error("mork_cursor_child_count is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_value("mork_cursor_child_count failed: ",
                                    g_mork_bridge_api.cursor_child_count(cursor),
                                    out_child_count,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_path_bytes(const CettaMorkCursorHandle *cursor,
                                         uint8_t **out_bytes,
                                         size_t *out_len) {
    uint32_t ignored = 0;
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK cursor");
        return false;
    }
    if (!g_mork_bridge_api.cursor_path_bytes) {
        bridge_set_error("mork_cursor_path_bytes is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_buffer("mork_cursor_path_bytes failed: ",
                              g_mork_bridge_api.cursor_path_bytes(cursor),
                              out_bytes, out_len, &ignored,
                              bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_child_bytes(const CettaMorkCursorHandle *cursor,
                                          uint8_t **out_bytes,
                                          size_t *out_len) {
    uint32_t ignored = 0;
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK cursor");
        return false;
    }
    if (!g_mork_bridge_api.cursor_child_bytes) {
        bridge_set_error("mork_cursor_child_bytes is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_buffer("mork_cursor_child_bytes failed: ",
                              g_mork_bridge_api.cursor_child_bytes(cursor),
                              out_bytes, out_len, &ignored,
                              bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_val_count(const CettaMorkCursorHandle *cursor,
                                        uint64_t *out_val_count) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK cursor");
        return false;
    }
    if (!g_mork_bridge_api.cursor_val_count) {
        bridge_set_error("mork_cursor_val_count is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_value("mork_cursor_val_count failed: ",
                                    g_mork_bridge_api.cursor_val_count(cursor),
                                    out_val_count,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_depth(const CettaMorkCursorHandle *cursor,
                                    uint64_t *out_depth) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK cursor");
        return false;
    }
    if (!g_mork_bridge_api.cursor_depth) {
        bridge_set_error("mork_cursor_depth is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_value("mork_cursor_depth failed: ",
                                    g_mork_bridge_api.cursor_depth(cursor),
                                    out_depth,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_reset(CettaMorkCursorHandle *cursor) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot reset null or unavailable MORK cursor");
        return false;
    }
    if (!g_mork_bridge_api.cursor_reset) {
        bridge_set_error("mork_cursor_reset is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_status_ok("mork_cursor_reset failed: ",
                            g_mork_bridge_api.cursor_reset(cursor));
}

bool cetta_mork_bridge_cursor_ascend(CettaMorkCursorHandle *cursor,
                                     uint64_t steps,
                                     bool *out_moved) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot ascend null or unavailable MORK cursor");
        return false;
    }
    if (!g_mork_bridge_api.cursor_ascend) {
        bridge_set_error("mork_cursor_ascend is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_ascend failed: ",
                                   g_mork_bridge_api.cursor_ascend(cursor, steps),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_descend_byte(CettaMorkCursorHandle *cursor,
                                           uint32_t byte,
                                           bool *out_moved) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot descend null or unavailable MORK cursor");
        return false;
    }
    if (!g_mork_bridge_api.cursor_descend_byte) {
        bridge_set_error("mork_cursor_descend_byte is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_descend_byte failed: ",
                                   g_mork_bridge_api.cursor_descend_byte(cursor, byte),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_descend_index(CettaMorkCursorHandle *cursor,
                                            uint64_t index,
                                            bool *out_moved) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot descend null or unavailable MORK cursor");
        return false;
    }
    if (!g_mork_bridge_api.cursor_descend_index) {
        bridge_set_error("mork_cursor_descend_index is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_descend_index failed: ",
                                   g_mork_bridge_api.cursor_descend_index(cursor, index),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_descend_first(CettaMorkCursorHandle *cursor,
                                            bool *out_moved) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot descend null or unavailable MORK cursor");
        return false;
    }
    if (!g_mork_bridge_api.cursor_descend_first) {
        bridge_set_error("mork_cursor_descend_first is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_descend_first failed: ",
                                   g_mork_bridge_api.cursor_descend_first(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_cursor_descend_until(CettaMorkCursorHandle *cursor,
                                            bool *out_moved) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot descend null or unavailable MORK cursor");
        return false;
    }
    if (!g_mork_bridge_api.cursor_descend_until) {
        bridge_set_error("mork_cursor_descend_until is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_cursor_descend_until failed: ",
                                   g_mork_bridge_api.cursor_descend_until(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

CettaMorkCursorHandle *cetta_mork_bridge_cursor_fork(
    const CettaMorkCursorHandle *cursor) {
    CettaMorkCursorHandle *forked;
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot fork null or unavailable MORK cursor");
        return NULL;
    }
    if (!g_mork_bridge_api.cursor_fork) {
        bridge_set_error("mork_cursor_fork is unavailable in the loaded MORK bridge");
        return NULL;
    }
    forked = g_mork_bridge_api.cursor_fork(cursor);
    if (!forked)
        bridge_set_error("mork_cursor_fork returned null");
    return forked;
}

CettaMorkSpaceHandle *cetta_mork_bridge_cursor_make_map(
    const CettaMorkCursorHandle *cursor) {
    CettaMorkSpaceHandle *subspace;
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot materialize structural map from null or unavailable MORK cursor");
        return NULL;
    }
    if (!g_mork_bridge_api.cursor_make_map) {
        bridge_set_error("mork_cursor_make_map is unavailable in the loaded MORK bridge");
        return NULL;
    }
    subspace = g_mork_bridge_api.cursor_make_map(cursor);
    if (!subspace)
        bridge_set_error("mork_cursor_make_map returned null");
    return subspace;
}

CettaMorkSpaceHandle *cetta_mork_bridge_cursor_make_snapshot_map(
    const CettaMorkCursorHandle *cursor) {
    CettaMorkSpaceHandle *subspace;
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot materialize snapshot map from null or unavailable MORK cursor");
        return NULL;
    }
    if (!g_mork_bridge_api.cursor_make_snapshot_map) {
        bridge_set_error("mork_cursor_make_snapshot_map is unavailable in the loaded MORK bridge");
        return NULL;
    }
    subspace = g_mork_bridge_api.cursor_make_snapshot_map(cursor);
    if (!subspace)
        bridge_set_error("mork_cursor_make_snapshot_map returned null");
    return subspace;
}

CettaMorkProductCursorHandle *cetta_mork_bridge_product_cursor_new(
    const CettaMorkSpaceHandle *const *spaces,
    size_t count) {
    CettaMorkProductCursorHandle *cursor;
    if (!spaces || count < 2 || !bridge_load_api()) {
        bridge_set_error("cannot create MORK product cursor with fewer than two spaces or unavailable bridge");
        return NULL;
    }
    if (!g_mork_bridge_api.product_cursor_new) {
        bridge_set_error("mork_product_cursor_new is unavailable in the loaded MORK bridge");
        return NULL;
    }
    cursor = g_mork_bridge_api.product_cursor_new(spaces, count);
    if (!cursor)
        bridge_set_error("mork_product_cursor_new returned null");
    return cursor;
}

void cetta_mork_bridge_product_cursor_free(CettaMorkProductCursorHandle *cursor) {
    if (!cursor || !bridge_load_api() || !g_mork_bridge_api.product_cursor_free)
        return;
    g_mork_bridge_api.product_cursor_free(cursor);
}

bool cetta_mork_bridge_product_cursor_path_exists(
    const CettaMorkProductCursorHandle *cursor,
    bool *out_exists) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK product cursor");
        return false;
    }
    if (!g_mork_bridge_api.product_cursor_path_exists) {
        bridge_set_error("mork_product_cursor_path_exists is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_path_exists failed: ",
                                   g_mork_bridge_api.product_cursor_path_exists(cursor),
                                   out_exists,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_is_val(
    const CettaMorkProductCursorHandle *cursor,
    bool *out_is_val) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK product cursor");
        return false;
    }
    if (!g_mork_bridge_api.product_cursor_is_val) {
        bridge_set_error("mork_product_cursor_is_val is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_is_val failed: ",
                                   g_mork_bridge_api.product_cursor_is_val(cursor),
                                   out_is_val,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_child_count(
    const CettaMorkProductCursorHandle *cursor,
    uint64_t *out_child_count) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK product cursor");
        return false;
    }
    if (!g_mork_bridge_api.product_cursor_child_count) {
        bridge_set_error("mork_product_cursor_child_count is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_value("mork_product_cursor_child_count failed: ",
                                    g_mork_bridge_api.product_cursor_child_count(cursor),
                                    out_child_count,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_path_bytes(
    const CettaMorkProductCursorHandle *cursor,
    uint8_t **out_bytes,
    size_t *out_len) {
    uint32_t ignored = 0;
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK product cursor");
        return false;
    }
    if (!g_mork_bridge_api.product_cursor_path_bytes) {
        bridge_set_error("mork_product_cursor_path_bytes is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_buffer("mork_product_cursor_path_bytes failed: ",
                              g_mork_bridge_api.product_cursor_path_bytes(cursor),
                              out_bytes, out_len, &ignored,
                              bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_child_bytes(
    const CettaMorkProductCursorHandle *cursor,
    uint8_t **out_bytes,
    size_t *out_len) {
    uint32_t ignored = 0;
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK product cursor");
        return false;
    }
    if (!g_mork_bridge_api.product_cursor_child_bytes) {
        bridge_set_error("mork_product_cursor_child_bytes is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_buffer("mork_product_cursor_child_bytes failed: ",
                              g_mork_bridge_api.product_cursor_child_bytes(cursor),
                              out_bytes, out_len, &ignored,
                              bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_val_count(
    const CettaMorkProductCursorHandle *cursor,
    uint64_t *out_val_count) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK product cursor");
        return false;
    }
    if (!g_mork_bridge_api.product_cursor_val_count) {
        bridge_set_error("mork_product_cursor_val_count is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_value("mork_product_cursor_val_count failed: ",
                                    g_mork_bridge_api.product_cursor_val_count(cursor),
                                    out_val_count,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_depth(
    const CettaMorkProductCursorHandle *cursor,
    uint64_t *out_depth) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK product cursor");
        return false;
    }
    if (!g_mork_bridge_api.product_cursor_depth) {
        bridge_set_error("mork_product_cursor_depth is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_value("mork_product_cursor_depth failed: ",
                                    g_mork_bridge_api.product_cursor_depth(cursor),
                                    out_depth,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_factor_count(
    const CettaMorkProductCursorHandle *cursor,
    uint64_t *out_factor_count) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK product cursor");
        return false;
    }
    if (!g_mork_bridge_api.product_cursor_factor_count) {
        bridge_set_error("mork_product_cursor_factor_count is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_value("mork_product_cursor_factor_count failed: ",
                                    g_mork_bridge_api.product_cursor_factor_count(cursor),
                                    out_factor_count,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_focus_factor(
    const CettaMorkProductCursorHandle *cursor,
    uint64_t *out_focus_factor) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK product cursor");
        return false;
    }
    if (!g_mork_bridge_api.product_cursor_focus_factor) {
        bridge_set_error("mork_product_cursor_focus_factor is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_value("mork_product_cursor_focus_factor failed: ",
                                    g_mork_bridge_api.product_cursor_focus_factor(cursor),
                                    out_focus_factor,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_path_indices(
    const CettaMorkProductCursorHandle *cursor,
    uint8_t **out_bytes,
    size_t *out_len,
    uint32_t *out_count) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK product cursor");
        return false;
    }
    if (!g_mork_bridge_api.product_cursor_path_indices) {
        bridge_set_error("mork_product_cursor_path_indices is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_buffer("mork_product_cursor_path_indices failed: ",
                              g_mork_bridge_api.product_cursor_path_indices(cursor),
                              out_bytes, out_len, out_count,
                              bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_reset(CettaMorkProductCursorHandle *cursor) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot reset null or unavailable MORK product cursor");
        return false;
    }
    if (!g_mork_bridge_api.product_cursor_reset) {
        bridge_set_error("mork_product_cursor_reset is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_status_ok("mork_product_cursor_reset failed: ",
                            g_mork_bridge_api.product_cursor_reset(cursor));
}

bool cetta_mork_bridge_product_cursor_ascend(CettaMorkProductCursorHandle *cursor,
                                             uint64_t steps,
                                             bool *out_moved) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot ascend null or unavailable MORK product cursor");
        return false;
    }
    if (!g_mork_bridge_api.product_cursor_ascend) {
        bridge_set_error("mork_product_cursor_ascend is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_ascend failed: ",
                                   g_mork_bridge_api.product_cursor_ascend(cursor, steps),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_descend_byte(CettaMorkProductCursorHandle *cursor,
                                                   uint32_t byte,
                                                   bool *out_moved) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot descend null or unavailable MORK product cursor");
        return false;
    }
    if (!g_mork_bridge_api.product_cursor_descend_byte) {
        bridge_set_error("mork_product_cursor_descend_byte is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_descend_byte failed: ",
                                   g_mork_bridge_api.product_cursor_descend_byte(cursor, byte),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_descend_index(CettaMorkProductCursorHandle *cursor,
                                                    uint64_t index,
                                                    bool *out_moved) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot descend null or unavailable MORK product cursor");
        return false;
    }
    if (!g_mork_bridge_api.product_cursor_descend_index) {
        bridge_set_error("mork_product_cursor_descend_index is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_descend_index failed: ",
                                   g_mork_bridge_api.product_cursor_descend_index(cursor, index),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_descend_first(CettaMorkProductCursorHandle *cursor,
                                                    bool *out_moved) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot descend null or unavailable MORK product cursor");
        return false;
    }
    if (!g_mork_bridge_api.product_cursor_descend_first) {
        bridge_set_error("mork_product_cursor_descend_first is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_descend_first failed: ",
                                   g_mork_bridge_api.product_cursor_descend_first(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_product_cursor_descend_until(CettaMorkProductCursorHandle *cursor,
                                                    bool *out_moved) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot descend null or unavailable MORK product cursor");
        return false;
    }
    if (!g_mork_bridge_api.product_cursor_descend_until) {
        bridge_set_error("mork_product_cursor_descend_until is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_product_cursor_descend_until failed: ",
                                   g_mork_bridge_api.product_cursor_descend_until(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

CettaMorkOverlayCursorHandle *cetta_mork_bridge_overlay_cursor_new(
    const CettaMorkSpaceHandle *base,
    const CettaMorkSpaceHandle *overlay) {
    CettaMorkOverlayCursorHandle *cursor;
    if (!base || !overlay || !bridge_load_api()) {
        bridge_set_error("cannot create MORK overlay cursor with null spaces or unavailable bridge");
        return NULL;
    }
    if (!g_mork_bridge_api.overlay_cursor_new) {
        bridge_set_error("mork_overlay_cursor_new is unavailable in the loaded MORK bridge");
        return NULL;
    }
    cursor = g_mork_bridge_api.overlay_cursor_new(base, overlay);
    if (!cursor)
        bridge_set_error("mork_overlay_cursor_new returned null");
    return cursor;
}

void cetta_mork_bridge_overlay_cursor_free(CettaMorkOverlayCursorHandle *cursor) {
    if (!cursor || !bridge_load_api() || !g_mork_bridge_api.overlay_cursor_free)
        return;
    g_mork_bridge_api.overlay_cursor_free(cursor);
}

bool cetta_mork_bridge_overlay_cursor_path_exists(
    const CettaMorkOverlayCursorHandle *cursor,
    bool *out_exists) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK overlay cursor");
        return false;
    }
    if (!g_mork_bridge_api.overlay_cursor_path_exists) {
        bridge_set_error("mork_overlay_cursor_path_exists is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_path_exists failed: ",
                                   g_mork_bridge_api.overlay_cursor_path_exists(cursor),
                                   out_exists,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_is_val(
    const CettaMorkOverlayCursorHandle *cursor,
    bool *out_is_val) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK overlay cursor");
        return false;
    }
    if (!g_mork_bridge_api.overlay_cursor_is_val) {
        bridge_set_error("mork_overlay_cursor_is_val is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_is_val failed: ",
                                   g_mork_bridge_api.overlay_cursor_is_val(cursor),
                                   out_is_val,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_child_count(
    const CettaMorkOverlayCursorHandle *cursor,
    uint64_t *out_child_count) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK overlay cursor");
        return false;
    }
    if (!g_mork_bridge_api.overlay_cursor_child_count) {
        bridge_set_error("mork_overlay_cursor_child_count is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_value("mork_overlay_cursor_child_count failed: ",
                                    g_mork_bridge_api.overlay_cursor_child_count(cursor),
                                    out_child_count,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_path_bytes(
    const CettaMorkOverlayCursorHandle *cursor,
    uint8_t **out_bytes,
    size_t *out_len) {
    uint32_t ignored = 0;
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK overlay cursor");
        return false;
    }
    if (!g_mork_bridge_api.overlay_cursor_path_bytes) {
        bridge_set_error("mork_overlay_cursor_path_bytes is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_buffer("mork_overlay_cursor_path_bytes failed: ",
                              g_mork_bridge_api.overlay_cursor_path_bytes(cursor),
                              out_bytes, out_len, &ignored,
                              bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_child_bytes(
    const CettaMorkOverlayCursorHandle *cursor,
    uint8_t **out_bytes,
    size_t *out_len) {
    uint32_t ignored = 0;
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK overlay cursor");
        return false;
    }
    if (!g_mork_bridge_api.overlay_cursor_child_bytes) {
        bridge_set_error("mork_overlay_cursor_child_bytes is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_buffer("mork_overlay_cursor_child_bytes failed: ",
                              g_mork_bridge_api.overlay_cursor_child_bytes(cursor),
                              out_bytes, out_len, &ignored,
                              bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_depth(
    const CettaMorkOverlayCursorHandle *cursor,
    uint64_t *out_depth) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot inspect null or unavailable MORK overlay cursor");
        return false;
    }
    if (!g_mork_bridge_api.overlay_cursor_depth) {
        bridge_set_error("mork_overlay_cursor_depth is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_value("mork_overlay_cursor_depth failed: ",
                                    g_mork_bridge_api.overlay_cursor_depth(cursor),
                                    out_depth,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_reset(CettaMorkOverlayCursorHandle *cursor) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot reset null or unavailable MORK overlay cursor");
        return false;
    }
    if (!g_mork_bridge_api.overlay_cursor_reset) {
        bridge_set_error("mork_overlay_cursor_reset is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_status_ok("mork_overlay_cursor_reset failed: ",
                            g_mork_bridge_api.overlay_cursor_reset(cursor));
}

bool cetta_mork_bridge_overlay_cursor_ascend(CettaMorkOverlayCursorHandle *cursor,
                                             uint64_t steps,
                                             bool *out_moved) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot ascend null or unavailable MORK overlay cursor");
        return false;
    }
    if (!g_mork_bridge_api.overlay_cursor_ascend) {
        bridge_set_error("mork_overlay_cursor_ascend is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_ascend failed: ",
                                   g_mork_bridge_api.overlay_cursor_ascend(cursor, steps),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_descend_byte(CettaMorkOverlayCursorHandle *cursor,
                                                   uint32_t byte,
                                                   bool *out_moved) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot descend null or unavailable MORK overlay cursor");
        return false;
    }
    if (!g_mork_bridge_api.overlay_cursor_descend_byte) {
        bridge_set_error("mork_overlay_cursor_descend_byte is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_descend_byte failed: ",
                                   g_mork_bridge_api.overlay_cursor_descend_byte(cursor, byte),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_descend_index(CettaMorkOverlayCursorHandle *cursor,
                                                    uint64_t index,
                                                    bool *out_moved) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot descend null or unavailable MORK overlay cursor");
        return false;
    }
    if (!g_mork_bridge_api.overlay_cursor_descend_index) {
        bridge_set_error("mork_overlay_cursor_descend_index is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_descend_index failed: ",
                                   g_mork_bridge_api.overlay_cursor_descend_index(cursor, index),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_descend_first(CettaMorkOverlayCursorHandle *cursor,
                                                    bool *out_moved) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot descend null or unavailable MORK overlay cursor");
        return false;
    }
    if (!g_mork_bridge_api.overlay_cursor_descend_first) {
        bridge_set_error("mork_overlay_cursor_descend_first is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_descend_first failed: ",
                                   g_mork_bridge_api.overlay_cursor_descend_first(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_overlay_cursor_descend_until(CettaMorkOverlayCursorHandle *cursor,
                                                    bool *out_moved) {
    if (!cursor || !bridge_load_api()) {
        bridge_set_error("cannot descend null or unavailable MORK overlay cursor");
        return false;
    }
    if (!g_mork_bridge_api.overlay_cursor_descend_until) {
        bridge_set_error("mork_overlay_cursor_descend_until is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_status_bool("mork_overlay_cursor_descend_until failed: ",
                                   g_mork_bridge_api.overlay_cursor_descend_until(cursor),
                                   out_moved,
                                   bridge_free_bytes);
}

bool cetta_mork_bridge_space_dump_act_file(CettaMorkSpaceHandle *space,
                                           const uint8_t *path,
                                           size_t len,
                                           uint64_t *out_saved) {
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot dump ACT from null or unavailable MORK bridge space");
        return false;
    }
    return bridge_take_status_value("mork_space_dump_act_file failed: ",
                                    g_mork_bridge_api.space_dump_act_file(space, path, len),
                                    out_saved,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_space_load_act_file(CettaMorkSpaceHandle *space,
                                           const uint8_t *path,
                                           size_t len,
                                           uint64_t *out_loaded) {
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot load ACT into null or unavailable MORK bridge space");
        return false;
    }
    return bridge_take_status_value("mork_space_load_act_file failed: ",
                                    g_mork_bridge_api.space_load_act_file(space, path, len),
                                    out_loaded,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_space_query_candidates_text(CettaMorkSpaceHandle *space,
                                                   const char *pattern_text,
                                                   uint32_t **out_indices,
                                                   uint32_t *out_count) {
    if (!pattern_text) {
        bridge_set_error("cannot query null pattern text in MORK bridge space");
        return false;
    }
    return cetta_mork_bridge_space_query_candidates(space,
                                                    (const uint8_t *)pattern_text,
                                                    strlen(pattern_text),
                                                    out_indices,
                                                    out_count);
}

bool cetta_mork_bridge_space_compile_query_expr_text(CettaMorkSpaceHandle *space,
                                                     const char *pattern_text,
                                                     uint8_t **out_expr,
                                                     size_t *out_len) {
    uint32_t ignored = 0;
    if (!pattern_text) {
        bridge_set_error("cannot compile null pattern text in MORK bridge space");
        return false;
    }
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot compile query expr for null or unavailable MORK bridge space");
        return false;
    }
    if (!g_mork_bridge_api.space_compile_query_expr_text) {
        bridge_set_error("mork_space_compile_query_expr_text is unavailable in the loaded MORK bridge");
        return false;
    }
    return bridge_take_buffer("mork_space_compile_query_expr_text failed: ",
                              g_mork_bridge_api.space_compile_query_expr_text(
                                  space,
                                  (const uint8_t *)pattern_text,
                                  strlen(pattern_text)),
                              out_expr, out_len, &ignored, bridge_free_bytes);
}

bool cetta_mork_bridge_space_query_candidates_expr_bytes(
    CettaMorkSpaceHandle *space,
    const uint8_t *pattern_expr,
    size_t len,
    uint32_t **out_indices,
    uint32_t *out_count) {
    *out_indices = NULL;
    *out_count = 0;
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot query null or unavailable MORK bridge space");
        return false;
    }
    if (!g_mork_bridge_api.space_query_candidates_expr_bytes) {
        bridge_set_error("mork_space_query_candidates_expr_bytes is unavailable in the loaded MORK bridge");
        return false;
    }

    CettaMorkBuffer buf =
        g_mork_bridge_api.space_query_candidates_expr_bytes(space, pattern_expr, len);
    if (buf.code != 0) {
        if (buf.message && buf.message_len > 0)
            bridge_set_error_bytes("mork_space_query_candidates_expr_bytes failed: ", buf.message, buf.message_len);
        else
            bridge_set_error("mork_space_query_candidates_expr_bytes failed with code %d", buf.code);
        bridge_free_bytes(buf.message, buf.message_len);
        bridge_free_bytes(buf.data, buf.len);
        return false;
    }

    bridge_free_bytes(buf.message, buf.message_len);
    if (buf.len == 0 || buf.count == 0) {
        bridge_free_bytes(buf.data, buf.len);
        return true;
    }
    if (buf.len != (size_t)buf.count * 4u || (buf.len % 4u) != 0u) {
        bridge_set_error("mork_space_query_candidates_expr_bytes returned malformed packet (%zu bytes for %u indices)",
                         buf.len, buf.count);
        bridge_free_bytes(buf.data, buf.len);
        return false;
    }

    uint32_t *indices = cetta_malloc(sizeof(uint32_t) * buf.count);
    for (uint32_t i = 0; i < buf.count; i++)
        indices[i] = read_u32_be(buf.data + (size_t)i * 4u);
    bridge_free_bytes(buf.data, buf.len);
    *out_indices = indices;
    *out_count = buf.count;
    return true;
}

bool cetta_mork_bridge_space_query_candidates_prefix_expr_bytes(
    CettaMorkSpaceHandle *space,
    const uint8_t *pattern_expr,
    size_t len,
    uint32_t **out_indices,
    uint32_t *out_count) {
    *out_indices = NULL;
    *out_count = 0;
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot query null or unavailable MORK bridge space");
        return false;
    }
    if (!g_mork_bridge_api.space_query_candidates_prefix_expr_bytes) {
        bridge_set_error("mork_space_query_candidates_prefix_expr_bytes is unavailable in the loaded MORK bridge");
        return false;
    }

    CettaMorkBuffer buf =
        g_mork_bridge_api.space_query_candidates_prefix_expr_bytes(space, pattern_expr, len);
    if (buf.code != 0) {
        if (buf.message && buf.message_len > 0)
            bridge_set_error_bytes("mork_space_query_candidates_prefix_expr_bytes failed: ", buf.message, buf.message_len);
        else
            bridge_set_error("mork_space_query_candidates_prefix_expr_bytes failed with code %d", buf.code);
        bridge_free_bytes(buf.message, buf.message_len);
        bridge_free_bytes(buf.data, buf.len);
        return false;
    }

    bridge_free_bytes(buf.message, buf.message_len);
    if (buf.len == 0 || buf.count == 0) {
        bridge_free_bytes(buf.data, buf.len);
        return true;
    }
    if (buf.len != (size_t)buf.count * 4u || (buf.len % 4u) != 0u) {
        bridge_set_error("mork_space_query_candidates_prefix_expr_bytes returned malformed packet (%zu bytes for %u indices)",
                         buf.len, buf.count);
        bridge_free_bytes(buf.data, buf.len);
        return false;
    }

    uint32_t *indices = cetta_malloc(sizeof(uint32_t) * buf.count);
    for (uint32_t i = 0; i < buf.count; i++)
        indices[i] = read_u32_be(buf.data + (size_t)i * 4u);
    bridge_free_bytes(buf.data, buf.len);
    *out_indices = indices;
    *out_count = buf.count;
    return true;
}

bool cetta_mork_bridge_space_query_indices(CettaMorkSpaceHandle *space,
                                           const uint8_t *pattern,
                                           size_t len,
                                           uint32_t **out_indices,
                                           uint32_t *out_count) {
    *out_indices = NULL;
    *out_count = 0;
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot query null or unavailable MORK bridge space");
        return false;
    }

    CettaMorkBuffer buf = g_mork_bridge_api.space_query_indices(space, pattern, len);
    if (buf.code != 0) {
        if (buf.message && buf.message_len > 0)
            bridge_set_error_bytes("mork_space_query_indices failed: ", buf.message, buf.message_len);
        else
            bridge_set_error("mork_space_query_indices failed with code %d", buf.code);
        bridge_free_bytes(buf.message, buf.message_len);
        bridge_free_bytes(buf.data, buf.len);
        return false;
    }

    bridge_free_bytes(buf.message, buf.message_len);
    if (buf.len == 0 || buf.count == 0) {
        bridge_free_bytes(buf.data, buf.len);
        return true;
    }
    if (buf.len != (size_t)buf.count * 4u || (buf.len % 4u) != 0u) {
        bridge_set_error("mork_space_query_indices returned malformed packet (%zu bytes for %u indices)",
                         buf.len, buf.count);
        bridge_free_bytes(buf.data, buf.len);
        return false;
    }

    uint32_t *indices = cetta_malloc(sizeof(uint32_t) * buf.count);
    for (uint32_t i = 0; i < buf.count; i++)
        indices[i] = read_u32_be(buf.data + (size_t)i * 4u);
    bridge_free_bytes(buf.data, buf.len);
    *out_indices = indices;
    *out_count = buf.count;
    return true;
}

bool cetta_mork_bridge_space_query_candidates(CettaMorkSpaceHandle *space,
                                              const uint8_t *pattern,
                                              size_t len,
                                              uint32_t **out_indices,
                                              uint32_t *out_count) {
    return cetta_mork_bridge_space_query_indices(space, pattern, len,
                                                 out_indices, out_count);
}

bool cetta_mork_bridge_space_query_bindings_text(CettaMorkSpaceHandle *space,
                                                 const char *pattern_text,
                                                 uint8_t **out_packet,
                                                 size_t *out_len,
                                                 uint32_t *out_rows) {
    if (!pattern_text) {
        bridge_set_error("cannot query null pattern text in MORK bridge space");
        return false;
    }
    return cetta_mork_bridge_space_query_bindings(space,
                                                  (const uint8_t *)pattern_text,
                                                  strlen(pattern_text),
                                                  out_packet,
                                                  out_len,
                                                  out_rows);
}

bool cetta_mork_bridge_space_query_bindings(CettaMorkSpaceHandle *space,
                                            const uint8_t *pattern,
                                            size_t len,
                                            uint8_t **out_packet,
                                            size_t *out_len,
                                            uint32_t *out_rows) {
    *out_packet = NULL;
    *out_len = 0;
    *out_rows = 0;
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot query null or unavailable MORK bridge space");
        return false;
    }

    CettaMorkBuffer buf = g_mork_bridge_api.space_query_bindings(space, pattern, len);
    if (buf.code != 0) {
        if (buf.message && buf.message_len > 0)
            bridge_set_error_bytes("mork_space_query_bindings failed: ", buf.message, buf.message_len);
        else
            bridge_set_error("mork_space_query_bindings failed with code %d", buf.code);
        bridge_free_bytes(buf.message, buf.message_len);
        bridge_free_bytes(buf.data, buf.len);
        return false;
    }

    bridge_free_bytes(buf.message, buf.message_len);
    *out_packet = buf.data;
    *out_len = buf.len;
    *out_rows = buf.count;
    return true;
}

bool cetta_mork_bridge_space_query_bindings_query_only_v2(CettaMorkSpaceHandle *space,
                                                          const uint8_t *pattern,
                                                          size_t len,
                                                          uint8_t **out_packet,
                                                          size_t *out_len,
                                                          uint32_t *out_rows) {
    *out_packet = NULL;
    *out_len = 0;
    *out_rows = 0;
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot query null or unavailable MORK bridge space");
        return false;
    }
    if (!g_mork_bridge_api.space_query_bindings_query_only_v2) {
        bridge_set_error("mork_space_query_bindings_query_only_v2 is unavailable in the loaded MORK bridge");
        return false;
    }

    CettaMorkBuffer buf =
        g_mork_bridge_api.space_query_bindings_query_only_v2(space, pattern, len);
    if (buf.code != 0) {
        if (buf.message && buf.message_len > 0)
            bridge_set_error_bytes("mork_space_query_bindings_query_only_v2 failed: ",
                                   buf.message, buf.message_len);
        else
            bridge_set_error("mork_space_query_bindings_query_only_v2 failed with code %d",
                             buf.code);
        bridge_free_bytes(buf.message, buf.message_len);
        bridge_free_bytes(buf.data, buf.len);
        return false;
    }

    bridge_free_bytes(buf.message, buf.message_len);
    *out_packet = buf.data;
    *out_len = buf.len;
    *out_rows = buf.count;
    return true;
}

bool cetta_mork_bridge_space_query_bindings_multi_ref_v3(CettaMorkSpaceHandle *space,
                                                         const uint8_t *pattern,
                                                         size_t len,
                                                         uint8_t **out_packet,
                                                         size_t *out_len,
                                                         uint32_t *out_rows) {
    *out_packet = NULL;
    *out_len = 0;
    *out_rows = 0;
    if (!space || !bridge_load_api()) {
        bridge_set_error("cannot query null or unavailable MORK bridge space");
        return false;
    }
    if (!g_mork_bridge_api.space_query_bindings_multi_ref_v3) {
        bridge_set_error("mork_space_query_bindings_multi_ref_v3 is unavailable in the loaded MORK bridge");
        return false;
    }

    CettaMorkBuffer buf =
        g_mork_bridge_api.space_query_bindings_multi_ref_v3(space, pattern, len);
    if (buf.code != 0) {
        if (buf.message && buf.message_len > 0)
            bridge_set_error_bytes("mork_space_query_bindings_multi_ref_v3 failed: ",
                                   buf.message, buf.message_len);
        else
            bridge_set_error("mork_space_query_bindings_multi_ref_v3 failed with code %d",
                             buf.code);
        bridge_free_bytes(buf.message, buf.message_len);
        bridge_free_bytes(buf.data, buf.len);
        return false;
    }

    bridge_free_bytes(buf.message, buf.message_len);
    *out_packet = buf.data;
    *out_len = buf.len;
    *out_rows = buf.count;
    return true;
}

#endif

CettaMorkProgramHandle *cetta_mork_bridge_program_new(void) {
    if (!BRIDGE_PROGRAM_CONTEXT_AVAILABLE())
        return NULL;
    CettaMorkProgramHandle *program = BRIDGE_PROGRAM_NEW();
    if (!program)
        bridge_set_error("mork_program_new returned null");
    return program;
}

void cetta_mork_bridge_program_free(CettaMorkProgramHandle *program) {
    if (!program || !BRIDGE_PROGRAM_CONTEXT_AVAILABLE())
        return;
    BRIDGE_PROGRAM_FREE(program);
}

bool cetta_mork_bridge_program_clear(CettaMorkProgramHandle *program) {
    if (!program || !BRIDGE_PROGRAM_CONTEXT_AVAILABLE()) {
        bridge_set_error("cannot clear null or unavailable MORK bridge program");
        return false;
    }
    return bridge_status_ok("mork_program_clear failed: ",
                            BRIDGE_PROGRAM_CLEAR(program));
}

bool cetta_mork_bridge_program_add_sexpr(CettaMorkProgramHandle *program,
                                         const uint8_t *text, size_t len,
                                         uint64_t *out_added) {
    if (!program || !BRIDGE_PROGRAM_CONTEXT_AVAILABLE()) {
        if (out_added) *out_added = 0;
        bridge_set_error("cannot add to null or unavailable MORK bridge program");
        return false;
    }
    return bridge_take_status_value("mork_program_add_sexpr failed: ",
                                    BRIDGE_PROGRAM_ADD(program, text, len),
                                    out_added,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_program_size(const CettaMorkProgramHandle *program,
                                    uint64_t *out_size) {
    if (!program || !BRIDGE_PROGRAM_CONTEXT_AVAILABLE()) {
        if (out_size) *out_size = 0;
        bridge_set_error("cannot size null or unavailable MORK bridge program");
        return false;
    }
    return bridge_take_status_value("mork_program_size failed: ",
                                    BRIDGE_PROGRAM_SIZE(program),
                                    out_size,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_program_dump(CettaMorkProgramHandle *program,
                                    uint8_t **out_packet, size_t *out_len,
                                    uint32_t *out_rows) {
    if (!program || !BRIDGE_PROGRAM_CONTEXT_AVAILABLE()) {
        *out_packet = NULL;
        *out_len = 0;
        *out_rows = 0;
        bridge_set_error("cannot dump null or unavailable MORK bridge program");
        return false;
    }
    return bridge_take_buffer("mork_program_dump failed: ",
                              BRIDGE_PROGRAM_DUMP(program),
                              out_packet, out_len, out_rows,
                              bridge_free_bytes);
}

CettaMorkContextHandle *cetta_mork_bridge_context_new(void) {
    if (!BRIDGE_PROGRAM_CONTEXT_AVAILABLE())
        return NULL;
    CettaMorkContextHandle *context = BRIDGE_CONTEXT_NEW();
    if (!context)
        bridge_set_error("mork_context_new returned null");
    return context;
}

void cetta_mork_bridge_context_free(CettaMorkContextHandle *context) {
    if (!context || !BRIDGE_PROGRAM_CONTEXT_AVAILABLE())
        return;
    BRIDGE_CONTEXT_FREE(context);
}

bool cetta_mork_bridge_context_clear(CettaMorkContextHandle *context) {
    if (!context || !BRIDGE_PROGRAM_CONTEXT_AVAILABLE()) {
        bridge_set_error("cannot clear null or unavailable MORK bridge context");
        return false;
    }
    return bridge_status_ok("mork_context_clear failed: ",
                            BRIDGE_CONTEXT_CLEAR(context));
}

bool cetta_mork_bridge_context_load_program(CettaMorkContextHandle *context,
                                            const CettaMorkProgramHandle *program,
                                            uint64_t *out_added) {
    if ((!context || !program) || !BRIDGE_PROGRAM_CONTEXT_AVAILABLE()) {
        if (out_added) *out_added = 0;
        bridge_set_error("cannot load null or unavailable MORK bridge program or context");
        return false;
    }
    return bridge_take_status_value("mork_context_load_program failed: ",
                                    BRIDGE_CONTEXT_LOAD_PROGRAM(context, program),
                                    out_added,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_context_add_sexpr(CettaMorkContextHandle *context,
                                         const uint8_t *text, size_t len,
                                         uint64_t *out_added) {
    if (!context || !BRIDGE_PROGRAM_CONTEXT_AVAILABLE()) {
        if (out_added) *out_added = 0;
        bridge_set_error("cannot add to null or unavailable MORK bridge context");
        return false;
    }
    return bridge_take_status_value("mork_context_add_sexpr failed: ",
                                    BRIDGE_CONTEXT_ADD(context, text, len),
                                    out_added,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_context_remove_sexpr(CettaMorkContextHandle *context,
                                            const uint8_t *text, size_t len,
                                            uint64_t *out_removed) {
    if (!context || !BRIDGE_PROGRAM_CONTEXT_AVAILABLE()) {
        if (out_removed) *out_removed = 0;
        bridge_set_error("cannot remove from null or unavailable MORK bridge context");
        return false;
    }
    return bridge_take_status_value("mork_context_remove_sexpr failed: ",
                                    BRIDGE_CONTEXT_REMOVE(context, text, len),
                                    out_removed,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_context_size(const CettaMorkContextHandle *context,
                                    uint64_t *out_size) {
    if (!context || !BRIDGE_PROGRAM_CONTEXT_AVAILABLE()) {
        if (out_size) *out_size = 0;
        bridge_set_error("cannot size null or unavailable MORK bridge context");
        return false;
    }
    return bridge_take_status_value("mork_context_size failed: ",
                                    BRIDGE_CONTEXT_SIZE(context),
                                    out_size,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_context_run(CettaMorkContextHandle *context,
                                   uint64_t steps, uint64_t *out_performed) {
    if (!context || !BRIDGE_PROGRAM_CONTEXT_AVAILABLE()) {
        if (out_performed) *out_performed = 0;
        bridge_set_error("cannot run null or unavailable MORK bridge context");
        return false;
    }
    return bridge_take_status_value("mork_context_run failed: ",
                                    BRIDGE_CONTEXT_RUN(context, steps),
                                    out_performed,
                                    bridge_free_bytes);
}

bool cetta_mork_bridge_context_dump(CettaMorkContextHandle *context,
                                    uint8_t **out_packet, size_t *out_len,
                                    uint32_t *out_rows) {
    if (!context || !BRIDGE_PROGRAM_CONTEXT_AVAILABLE()) {
        *out_packet = NULL;
        *out_len = 0;
        *out_rows = 0;
        bridge_set_error("cannot dump null or unavailable MORK bridge context");
        return false;
    }
    return bridge_take_buffer("mork_context_dump failed: ",
                              BRIDGE_CONTEXT_DUMP(context),
                              out_packet, out_len, out_rows,
                              bridge_free_bytes);
}

void cetta_mork_bridge_bytes_free(uint8_t *data, size_t len) {
    bridge_free_bytes(data, len);
}

#undef BRIDGE_PROGRAM_CONTEXT_AVAILABLE
#undef BRIDGE_PROGRAM_NEW
#undef BRIDGE_PROGRAM_FREE
#undef BRIDGE_PROGRAM_CLEAR
#undef BRIDGE_PROGRAM_ADD
#undef BRIDGE_PROGRAM_SIZE
#undef BRIDGE_PROGRAM_DUMP
#undef BRIDGE_CONTEXT_NEW
#undef BRIDGE_CONTEXT_FREE
#undef BRIDGE_CONTEXT_CLEAR
#undef BRIDGE_CONTEXT_LOAD_PROGRAM
#undef BRIDGE_CONTEXT_ADD
#undef BRIDGE_CONTEXT_REMOVE
#undef BRIDGE_CONTEXT_SIZE
#undef BRIDGE_CONTEXT_RUN
#undef BRIDGE_CONTEXT_DUMP
