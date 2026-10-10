#include "tensor.h"
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#include <immintrin.h>
#define CETTA_HAS_AVX4 1
#else
#define CETTA_HAS_AVX4 0
#endif

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize ("fp-contract=off")
#endif

_Static_assert(sizeof(double) == 8 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024,
               "native f64 tensors require binary64");

typedef struct {
    _Atomic size_t holds;
    size_t bytes;
    uint32_t hash;
    unsigned char *data;
} TensorStorage;

struct CettaTensor {
    _Atomic size_t holds;
    TensorStorage *storage;
    CettaTensorDType dtype;
    size_t rank, count;
    uint32_t hash;
    size_t shape[];
};

const char *cetta_tensor_status_name(CettaTensorStatus status) {
    switch (status) {
    case CETTA_TENSOR_OK: return "ok";
    case CETTA_TENSOR_DTYPE: return "unsupported-dtype";
    case CETTA_TENSOR_SHAPE: return "shape-mismatch";
    case CETTA_TENSOR_COUNT: return "element-count-mismatch";
    case CETTA_TENSOR_OVERFLOW: return "integer-overflow";
    case CETTA_TENSOR_NOMEM: return "allocation-failure";
    case CETTA_TENSOR_INDEX: return "index-out-of-bounds";
    case CETTA_TENSOR_VALUE: return "invalid-value";
    case CETTA_TENSOR_ENCODING: return "invalid-encoding";
    case CETTA_TENSOR_PROVIDER: return "unsupported-provider";
    }
    return "invalid-status";
}

const char *cetta_tensor_dtype_name(CettaTensorDType dtype) {
    switch (dtype) {
    case CETTA_TENSOR_I64: return "i64";
    case CETTA_TENSOR_F64: return "f64";
    case CETTA_TENSOR_U8: return "u8";
    }
    return "unsupported";
}

size_t cetta_tensor_itemsize(CettaTensorDType dtype) {
    switch (dtype) {
    case CETTA_TENSOR_I64: case CETTA_TENSOR_F64: return 8u;
    case CETTA_TENSOR_U8: return 1u;
    }
    return 0u;
}

CettaTensorStatus cetta_tensor_byte_count(CettaTensorDType dtype,
    const size_t *shape, size_t rank, size_t *count, size_t *bytes) {
    if (!count || !bytes || (rank && !shape)) return CETTA_TENSOR_VALUE;
    size_t width = cetta_tensor_itemsize(dtype);
    if (!width) return CETTA_TENSOR_DTYPE;
    for (size_t i = 0; i < rank; ++i) {
        if (!shape[i]) { *count = 0u; *bytes = 0u; return CETTA_TENSOR_OK; }
    }
    size_t n = 1u;
    for (size_t i = 0; i < rank; ++i) {
        if (n > SIZE_MAX / shape[i]) return CETTA_TENSOR_OVERFLOW;
        n *= shape[i];
    }
    if (n > SIZE_MAX / width) return CETTA_TENSOR_OVERFLOW;
    *count = n; *bytes = n * width;
    return CETTA_TENSOR_OK;
}

static uint32_t hash_byte(uint32_t hash, unsigned char byte) {
    return (hash ^ byte) * UINT32_C(16777619);
}

static uint32_t hash_word(uint32_t hash, uint64_t bits) {
    for (unsigned i = 0; i < 8u; ++i) hash = hash_byte(hash, (unsigned char)(bits >> (i * 8u)));
    return hash;
}

static uint32_t storage_hash(CettaTensorDType dtype, const TensorStorage *storage) {
    uint32_t hash = UINT32_C(2166136261);
    if (dtype == CETTA_TENSOR_U8) {
        for (size_t i = 0; i < storage->bytes; ++i) hash = hash_byte(hash, storage->data[i]);
    } else {
        for (size_t i = 0; i < storage->bytes; i += 8u) {
            uint64_t bits;
            memcpy(&bits, storage->data + i, 8u);
            hash = hash_word(hash, bits);
        }
    }
    return hash;
}

static void finish_hash(CettaTensor *tensor) {
    uint32_t hash = hash_word(tensor->storage->hash, tensor->dtype);
    hash = hash_word(hash, tensor->rank);
    for (size_t i = 0; i < tensor->rank; ++i) hash = hash_word(hash, tensor->shape[i]);
    tensor->hash = hash;
}

static CettaTensorStatus descriptor(TensorStorage *storage, CettaTensorDType dtype,
    const size_t *shape, size_t rank, size_t count, CettaTensor **out) {
    if (rank > (SIZE_MAX - sizeof(CettaTensor)) / sizeof(size_t)) return CETTA_TENSOR_OVERFLOW;
    CettaTensor *tensor = malloc(sizeof(*tensor) + rank * sizeof(size_t));
    if (!tensor) return CETTA_TENSOR_NOMEM;
    atomic_init(&tensor->holds, 1u);
    tensor->storage = storage; tensor->dtype = dtype;
    tensor->rank = rank; tensor->count = count;
    if (rank) memcpy(tensor->shape, shape, rank * sizeof(size_t));
    finish_hash(tensor);
    *out = tensor;
    return CETTA_TENSOR_OK;
}

/* Unpublished allocations may be filled privately; publication exposes const data. */
static CettaTensorStatus allocate(CettaTensorDType dtype, const size_t *shape,
    size_t rank, CettaTensor **out) {
    if (!out) return CETTA_TENSOR_VALUE;
    *out = NULL;
    size_t count, bytes;
    CettaTensorStatus status = cetta_tensor_byte_count(dtype, shape, rank, &count, &bytes);
    if (status != CETTA_TENSOR_OK) return status;
    if (rank > (SIZE_MAX - sizeof(CettaTensor)) / sizeof(size_t)) return CETTA_TENSOR_OVERFLOW;
    TensorStorage *storage = calloc(1u, sizeof(*storage));
    if (!storage) return CETTA_TENSOR_NOMEM;
    atomic_init(&storage->holds, 1u); storage->bytes = bytes;
    if (bytes) {
        storage->data = malloc(bytes);
        if (!storage->data) { free(storage); return CETTA_TENSOR_NOMEM; }
    }
    status = descriptor(storage, dtype, shape, rank, count, out);
    if (status != CETTA_TENSOR_OK) { free(storage->data); free(storage); }
    return status;
}

static void finish_storage(CettaTensor *tensor) {
    tensor->storage->hash = storage_hash(tensor->dtype, tensor->storage);
    finish_hash(tensor);
}

CettaTensorStatus cetta_tensor_new(CettaTensorDType dtype, const size_t *shape,
    size_t rank, const void *data, size_t bytes, CettaTensor **out) {
    if (!out) return CETTA_TENSOR_VALUE;
    *out = NULL;
    size_t count, required;
    CettaTensorStatus status = cetta_tensor_byte_count(dtype, shape, rank, &count, &required);
    if (status != CETTA_TENSOR_OK) return status;
    if (bytes != required) return CETTA_TENSOR_COUNT;
    if (bytes && !data) return CETTA_TENSOR_VALUE;
    status = allocate(dtype, shape, rank, out);
    if (status != CETTA_TENSOR_OK) return status;
    if (bytes) memcpy((*out)->storage->data, data, bytes);
    finish_storage(*out);
    return CETTA_TENSOR_OK;
}

void cetta_tensor_retain(void *value) {
    CettaTensor *tensor = value;
    if (tensor) {
        size_t old = atomic_fetch_add_explicit(&tensor->holds, 1u, memory_order_relaxed);
        if (!old || old == SIZE_MAX) abort();
    }
}

void cetta_tensor_release(void *value) {
    CettaTensor *tensor = value;
    if (!tensor) return;
    size_t old = atomic_fetch_sub_explicit(&tensor->holds, 1u, memory_order_acq_rel);
    if (!old) abort();
    if (old != 1u) return;
    TensorStorage *storage = tensor->storage;
    free(tensor);
    old = atomic_fetch_sub_explicit(&storage->holds, 1u, memory_order_acq_rel);
    if (!old) abort();
    if (old == 1u) { free(storage->data); free(storage); }
}

size_t cetta_tensor_retained_bytes(const CettaTensor *tensor) {
    if (!tensor) return 0u;
    size_t metadata = sizeof(*tensor) + tensor->rank * sizeof(size_t);
    if (metadata > SIZE_MAX - sizeof(TensorStorage)) return SIZE_MAX;
    metadata += sizeof(TensorStorage);
    if (metadata > SIZE_MAX - tensor->storage->bytes) return SIZE_MAX;
    return metadata + tensor->storage->bytes;
}

size_t cetta_tensor_rank(const CettaTensor *tensor) { return tensor->rank; }
size_t cetta_tensor_count(const CettaTensor *tensor) { return tensor->count; }
size_t cetta_tensor_nbytes(const CettaTensor *tensor) { return tensor->storage->bytes; }
const size_t *cetta_tensor_shape(const CettaTensor *tensor) { return tensor->shape; }
CettaTensorDType cetta_tensor_dtype(const CettaTensor *tensor) { return tensor->dtype; }
const void *cetta_tensor_data(const CettaTensor *tensor) { return tensor->storage->data; }
uint32_t cetta_tensor_hash(const CettaTensor *tensor) { return tensor ? tensor->hash : 0u; }
bool cetta_tensor_shares_storage(const CettaTensor *left, const CettaTensor *right) {
    return left && right && left->storage == right->storage;
}

static bool same_shape(const CettaTensor *left, const CettaTensor *right) {
    return left->rank == right->rank && (!left->rank ||
        memcmp(left->shape, right->shape, left->rank * sizeof(size_t)) == 0);
}

bool cetta_tensor_equal(const CettaTensor *left, const CettaTensor *right) {
    if (left == right) return true;
    return left && right && left->dtype == right->dtype && left->hash == right->hash &&
        same_shape(left, right) && (!left->storage->bytes ||
        memcmp(left->storage->data, right->storage->data, left->storage->bytes) == 0);
}

int cetta_tensor_compare(const CettaTensor *left, const CettaTensor *right) {
    if (left == right) return 0;
    if (!left || !right) return left ? 1 : -1;
    if (left->dtype != right->dtype) return left->dtype < right->dtype ? -1 : 1;
    if (left->rank != right->rank) return left->rank < right->rank ? -1 : 1;
    for (size_t i = 0; i < left->rank; ++i) {
        if (left->shape[i] != right->shape[i]) return left->shape[i] < right->shape[i] ? -1 : 1;
    }
    if (!left->storage->bytes) return 0;
    if (left->dtype == CETTA_TENSOR_U8)
        return memcmp(left->storage->data, right->storage->data, left->storage->bytes);
    for (size_t i = 0; i < left->count; ++i) {
        uint64_t a, b;
        memcpy(&a, left->storage->data + i * 8u, 8u);
        memcpy(&b, right->storage->data + i * 8u, 8u);
        if (a != b) return a < b ? -1 : 1;
    }
    return 0;
}

CettaTensorStatus cetta_tensor_reshape(const CettaTensor *tensor,
    const size_t *shape, size_t rank, CettaTensor **out) {
    if (!out || !tensor) return CETTA_TENSOR_VALUE;
    *out = NULL;
    size_t count, bytes;
    CettaTensorStatus status = cetta_tensor_byte_count(tensor->dtype, shape, rank, &count, &bytes);
    if (status != CETTA_TENSOR_OK) return status;
    if (count != tensor->count) return CETTA_TENSOR_COUNT;
    status = descriptor(tensor->storage, tensor->dtype, shape, rank, count, out);
    if (status == CETTA_TENSOR_OK) {
        size_t old = atomic_fetch_add_explicit(&tensor->storage->holds, 1u, memory_order_relaxed);
        if (!old || old == SIZE_MAX) abort();
    }
    return status;
}

CettaTensorStatus cetta_tensor_copy(const CettaTensor *tensor, CettaTensor **out) {
    if (!tensor) return CETTA_TENSOR_VALUE;
    return cetta_tensor_new(tensor->dtype, tensor->shape, tensor->rank,
        tensor->storage->data, tensor->storage->bytes, out);
}

CettaTensorStatus cetta_tensor_with(const CettaTensor *tensor, size_t index,
    const void *element, CettaTensor **out) {
    if (!out || !tensor) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (index >= tensor->count) return CETTA_TENSOR_INDEX;
    if (!element) return CETTA_TENSOR_VALUE;
    CettaTensorStatus status = cetta_tensor_copy(tensor, out);
    if (status != CETTA_TENSOR_OK) return status;
    size_t width = cetta_tensor_itemsize(tensor->dtype);
    memcpy((*out)->storage->data + index * width, element, width);
    finish_storage(*out);
    return CETTA_TENSOR_OK;
}

CettaTensorStatus cetta_tensor_binary(const CettaTensor *left,
    const CettaTensor *right, bool multiply, CettaTensor **out) {
    if (!out || !left || !right) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (left->dtype != right->dtype) return CETTA_TENSOR_DTYPE;
    if (!same_shape(left, right)) return CETTA_TENSOR_SHAPE;
    CettaTensorStatus status = allocate(left->dtype, left->shape, left->rank, out);
    if (status != CETTA_TENSOR_OK) return status;
    for (size_t i = 0; i < left->count; ++i) {
        if (left->dtype == CETTA_TENSOR_F64) {
            double a, b, value;
            memcpy(&a, left->storage->data + i * 8u, 8u);
            memcpy(&b, right->storage->data + i * 8u, 8u);
            value = multiply ? a * b : a + b;
            memcpy((*out)->storage->data + i * 8u, &value, 8u);
        } else if (left->dtype == CETTA_TENSOR_I64) {
            int64_t a, b, value;
            memcpy(&a, left->storage->data + i * 8u, 8u);
            memcpy(&b, right->storage->data + i * 8u, 8u);
            if (multiply ? __builtin_mul_overflow(a, b, &value) :
                           __builtin_add_overflow(a, b, &value)) {
                cetta_tensor_release(*out); *out = NULL; return CETTA_TENSOR_OVERFLOW;
            }
            memcpy((*out)->storage->data + i * 8u, &value, 8u);
        } else {
            unsigned a = left->storage->data[i], b = right->storage->data[i];
            unsigned value = multiply ? a * b : a + b;
            if (value > UINT8_MAX) {
                cetta_tensor_release(*out); *out = NULL; return CETTA_TENSOR_OVERFLOW;
            }
            (*out)->storage->data[i] = (unsigned char)value;
        }
    }
    finish_storage(*out);
    return CETTA_TENSOR_OK;
}

CettaTensorStatus cetta_tensor_reduce(const CettaTensor *left,
    const CettaTensor *right, CettaTensorScalar *out) {
    if (!left || !out) return CETTA_TENSOR_VALUE;
    if (right) {
        if (left->dtype != right->dtype) return CETTA_TENSOR_DTYPE;
        if (left->rank != 1u || right->rank != 1u || !same_shape(left, right))
            return CETTA_TENSOR_SHAPE;
    }
    out->dtype = left->dtype == CETTA_TENSOR_F64 ? CETTA_TENSOR_F64 : CETTA_TENSOR_I64;
    if (out->dtype == CETTA_TENSOR_F64) {
        double sum = 0.0;
        /* Explicit product and addition: baseline reductions do not permit FMA contraction. */
        for (size_t i = 0; i < left->count; ++i) {
            double a, b = 0.0;
            memcpy(&a, left->storage->data + i * 8u, 8u);
            if (right) memcpy(&b, right->storage->data + i * 8u, 8u);
            volatile double item = right ? a * b : a;
            sum = sum + item;
        }
        out->value.floating = sum;
    } else {
        int64_t sum = 0;
        for (size_t i = 0; i < left->count; ++i) {
            int64_t a, b = 0, item;
            if (left->dtype == CETTA_TENSOR_U8) {
                a = left->storage->data[i];
                if (right) b = right->storage->data[i];
            } else {
                memcpy(&a, left->storage->data + i * 8u, 8u);
                if (right) memcpy(&b, right->storage->data + i * 8u, 8u);
            }
            item = a;
            if ((right && __builtin_mul_overflow(a, b, &item)) ||
                __builtin_add_overflow(sum, item, &sum)) return CETTA_TENSOR_OVERFLOW;
        }
        out->value.integer = sum;
    }
    return CETTA_TENSOR_OK;
}

static bool indices_valid(const CettaTensor *indices, size_t rows) {
    if (!indices || indices->dtype != CETTA_TENSOR_I64 || indices->rank != 1u)
        return false;
    const int64_t *values = (const int64_t *)indices->storage->data;
    for (size_t i = 0; i < indices->count; ++i)
        if (values[i] < 0 || (uint64_t)values[i] >= rows) return false;
    return true;
}

CettaTensorStatus cetta_tensor_gather(const CettaTensor *source,
    const CettaTensor *indices, CettaTensor **out) {
    if (!out || !source || !indices) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (!source->rank || indices->dtype != CETTA_TENSOR_I64 || indices->rank != 1u)
        return CETTA_TENSOR_SHAPE;
    if (!indices_valid(indices, source->shape[0])) return CETTA_TENSOR_INDEX;
    size_t *shape = malloc(source->rank * sizeof(size_t));
    if (!shape) return CETTA_TENSOR_NOMEM;
    memcpy(shape, source->shape, source->rank * sizeof(size_t));
    shape[0] = indices->count;
    CettaTensorStatus status = allocate(source->dtype, shape, source->rank, out);
    free(shape);
    if (status != CETTA_TENSOR_OK) return status;
    size_t stride = source->shape[0] ? source->storage->bytes / source->shape[0] : 0u;
    const int64_t *values = (const int64_t *)indices->storage->data;
    if (stride) for (size_t i = 0; i < indices->count; ++i)
        memcpy((*out)->storage->data + i * stride,
               source->storage->data + (size_t)values[i] * stride, stride);
    finish_storage(*out);
    return CETTA_TENSOR_OK;
}

static double dot4_scalar(const double *v, const double *q, size_t width) {
    double a = 0, b = 0, c = 0, d = 0;
    size_t j = 0;
    for (; width - j >= 4u; j += 4u) {
        a += v[j] * q[j]; b += v[j+1] * q[j+1];
        c += v[j+2] * q[j+2]; d += v[j+3] * q[j+3];
    }
    double total = ((a + b) + c) + d;
    for (; j < width; ++j) total += v[j] * q[j];
    return total;
}

#if CETTA_HAS_AVX4
__attribute__((target("avx")))
static double dot4_avx(const double *v, const double *q, size_t width) {
    __m256d lanes = _mm256_setzero_pd();
    size_t j = 0;
    for (; width - j >= 4u; j += 4u)
        lanes = _mm256_add_pd(lanes, _mm256_mul_pd(_mm256_loadu_pd(v+j), _mm256_loadu_pd(q+j)));
    double sums[4]; _mm256_storeu_pd(sums, lanes);
    double total = ((sums[0] + sums[1]) + sums[2]) + sums[3];
    for (; j < width; ++j) total += v[j] * q[j];
    return total;
}
#endif

bool cetta_tensor_matvec_provider_available(CettaTensorMatvecProvider provider) {
    if (provider == CETTA_MATVEC_AUTO || provider == CETTA_MATVEC_SCALAR4) return true;
#if CETTA_HAS_AVX4
    if (provider == CETTA_MATVEC_AVX4) return __builtin_cpu_supports("avx") != 0;
#endif
    return false;
}

CettaTensorStatus cetta_tensor_matvec(const CettaTensor *matrix,
    const CettaTensor *query, const CettaTensor *indices, CettaTensor **out) {
    return cetta_tensor_matvec_with_provider(matrix, query, indices, CETTA_MATVEC_AUTO, out);
}

CettaTensorStatus cetta_tensor_matvec_with_provider(const CettaTensor *matrix,
    const CettaTensor *query, const CettaTensor *indices,
    CettaTensorMatvecProvider provider, CettaTensor **out) {
    if (!out || !matrix || !query) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (matrix->dtype != CETTA_TENSOR_F64 || query->dtype != CETTA_TENSOR_F64)
        return CETTA_TENSOR_DTYPE;
    if (matrix->rank != 2u || query->rank != 1u || query->count != matrix->shape[1])
        return CETTA_TENSOR_SHAPE;
    if (!cetta_tensor_matvec_provider_available(provider)) return CETTA_TENSOR_PROVIDER;
    double (*dot)(const double *, const double *, size_t) = dot4_scalar;
#if CETTA_HAS_AVX4
    if (provider == CETTA_MATVEC_AVX4 || (provider == CETTA_MATVEC_AUTO &&
        cetta_tensor_matvec_provider_available(CETTA_MATVEC_AVX4))) dot = dot4_avx;
#endif
    size_t rows = matrix->shape[0], width = matrix->shape[1];
    if (indices && !indices_valid(indices, rows)) return CETTA_TENSOR_INDEX;
    size_t count = indices ? indices->count : rows;
    CettaTensorStatus status = allocate(CETTA_TENSOR_F64, &count, 1u, out);
    if (status != CETTA_TENSOR_OK) return status;
    const double *data = (const double *)matrix->storage->data;
    const double *q = (const double *)query->storage->data;
    const int64_t *selected = indices ? (const int64_t *)indices->storage->data : NULL;
    double *result = (double *)(*out)->storage->data;
    for (size_t i = 0; i < count; ++i) {
        size_t row = selected ? (size_t)selected[i] : i;
        /* An empty row never forms a pointer into absent storage. */
        const double *v = width ? data + row * width : NULL;
        result[i] = dot(v, q, width);
    }
    finish_storage(*out);
    return CETTA_TENSOR_OK;
}

CettaTensorStatus cetta_tensor_div(const CettaTensor *left,
    const CettaTensor *right, CettaTensor **out) {
    if (!out || !left || !right) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (left->dtype != CETTA_TENSOR_F64 || right->dtype != CETTA_TENSOR_F64)
        return CETTA_TENSOR_DTYPE;
    if (!same_shape(left, right)) return CETTA_TENSOR_SHAPE;
    CettaTensorStatus status = allocate(left->dtype, left->shape, left->rank, out);
    if (status != CETTA_TENSOR_OK) return status;
    const double *a = (const double *)left->storage->data;
    const double *b = (const double *)right->storage->data;
    double *result = (double *)(*out)->storage->data;
    for (size_t i = 0; i < left->count; ++i) result[i] = a[i] / b[i];
    finish_storage(*out);
    return CETTA_TENSOR_OK;
}

CettaTensorStatus cetta_tensor_scale(const CettaTensor *source, double factor,
    CettaTensor **out) {
    if (!out || !source) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (source->dtype != CETTA_TENSOR_F64) return CETTA_TENSOR_DTYPE;
    CettaTensorStatus status = allocate(source->dtype, source->shape, source->rank, out);
    if (status != CETTA_TENSOR_OK) return status;
    const double *values = (const double *)source->storage->data;
    double *result = (double *)(*out)->storage->data;
    for (size_t i = 0; i < source->count; ++i) result[i] = values[i] * factor;
    finish_storage(*out);
    return CETTA_TENSOR_OK;
}

/* Broadcast one divisor across each axis-zero matrix row. */
static CettaTensorStatus divide_rows(const CettaTensor *matrix,
    const CettaTensor *divisors, CettaTensor **out) {
    if (!out || !matrix || !divisors) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (matrix->dtype != CETTA_TENSOR_F64 || divisors->dtype != CETTA_TENSOR_F64)
        return CETTA_TENSOR_DTYPE;
    if (matrix->rank != 2u || divisors->rank != 1u || divisors->count != matrix->shape[0])
        return CETTA_TENSOR_SHAPE;
    CettaTensorStatus status = allocate(matrix->dtype, matrix->shape, 2u, out);
    if (status != CETTA_TENSOR_OK) return status;
    const double *data = (const double *)matrix->storage->data;
    const double *denoms = (const double *)divisors->storage->data;
    double *result = (double *)(*out)->storage->data;
    size_t width = matrix->shape[1];
    for (size_t i = 0; i < matrix->shape[0]; ++i)
        for (size_t j = 0; j < width; ++j) result[i*width+j] = data[i*width+j] / denoms[i];
    finish_storage(*out);
    return CETTA_TENSOR_OK;
}

static bool mask_valid(const CettaTensor *mask) {
    if (!mask || mask->dtype != CETTA_TENSOR_U8 || mask->rank != 1u) return false;
    for (size_t i = 0; i < mask->count; ++i)
        if (mask->storage->data[i] > 1u) return false;
    return true;
}

CettaTensorStatus cetta_tensor_mask_indices(size_t length,
    const CettaTensor *indices, CettaTensor **out) {
    if (!out || !indices) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (!indices_valid(indices, length)) return CETTA_TENSOR_INDEX;
    CettaTensorStatus status = allocate(CETTA_TENSOR_U8, &length, 1u, out);
    if (status != CETTA_TENSOR_OK) return status;
    if (length) memset((*out)->storage->data, 0, length);
    const int64_t *values = (const int64_t *)indices->storage->data;
    for (size_t i = 0; i < indices->count; ++i) (*out)->storage->data[values[i]] = 1u;
    finish_storage(*out);
    return CETTA_TENSOR_OK;
}

CettaTensorStatus cetta_tensor_mask_binary(const CettaTensor *left,
    const CettaTensor *right, bool is_or, CettaTensor **out) {
    if (!out || !left || !right) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (!same_shape(left, right)) return CETTA_TENSOR_SHAPE;
    if (!mask_valid(left) || !mask_valid(right)) return CETTA_TENSOR_VALUE;
    CettaTensorStatus status = allocate(CETTA_TENSOR_U8, left->shape, 1u, out);
    if (status != CETTA_TENSOR_OK) return status;
    for (size_t i = 0; i < left->count; ++i)
        (*out)->storage->data[i] = is_or ? left->storage->data[i] | right->storage->data[i] :
                                         left->storage->data[i] & right->storage->data[i];
    finish_storage(*out);
    return CETTA_TENSOR_OK;
}

CettaTensorStatus cetta_tensor_mask_not(const CettaTensor *mask, CettaTensor **out) {
    if (!out || !mask) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (!mask_valid(mask)) return CETTA_TENSOR_VALUE;
    CettaTensorStatus status = allocate(CETTA_TENSOR_U8, mask->shape, 1u, out);
    if (status != CETTA_TENSOR_OK) return status;
    for (size_t i = 0; i < mask->count; ++i)
        (*out)->storage->data[i] = !mask->storage->data[i];
    finish_storage(*out);
    return CETTA_TENSOR_OK;
}

CettaTensorStatus cetta_tensor_nonzero(const CettaTensor *mask, CettaTensor **out) {
    if (!out || !mask) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (!mask_valid(mask)) return CETTA_TENSOR_VALUE;
    size_t count = 0;
    for (size_t i = 0; i < mask->count; ++i) count += mask->storage->data[i];
    CettaTensorStatus status = allocate(CETTA_TENSOR_I64, &count, 1u, out);
    if (status != CETTA_TENSOR_OK) return status;
    int64_t *result = (int64_t *)(*out)->storage->data;
    size_t next = 0;
    for (size_t i = 0; i < mask->count; ++i) if (mask->storage->data[i]) {
        if (i > INT64_MAX) { cetta_tensor_release(*out); *out = NULL; return CETTA_TENSOR_OVERFLOW; }
        result[next++] = (int64_t)i;
    }
    finish_storage(*out);
    return CETTA_TENSOR_OK;
}

static bool score_better(const CettaTensor *scores, size_t a, size_t b) {
    if (scores->dtype == CETTA_TENSOR_F64) {
        const double *values = (const double *)scores->storage->data;
        if (values[a] != values[b]) return values[a] > values[b];
    } else if (scores->dtype == CETTA_TENSOR_I64) {
        const int64_t *values = (const int64_t *)scores->storage->data;
        if (values[a] != values[b]) return values[a] > values[b];
    } else {
        const unsigned char *values = scores->storage->data;
        if (values[a] != values[b]) return values[a] > values[b];
    }
    return a < b;
}

/* The root is the worst retained score, with the largest index at a tie. */
static void heap_down(const CettaTensor *scores, size_t *heap, size_t count, size_t root) {
    while (root < count / 2u) {
        size_t child = root * 2u + 1u;
        if (child + 1u < count && score_better(scores, heap[child], heap[child+1])) ++child;
        if (!score_better(scores, heap[root], heap[child])) break;
        size_t old = heap[root]; heap[root] = heap[child]; heap[child] = old;
        root = child;
    }
}

CettaTensorStatus cetta_tensor_top_k(const CettaTensor *scores,
    const CettaTensor *mask, size_t k, CettaTensor **out) {
    if (!out || !scores) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (scores->rank != 1u) return CETTA_TENSOR_SHAPE;
    if (mask && (!same_shape(scores, mask) || !mask_valid(mask))) return CETTA_TENSOR_VALUE;
    if (scores->count > INT64_MAX) return CETTA_TENSOR_OVERFLOW;
    size_t capacity = k < scores->count ? k : scores->count;
    if (capacity > SIZE_MAX / sizeof(size_t)) return CETTA_TENSOR_OVERFLOW;
    size_t *heap = capacity ? malloc(capacity * sizeof(size_t)) : NULL;
    if (capacity && !heap) return CETTA_TENSOR_NOMEM;
    size_t count = 0;
    for (size_t i = 0; i < scores->count; ++i) {
        if (mask && !mask->storage->data[i]) continue;
        if (scores->dtype == CETTA_TENSOR_F64 && !isfinite(((const double *)scores->storage->data)[i])) {
            free(heap); return CETTA_TENSOR_VALUE;
        }
        if (!capacity) continue;
        if (count < capacity) {
            size_t child = count++; heap[child] = i;
            while (child) {
                size_t parent = (child - 1u) / 2u;
                if (!score_better(scores, heap[parent], heap[child])) break;
                size_t old = heap[parent]; heap[parent] = heap[child]; heap[child] = old;
                child = parent;
            }
        } else if (score_better(scores, i, heap[0])) {
            heap[0] = i; heap_down(scores, heap, count, 0);
        }
    }
    CettaTensorStatus status = allocate(CETTA_TENSOR_I64, &count, 1u, out);
    if (status == CETTA_TENSOR_OK) {
        int64_t *result = (int64_t *)(*out)->storage->data;
        for (size_t remaining = count; remaining; --remaining) {
            result[remaining-1] = (int64_t)heap[0];
            heap[0] = heap[remaining-1];
            heap_down(scores, heap, remaining-1, 0);
        }
        finish_storage(*out);
    }
    free(heap);
    return status;
}

static void put_le(unsigned char *dst, uint64_t value, size_t width) {
    for (size_t i = 0; i < width; ++i) dst[i] = (unsigned char)(value >> (8u * i));
}

static uint64_t get_le(const unsigned char *src, size_t width) {
    uint64_t value = 0u;
    for (size_t i = 0; i < width; ++i) value |= (uint64_t)src[i] << (8u * i);
    return value;
}

CettaTensorStatus cetta_tensor_encoded_size(const CettaTensor *tensor, size_t *size) {
    if (!tensor || !size) return CETTA_TENSOR_VALUE;
    *size = 0u;
    if (tensor->rank > UINT32_MAX || tensor->rank > (SIZE_MAX - 9u) / 8u)
        return CETTA_TENSOR_OVERFLOW;
    size_t prefix = 9u + 8u * tensor->rank;
    if (tensor->storage->bytes > SIZE_MAX - prefix) return CETTA_TENSOR_OVERFLOW;
    *size = prefix + tensor->storage->bytes;
    return CETTA_TENSOR_OK;
}

CettaTensorStatus cetta_tensor_encode(const CettaTensor *tensor,
    unsigned char **bytes, size_t *size) {
    if (!bytes || !size) return CETTA_TENSOR_VALUE;
    *bytes = NULL; *size = 0u;
    size_t required;
    CettaTensorStatus status = cetta_tensor_encoded_size(tensor, &required);
    if (status != CETTA_TENSOR_OK) return status;
    size_t prefix = 9u + 8u * tensor->rank;
    unsigned char *data = malloc(required);
    if (!data) return CETTA_TENSOR_NOMEM;
    memcpy(data, "CTN1", 4u); data[4] = (unsigned char)tensor->dtype;
    put_le(data + 5u, tensor->rank, 4u);
    for (size_t i = 0; i < tensor->rank; ++i) put_le(data + 9u + 8u * i, tensor->shape[i], 8u);
    if (tensor->dtype == CETTA_TENSOR_U8) {
        if (tensor->storage->bytes) memcpy(data + prefix, tensor->storage->data, tensor->storage->bytes);
    } else {
        for (size_t i = 0; i < tensor->count; ++i) {
            uint64_t bits;
            memcpy(&bits, tensor->storage->data + i * 8u, 8u);
            put_le(data + prefix + i * 8u, bits, 8u);
        }
    }
    *bytes = data; *size = required;
    return CETTA_TENSOR_OK;
}

CettaTensorStatus cetta_tensor_decode(const unsigned char *bytes, size_t size,
    CettaTensor **out) {
    if (!out) return CETTA_TENSOR_VALUE;
    *out = NULL;
    if (!bytes || size < 9u || memcmp(bytes, "CTN1", 4u)) return CETTA_TENSOR_ENCODING;
    CettaTensorDType dtype = (CettaTensorDType)bytes[4];
    size_t rank = (size_t)get_le(bytes + 5u, 4u);
    if (rank > (size - 9u) / 8u || rank > SIZE_MAX / sizeof(size_t)) return CETTA_TENSOR_ENCODING;
    size_t *shape = rank ? malloc(rank * sizeof(size_t)) : NULL;
    if (rank && !shape) return CETTA_TENSOR_NOMEM;
    for (size_t i = 0; i < rank; ++i) {
        uint64_t dimension = get_le(bytes + 9u + i * 8u, 8u);
        if (dimension > SIZE_MAX) { free(shape); return CETTA_TENSOR_OVERFLOW; }
        shape[i] = (size_t)dimension;
    }
    size_t count, required, prefix = 9u + rank * 8u;
    CettaTensorStatus status = cetta_tensor_byte_count(dtype, shape, rank, &count, &required);
    if (status == CETTA_TENSOR_OK && required != size - prefix) status = CETTA_TENSOR_ENCODING;
    if (status == CETTA_TENSOR_OK) status = allocate(dtype, shape, rank, out);
    free(shape);
    if (status != CETTA_TENSOR_OK) return status;
    if (dtype == CETTA_TENSOR_U8) {
        if (required) memcpy((*out)->storage->data, bytes + prefix, required);
    } else {
        for (size_t i = 0; i < count; ++i) {
            uint64_t bits = get_le(bytes + prefix + i * 8u, 8u);
            memcpy((*out)->storage->data + i * 8u, &bits, 8u);
        }
    }
    finish_storage(*out);
    return CETTA_TENSOR_OK;
}

void cetta_tensor_print(const CettaTensor *tensor, FILE *out) {
    if (!tensor) { fputs("<invalid-tensor>", out); return; }
    fprintf(out, "<tensor %s (", cetta_tensor_dtype_name(tensor->dtype));
    for (size_t i = 0; i < tensor->rank; ++i) fprintf(out, "%s%zu", i ? " " : "", tensor->shape[i]);
    fprintf(out, ") %zu elements>", tensor->count);
}

static Atom *failure(Arena *arena, CettaTensorStatus status) {
    return atom_expr2(arena, atom_symbol(arena, "TensorFailure"),
        atom_symbol(arena, cetta_tensor_status_name(status)));
}

static Atom *publish(Arena *arena, CettaTensorStatus status, CettaTensor *tensor) {
    if (status != CETTA_TENSOR_OK) return failure(arena, status);
    Atom *atom = atom_tensor(arena, tensor);
    cetta_tensor_release(tensor);
    return atom;
}

static CettaTensor *from_atom(const Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_TENSOR
        ? atom->ground.ptr : NULL;
}

static bool natural(const Atom *atom, size_t *value) {
    if (!atom || atom->kind != ATOM_GROUNDED || atom->ground.gkind != GV_INT ||
        atom->ground.ival < 0 || (uint64_t)atom->ground.ival > SIZE_MAX) return false;
    *value = (size_t)atom->ground.ival;
    return true;
}

static CettaTensorDType dtype_from_atom(const Atom *atom) {
    if (atom_is_symbol((Atom *)atom, "i64")) return CETTA_TENSOR_I64;
    if (atom_is_symbol((Atom *)atom, "f64")) return CETTA_TENSOR_F64;
    if (atom_is_symbol((Atom *)atom, "u8")) return CETTA_TENSOR_U8;
    return (CettaTensorDType)0;
}

static CettaTensorStatus shape_from_atom(const Atom *atom, size_t **shape, size_t *rank) {
    *shape = NULL; *rank = 0;
    if (!atom || atom->kind != ATOM_EXPR) return CETTA_TENSOR_SHAPE;
    size_t skip = atom_is_list(atom) ? 1u : 0u;
    if (atom->expr.len - skip > SIZE_MAX / sizeof(size_t)) return CETTA_TENSOR_OVERFLOW;
    *rank = (size_t)atom->expr.len - skip;
    if (*rank) {
        *shape = malloc(*rank * sizeof(size_t));
        if (!*shape) return CETTA_TENSOR_NOMEM;
    }
    for (size_t i = 0; i < *rank; ++i) {
        if (!natural(atom->expr.elems[i + skip], &(*shape)[i])) {
            free(*shape); *shape = NULL; return CETTA_TENSOR_SHAPE;
        }
    }
    return CETTA_TENSOR_OK;
}

static CettaTensorStatus element_from_atom(CettaTensorDType dtype, const Atom *atom,
    unsigned char *element) {
    if (!atom || atom->kind != ATOM_GROUNDED) return CETTA_TENSOR_VALUE;
    if (dtype == CETTA_TENSOR_F64) {
        double value;
        if (atom->ground.gkind == GV_FLOAT) value = atom->ground.fval;
        else if (atom->ground.gkind == GV_INT) value = (double)atom->ground.ival;
        else return CETTA_TENSOR_VALUE;
        memcpy(element, &value, 8u);
    } else if (dtype == CETTA_TENSOR_I64) {
        if (atom->ground.gkind != GV_INT) return CETTA_TENSOR_VALUE;
        memcpy(element, &atom->ground.ival, 8u);
    } else if (dtype == CETTA_TENSOR_U8) {
        if (atom->ground.gkind != GV_INT || atom->ground.ival < 0 || atom->ground.ival > 255)
            return CETTA_TENSOR_VALUE;
        element[0] = (unsigned char)atom->ground.ival;
    } else return CETTA_TENSOR_DTYPE;
    return CETTA_TENSOR_OK;
}

static Atom *element_atom(Arena *arena, const CettaTensor *tensor, size_t index) {
    if (tensor->dtype == CETTA_TENSOR_F64) {
        double value;
        memcpy(&value, tensor->storage->data + index * 8u, 8u);
        return atom_float(arena, value);
    }
    if (tensor->dtype == CETTA_TENSOR_I64) {
        int64_t value;
        memcpy(&value, tensor->storage->data + index * 8u, 8u);
        return atom_int(arena, value);
    }
    return atom_int(arena, tensor->storage->data[index]);
}

Atom *cetta_tensor_dispatch(Arena *arena, Atom *head, Atom **args, uint32_t nargs) {
    if (!head || head->kind != ATOM_SYMBOL) return NULL;
    CettaTensor *bulk = NULL;
    CettaTensorStatus bulk_status;
    if (atom_is_symbol(head, "__cetta_lib_tensor_matvec_provider")) {
        if (nargs != 1u || !atom_is_symbol(args[0], "auto"))
            return failure(arena, CETTA_TENSOR_PROVIDER);
        return atom_symbol(arena,
            cetta_tensor_matvec_provider_available(CETTA_MATVEC_AVX4) ? "avx4" : "scalar4");
    }
    bool provider_selected = atom_is_symbol(head, "__cetta_lib_tensor_matvec_selected_with");
    if (provider_selected || atom_is_symbol(head, "__cetta_lib_tensor_matvec_with")) {
        if (nargs != (provider_selected ? 4u : 3u)) return failure(arena, CETTA_TENSOR_VALUE);
        CettaTensorMatvecProvider provider;
        if (atom_is_symbol(args[0], "auto")) provider = CETTA_MATVEC_AUTO;
        else if (atom_is_symbol(args[0], "scalar4")) provider = CETTA_MATVEC_SCALAR4;
        else if (atom_is_symbol(args[0], "avx4")) provider = CETTA_MATVEC_AVX4;
        else return failure(arena, CETTA_TENSOR_PROVIDER);
        CettaTensor *indices = provider_selected ? from_atom(args[3]) : NULL;
        if (provider_selected && !indices) return failure(arena, CETTA_TENSOR_VALUE);
        bulk_status = cetta_tensor_matvec_with_provider(from_atom(args[1]), from_atom(args[2]), indices, provider, &bulk);
        return publish(arena, bulk_status, bulk);
    }
    if (atom_is_symbol(head, "__cetta_lib_tensor_gather")) {
        if (nargs != 2u) return failure(arena, CETTA_TENSOR_VALUE);
        bulk_status = cetta_tensor_gather(from_atom(args[0]), from_atom(args[1]), &bulk);
        return publish(arena, bulk_status, bulk);
    }
    bool selected = atom_is_symbol(head, "__cetta_lib_tensor_matvec_selected");
    if (selected || atom_is_symbol(head, "__cetta_lib_tensor_matvec")) {
        if (nargs != (selected ? 3u : 2u)) return failure(arena, CETTA_TENSOR_VALUE);
        CettaTensor *indices = selected ? from_atom(args[2]) : NULL;
        if (selected && !indices) return failure(arena, CETTA_TENSOR_VALUE);
        bulk_status = cetta_tensor_matvec(from_atom(args[0]), from_atom(args[1]), indices, &bulk);
        return publish(arena, bulk_status, bulk);
    }
    if (atom_is_symbol(head, "__cetta_lib_tensor_div")) {
        if (nargs != 2u) return failure(arena, CETTA_TENSOR_VALUE);
        bulk_status = cetta_tensor_div(from_atom(args[0]), from_atom(args[1]), &bulk);
        return publish(arena, bulk_status, bulk);
    }
    if (atom_is_symbol(head, "__cetta_lib_tensor_div_rows")) {
        if (nargs != 2u) return failure(arena, CETTA_TENSOR_VALUE);
        bulk_status = divide_rows(from_atom(args[0]), from_atom(args[1]), &bulk);
        return publish(arena, bulk_status, bulk);
    }
    if (atom_is_symbol(head, "__cetta_lib_tensor_scale")) {
        if (nargs != 2u) return failure(arena, CETTA_TENSOR_VALUE);
        unsigned char element[8];
        bulk_status = element_from_atom(CETTA_TENSOR_F64, args[1], element);
        if (bulk_status != CETTA_TENSOR_OK) return failure(arena, bulk_status);
        double factor; memcpy(&factor, element, sizeof(factor));
        bulk_status = cetta_tensor_scale(from_atom(args[0]), factor, &bulk);
        return publish(arena, bulk_status, bulk);
    }
    if (atom_is_symbol(head, "__cetta_lib_tensor_mask_indices")) {
        size_t length;
        if (nargs != 2u || !natural(args[0], &length)) return failure(arena, CETTA_TENSOR_VALUE);
        bulk_status = cetta_tensor_mask_indices(length, from_atom(args[1]), &bulk);
        return publish(arena, bulk_status, bulk);
    }
    bool mask_or = atom_is_symbol(head, "__cetta_lib_tensor_mask_or");
    if (mask_or || atom_is_symbol(head, "__cetta_lib_tensor_mask_and")) {
        if (nargs != 2u) return failure(arena, CETTA_TENSOR_VALUE);
        bulk_status = cetta_tensor_mask_binary(from_atom(args[0]), from_atom(args[1]), mask_or, &bulk);
        return publish(arena, bulk_status, bulk);
    }
    if (atom_is_symbol(head, "__cetta_lib_tensor_mask_not")) {
        if (nargs != 1u) return failure(arena, CETTA_TENSOR_VALUE);
        bulk_status = cetta_tensor_mask_not(from_atom(args[0]), &bulk);
        return publish(arena, bulk_status, bulk);
    }
    if (atom_is_symbol(head, "__cetta_lib_tensor_nonzero")) {
        if (nargs != 1u) return failure(arena, CETTA_TENSOR_VALUE);
        bulk_status = cetta_tensor_nonzero(from_atom(args[0]), &bulk);
        return publish(arena, bulk_status, bulk);
    }
    bool masked = atom_is_symbol(head, "__cetta_lib_tensor_top_k_masked");
    if (masked || atom_is_symbol(head, "__cetta_lib_tensor_top_k")) {
        size_t k;
        if (nargs != (masked ? 3u : 2u) || !natural(args[nargs-1], &k))
            return failure(arena, CETTA_TENSOR_VALUE);
        CettaTensor *mask = masked ? from_atom(args[1]) : NULL;
        if (masked && !mask) return failure(arena, CETTA_TENSOR_VALUE);
        bulk_status = cetta_tensor_top_k(from_atom(args[0]), mask, k, &bulk);
        return publish(arena, bulk_status, bulk);
    }
    /* Whole-buffer matrix primitives. Collection/profile/filter policy stays
     * in MeTTa. These names use the ordinary dynamic library-op boundary. */
    if (atom_is_symbol(head, "__cetta_lib_tensor_stack")) {
        if (nargs != 3 || args[2]->kind != ATOM_EXPR) return failure(arena,CETTA_TENSOR_VALUE);
        CettaTensorDType dtype=dtype_from_atom(args[0]); size_t width;
        if (!natural(args[1],&width)) return failure(arena,CETTA_TENSOR_SHAPE);
        size_t count=args[2]->expr.len, shape[2]={count,width};
        CettaTensor *matrix=NULL;
        CettaTensorStatus status=allocate(dtype,shape,2,&matrix);
        if (status!=CETTA_TENSOR_OK) return failure(arena,status);
        size_t row_bytes=count ? matrix->storage->bytes/count : 0;
        for (size_t i=0;i<count;++i) {
            CettaTensor *row=from_atom(args[2]->expr.elems[i]);
            if (!row || row->dtype!=dtype || row->rank!=1 || row->count!=width) {
                cetta_tensor_release(matrix); return failure(arena,CETTA_TENSOR_SHAPE);
            }
            if (row_bytes) memcpy(matrix->storage->data+i*row_bytes,row->storage->data,row_bytes);
        }
        finish_storage(matrix); return publish(arena,CETTA_TENSOR_OK,matrix);
    }
    bool row_norms=atom_is_symbol(head,"__cetta_lib_tensor_row_norms");
    bool cosine_rows=atom_is_symbol(head,"__cetta_lib_tensor_cosine_rows");
    if (row_norms || cosine_rows) {
        if (nargs!=(row_norms ? 1u : 3u)) return failure(arena,CETTA_TENSOR_VALUE);
        CettaTensor *matrix=from_atom(args[0]);
        if (!matrix || matrix->dtype!=CETTA_TENSOR_F64 || matrix->rank!=2)
            return failure(arena,CETTA_TENSOR_SHAPE);
        size_t rows=matrix->shape[0], columns=matrix->shape[1];
        CettaTensor *norms=cosine_rows ? from_atom(args[1]) : NULL;
        CettaTensor *query=cosine_rows ? from_atom(args[2]) : NULL;
        double query_norm=1;
        if (cosine_rows) {
            if (!norms || !query || norms->dtype!=CETTA_TENSOR_F64 || query->dtype!=CETTA_TENSOR_F64 ||
                norms->rank!=1 || norms->count!=rows || query->rank!=1 || query->count!=columns)
                return failure(arena,CETTA_TENSOR_SHAPE);
            double total=0;
            for (size_t j=0;j<columns;++j) {
                double v;memcpy(&v,query->storage->data+j*8,8);
                volatile double product=v*v;total+=product;
            }
            query_norm=sqrt(total);
            if (!isfinite(query_norm) || query_norm<=0) return failure(arena,CETTA_TENSOR_VALUE);
        }
        CettaTensor *output=NULL;
        CettaTensorStatus status=allocate(CETTA_TENSOR_F64,&rows,1,&output);
        if (status!=CETTA_TENSOR_OK) return failure(arena,status);
        for (size_t i=0;i<rows;++i) {
            double total=0;
            for (size_t j=0;j<columns;++j) {
                double v,q;
                memcpy(&v,matrix->storage->data+(i*columns+j)*8,8);
                if (cosine_rows) memcpy(&q,query->storage->data+j*8,8);else q=v;
                volatile double product=v*q;total+=product;
            }
            if (cosine_rows) {
                double norm;memcpy(&norm,norms->storage->data+i*8,8);
                if (!isfinite(norm) || norm<=0) { cetta_tensor_release(output);return failure(arena,CETTA_TENSOR_VALUE); }
                total/=norm*query_norm;
            } else total=sqrt(total);
            if (!isfinite(total) || (row_norms && total<=0)) {
                cetta_tensor_release(output);return failure(arena,CETTA_TENSOR_VALUE);
            }
            memcpy(output->storage->data+i*8,&total,8);
        }
        finish_storage(output);return publish(arena,CETTA_TENSOR_OK,output);
    }
    SymbolId id = atom_symbol_current_id(head);
    if (id < g_builtin_syms.tensor_from || id > g_builtin_syms.tensor_decode) return NULL;
    CettaTensor *tensor = NULL, *left = nargs ? from_atom(args[0]) : NULL;
    CettaTensorStatus status = CETTA_TENSOR_VALUE;
    if (id == g_builtin_syms.tensor_from || id == g_builtin_syms.tensor_full) {
        if (nargs != 3u) return failure(arena, CETTA_TENSOR_VALUE);
        CettaTensorDType dtype = dtype_from_atom(args[0]);
        if (!cetta_tensor_itemsize(dtype)) return failure(arena, CETTA_TENSOR_DTYPE);
        size_t *shape, rank;
        status = shape_from_atom(args[1], &shape, &rank);
        if (status != CETTA_TENSOR_OK) return failure(arena, status);
        size_t count = 0, bytes = 0;
        status = cetta_tensor_byte_count(dtype, shape, rank, &count, &bytes);
        size_t skip = atom_is_list(args[2]) ? 1u : 0u;
        if (status == CETTA_TENSOR_OK && id == g_builtin_syms.tensor_from &&
            (args[2]->kind != ATOM_EXPR || args[2]->expr.len - skip != count))
            status = CETTA_TENSOR_COUNT;
        unsigned char element[8];
        if (status == CETTA_TENSOR_OK && id == g_builtin_syms.tensor_full)
            status = element_from_atom(dtype, args[2], element);
        if (status == CETTA_TENSOR_OK) status = allocate(dtype, shape, rank, &tensor);
        free(shape);
        if (status != CETTA_TENSOR_OK) return failure(arena, status);
        size_t width = cetta_tensor_itemsize(dtype);
        for (size_t i = 0; i < count; ++i) {
            if (id == g_builtin_syms.tensor_from)
                status = element_from_atom(dtype, args[2]->expr.elems[i + skip], element);
            if (status != CETTA_TENSOR_OK) { cetta_tensor_release(tensor); return failure(arena, status); }
            memcpy(tensor->storage->data + i * width, element, width);
        }
        finish_storage(tensor);
        return publish(arena, CETTA_TENSOR_OK, tensor);
    }
    if (id == g_builtin_syms.tensor_decode) {
        if (nargs != 1u || args[0]->kind != ATOM_GROUNDED || args[0]->ground.gkind != GV_STRING)
            return failure(arena, CETTA_TENSOR_VALUE);
        status = cetta_tensor_decode((const unsigned char *)args[0]->ground.sval,
            args[0]->ground.slen, &tensor);
        return publish(arena, status, tensor);
    }
    if (!left) return failure(arena, CETTA_TENSOR_VALUE);
    if (id == g_builtin_syms.tensor_shape && nargs == 1u) {
        Atom **dims = arena_alloc(arena, left->rank * sizeof(Atom *));
        for (size_t i = 0; i < left->rank; ++i) {
            if (left->shape[i] > INT64_MAX) return failure(arena, CETTA_TENSOR_OVERFLOW);
            dims[i] = atom_int(arena, (int64_t)left->shape[i]);
        }
        return atom_expr(arena, dims, left->rank);
    }
    if (id == g_builtin_syms.tensor_dtype && nargs == 1u)
        return atom_symbol(arena, cetta_tensor_dtype_name(left->dtype));
    if (id == g_builtin_syms.tensor_to_expr && nargs == 1u) {
        if (left->count > SIZE_MAX / sizeof(Atom *)) return failure(arena, CETTA_TENSOR_OVERFLOW);
        Atom **items = arena_alloc(arena, left->count * sizeof(Atom *));
        for (size_t i = 0; i < left->count; ++i) items[i] = element_atom(arena, left, i);
        return atom_expr(arena, items, left->count);
    }
    if (id == g_builtin_syms.tensor_reshape && nargs == 2u) {
        size_t *shape, rank;
        status = shape_from_atom(args[1], &shape, &rank);
        if (status == CETTA_TENSOR_OK) status = cetta_tensor_reshape(left, shape, rank, &tensor);
        free(shape);
    } else if (id == g_builtin_syms.tensor_copy && nargs == 1u) {
        status = cetta_tensor_copy(left, &tensor);
    } else if (id == g_builtin_syms.tensor_with && nargs == 3u) {
        size_t index;
        if (!natural(args[1], &index)) return failure(arena, CETTA_TENSOR_INDEX);
        unsigned char element[8];
        status = element_from_atom(left->dtype, args[2], element);
        if (status == CETTA_TENSOR_OK) status = cetta_tensor_with(left, index, element, &tensor);
    } else if ((id == g_builtin_syms.tensor_add || id == g_builtin_syms.tensor_mul) && nargs == 2u) {
        status = cetta_tensor_binary(left, from_atom(args[1]), id == g_builtin_syms.tensor_mul, &tensor);
    } else if ((id == g_builtin_syms.tensor_sum && nargs == 1u) ||
               (id == g_builtin_syms.tensor_dot && nargs == 2u)) {
        CettaTensorScalar scalar;
        CettaTensor *right = nargs == 2u ? from_atom(args[1]) : NULL;
        if (nargs == 2u && !right) return failure(arena, CETTA_TENSOR_VALUE);
        status = cetta_tensor_reduce(left, right, &scalar);
        if (status != CETTA_TENSOR_OK) return failure(arena, status);
        return scalar.dtype == CETTA_TENSOR_F64 ? atom_float(arena, scalar.value.floating) :
            atom_int(arena, scalar.value.integer);
    } else if (id == g_builtin_syms.tensor_encode && nargs == 1u) {
        unsigned char *bytes; size_t size;
        status = cetta_tensor_encoded_size(left, &size);
        if (status != CETTA_TENSOR_OK) return failure(arena, status);
        if (size > UINT32_MAX) return failure(arena, CETTA_TENSOR_OVERFLOW);
        status = cetta_tensor_encode(left, &bytes, &size);
        if (status != CETTA_TENSOR_OK) return failure(arena, status);
        Atom *encoded = atom_string_n(arena, (const char *)bytes, size);
        free(bytes);
        return encoded;
    }
    return publish(arena, status, tensor);
}
