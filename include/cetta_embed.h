#ifndef CETTA_EMBED_H
#define CETTA_EMBED_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CETTA_EMBED_ABI_VERSION 1u
typedef struct cetta_engine cetta_engine_t;
typedef struct cetta_atom cetta_atom_t;
typedef struct cetta_result cetta_result_t;
typedef struct cetta_query cetta_query_t;
typedef struct cetta_space cetta_space_t;
typedef struct cetta_buffer cetta_buffer_t;

typedef enum {
    CETTA_ATOM_SYMBOL, CETTA_ATOM_VARIABLE, CETTA_ATOM_EXPRESSION,
    CETTA_ATOM_INTEGER, CETTA_ATOM_FLOAT, CETTA_ATOM_BOOLEAN,
    CETTA_ATOM_STRING, CETTA_ATOM_BIG_INTEGER, CETTA_ATOM_RATIONAL,
    CETTA_ATOM_RESOURCE
} cetta_atom_kind_t;

typedef enum {
    CETTA_OK = 0,
    CETTA_INVALID_ARGUMENT,
    CETTA_OUT_OF_MEMORY,
    CETTA_ENGINE_CLOSED,
    CETTA_FOREIGN_ENGINE,
    CETTA_PARSE_ERROR,
    CETTA_LANGUAGE_RAISED,
    CETTA_FUEL_EXHAUSTED,
    CETTA_CANCELLED,
    CETTA_RESOURCE_ERROR,
    CETTA_ENGINE_ERROR,
    CETTA_BUSY,
    CETTA_QUERY_CLOSED,
    CETTA_INVALIDATED,
    CETTA_PREPARATION_REQUIRED,
    CETTA_BUFFER_INCOMPATIBLE,
    CETTA_IO_ERROR,
    CETTA_HALTED
} cetta_status_t;

typedef struct {
    size_t size;
    char message[512];
    char source[1024];
    uint64_t line, column;
    int system_error, exit_code;
} cetta_diagnostic_t;

typedef struct {
    size_t size;
    void *context;
    void (*retain)(void *context);
    void (*release)(void *context);
    /* Channel 0 is program output; channel 1 is diagnostics. Bytes are borrowed. */
    cetta_status_t (*write)(void *context, unsigned channel, const char *, size_t);
    cetta_status_t (*flush)(void *context, unsigned channel);
    int (*interrupt)(void *context);
} cetta_host_hooks_t;
/* Hooks run on the calling thread. A successful update retains context;
 * replacement during callback reentry reports BUSY. NULL restores stdio. */
cetta_status_t cetta_embed_engine_set_host_hooks(cetta_engine_t *, const cetta_host_hooks_t *);

typedef enum { CETTA_BYTE_ORDER_NATIVE, CETTA_BYTE_ORDER_LITTLE, CETTA_BYTE_ORDER_BIG } cetta_byte_order_t;
typedef struct {
    size_t size;
    const char *dtype; /* i64, f64, u8, f32, bool or index. */
    cetta_byte_order_t byte_order;
    const void *allocation;
    size_t allocation_bytes;
    size_t offset; /* Bytes from the allocation to logical element zero. */
    size_t rank;
    const size_t *shape;
    const ptrdiff_t *strides; /* Bytes; NULL selects row-major strides. */
} cetta_buffer_descriptor_t;
typedef struct {
    void *context;
    void (*retain)(void *context);
    void (*release)(void *context);
} cetta_buffer_owner_t;
/* Copy the described allocation. Borrow instead requires immutable backing
 * storage through its last release, and thread-safe ownership hooks. */
cetta_status_t cetta_embed_buffer_copy(const cetta_buffer_descriptor_t *, cetta_buffer_t **);
cetta_status_t cetta_embed_buffer_borrow(const cetta_buffer_descriptor_t *,
    const cetta_buffer_owner_t *, cetta_buffer_t **);
void cetta_embed_buffer_retain(cetta_buffer_t *);
void cetta_embed_buffer_release(cetta_buffer_t *);
/* All descriptor pointers remain borrowed from the retained buffer. */
cetta_status_t cetta_embed_buffer_describe(const cetta_buffer_t *, cetta_buffer_descriptor_t *);
cetta_status_t cetta_embed_buffer_view(const cetta_buffer_t *, const size_t *shape,
    const ptrdiff_t *strides, size_t rank, size_t offset, cetta_buffer_t **);
/* Reinterpret retained bytes with an explicit dtype/order, without a copy. */
cetta_status_t cetta_embed_buffer_reinterpret(const cetta_buffer_t *, const char *dtype,
    cetta_byte_order_t byte_order, const size_t *shape, const ptrdiff_t *strides, size_t rank, size_t offset, cetta_buffer_t **);
cetta_status_t cetta_embed_buffer_from_atom(const cetta_atom_t *, cetta_buffer_t **);
cetta_status_t cetta_embed_atom_buffer(cetta_engine_t *, const cetta_buffer_t *, cetta_atom_t **);

typedef enum { CETTA_LAYOUT_DENSE, CETTA_LAYOUT_COO, CETTA_LAYOUT_CSR, CETTA_LAYOUT_CSC } cetta_tensor_layout_t;
typedef enum { CETTA_DUPLICATES_REJECT, CETTA_DUPLICATES_FIRST, CETTA_DUPLICATES_LAST, CETTA_DUPLICATES_SUM } cetta_duplicates_t;
typedef enum { CETTA_COPY_AUTO, CETTA_COPY_ALWAYS, CETTA_COPY_NEVER } cetta_copy_t;
typedef struct {
    size_t size;
    cetta_tensor_layout_t layout;
    size_t rank;
    const size_t *shape;
    const cetta_buffer_t *values, *coordinates, *indptr, *indices;
} cetta_sparse_descriptor_t;
typedef struct {
    size_t size;
    const char *dtype;
    cetta_tensor_layout_t layout;
    size_t rank;
    const size_t *shape;
    size_t stored;
} cetta_tensor_info_t;
typedef struct {
    size_t size;
    cetta_buffer_t *values, *coordinates, *indptr, *indices;
} cetta_sparse_components_t;
/* Borrowed metadata remains valid while the atom is retained. */
cetta_status_t cetta_embed_tensor_describe(const cetta_atom_t *, cetta_tensor_info_t *);
/* Components are independently owned readonly buffers; release each non-null
 * handle. No missing coordinate is filled and no stored zero is removed. */
cetta_status_t cetta_embed_tensor_components(const cetta_atom_t *, cetta_sparse_components_t *);
cetta_status_t cetta_embed_tensor_from_sparse(cetta_engine_t *, const cetta_sparse_descriptor_t *,
    cetta_duplicates_t, cetta_copy_t, cetta_atom_t **);

typedef struct {
    size_t size;
    uint32_t abi_version;
    const char *language;
    const char *profile;
    /* Resource root for lib/, langdef/ and runtime/ assets. UTF-8 strings
     * are copied on creation. */
    const char *resource_root;
} cetta_engine_config_t;

typedef struct {
    size_t size;
    int64_t semantic_fuel; /* -1 means unbounded. */
} cetta_eval_options_t;

typedef enum {
    CETTA_CALLBACK_VALUE,
    CETTA_CALLBACK_COMPLETE,
    CETTA_CALLBACK_PAUSED,
    CETTA_CALLBACK_RAISED
} cetta_callback_state_t;
typedef enum { CETTA_CALLBACK_SINGLE, CETTA_CALLBACK_STREAM } cetta_callback_kind_t;
typedef enum { CETTA_CALLBACK_CLOSE_DIRECT, CETTA_CALLBACK_CLOSE_DEFERRED } cetta_callback_close_mode_t;
typedef struct {
    size_t size;
    size_t arity;
    cetta_callback_kind_t kind;
    void *context;
    void (*retain)(void *context);
    void (*release)(void *context);
    /* Arguments are borrowed values and stay live until close. Open is
     * administrative; execute host code in next so errors have a payload. */
    cetta_status_t (*open)(void *context, cetta_engine_t *,
        cetta_atom_t *const *arguments, size_t arity, void **cursor);
    /* VALUE and RAISED transfer one owned atom. COMPLETE/PAUSED transfer
     * none. Each pull produces at most one occurrence. */
    cetta_status_t (*next)(void *cursor, cetta_callback_state_t *, cetta_atom_t **);
    /* A deferred close runs after its requesting entry has returned. Host
     * cleanup errors then need asynchronous reporting, not another call's
     * error channel. Close must not unwind across the C boundary. */
    void (*close)(void *cursor, cetta_callback_close_mode_t);
} cetta_callback_descriptor_t;

cetta_status_t cetta_embed_callback_new(cetta_engine_t *,
    const cetta_callback_descriptor_t *, cetta_atom_t **);
/* Register an ordinary equation invoking the callback. No global host
 * namespace is installed. The callable output is independently retained. */
cetta_status_t cetta_embed_register_callback(cetta_engine_t *, const char *name,
    const cetta_callback_descriptor_t *, cetta_atom_t **);

typedef enum {
    CETTA_QUERY_STATE_ANSWER,
    CETTA_QUERY_STATE_PAUSED,
    CETTA_QUERY_STATE_COMPLETE,
    CETTA_QUERY_STATE_STOPPED
} cetta_query_state_t;
typedef struct {
    size_t size;
    cetta_query_state_t state;
    uint64_t physical_steps;
    uint64_t source_steps;
    uint64_t source_remaining;
    uint32_t source_limited;
    uint32_t conditional;
    /* Independently owned structural substitution (assignments and
     * equalities) and conditional goals.
     * Release each non-null handle with atom_release. */
    cetta_atom_t *bindings;
    cetta_atom_t *delayed;
} cetta_query_step_t;

cetta_status_t cetta_embed_query_open(cetta_engine_t *, const cetta_atom_t *,
    const cetta_eval_options_t *, cetta_query_t **);
void cetta_embed_query_retain(cetta_query_t *);
void cetta_embed_query_cancel(cetta_query_t *);
void cetta_embed_query_close(cetta_query_t *);
void cetta_embed_query_release(cetta_query_t *);
/* One slice yields at most one answer. Zero allowance advances no source
 * work; cancellation and terminal state can still be observed. */
cetta_status_t cetta_embed_query_step(cetta_query_t *, uint64_t physical_allowance,
    cetta_query_step_t *, cetta_result_t **);

cetta_status_t cetta_embed_engine_space(cetta_engine_t *, cetta_space_t **);
cetta_status_t cetta_embed_space_new(cetta_engine_t *, cetta_space_t **);
cetta_status_t cetta_embed_space_from_atom(cetta_engine_t *, const cetta_atom_t *, cetta_space_t **);
cetta_status_t cetta_embed_space_atom(const cetta_space_t *, cetta_atom_t **);
void cetta_embed_space_retain(cetta_space_t *);
void cetta_embed_space_release(cetta_space_t *);
cetta_status_t cetta_embed_space_count(cetta_space_t *, uint64_t *);
cetta_status_t cetta_embed_space_add_atom(cetta_space_t *, const cetta_atom_t *);
/* Use the language's removal semantics; removed reports whether the count fell. */
cetta_status_t cetta_embed_space_remove_atom(cetta_space_t *, const cetta_atom_t *, int *removed);
/* A null template returns the matched pattern as data. Otherwise the
 * language evaluates the explicitly supplied template. Bindings export
 * the pattern's scoped variables. */
cetta_status_t cetta_embed_space_match(cetta_space_t *, const cetta_atom_t *pattern,
    const cetta_atom_t *template_value, const cetta_eval_options_t *, cetta_query_t **);

uint32_t cetta_embed_abi_version(void);
cetta_status_t cetta_embed_engine_new(const cetta_engine_config_t *, cetta_engine_t **);
/* Limit native-core allocation requests, including retained native results.
 * UINT64_MAX is unlimited. GMP and host-owned input storage are excluded. */
cetta_status_t cetta_embed_engine_new_limited(const cetta_engine_config_t *, uint64_t memory_limit,
    cetta_engine_t **);
void cetta_embed_engine_retain(cetta_engine_t *);
void cetta_embed_engine_close(cetta_engine_t *);
void cetta_embed_engine_release(cetta_engine_t *);

/* Parse exactly one term. Source text is not evaluated by construction. */
cetta_status_t cetta_embed_parse(cetta_engine_t *, const char *, size_t, cetta_atom_t **);
cetta_status_t cetta_embed_parse_diagnostic(cetta_engine_t *, const char *, size_t,
    cetta_diagnostic_t *, cetta_atom_t **);
cetta_status_t cetta_embed_atom_symbol(cetta_engine_t *, const char *, cetta_atom_t **);
cetta_status_t cetta_embed_atom_string(cetta_engine_t *, const char *, cetta_atom_t **);
cetta_status_t cetta_embed_atom_string_n(cetta_engine_t *, const char *, size_t, cetta_atom_t **);
cetta_status_t cetta_embed_atom_integer(cetta_engine_t *, int64_t, cetta_atom_t **);
cetta_status_t cetta_embed_atom_float(cetta_engine_t *, double, cetta_atom_t **);
cetta_status_t cetta_embed_atom_boolean(cetta_engine_t *, int, cetta_atom_t **);
cetta_status_t cetta_embed_atom_variable(cetta_engine_t *, const char *, cetta_atom_t **);
cetta_status_t cetta_embed_atom_integer_decimal(cetta_engine_t *, const char *, cetta_atom_t **);
cetta_status_t cetta_embed_atom_rational_decimal(cetta_engine_t *, const char *, cetta_atom_t **);
cetta_status_t cetta_embed_atom_expression(cetta_engine_t *, cetta_atom_t *const *,
                                    size_t, cetta_atom_t **);
void cetta_embed_atom_retain(cetta_atom_t *);
void cetta_embed_atom_release(cetta_atom_t *);
cetta_atom_kind_t cetta_embed_atom_kind(const cetta_atom_t *);
cetta_status_t cetta_embed_atom_get_integer(const cetta_atom_t *, int64_t *);
cetta_status_t cetta_embed_atom_get_float(const cetta_atom_t *, double *);
cetta_status_t cetta_embed_atom_get_boolean(const cetta_atom_t *, int *);
/* Text is copied, without a terminating NUL; size reports its byte length. */
cetta_status_t cetta_embed_atom_get_text(const cetta_atom_t *, char *, size_t, size_t *);
size_t cetta_embed_atom_arity(const cetta_atom_t *);
cetta_status_t cetta_embed_atom_child(const cetta_atom_t *, size_t, cetta_atom_t **);
/* Equality preserves variable identity, including equal display spellings.
 * A busy owning context is a status, not a false comparison. */
cetta_status_t cetta_embed_atom_equal(const cetta_atom_t *, const cetta_atom_t *, int *equal);
/* Formatting remains valid after engine close. required includes the NUL.
 * A short/null output buffer reports its required size without truncating. */
cetta_status_t cetta_embed_atom_format(const cetta_atom_t *, char *, size_t, size_t *required);

cetta_status_t cetta_embed_eval(cetta_engine_t *, const cetta_atom_t *,
                         const cetta_eval_options_t *, cetta_result_t **);
/* Invoke a callable on values. An argument's contents are not source to run. */
cetta_status_t cetta_embed_call(cetta_engine_t *, const cetta_atom_t *,
    cetta_atom_t *const *, size_t, const cetta_eval_options_t *, cetta_result_t **);
/* Run one document. Non-directive forms are definitions; each ! directive
 * contributes one group, including a complete group with zero answers. */
cetta_status_t cetta_embed_run(cetta_engine_t *, const char *, size_t,
                        const cetta_eval_options_t *, cetta_result_t **);
/* Execute a file into the root space, retaining directive groups. Relative
 * filenames follow the host working directory; imports during execution
 * follow the file's canonical directory. No process chdir is performed. */
cetta_status_t cetta_embed_load(cetta_engine_t *, const char *path,
    const cetta_eval_options_t *, cetta_result_t **);
/* Invoke this session's import! with a destination value (NULL means root).
 * The module spec is data: e.g. (library tensor) in PeTTa or tensor in HE. */
cetta_status_t cetta_embed_import(cetta_engine_t *, const cetta_atom_t *target,
    const cetta_atom_t *spec, const cetta_eval_options_t *, cetta_result_t **);
void cetta_embed_result_release(cetta_result_t *);
size_t cetta_embed_result_count(const cetta_result_t *);
size_t cetta_embed_result_group_count(const cetta_result_t *);
size_t cetta_embed_result_group_size(const cetta_result_t *, size_t);
/* Get returns a retained atom. The caller releases it independently. */
cetta_status_t cetta_embed_result_get(const cetta_result_t *, size_t, cetta_atom_t **);
cetta_status_t cetta_embed_result_status(const cetta_result_t *);
/* The captured errno for an IO_ERROR; zero for other outcomes. */
int cetta_embed_result_system_error(const cetta_result_t *);
/* The language-requested exit code for a halted invocation, otherwise zero. */
int cetta_embed_result_exit_code(const cetta_result_t *);
cetta_status_t cetta_embed_result_diagnostic(const cetta_result_t *, cetta_diagnostic_t *);
const char *cetta_embed_status_name(cetta_status_t);

#ifdef __cplusplus
}
#endif
#endif
