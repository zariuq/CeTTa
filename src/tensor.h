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
