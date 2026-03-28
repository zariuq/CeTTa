#include "mork_space_bridge_runtime.h"

#include "atom.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CETTA_MORK_BRIDGE_STATIC
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

#ifdef CETTA_MORK_BRIDGE_STATIC

extern CettaMorkSpaceHandle *mork_space_new(void);
extern void mork_space_free(CettaMorkSpaceHandle *space);
extern CettaMorkStatus mork_space_clear(CettaMorkSpaceHandle *space);
extern CettaMorkStatus mork_space_add_indexed_sexpr(CettaMorkSpaceHandle *space,
                                                    uint32_t atom_idx,
                                                    const uint8_t *text,
                                                    size_t len);
extern CettaMorkStatus mork_space_size(const CettaMorkSpaceHandle *space);
extern CettaMorkStatus mork_space_dump_act_file(CettaMorkSpaceHandle *space,
                                                const uint8_t *path,
                                                size_t len);
extern CettaMorkStatus mork_space_load_act_file(CettaMorkSpaceHandle *space,
                                                const uint8_t *path,
                                                size_t len);
extern CettaMorkBuffer mork_space_dump(CettaMorkSpaceHandle *space);
extern CettaMorkBuffer mork_space_query_indices(CettaMorkSpaceHandle *space,
                                                const uint8_t *pattern,
                                                size_t len);
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

CettaMorkProgramHandle *cetta_mork_bridge_program_new(void) {
    CettaMorkProgramHandle *program = mork_program_new();
    if (!program)
        bridge_set_error("mork_program_new returned null");
    return program;
}

void cetta_mork_bridge_program_free(CettaMorkProgramHandle *program) {
    if (program)
        mork_program_free(program);
}

bool cetta_mork_bridge_program_clear(CettaMorkProgramHandle *program) {
    if (!program) {
        bridge_set_error("cannot clear null MORK bridge program");
        return false;
    }
    return bridge_status_ok("mork_program_clear failed: ", mork_program_clear(program));
}

bool cetta_mork_bridge_program_add_sexpr(CettaMorkProgramHandle *program,
                                         const uint8_t *text, size_t len,
                                         uint64_t *out_added) {
    if (out_added) *out_added = 0;
    if (!program) {
        bridge_set_error("cannot add to null MORK bridge program");
        return false;
    }
    CettaMorkStatus st = mork_program_add_sexpr(program, text, len);
    if (!bridge_status_ok("mork_program_add_sexpr failed: ", st))
        return false;
    if (out_added) *out_added = st.value;
    return true;
}

bool cetta_mork_bridge_program_size(const CettaMorkProgramHandle *program,
                                    uint64_t *out_size) {
    if (out_size) *out_size = 0;
    if (!program) {
        bridge_set_error("cannot size null MORK bridge program");
        return false;
    }
    CettaMorkStatus st = mork_program_size(program);
    if (!bridge_status_ok("mork_program_size failed: ", st))
        return false;
    if (out_size) *out_size = st.value;
    return true;
}

bool cetta_mork_bridge_program_dump(CettaMorkProgramHandle *program,
                                    uint8_t **out_packet, size_t *out_len,
                                    uint32_t *out_rows) {
    *out_packet = NULL;
    *out_len = 0;
    *out_rows = 0;
    if (!program) {
        bridge_set_error("cannot dump null MORK bridge program");
        return false;
    }
    CettaMorkBuffer buf = mork_program_dump(program);
    if (buf.code != 0) {
        if (buf.message && buf.message_len > 0)
            bridge_set_error_bytes("mork_program_dump failed: ", buf.message, buf.message_len);
        else
            bridge_set_error("mork_program_dump failed with code %d", buf.code);
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

CettaMorkContextHandle *cetta_mork_bridge_context_new(void) {
    CettaMorkContextHandle *context = mork_context_new();
    if (!context)
        bridge_set_error("mork_context_new returned null");
    return context;
}

void cetta_mork_bridge_context_free(CettaMorkContextHandle *context) {
    if (context)
        mork_context_free(context);
}

bool cetta_mork_bridge_context_clear(CettaMorkContextHandle *context) {
    if (!context) {
        bridge_set_error("cannot clear null MORK bridge context");
        return false;
    }
    return bridge_status_ok("mork_context_clear failed: ", mork_context_clear(context));
}

bool cetta_mork_bridge_context_load_program(CettaMorkContextHandle *context,
                                            const CettaMorkProgramHandle *program,
                                            uint64_t *out_added) {
    if (out_added) *out_added = 0;
    if (!context || !program) {
        bridge_set_error("cannot load null MORK bridge program or context");
        return false;
    }
    CettaMorkStatus st = mork_context_load_program(context, program);
    if (!bridge_status_ok("mork_context_load_program failed: ", st))
        return false;
    if (out_added) *out_added = st.value;
    return true;
}

bool cetta_mork_bridge_context_add_sexpr(CettaMorkContextHandle *context,
                                         const uint8_t *text, size_t len,
                                         uint64_t *out_added) {
    if (out_added) *out_added = 0;
    if (!context) {
        bridge_set_error("cannot add to null MORK bridge context");
        return false;
    }
    CettaMorkStatus st = mork_context_add_sexpr(context, text, len);
    if (!bridge_status_ok("mork_context_add_sexpr failed: ", st))
        return false;
    if (out_added) *out_added = st.value;
    return true;
}

bool cetta_mork_bridge_context_remove_sexpr(CettaMorkContextHandle *context,
                                            const uint8_t *text, size_t len,
                                            uint64_t *out_removed) {
    if (out_removed) *out_removed = 0;
    if (!context) {
        bridge_set_error("cannot remove from null MORK bridge context");
        return false;
    }
    CettaMorkStatus st = mork_context_remove_sexpr(context, text, len);
    if (!bridge_status_ok("mork_context_remove_sexpr failed: ", st))
        return false;
    if (out_removed) *out_removed = st.value;
    return true;
}

bool cetta_mork_bridge_context_size(const CettaMorkContextHandle *context,
                                    uint64_t *out_size) {
    if (out_size) *out_size = 0;
    if (!context) {
        bridge_set_error("cannot size null MORK bridge context");
        return false;
    }
    CettaMorkStatus st = mork_context_size(context);
    if (!bridge_status_ok("mork_context_size failed: ", st))
        return false;
    if (out_size) *out_size = st.value;
    return true;
}

bool cetta_mork_bridge_context_run(CettaMorkContextHandle *context,
                                   uint64_t steps, uint64_t *out_performed) {
    if (out_performed) *out_performed = 0;
    if (!context) {
        bridge_set_error("cannot run null MORK bridge context");
        return false;
    }
    CettaMorkStatus st = mork_context_run(context, steps);
    if (!bridge_status_ok("mork_context_run failed: ", st))
        return false;
    if (out_performed) *out_performed = st.value;
    return true;
}

bool cetta_mork_bridge_context_dump(CettaMorkContextHandle *context,
                                    uint8_t **out_packet, size_t *out_len,
                                    uint32_t *out_rows) {
    *out_packet = NULL;
    *out_len = 0;
    *out_rows = 0;
    if (!context) {
        bridge_set_error("cannot dump null MORK bridge context");
        return false;
    }
    CettaMorkBuffer buf = mork_context_dump(context);
    if (buf.code != 0) {
        if (buf.message && buf.message_len > 0)
            bridge_set_error_bytes("mork_context_dump failed: ", buf.message, buf.message_len);
        else
            bridge_set_error("mork_context_dump failed with code %d", buf.code);
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

void cetta_mork_bridge_bytes_free(uint8_t *data, size_t len) {
    bridge_free_bytes(data, len);
}

#else

typedef struct CettaMorkBridgeApi {
    bool attempted;
    void *handle;
    CettaMorkSpaceHandle *(*space_new)(void);
    void (*space_free)(CettaMorkSpaceHandle *space);
    CettaMorkStatus (*space_clear)(CettaMorkSpaceHandle *space);
    CettaMorkStatus (*space_add_indexed_sexpr)(CettaMorkSpaceHandle *space,
                                               uint32_t atom_idx,
                                               const uint8_t *text,
                                               size_t len);
    CettaMorkStatus (*space_size)(const CettaMorkSpaceHandle *space);
    CettaMorkStatus (*space_dump_act_file)(CettaMorkSpaceHandle *space,
                                           const uint8_t *path,
                                           size_t len);
    CettaMorkStatus (*space_load_act_file)(CettaMorkSpaceHandle *space,
                                           const uint8_t *path,
                                           size_t len);
    CettaMorkBuffer (*space_dump)(CettaMorkSpaceHandle *space);
    CettaMorkBuffer (*space_query_indices)(CettaMorkSpaceHandle *space,
                                           const uint8_t *pattern,
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

CettaMorkProgramHandle *cetta_mork_bridge_program_new(void) {
    if (!bridge_load_api())
        return NULL;
    CettaMorkProgramHandle *program = g_mork_bridge_api.program_new();
    if (!program)
        bridge_set_error("mork_program_new returned null");
    return program;
}

void cetta_mork_bridge_program_free(CettaMorkProgramHandle *program) {
    if (!program || !bridge_load_api())
        return;
    g_mork_bridge_api.program_free(program);
}

bool cetta_mork_bridge_program_clear(CettaMorkProgramHandle *program) {
    if (!program || !bridge_load_api()) {
        bridge_set_error("cannot clear null or unavailable MORK bridge program");
        return false;
    }
    return bridge_status_ok("mork_program_clear failed: ",
                            g_mork_bridge_api.program_clear(program));
}

bool cetta_mork_bridge_program_add_sexpr(CettaMorkProgramHandle *program,
                                         const uint8_t *text, size_t len,
                                         uint64_t *out_added) {
    if (out_added) *out_added = 0;
    if (!program || !bridge_load_api()) {
        bridge_set_error("cannot add to null or unavailable MORK bridge program");
        return false;
    }
    CettaMorkStatus st = g_mork_bridge_api.program_add_sexpr(program, text, len);
    if (!bridge_status_ok("mork_program_add_sexpr failed: ", st))
        return false;
    if (out_added) *out_added = st.value;
    return true;
}

bool cetta_mork_bridge_program_size(const CettaMorkProgramHandle *program,
                                    uint64_t *out_size) {
    if (out_size) *out_size = 0;
    if (!program || !bridge_load_api()) {
        bridge_set_error("cannot size null or unavailable MORK bridge program");
        return false;
    }
    CettaMorkStatus st = g_mork_bridge_api.program_size(program);
    if (!bridge_status_ok("mork_program_size failed: ", st))
        return false;
    if (out_size) *out_size = st.value;
    return true;
}

bool cetta_mork_bridge_program_dump(CettaMorkProgramHandle *program,
                                    uint8_t **out_packet, size_t *out_len,
                                    uint32_t *out_rows) {
    *out_packet = NULL;
    *out_len = 0;
    *out_rows = 0;
    if (!program || !bridge_load_api()) {
        bridge_set_error("cannot dump null or unavailable MORK bridge program");
        return false;
    }
    CettaMorkBuffer buf = g_mork_bridge_api.program_dump(program);
    if (buf.code != 0) {
        if (buf.message && buf.message_len > 0)
            bridge_set_error_bytes("mork_program_dump failed: ", buf.message, buf.message_len);
        else
            bridge_set_error("mork_program_dump failed with code %d", buf.code);
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

CettaMorkContextHandle *cetta_mork_bridge_context_new(void) {
    if (!bridge_load_api())
        return NULL;
    CettaMorkContextHandle *context = g_mork_bridge_api.context_new();
    if (!context)
        bridge_set_error("mork_context_new returned null");
    return context;
}

void cetta_mork_bridge_context_free(CettaMorkContextHandle *context) {
    if (!context || !bridge_load_api())
        return;
    g_mork_bridge_api.context_free(context);
}

bool cetta_mork_bridge_context_clear(CettaMorkContextHandle *context) {
    if (!context || !bridge_load_api()) {
        bridge_set_error("cannot clear null or unavailable MORK bridge context");
        return false;
    }
    return bridge_status_ok("mork_context_clear failed: ",
                            g_mork_bridge_api.context_clear(context));
}

bool cetta_mork_bridge_context_load_program(CettaMorkContextHandle *context,
                                            const CettaMorkProgramHandle *program,
                                            uint64_t *out_added) {
    if (out_added) *out_added = 0;
    if (!context || !program || !bridge_load_api()) {
        bridge_set_error("cannot load null or unavailable MORK bridge program or context");
        return false;
    }
    CettaMorkStatus st = g_mork_bridge_api.context_load_program(context, program);
    if (!bridge_status_ok("mork_context_load_program failed: ", st))
        return false;
    if (out_added) *out_added = st.value;
    return true;
}

bool cetta_mork_bridge_context_add_sexpr(CettaMorkContextHandle *context,
                                         const uint8_t *text, size_t len,
                                         uint64_t *out_added) {
    if (out_added) *out_added = 0;
    if (!context || !bridge_load_api()) {
        bridge_set_error("cannot add to null or unavailable MORK bridge context");
        return false;
    }
    CettaMorkStatus st = g_mork_bridge_api.context_add_sexpr(context, text, len);
    if (!bridge_status_ok("mork_context_add_sexpr failed: ", st))
        return false;
    if (out_added) *out_added = st.value;
    return true;
}

bool cetta_mork_bridge_context_remove_sexpr(CettaMorkContextHandle *context,
                                            const uint8_t *text, size_t len,
                                            uint64_t *out_removed) {
    if (out_removed) *out_removed = 0;
    if (!context || !bridge_load_api()) {
        bridge_set_error("cannot remove from null or unavailable MORK bridge context");
        return false;
    }
    CettaMorkStatus st = g_mork_bridge_api.context_remove_sexpr(context, text, len);
    if (!bridge_status_ok("mork_context_remove_sexpr failed: ", st))
        return false;
    if (out_removed) *out_removed = st.value;
    return true;
}

bool cetta_mork_bridge_context_size(const CettaMorkContextHandle *context,
                                    uint64_t *out_size) {
    if (out_size) *out_size = 0;
    if (!context || !bridge_load_api()) {
        bridge_set_error("cannot size null or unavailable MORK bridge context");
        return false;
    }
    CettaMorkStatus st = g_mork_bridge_api.context_size(context);
    if (!bridge_status_ok("mork_context_size failed: ", st))
        return false;
    if (out_size) *out_size = st.value;
    return true;
}

bool cetta_mork_bridge_context_run(CettaMorkContextHandle *context,
                                   uint64_t steps, uint64_t *out_performed) {
    if (out_performed) *out_performed = 0;
    if (!context || !bridge_load_api()) {
        bridge_set_error("cannot run null or unavailable MORK bridge context");
        return false;
    }
    CettaMorkStatus st = g_mork_bridge_api.context_run(context, steps);
    if (!bridge_status_ok("mork_context_run failed: ", st))
        return false;
    if (out_performed) *out_performed = st.value;
    return true;
}

bool cetta_mork_bridge_context_dump(CettaMorkContextHandle *context,
                                    uint8_t **out_packet, size_t *out_len,
                                    uint32_t *out_rows) {
    *out_packet = NULL;
    *out_len = 0;
    *out_rows = 0;
    if (!context || !bridge_load_api()) {
        bridge_set_error("cannot dump null or unavailable MORK bridge context");
        return false;
    }
    CettaMorkBuffer buf = g_mork_bridge_api.context_dump(context);
    if (buf.code != 0) {
        if (buf.message && buf.message_len > 0)
            bridge_set_error_bytes("mork_context_dump failed: ", buf.message, buf.message_len);
        else
            bridge_set_error("mork_context_dump failed with code %d", buf.code);
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

static bool bridge_load_api(void) {
    if (g_mork_bridge_api.attempted)
        return g_mork_bridge_api.handle != NULL;

    g_mork_bridge_api.attempted = true;

    const char *env = getenv("CETTA_MORK_SPACE_BRIDGE_LIB");
    const char *candidates[2];
    uint32_t ncandidates = 0;
    if (env && *env)
        candidates[ncandidates++] = env;
    candidates[ncandidates++] = "libcetta_space_bridge.so";

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
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.space_add_indexed_sexpr, "mork_space_add_indexed_sexpr") ||
        !bridge_resolve_symbol((void **)&g_mork_bridge_api.space_size, "mork_space_size") ||
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

void cetta_mork_bridge_bytes_free(uint8_t *data, size_t len) {
    bridge_free_bytes(data, len);
}

#endif
