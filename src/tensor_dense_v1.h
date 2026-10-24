#ifndef CETTA_DENSE_TENSOR_V1_H
#define CETTA_DENSE_TENSOR_V1_H

#include "atom.h"

/* Immutable, compact CPU tensors. A returned tensor has one caller hold.
 * Atom publication retains it; the caller then releases its own hold. Every
 * arena copy acquires its own hold. Reshape shares storage; writes allocate.
 * Numeric identity is dtype + shape + exact element bits, not addresses. */
typedef struct CettaDenseTensorV1 CettaDenseTensorV1;
typedef enum {
    CETTA_DENSE_TENSOR_V1_I64 = 1,
    CETTA_DENSE_TENSOR_V1_F64 = 2,
    CETTA_DENSE_TENSOR_V1_U8 = 3,
    CETTA_DENSE_TENSOR_V1_F32 = 4,
} CettaDenseTensorV1DType;

typedef enum {
    CETTA_DENSE_TENSOR_V1_OK = 0,
    CETTA_DENSE_TENSOR_V1_DTYPE,
    CETTA_DENSE_TENSOR_V1_SHAPE,
    CETTA_DENSE_TENSOR_V1_COUNT,
    CETTA_DENSE_TENSOR_V1_OVERFLOW,
    CETTA_DENSE_TENSOR_V1_NOMEM,
    CETTA_DENSE_TENSOR_V1_INDEX,
    CETTA_DENSE_TENSOR_V1_VALUE,
    CETTA_DENSE_TENSOR_V1_ENCODING,
    CETTA_DENSE_TENSOR_V1_PROVIDER,
} CettaDenseTensorV1Status;

const char *cetta_dense_tensor_v1_status_name(CettaDenseTensorV1Status status);
const char *cetta_dense_tensor_v1_dtype_name(CettaDenseTensorV1DType dtype);
size_t cetta_dense_tensor_v1_itemsize(CettaDenseTensorV1DType dtype);
/* The two-pass count admits zero axes without overflowing an earlier product. */
CettaDenseTensorV1Status cetta_dense_tensor_v1_byte_count(CettaDenseTensorV1DType dtype,
    const size_t *shape, size_t rank, size_t *count, size_t *bytes);
CettaDenseTensorV1Status cetta_dense_tensor_v1_new(CettaDenseTensorV1DType dtype,
    const size_t *shape, size_t rank, const void *data, size_t bytes,
    CettaDenseTensorV1 **out);
void cetta_dense_tensor_v1_retain(void *tensor);
void cetta_dense_tensor_v1_release(void *tensor);
size_t cetta_dense_tensor_v1_rank(const CettaDenseTensorV1 *tensor);
size_t cetta_dense_tensor_v1_count(const CettaDenseTensorV1 *tensor);
size_t cetta_dense_tensor_v1_nbytes(const CettaDenseTensorV1 *tensor);
/* Conservative retained allocation charge, including shared storage. */
size_t cetta_dense_tensor_v1_retained_bytes(const CettaDenseTensorV1 *tensor);
const size_t *cetta_dense_tensor_v1_shape(const CettaDenseTensorV1 *tensor);
CettaDenseTensorV1DType cetta_dense_tensor_v1_dtype(const CettaDenseTensorV1 *tensor);
const void *cetta_dense_tensor_v1_data(const CettaDenseTensorV1 *tensor);
bool cetta_dense_tensor_v1_equal(const CettaDenseTensorV1 *left, const CettaDenseTensorV1 *right);
int cetta_dense_tensor_v1_compare(const CettaDenseTensorV1 *left, const CettaDenseTensorV1 *right);
uint32_t cetta_dense_tensor_v1_hash(const CettaDenseTensorV1 *tensor);
bool cetta_dense_tensor_v1_shares_storage(const CettaDenseTensorV1 *left, const CettaDenseTensorV1 *right);
CettaDenseTensorV1Status cetta_dense_tensor_v1_reshape(const CettaDenseTensorV1 *tensor,
    const size_t *shape, size_t rank, CettaDenseTensorV1 **out);
CettaDenseTensorV1Status cetta_dense_tensor_v1_copy(const CettaDenseTensorV1 *tensor, CettaDenseTensorV1 **out);
CettaDenseTensorV1Status cetta_dense_tensor_v1_cast(const CettaDenseTensorV1 *tensor,
    CettaDenseTensorV1DType dtype, CettaDenseTensorV1 **out);
CettaDenseTensorV1Status cetta_dense_tensor_v1_with(const CettaDenseTensorV1 *tensor, size_t index,
    const void *element, CettaDenseTensorV1 **out);
CettaDenseTensorV1Status cetta_dense_tensor_v1_binary(const CettaDenseTensorV1 *left,
    const CettaDenseTensorV1 *right, bool multiply, CettaDenseTensorV1 **out);
typedef struct {
    CettaDenseTensorV1DType dtype;
    union { int64_t integer; double floating; } value;
} CettaDenseTensorV1Scalar;
/* With a right operand, this is a rank-one dot; otherwise an ordered sum. */
CettaDenseTensorV1Status cetta_dense_tensor_v1_reduce(const CettaDenseTensorV1 *left,
    const CettaDenseTensorV1 *right, CettaDenseTensorV1Scalar *out);
/* Axis-zero gather preserves order and repeated indices. Selectors are i64. */
CettaDenseTensorV1Status cetta_dense_tensor_v1_gather(const CettaDenseTensorV1 *source,
    const CettaDenseTensorV1 *indices, CettaDenseTensorV1 **out);
/* Dense f64/f32 matrix with an f64 query, accumulation and result. NULL indices
 * selects all rows. Four ordered
 * accumulation lanes are combined in a fixed order, without FMA contraction.
 * This is a different reduction order from tensor:dot's scalar reference. */
CettaDenseTensorV1Status cetta_dense_tensor_v1_matvec(const CettaDenseTensorV1 *matrix,
    const CettaDenseTensorV1 *query, const CettaDenseTensorV1 *indices, CettaDenseTensorV1 **out);
typedef enum {
    CETTA_MATVEC_AUTO,
    CETTA_MATVEC_SCALAR4,
    CETTA_MATVEC_AVX4,
} CettaDenseTensorV1MatvecProvider;
bool cetta_dense_tensor_v1_matvec_provider_available(CettaDenseTensorV1MatvecProvider provider);
CettaDenseTensorV1Status cetta_dense_tensor_v1_matvec_with_provider(const CettaDenseTensorV1 *matrix,
    const CettaDenseTensorV1 *query, const CettaDenseTensorV1 *indices,
    CettaDenseTensorV1MatvecProvider provider, CettaDenseTensorV1 **out);
CettaDenseTensorV1Status cetta_dense_tensor_v1_div(const CettaDenseTensorV1 *left,
    const CettaDenseTensorV1 *right, CettaDenseTensorV1 **out);
CettaDenseTensorV1Status cetta_dense_tensor_v1_scale(const CettaDenseTensorV1 *source, double factor,
    CettaDenseTensorV1 **out);
/* Boolean masks use u8 entries restricted to 0 and 1. */
CettaDenseTensorV1Status cetta_dense_tensor_v1_mask_indices(size_t length,
    const CettaDenseTensorV1 *indices, CettaDenseTensorV1 **out);
CettaDenseTensorV1Status cetta_dense_tensor_v1_mask_binary(const CettaDenseTensorV1 *left,
    const CettaDenseTensorV1 *right, bool is_or, CettaDenseTensorV1 **out);
CettaDenseTensorV1Status cetta_dense_tensor_v1_mask_not(const CettaDenseTensorV1 *mask, CettaDenseTensorV1 **out);
CettaDenseTensorV1Status cetta_dense_tensor_v1_nonzero(const CettaDenseTensorV1 *mask, CettaDenseTensorV1 **out);
/* Sorted descending, with smaller source indices first at equal scores.
 * Enabled nonfinite scores are refused, including when k is zero. */
CettaDenseTensorV1Status cetta_dense_tensor_v1_top_k(const CettaDenseTensorV1 *scores,
    const CettaDenseTensorV1 *mask, size_t k, CettaDenseTensorV1 **out);
/* Derived row certificate: [L1 conversion error upper bound, combined L1
 * magnitude upper bound]. Must belong to this exact source/compact pair. */
CettaDenseTensorV1Status cetta_dense_tensor_v1_row_error_bounds(const CettaDenseTensorV1 *source,
    const CettaDenseTensorV1 *compact, CettaDenseTensorV1 **out);
/* Conservative bounds on the existing f64 lane4 score times positive scale.
 * A rounding environment outside the admitted fragment yields [-inf, inf],
 * retaining every row. NULL indices selects all rows. */
CettaDenseTensorV1Status cetta_dense_tensor_v1_matvec_bounds(const CettaDenseTensorV1 *compact,
    const CettaDenseTensorV1 *query, const CettaDenseTensorV1 *certificate,
    const CettaDenseTensorV1 *indices, double scale, CettaDenseTensorV1 **lower, CettaDenseTensorV1 **upper);
/* Keep every upper bound reaching the k-th largest lower bound; ties survive.
 * This returns a candidate superset, never a final approximate ranking. */
CettaDenseTensorV1Status cetta_dense_tensor_v1_top_k_candidates(const CettaDenseTensorV1 *lower,
    const CettaDenseTensorV1 *upper, size_t k, CettaDenseTensorV1 **out);
/* CTN1: dtype:u8, rank:u32 LE, dimensions:u64 LE, ordered element bits LE.
 * Encoding is pointer-free, lossless and portable for the supported dtypes. */
CettaDenseTensorV1Status cetta_dense_tensor_v1_encoded_size(const CettaDenseTensorV1 *tensor, size_t *size);
CettaDenseTensorV1Status cetta_dense_tensor_v1_encode(const CettaDenseTensorV1 *tensor,
    unsigned char **bytes, size_t *size);
CettaDenseTensorV1Status cetta_dense_tensor_v1_decode(const unsigned char *bytes,
    size_t size, CettaDenseTensorV1 **out);
void cetta_dense_tensor_v1_print(const CettaDenseTensorV1 *tensor, FILE *out);
Atom *cetta_dense_tensor_v1_dispatch(Arena *arena, Atom *head, Atom **args, uint32_t nargs);

#endif
