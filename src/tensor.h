#ifndef CETTA_TENSOR_H
#define CETTA_TENSOR_H

#include "atom.h"

/* Immutable, compact CPU tensors. A returned tensor has one caller hold.
 * Atom publication retains it; the caller then releases its own hold. Every
 * arena copy acquires its own hold. Reshape shares storage; writes allocate.
 * Numeric identity is dtype + shape + exact element bits, not addresses. */
typedef struct CettaTensor CettaTensor;
typedef enum {
    CETTA_TENSOR_I64 = 1,
    CETTA_TENSOR_F64 = 2,
    CETTA_TENSOR_U8 = 3,
    CETTA_TENSOR_F32 = 4,
} CettaTensorDType;

typedef enum {
    CETTA_TENSOR_OK = 0,
    CETTA_TENSOR_DTYPE,
    CETTA_TENSOR_SHAPE,
    CETTA_TENSOR_COUNT,
    CETTA_TENSOR_OVERFLOW,
    CETTA_TENSOR_NOMEM,
    CETTA_TENSOR_INDEX,
    CETTA_TENSOR_VALUE,
    CETTA_TENSOR_ENCODING,
    CETTA_TENSOR_PROVIDER,
} CettaTensorStatus;

const char *cetta_tensor_status_name(CettaTensorStatus status);
const char *cetta_tensor_dtype_name(CettaTensorDType dtype);
size_t cetta_tensor_itemsize(CettaTensorDType dtype);
/* The two-pass count admits zero axes without overflowing an earlier product. */
CettaTensorStatus cetta_tensor_byte_count(CettaTensorDType dtype,
    const size_t *shape, size_t rank, size_t *count, size_t *bytes);
CettaTensorStatus cetta_tensor_new(CettaTensorDType dtype,
    const size_t *shape, size_t rank, const void *data, size_t bytes,
    CettaTensor **out);
void cetta_tensor_retain(void *tensor);
void cetta_tensor_release(void *tensor);
size_t cetta_tensor_rank(const CettaTensor *tensor);
size_t cetta_tensor_count(const CettaTensor *tensor);
size_t cetta_tensor_nbytes(const CettaTensor *tensor);
/* Conservative retained allocation charge, including shared storage. */
size_t cetta_tensor_retained_bytes(const CettaTensor *tensor);
const size_t *cetta_tensor_shape(const CettaTensor *tensor);
CettaTensorDType cetta_tensor_dtype(const CettaTensor *tensor);
const void *cetta_tensor_data(const CettaTensor *tensor);
bool cetta_tensor_equal(const CettaTensor *left, const CettaTensor *right);
int cetta_tensor_compare(const CettaTensor *left, const CettaTensor *right);
uint32_t cetta_tensor_hash(const CettaTensor *tensor);
bool cetta_tensor_shares_storage(const CettaTensor *left, const CettaTensor *right);
CettaTensorStatus cetta_tensor_reshape(const CettaTensor *tensor,
    const size_t *shape, size_t rank, CettaTensor **out);
CettaTensorStatus cetta_tensor_copy(const CettaTensor *tensor, CettaTensor **out);
CettaTensorStatus cetta_tensor_cast(const CettaTensor *tensor,
    CettaTensorDType dtype, CettaTensor **out);
CettaTensorStatus cetta_tensor_with(const CettaTensor *tensor, size_t index,
    const void *element, CettaTensor **out);
CettaTensorStatus cetta_tensor_binary(const CettaTensor *left,
    const CettaTensor *right, bool multiply, CettaTensor **out);
typedef struct {
    CettaTensorDType dtype;
    union { int64_t integer; double floating; } value;
} CettaTensorScalar;
/* With a right operand, this is a rank-one dot; otherwise an ordered sum. */
CettaTensorStatus cetta_tensor_reduce(const CettaTensor *left,
    const CettaTensor *right, CettaTensorScalar *out);
/* Axis-zero gather preserves order and repeated indices. Selectors are i64. */
CettaTensorStatus cetta_tensor_gather(const CettaTensor *source,
    const CettaTensor *indices, CettaTensor **out);
/* Dense f64/f32 matrix with an f64 query, accumulation and result. NULL indices
 * selects all rows. Four ordered
 * accumulation lanes are combined in a fixed order, without FMA contraction.
 * This is a different reduction order from tensor:dot's scalar reference. */
CettaTensorStatus cetta_tensor_matvec(const CettaTensor *matrix,
    const CettaTensor *query, const CettaTensor *indices, CettaTensor **out);
typedef enum {
    CETTA_MATVEC_AUTO,
    CETTA_MATVEC_SCALAR4,
    CETTA_MATVEC_AVX4,
} CettaTensorMatvecProvider;
bool cetta_tensor_matvec_provider_available(CettaTensorMatvecProvider provider);
CettaTensorStatus cetta_tensor_matvec_with_provider(const CettaTensor *matrix,
    const CettaTensor *query, const CettaTensor *indices,
    CettaTensorMatvecProvider provider, CettaTensor **out);
CettaTensorStatus cetta_tensor_div(const CettaTensor *left,
    const CettaTensor *right, CettaTensor **out);
CettaTensorStatus cetta_tensor_scale(const CettaTensor *source, double factor,
    CettaTensor **out);
/* Boolean masks use u8 entries restricted to 0 and 1. */
CettaTensorStatus cetta_tensor_mask_indices(size_t length,
    const CettaTensor *indices, CettaTensor **out);
CettaTensorStatus cetta_tensor_mask_binary(const CettaTensor *left,
    const CettaTensor *right, bool is_or, CettaTensor **out);
CettaTensorStatus cetta_tensor_mask_not(const CettaTensor *mask, CettaTensor **out);
CettaTensorStatus cetta_tensor_nonzero(const CettaTensor *mask, CettaTensor **out);
/* Sorted descending, with smaller source indices first at equal scores.
 * Enabled nonfinite scores are refused, including when k is zero. */
CettaTensorStatus cetta_tensor_top_k(const CettaTensor *scores,
    const CettaTensor *mask, size_t k, CettaTensor **out);
/* Derived row certificate: [L1 conversion error upper bound, combined L1
 * magnitude upper bound]. Must belong to this exact source/compact pair. */
CettaTensorStatus cetta_tensor_row_error_bounds(const CettaTensor *source,
    const CettaTensor *compact, CettaTensor **out);
/* Conservative bounds on the existing f64 lane4 score times positive scale.
 * A rounding environment outside the admitted fragment yields [-inf, inf],
 * retaining every row. NULL indices selects all rows. */
CettaTensorStatus cetta_tensor_matvec_bounds(const CettaTensor *compact,
    const CettaTensor *query, const CettaTensor *certificate,
    const CettaTensor *indices, double scale, CettaTensor **lower, CettaTensor **upper);
/* Keep every upper bound reaching the k-th largest lower bound; ties survive.
 * This returns a candidate superset, never a final approximate ranking. */
CettaTensorStatus cetta_tensor_top_k_candidates(const CettaTensor *lower,
    const CettaTensor *upper, size_t k, CettaTensor **out);
/* CTN1: dtype:u8, rank:u32 LE, dimensions:u64 LE, ordered element bits LE.
 * Encoding is pointer-free, lossless and portable for the supported dtypes. */
CettaTensorStatus cetta_tensor_encoded_size(const CettaTensor *tensor, size_t *size);
CettaTensorStatus cetta_tensor_encode(const CettaTensor *tensor,
    unsigned char **bytes, size_t *size);
CettaTensorStatus cetta_tensor_decode(const unsigned char *bytes,
    size_t size, CettaTensor **out);
void cetta_tensor_print(const CettaTensor *tensor, FILE *out);
Atom *cetta_tensor_dispatch(Arena *arena, Atom *head, Atom **args, uint32_t nargs);

#endif
