#include "tensor_dense_v1.h"
#include <float.h>
#include <fenv.h>
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
_Static_assert(sizeof(float) == 4 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128 && FLT_RADIX == 2,
               "native f32 tensors require binary32");

typedef struct {
    _Atomic size_t holds;
    size_t bytes;
    uint32_t hash;
    unsigned char *data;
} TensorStorage;

struct CettaDenseTensorV1 {
    _Atomic size_t holds;
    TensorStorage *storage;
    CettaDenseTensorV1DType dtype;
    size_t rank, count;
    uint32_t hash;
    size_t shape[];
};

const char *cetta_dense_tensor_v1_status_name(CettaDenseTensorV1Status status) {
    switch (status) {
    case CETTA_DENSE_TENSOR_V1_OK: return "ok";
    case CETTA_DENSE_TENSOR_V1_DTYPE: return "unsupported-dtype";
    case CETTA_DENSE_TENSOR_V1_SHAPE: return "shape-mismatch";
    case CETTA_DENSE_TENSOR_V1_COUNT: return "element-count-mismatch";
    case CETTA_DENSE_TENSOR_V1_OVERFLOW: return "integer-overflow";
    case CETTA_DENSE_TENSOR_V1_NOMEM: return "allocation-failure";
    case CETTA_DENSE_TENSOR_V1_INDEX: return "index-out-of-bounds";
    case CETTA_DENSE_TENSOR_V1_VALUE: return "invalid-value";
    case CETTA_DENSE_TENSOR_V1_ENCODING: return "invalid-encoding";
    case CETTA_DENSE_TENSOR_V1_PROVIDER: return "unsupported-provider";
    }
    return "invalid-status";
}

const char *cetta_dense_tensor_v1_dtype_name(CettaDenseTensorV1DType dtype) {
    switch (dtype) {
    case CETTA_DENSE_TENSOR_V1_I64: return "i64";
    case CETTA_DENSE_TENSOR_V1_F64: return "f64";
    case CETTA_DENSE_TENSOR_V1_U8: return "u8";
    case CETTA_DENSE_TENSOR_V1_F32: return "f32";
    }
    return "unsupported";
}

size_t cetta_dense_tensor_v1_itemsize(CettaDenseTensorV1DType dtype) {
    switch (dtype) {
    case CETTA_DENSE_TENSOR_V1_I64: case CETTA_DENSE_TENSOR_V1_F64: return 8u;
    case CETTA_DENSE_TENSOR_V1_U8: return 1u;
    case CETTA_DENSE_TENSOR_V1_F32: return 4u;
    }
    return 0u;
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_byte_count(CettaDenseTensorV1DType dtype,
    const size_t *shape, size_t rank, size_t *count, size_t *bytes) {
    if (!count || !bytes || (rank && !shape)) return CETTA_DENSE_TENSOR_V1_VALUE;
    size_t width = cetta_dense_tensor_v1_itemsize(dtype);
    if (!width) return CETTA_DENSE_TENSOR_V1_DTYPE;
    for (size_t i = 0; i < rank; ++i) {
        if (!shape[i]) { *count = 0u; *bytes = 0u; return CETTA_DENSE_TENSOR_V1_OK; }
    }
    size_t n = 1u;
    for (size_t i = 0; i < rank; ++i) {
        if (n > SIZE_MAX / shape[i]) return CETTA_DENSE_TENSOR_V1_OVERFLOW;
        n *= shape[i];
    }
    if (n > SIZE_MAX / width) return CETTA_DENSE_TENSOR_V1_OVERFLOW;
    *count = n; *bytes = n * width;
    return CETTA_DENSE_TENSOR_V1_OK;
}

static uint32_t hash_byte(uint32_t hash, unsigned char byte) {
    return (hash ^ byte) * UINT32_C(16777619);
}

static uint32_t hash_word(uint32_t hash, uint64_t bits) {
    for (unsigned i = 0; i < 8u; ++i) hash = hash_byte(hash, (unsigned char)(bits >> (i * 8u)));
    return hash;
}

static uint32_t storage_hash(CettaDenseTensorV1DType dtype, const TensorStorage *storage) {
    uint32_t hash = UINT32_C(2166136261);
    if (dtype == CETTA_DENSE_TENSOR_V1_F32) {
        for (size_t i = 0; i < storage->bytes; i += 4u) {
            uint32_t bits; memcpy(&bits, storage->data + i, 4u);
            for (unsigned j = 0; j < 4u; ++j) hash = hash_byte(hash, (unsigned char)(bits >> (8u*j)));
        }
    } else if (dtype == CETTA_DENSE_TENSOR_V1_U8) {
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

static void finish_hash(CettaDenseTensorV1 *tensor) {
    uint32_t hash = hash_word(tensor->storage->hash, tensor->dtype);
    hash = hash_word(hash, tensor->rank);
    for (size_t i = 0; i < tensor->rank; ++i) hash = hash_word(hash, tensor->shape[i]);
    tensor->hash = hash;
}

static CettaDenseTensorV1Status descriptor(TensorStorage *storage, CettaDenseTensorV1DType dtype,
    const size_t *shape, size_t rank, size_t count, CettaDenseTensorV1 **out) {
    if (rank > (SIZE_MAX - sizeof(CettaDenseTensorV1)) / sizeof(size_t)) return CETTA_DENSE_TENSOR_V1_OVERFLOW;
    CettaDenseTensorV1 *tensor = malloc(sizeof(*tensor) + rank * sizeof(size_t));
    if (!tensor) return CETTA_DENSE_TENSOR_V1_NOMEM;
    atomic_init(&tensor->holds, 1u);
    tensor->storage = storage; tensor->dtype = dtype;
    tensor->rank = rank; tensor->count = count;
    if (rank) memcpy(tensor->shape, shape, rank * sizeof(size_t));
    finish_hash(tensor);
    *out = tensor;
    return CETTA_DENSE_TENSOR_V1_OK;
}

/* Unpublished allocations may be filled privately; publication exposes const data. */
static CettaDenseTensorV1Status allocate(CettaDenseTensorV1DType dtype, const size_t *shape,
    size_t rank, CettaDenseTensorV1 **out) {
    if (!out) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    size_t count, bytes;
    CettaDenseTensorV1Status status = cetta_dense_tensor_v1_byte_count(dtype, shape, rank, &count, &bytes);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    if (rank > (SIZE_MAX - sizeof(CettaDenseTensorV1)) / sizeof(size_t)) return CETTA_DENSE_TENSOR_V1_OVERFLOW;
    TensorStorage *storage = calloc(1u, sizeof(*storage));
    if (!storage) return CETTA_DENSE_TENSOR_V1_NOMEM;
    atomic_init(&storage->holds, 1u); storage->bytes = bytes;
    if (bytes) {
        storage->data = malloc(bytes);
        if (!storage->data) { free(storage); return CETTA_DENSE_TENSOR_V1_NOMEM; }
    }
    status = descriptor(storage, dtype, shape, rank, count, out);
    if (status != CETTA_DENSE_TENSOR_V1_OK) { free(storage->data); free(storage); }
    return status;
}

static void finish_storage(CettaDenseTensorV1 *tensor) {
    tensor->storage->hash = storage_hash(tensor->dtype, tensor->storage);
    finish_hash(tensor);
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_new(CettaDenseTensorV1DType dtype, const size_t *shape,
    size_t rank, const void *data, size_t bytes, CettaDenseTensorV1 **out) {
    if (!out) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    size_t count, required;
    CettaDenseTensorV1Status status = cetta_dense_tensor_v1_byte_count(dtype, shape, rank, &count, &required);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    if (bytes != required) return CETTA_DENSE_TENSOR_V1_COUNT;
    if (bytes && !data) return CETTA_DENSE_TENSOR_V1_VALUE;
    status = allocate(dtype, shape, rank, out);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    if (bytes) memcpy((*out)->storage->data, data, bytes);
    finish_storage(*out);
    return CETTA_DENSE_TENSOR_V1_OK;
}

void cetta_dense_tensor_v1_retain(void *value) {
    CettaDenseTensorV1 *tensor = value;
    if (tensor) {
        size_t old = atomic_fetch_add_explicit(&tensor->holds, 1u, memory_order_relaxed);
        if (!old || old == SIZE_MAX) abort();
    }
}

void cetta_dense_tensor_v1_release(void *value) {
    CettaDenseTensorV1 *tensor = value;
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

size_t cetta_dense_tensor_v1_retained_bytes(const CettaDenseTensorV1 *tensor) {
    if (!tensor) return 0u;
    size_t metadata = sizeof(*tensor) + tensor->rank * sizeof(size_t);
    if (metadata > SIZE_MAX - sizeof(TensorStorage)) return SIZE_MAX;
    metadata += sizeof(TensorStorage);
    if (metadata > SIZE_MAX - tensor->storage->bytes) return SIZE_MAX;
    return metadata + tensor->storage->bytes;
}

size_t cetta_dense_tensor_v1_rank(const CettaDenseTensorV1 *tensor) { return tensor->rank; }
size_t cetta_dense_tensor_v1_count(const CettaDenseTensorV1 *tensor) { return tensor->count; }
size_t cetta_dense_tensor_v1_nbytes(const CettaDenseTensorV1 *tensor) { return tensor->storage->bytes; }
const size_t *cetta_dense_tensor_v1_shape(const CettaDenseTensorV1 *tensor) { return tensor->shape; }
CettaDenseTensorV1DType cetta_dense_tensor_v1_dtype(const CettaDenseTensorV1 *tensor) { return tensor->dtype; }
const void *cetta_dense_tensor_v1_data(const CettaDenseTensorV1 *tensor) { return tensor->storage->data; }
uint32_t cetta_dense_tensor_v1_hash(const CettaDenseTensorV1 *tensor) { return tensor ? tensor->hash : 0u; }
bool cetta_dense_tensor_v1_shares_storage(const CettaDenseTensorV1 *left, const CettaDenseTensorV1 *right) {
    return left && right && left->storage == right->storage;
}

static bool same_shape(const CettaDenseTensorV1 *left, const CettaDenseTensorV1 *right) {
    return left->rank == right->rank && (!left->rank ||
        memcmp(left->shape, right->shape, left->rank * sizeof(size_t)) == 0);
}

bool cetta_dense_tensor_v1_equal(const CettaDenseTensorV1 *left, const CettaDenseTensorV1 *right) {
    if (left == right) return true;
    return left && right && left->dtype == right->dtype && left->hash == right->hash &&
        same_shape(left, right) && (!left->storage->bytes ||
        memcmp(left->storage->data, right->storage->data, left->storage->bytes) == 0);
}

int cetta_dense_tensor_v1_compare(const CettaDenseTensorV1 *left, const CettaDenseTensorV1 *right) {
    if (left == right) return 0;
    if (!left || !right) return left ? 1 : -1;
    if (left->dtype != right->dtype) return left->dtype < right->dtype ? -1 : 1;
    if (left->rank != right->rank) return left->rank < right->rank ? -1 : 1;
    for (size_t i = 0; i < left->rank; ++i) {
        if (left->shape[i] != right->shape[i]) return left->shape[i] < right->shape[i] ? -1 : 1;
    }
    if (!left->storage->bytes) return 0;
    if (left->dtype == CETTA_DENSE_TENSOR_V1_U8)
        return memcmp(left->storage->data, right->storage->data, left->storage->bytes);
    if (left->dtype == CETTA_DENSE_TENSOR_V1_F32) {
        for (size_t i = 0; i < left->count; ++i) {
            uint32_t a, b;
            memcpy(&a, left->storage->data + i*4u, 4u);
            memcpy(&b, right->storage->data + i*4u, 4u);
            if (a != b) return a < b ? -1 : 1;
        }
        return 0;
    }
    for (size_t i = 0; i < left->count; ++i) {
        uint64_t a, b;
        memcpy(&a, left->storage->data + i * 8u, 8u);
        memcpy(&b, right->storage->data + i * 8u, 8u);
        if (a != b) return a < b ? -1 : 1;
    }
    return 0;
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_reshape(const CettaDenseTensorV1 *tensor,
    const size_t *shape, size_t rank, CettaDenseTensorV1 **out) {
    if (!out || !tensor) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    size_t count, bytes;
    CettaDenseTensorV1Status status = cetta_dense_tensor_v1_byte_count(tensor->dtype, shape, rank, &count, &bytes);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    if (count != tensor->count) return CETTA_DENSE_TENSOR_V1_COUNT;
    status = descriptor(tensor->storage, tensor->dtype, shape, rank, count, out);
    if (status == CETTA_DENSE_TENSOR_V1_OK) {
        size_t old = atomic_fetch_add_explicit(&tensor->storage->holds, 1u, memory_order_relaxed);
        if (!old || old == SIZE_MAX) abort();
    }
    return status;
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_copy(const CettaDenseTensorV1 *tensor, CettaDenseTensorV1 **out) {
    if (!tensor) return CETTA_DENSE_TENSOR_V1_VALUE;
    return cetta_dense_tensor_v1_new(tensor->dtype, tensor->shape, tensor->rank,
        tensor->storage->data, tensor->storage->bytes, out);
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_with(const CettaDenseTensorV1 *tensor, size_t index,
    const void *element, CettaDenseTensorV1 **out) {
    if (!out || !tensor) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    if (index >= tensor->count) return CETTA_DENSE_TENSOR_V1_INDEX;
    if (!element) return CETTA_DENSE_TENSOR_V1_VALUE;
    CettaDenseTensorV1Status status = cetta_dense_tensor_v1_copy(tensor, out);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    size_t width = cetta_dense_tensor_v1_itemsize(tensor->dtype);
    memcpy((*out)->storage->data + index * width, element, width);
    finish_storage(*out);
    return CETTA_DENSE_TENSOR_V1_OK;
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_binary(const CettaDenseTensorV1 *left,
    const CettaDenseTensorV1 *right, bool multiply, CettaDenseTensorV1 **out) {
    if (!out || !left || !right) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    if (left->dtype != right->dtype) return CETTA_DENSE_TENSOR_V1_DTYPE;
    if (!same_shape(left, right)) return CETTA_DENSE_TENSOR_V1_SHAPE;
    CettaDenseTensorV1Status status = allocate(left->dtype, left->shape, left->rank, out);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    for (size_t i = 0; i < left->count; ++i) {
        if (left->dtype == CETTA_DENSE_TENSOR_V1_F64) {
            double a, b, value;
            memcpy(&a, left->storage->data + i * 8u, 8u);
            memcpy(&b, right->storage->data + i * 8u, 8u);
            value = multiply ? a * b : a + b;
            memcpy((*out)->storage->data + i * 8u, &value, 8u);
        } else if (left->dtype == CETTA_DENSE_TENSOR_V1_F32) {
            float a, b, value;
            memcpy(&a, left->storage->data + i * 4u, 4u);
            memcpy(&b, right->storage->data + i * 4u, 4u);
            value = multiply ? a * b : a + b;
            memcpy((*out)->storage->data + i * 4u, &value, 4u);
        } else if (left->dtype == CETTA_DENSE_TENSOR_V1_I64) {
            int64_t a, b, value;
            memcpy(&a, left->storage->data + i * 8u, 8u);
            memcpy(&b, right->storage->data + i * 8u, 8u);
            if (multiply ? __builtin_mul_overflow(a, b, &value) :
                           __builtin_add_overflow(a, b, &value)) {
                cetta_dense_tensor_v1_release(*out); *out = NULL; return CETTA_DENSE_TENSOR_V1_OVERFLOW;
            }
            memcpy((*out)->storage->data + i * 8u, &value, 8u);
        } else {
            unsigned a = left->storage->data[i], b = right->storage->data[i];
            unsigned value = multiply ? a * b : a + b;
            if (value > UINT8_MAX) {
                cetta_dense_tensor_v1_release(*out); *out = NULL; return CETTA_DENSE_TENSOR_V1_OVERFLOW;
            }
            (*out)->storage->data[i] = (unsigned char)value;
        }
    }
    finish_storage(*out);
    return CETTA_DENSE_TENSOR_V1_OK;
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_reduce(const CettaDenseTensorV1 *left,
    const CettaDenseTensorV1 *right, CettaDenseTensorV1Scalar *out) {
    if (!left || !out) return CETTA_DENSE_TENSOR_V1_VALUE;
    if (right) {
        if (left->dtype != right->dtype) return CETTA_DENSE_TENSOR_V1_DTYPE;
        if (left->rank != 1u || right->rank != 1u || !same_shape(left, right))
            return CETTA_DENSE_TENSOR_V1_SHAPE;
    }
    out->dtype = (left->dtype == CETTA_DENSE_TENSOR_V1_F64 || left->dtype == CETTA_DENSE_TENSOR_V1_F32)
        ? CETTA_DENSE_TENSOR_V1_F64 : CETTA_DENSE_TENSOR_V1_I64;
    if (out->dtype == CETTA_DENSE_TENSOR_V1_F64) {
        double sum = 0.0;
        /* Explicit product and addition: baseline reductions do not permit FMA contraction. */
        for (size_t i = 0; i < left->count; ++i) {
            double a, b = 0.0;
            if (left->dtype == CETTA_DENSE_TENSOR_V1_F32) {
                float value; memcpy(&value, left->storage->data + i * 4u, 4u); a = value;
                if (right) { memcpy(&value, right->storage->data + i * 4u, 4u); b = value; }
            } else {
                memcpy(&a, left->storage->data + i * 8u, 8u);
                if (right) memcpy(&b, right->storage->data + i * 8u, 8u);
            }
            volatile double item = right ? a * b : a;
            sum = sum + item;
        }
        out->value.floating = sum;
    } else {
        int64_t sum = 0;
        for (size_t i = 0; i < left->count; ++i) {
            int64_t a, b = 0, item;
            if (left->dtype == CETTA_DENSE_TENSOR_V1_U8) {
                a = left->storage->data[i];
                if (right) b = right->storage->data[i];
            } else {
                memcpy(&a, left->storage->data + i * 8u, 8u);
                if (right) memcpy(&b, right->storage->data + i * 8u, 8u);
            }
            item = a;
            if ((right && __builtin_mul_overflow(a, b, &item)) ||
                __builtin_add_overflow(sum, item, &sum)) return CETTA_DENSE_TENSOR_V1_OVERFLOW;
        }
        out->value.integer = sum;
    }
    return CETTA_DENSE_TENSOR_V1_OK;
}

static bool indices_valid(const CettaDenseTensorV1 *indices, size_t rows) {
    if (!indices || indices->dtype != CETTA_DENSE_TENSOR_V1_I64 || indices->rank != 1u)
        return false;
    const int64_t *values = (const int64_t *)indices->storage->data;
    for (size_t i = 0; i < indices->count; ++i)
        if (values[i] < 0 || (uint64_t)values[i] >= rows) return false;
    return true;
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_gather(const CettaDenseTensorV1 *source,
    const CettaDenseTensorV1 *indices, CettaDenseTensorV1 **out) {
    if (!out || !source || !indices) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    if (!source->rank || indices->dtype != CETTA_DENSE_TENSOR_V1_I64 || indices->rank != 1u)
        return CETTA_DENSE_TENSOR_V1_SHAPE;
    if (!indices_valid(indices, source->shape[0])) return CETTA_DENSE_TENSOR_V1_INDEX;
    size_t *shape = malloc(source->rank * sizeof(size_t));
    if (!shape) return CETTA_DENSE_TENSOR_V1_NOMEM;
    memcpy(shape, source->shape, source->rank * sizeof(size_t));
    shape[0] = indices->count;
    CettaDenseTensorV1Status status = allocate(source->dtype, shape, source->rank, out);
    free(shape);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    size_t stride = source->shape[0] ? source->storage->bytes / source->shape[0] : 0u;
    const int64_t *values = (const int64_t *)indices->storage->data;
    if (stride) for (size_t i = 0; i < indices->count; ++i)
        memcpy((*out)->storage->data + i * stride,
               source->storage->data + (size_t)values[i] * stride, stride);
    finish_storage(*out);
    return CETTA_DENSE_TENSOR_V1_OK;
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_cast(const CettaDenseTensorV1 *source,
    CettaDenseTensorV1DType dtype, CettaDenseTensorV1 **out) {
    if (!out || !source) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    if (source->dtype == dtype) return cetta_dense_tensor_v1_copy(source, out);
    if (!((source->dtype == CETTA_DENSE_TENSOR_V1_F64 && dtype == CETTA_DENSE_TENSOR_V1_F32) ||
          (source->dtype == CETTA_DENSE_TENSOR_V1_F32 && dtype == CETTA_DENSE_TENSOR_V1_F64))) return CETTA_DENSE_TENSOR_V1_DTYPE;
    CettaDenseTensorV1Status status = allocate(dtype, source->shape, source->rank, out);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    for (size_t i = 0; i < source->count; ++i) {
        if (dtype == CETTA_DENSE_TENSOR_V1_F32) {
            double value = ((const double *)source->storage->data)[i];
            if (isfinite(value) && fabs(value) > FLT_MAX) {
                cetta_dense_tensor_v1_release(*out); *out = NULL; return CETTA_DENSE_TENSOR_V1_VALUE;
            }
            ((float *)(*out)->storage->data)[i] = (float)value;
        } else ((double *)(*out)->storage->data)[i] = ((const float *)source->storage->data)[i];
    }
    finish_storage(*out);
    return CETTA_DENSE_TENSOR_V1_OK;
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

static double dot4_f32_scalar(const float *v, const double *q, size_t width) {
    double a = 0, b = 0, c = 0, d = 0;
    size_t j = 0;
    for (; width - j >= 4u; j += 4u) {
        a += (double)v[j] * q[j]; b += (double)v[j+1] * q[j+1];
        c += (double)v[j+2] * q[j+2]; d += (double)v[j+3] * q[j+3];
    }
    double total = ((a + b) + c) + d;
    for (; j < width; ++j) total += (double)v[j] * q[j];
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

__attribute__((target("avx")))
static double dot4_f32_avx(const float *v, const double *q, size_t width) {
    __m256d lanes = _mm256_setzero_pd();
    size_t j = 0;
    for (; width - j >= 4u; j += 4u)
        lanes = _mm256_add_pd(lanes, _mm256_mul_pd(_mm256_cvtps_pd(_mm_loadu_ps(v+j)), _mm256_loadu_pd(q+j)));
    double sums[4]; _mm256_storeu_pd(sums, lanes);
    double total = ((sums[0] + sums[1]) + sums[2]) + sums[3];
    for (; j < width; ++j) total += (double)v[j] * q[j];
    return total;
}
#endif

bool cetta_dense_tensor_v1_matvec_provider_available(CettaDenseTensorV1MatvecProvider provider) {
    if (provider == CETTA_MATVEC_AUTO || provider == CETTA_MATVEC_SCALAR4) return true;
#if CETTA_HAS_AVX4
    if (provider == CETTA_MATVEC_AVX4) return __builtin_cpu_supports("avx") != 0;
#endif
    return false;
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_matvec(const CettaDenseTensorV1 *matrix,
    const CettaDenseTensorV1 *query, const CettaDenseTensorV1 *indices, CettaDenseTensorV1 **out) {
    return cetta_dense_tensor_v1_matvec_with_provider(matrix, query, indices, CETTA_MATVEC_AUTO, out);
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_matvec_with_provider(const CettaDenseTensorV1 *matrix,
    const CettaDenseTensorV1 *query, const CettaDenseTensorV1 *indices,
    CettaDenseTensorV1MatvecProvider provider, CettaDenseTensorV1 **out) {
    if (!out || !matrix || !query) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    if ((matrix->dtype != CETTA_DENSE_TENSOR_V1_F64 && matrix->dtype != CETTA_DENSE_TENSOR_V1_F32) || query->dtype != CETTA_DENSE_TENSOR_V1_F64)
        return CETTA_DENSE_TENSOR_V1_DTYPE;
    if (matrix->rank != 2u || query->rank != 1u || query->count != matrix->shape[1])
        return CETTA_DENSE_TENSOR_V1_SHAPE;
    if (!cetta_dense_tensor_v1_matvec_provider_available(provider)) return CETTA_DENSE_TENSOR_V1_PROVIDER;
    double (*dot)(const double *, const double *, size_t) = dot4_scalar;
    double (*compact_dot)(const float *, const double *, size_t) = dot4_f32_scalar;
#if CETTA_HAS_AVX4
    if (provider == CETTA_MATVEC_AVX4 || (provider == CETTA_MATVEC_AUTO &&
        cetta_dense_tensor_v1_matvec_provider_available(CETTA_MATVEC_AVX4))) {
        dot = dot4_avx; compact_dot = dot4_f32_avx;
    }
#endif
    size_t rows = matrix->shape[0], width = matrix->shape[1];
    if (indices && !indices_valid(indices, rows)) return CETTA_DENSE_TENSOR_V1_INDEX;
    size_t count = indices ? indices->count : rows;
    CettaDenseTensorV1Status status = allocate(CETTA_DENSE_TENSOR_V1_F64, &count, 1u, out);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    const double *data = (const double *)matrix->storage->data;
    const double *q = (const double *)query->storage->data;
    const int64_t *selected = indices ? (const int64_t *)indices->storage->data : NULL;
    double *result = (double *)(*out)->storage->data;
    for (size_t i = 0; i < count; ++i) {
        size_t row = selected ? (size_t)selected[i] : i;
        /* An empty row never forms a pointer into absent storage. */
        if (matrix->dtype == CETTA_DENSE_TENSOR_V1_F32) {
            const float *v = width ? (const float *)matrix->storage->data + row*width : NULL;
            result[i] = compact_dot(v, q, width);
        } else {
            const double *v = width ? data + row * width : NULL;
            result[i] = dot(v, q, width);
        }
    }
    finish_storage(*out);
    return CETTA_DENSE_TENSOR_V1_OK;
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_div(const CettaDenseTensorV1 *left,
    const CettaDenseTensorV1 *right, CettaDenseTensorV1 **out) {
    if (!out || !left || !right) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    if (left->dtype != CETTA_DENSE_TENSOR_V1_F64 || right->dtype != CETTA_DENSE_TENSOR_V1_F64)
        return CETTA_DENSE_TENSOR_V1_DTYPE;
    if (!same_shape(left, right)) return CETTA_DENSE_TENSOR_V1_SHAPE;
    CettaDenseTensorV1Status status = allocate(left->dtype, left->shape, left->rank, out);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    const double *a = (const double *)left->storage->data;
    const double *b = (const double *)right->storage->data;
    double *result = (double *)(*out)->storage->data;
    for (size_t i = 0; i < left->count; ++i) result[i] = a[i] / b[i];
    finish_storage(*out);
    return CETTA_DENSE_TENSOR_V1_OK;
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_scale(const CettaDenseTensorV1 *source, double factor,
    CettaDenseTensorV1 **out) {
    if (!out || !source) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    if (source->dtype != CETTA_DENSE_TENSOR_V1_F64) return CETTA_DENSE_TENSOR_V1_DTYPE;
    CettaDenseTensorV1Status status = allocate(source->dtype, source->shape, source->rank, out);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    const double *values = (const double *)source->storage->data;
    double *result = (double *)(*out)->storage->data;
    for (size_t i = 0; i < source->count; ++i) result[i] = values[i] * factor;
    finish_storage(*out);
    return CETTA_DENSE_TENSOR_V1_OK;
}

/* Broadcast one divisor across each axis-zero matrix row. */
static CettaDenseTensorV1Status divide_rows(const CettaDenseTensorV1 *matrix,
    const CettaDenseTensorV1 *divisors, CettaDenseTensorV1 **out) {
    if (!out || !matrix || !divisors) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    if (matrix->dtype != CETTA_DENSE_TENSOR_V1_F64 || divisors->dtype != CETTA_DENSE_TENSOR_V1_F64)
        return CETTA_DENSE_TENSOR_V1_DTYPE;
    if (matrix->rank != 2u || divisors->rank != 1u || divisors->count != matrix->shape[0])
        return CETTA_DENSE_TENSOR_V1_SHAPE;
    CettaDenseTensorV1Status status = allocate(matrix->dtype, matrix->shape, 2u, out);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    const double *data = (const double *)matrix->storage->data;
    const double *denoms = (const double *)divisors->storage->data;
    double *result = (double *)(*out)->storage->data;
    size_t width = matrix->shape[1];
    for (size_t i = 0; i < matrix->shape[0]; ++i)
        for (size_t j = 0; j < width; ++j) result[i*width+j] = data[i*width+j] / denoms[i];
    finish_storage(*out);
    return CETTA_DENSE_TENSOR_V1_OK;
}

static bool mask_valid(const CettaDenseTensorV1 *mask) {
    if (!mask || mask->dtype != CETTA_DENSE_TENSOR_V1_U8 || mask->rank != 1u) return false;
    for (size_t i = 0; i < mask->count; ++i)
        if (mask->storage->data[i] > 1u) return false;
    return true;
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_mask_indices(size_t length,
    const CettaDenseTensorV1 *indices, CettaDenseTensorV1 **out) {
    if (!out || !indices) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    if (!indices_valid(indices, length)) return CETTA_DENSE_TENSOR_V1_INDEX;
    CettaDenseTensorV1Status status = allocate(CETTA_DENSE_TENSOR_V1_U8, &length, 1u, out);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    if (length) memset((*out)->storage->data, 0, length);
    const int64_t *values = (const int64_t *)indices->storage->data;
    for (size_t i = 0; i < indices->count; ++i) (*out)->storage->data[values[i]] = 1u;
    finish_storage(*out);
    return CETTA_DENSE_TENSOR_V1_OK;
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_mask_binary(const CettaDenseTensorV1 *left,
    const CettaDenseTensorV1 *right, bool is_or, CettaDenseTensorV1 **out) {
    if (!out || !left || !right) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    if (!same_shape(left, right)) return CETTA_DENSE_TENSOR_V1_SHAPE;
    if (!mask_valid(left) || !mask_valid(right)) return CETTA_DENSE_TENSOR_V1_VALUE;
    CettaDenseTensorV1Status status = allocate(CETTA_DENSE_TENSOR_V1_U8, left->shape, 1u, out);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    for (size_t i = 0; i < left->count; ++i)
        (*out)->storage->data[i] = is_or ? left->storage->data[i] | right->storage->data[i] :
                                         left->storage->data[i] & right->storage->data[i];
    finish_storage(*out);
    return CETTA_DENSE_TENSOR_V1_OK;
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_mask_not(const CettaDenseTensorV1 *mask, CettaDenseTensorV1 **out) {
    if (!out || !mask) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    if (!mask_valid(mask)) return CETTA_DENSE_TENSOR_V1_VALUE;
    CettaDenseTensorV1Status status = allocate(CETTA_DENSE_TENSOR_V1_U8, mask->shape, 1u, out);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    for (size_t i = 0; i < mask->count; ++i)
        (*out)->storage->data[i] = !mask->storage->data[i];
    finish_storage(*out);
    return CETTA_DENSE_TENSOR_V1_OK;
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_nonzero(const CettaDenseTensorV1 *mask, CettaDenseTensorV1 **out) {
    if (!out || !mask) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    if (!mask_valid(mask)) return CETTA_DENSE_TENSOR_V1_VALUE;
    size_t count = 0;
    for (size_t i = 0; i < mask->count; ++i) count += mask->storage->data[i];
    CettaDenseTensorV1Status status = allocate(CETTA_DENSE_TENSOR_V1_I64, &count, 1u, out);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    int64_t *result = (int64_t *)(*out)->storage->data;
    size_t next = 0;
    for (size_t i = 0; i < mask->count; ++i) if (mask->storage->data[i]) {
        if (i > INT64_MAX) { cetta_dense_tensor_v1_release(*out); *out = NULL; return CETTA_DENSE_TENSOR_V1_OVERFLOW; }
        result[next++] = (int64_t)i;
    }
    finish_storage(*out);
    return CETTA_DENSE_TENSOR_V1_OK;
}

static bool score_better(const CettaDenseTensorV1 *scores, size_t a, size_t b) {
    if (scores->dtype == CETTA_DENSE_TENSOR_V1_F64) {
        const double *values = (const double *)scores->storage->data;
        if (values[a] != values[b]) return values[a] > values[b];
    } else if (scores->dtype == CETTA_DENSE_TENSOR_V1_F32) {
        const float *values = (const float *)scores->storage->data;
        if (values[a] != values[b]) return values[a] > values[b];
    } else if (scores->dtype == CETTA_DENSE_TENSOR_V1_I64) {
        const int64_t *values = (const int64_t *)scores->storage->data;
        if (values[a] != values[b]) return values[a] > values[b];
    } else {
        const unsigned char *values = scores->storage->data;
        if (values[a] != values[b]) return values[a] > values[b];
    }
    return a < b;
}

/* The root is the worst retained score, with the largest index at a tie. */
static void heap_down(const CettaDenseTensorV1 *scores, size_t *heap, size_t count, size_t root) {
    while (root < count / 2u) {
        size_t child = root * 2u + 1u;
        if (child + 1u < count && score_better(scores, heap[child], heap[child+1])) ++child;
        if (!score_better(scores, heap[root], heap[child])) break;
        size_t old = heap[root]; heap[root] = heap[child]; heap[child] = old;
        root = child;
    }
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_top_k(const CettaDenseTensorV1 *scores,
    const CettaDenseTensorV1 *mask, size_t k, CettaDenseTensorV1 **out) {
    if (!out || !scores) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    if (scores->rank != 1u) return CETTA_DENSE_TENSOR_V1_SHAPE;
    if (mask && (!same_shape(scores, mask) || !mask_valid(mask))) return CETTA_DENSE_TENSOR_V1_VALUE;
    if (scores->count > INT64_MAX) return CETTA_DENSE_TENSOR_V1_OVERFLOW;
    size_t capacity = k < scores->count ? k : scores->count;
    if (capacity > SIZE_MAX / sizeof(size_t)) return CETTA_DENSE_TENSOR_V1_OVERFLOW;
    size_t *heap = capacity ? malloc(capacity * sizeof(size_t)) : NULL;
    if (capacity && !heap) return CETTA_DENSE_TENSOR_V1_NOMEM;
    size_t count = 0;
    for (size_t i = 0; i < scores->count; ++i) {
        if (mask && !mask->storage->data[i]) continue;
        if (scores->dtype == CETTA_DENSE_TENSOR_V1_F64 && !isfinite(((const double *)scores->storage->data)[i])) {
            free(heap); return CETTA_DENSE_TENSOR_V1_VALUE;
        }
        if (scores->dtype == CETTA_DENSE_TENSOR_V1_F32 && !isfinite(((const float *)scores->storage->data)[i])) {
            free(heap); return CETTA_DENSE_TENSOR_V1_VALUE;
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
    CettaDenseTensorV1Status status = allocate(CETTA_DENSE_TENSOR_V1_I64, &count, 1u, out);
    if (status == CETTA_DENSE_TENSOR_V1_OK) {
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

static bool bounds_rounding_admitted(void) {
#if FLT_EVAL_METHOD != 0 || defined(__FAST_MATH__)
    return false;
#else
    if (fegetround() != FE_TONEAREST) return false;
#if CETTA_HAS_AVX4
    /* FTZ and DAZ violate the absolute underflow allowance below. */
    if (_mm_getcsr() & ((1u << 15) | (1u << 6))) return false;
#else
    /* FE_TONEAREST alone does not rule out flushing subnormals. Probe both
     * precisions: the first product detects flushed output, and the second
     * detects flushed input. Volatile operands prevent constant folding. */
    volatile double wide_min = DBL_MIN, half = .5, two = 2.;
    volatile double wide_subnormal = wide_min * half;
    if (wide_subnormal * two != DBL_MIN) return false;
    volatile float compact_min = FLT_MIN, compact_half = .5f, compact_two = 2.f;
    volatile float compact_subnormal = compact_min * compact_half;
    if (compact_subnormal * compact_two != FLT_MIN) return false;
#endif
    return true;
#endif
}

/* One representable step outwards, without a libm call for each row. The
 * admitted binary64 representation is also used by the portable tensor codec.
 * Infinities/NaNs stay unchanged; signed zero steps to the minimum subnormal. */
static double step_up(double value) {
    if (!isfinite(value)) return value;
    if (value == 0) return DBL_TRUE_MIN;
    uint64_t bits; memcpy(&bits, &value, 8);
    if (value > 0) ++bits; else --bits;
    memcpy(&value, &bits, 8); return value;
}
static double step_down(double value) {
    if (!isfinite(value)) return value;
    if (value == 0) return -DBL_TRUE_MIN;
    uint64_t bits; memcpy(&bits, &value, 8);
    if (value > 0) --bits; else ++bits;
    memcpy(&value, &bits, 8); return value;
}
/* Positive quantities are rounded outward after every elementary operation. */
static double add_up(double a, double b) { return step_up(a + b); }
static double mul_up(double a, double b) { return step_up(a * b); }

CettaDenseTensorV1Status cetta_dense_tensor_v1_row_error_bounds(const CettaDenseTensorV1 *source,
    const CettaDenseTensorV1 *compact, CettaDenseTensorV1 **out) {
    if (!out || !source || !compact) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    if (source->dtype != CETTA_DENSE_TENSOR_V1_F64 || compact->dtype != CETTA_DENSE_TENSOR_V1_F32) return CETTA_DENSE_TENSOR_V1_DTYPE;
    if (source->rank != 2u || !same_shape(source, compact)) return CETTA_DENSE_TENSOR_V1_SHAPE;
    size_t shape[] = {source->shape[0], 2u};
    CettaDenseTensorV1Status status = allocate(CETTA_DENSE_TENSOR_V1_F64, shape, 2u, out);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    double *result = (double *)(*out)->storage->data;
    const double *wide = (const double *)source->storage->data;
    const float *narrow = (const float *)compact->storage->data;
    bool admitted = bounds_rounding_admitted();
    size_t width = source->shape[1];
    for (size_t row = 0; row < shape[0]; ++row) {
        double error = 0, magnitude = 0;
        for (size_t j = 0; j < width; ++j) {
            double v = wide[row*width+j], w = narrow[row*width+j];
            if (!isfinite(v) || !isfinite(w)) {
                cetta_dense_tensor_v1_release(*out); *out = NULL; return CETTA_DENSE_TENSOR_V1_VALUE;
            }
            error = add_up(error, step_up(fabs(v-w)));
            magnitude = add_up(magnitude, add_up(fabs(v), fabs(w)));
        }
        result[row*2] = admitted ? error : INFINITY;
        result[row*2+1] = admitted ? magnitude : INFINITY;
    }
    finish_storage(*out);
    return CETTA_DENSE_TENSOR_V1_OK;
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_matvec_bounds(const CettaDenseTensorV1 *compact,
    const CettaDenseTensorV1 *query, const CettaDenseTensorV1 *certificate,
    const CettaDenseTensorV1 *indices, double scale, CettaDenseTensorV1 **lower, CettaDenseTensorV1 **upper) {
    if (!lower || !upper || lower == upper || !compact || !query || !certificate) return CETTA_DENSE_TENSOR_V1_VALUE;
    *lower = NULL; *upper = NULL;
    if (compact->dtype != CETTA_DENSE_TENSOR_V1_F32 || certificate->dtype != CETTA_DENSE_TENSOR_V1_F64 ||
        query->dtype != CETTA_DENSE_TENSOR_V1_F64) return CETTA_DENSE_TENSOR_V1_DTYPE;
    if (compact->rank != 2u || certificate->rank != 2u || certificate->shape[1] != 2u ||
        certificate->shape[0] != compact->shape[0]) return CETTA_DENSE_TENSOR_V1_SHAPE;
    if (!(scale > 0) || !isfinite(scale)) return CETTA_DENSE_TENSOR_V1_VALUE;
    double qmax = 0;
    const double *q = (const double *)query->storage->data;
    for (size_t j = 0; j < query->count; ++j) {
        if (!isfinite(q[j])) return CETTA_DENSE_TENSOR_V1_VALUE;
        if (fabs(q[j]) > qmax) qmax = fabs(q[j]);
    }
    CettaDenseTensorV1Status status = cetta_dense_tensor_v1_matvec(compact, query, indices, lower);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    status = allocate(CETTA_DENSE_TENSOR_V1_F64, (*lower)->shape, 1u, upper);
    if (status != CETTA_DENSE_TENSOR_V1_OK) { cetta_dense_tensor_v1_release(*lower); *lower = NULL; return status; }
    double *lo = (double *)(*lower)->storage->data, *hi = (double *)(*upper)->storage->data;
    const double *cert = (const double *)certificate->storage->data;
    const int64_t *selected = indices ? (const int64_t *)indices->storage->data : NULL;
    /* For lane4 dot products plus scale, this deliberately exceeds the usual
     * gamma_(2d+8) bound for both reductions. Absolute error covers gradual
     * underflow. Overflow, unsuitable rounding, or excessive d disables pruning.
     * Certificate and compact must come from one admitted source row view. */
    double operations = add_up(mul_up(8., (double)compact->shape[1]), 64.);
    double relative = mul_up(operations, DBL_EPSILON);
    bool admitted = bounds_rounding_admitted() && relative < .125;
    double underflow = add_up(mul_up(mul_up(operations, DBL_TRUE_MIN), scale),
                              mul_up(operations, DBL_TRUE_MIN));
    double multiplier = mul_up(qmax, scale);
    for (size_t i = 0; i < (*lower)->count; ++i) {
        size_t row = selected ? (size_t)selected[i] : i;
        double error = cert[row*2], magnitude = cert[row*2+1];
        if (isnan(error) || isnan(magnitude) || error < 0 || magnitude < 0) {
            cetta_dense_tensor_v1_release(*lower); cetta_dense_tensor_v1_release(*upper);
            *lower = NULL; *upper = NULL; return CETTA_DENSE_TENSOR_V1_VALUE;
        }
        double score = lo[i] * scale;
        double radius = add_up(mul_up(multiplier, add_up(error, mul_up(relative, magnitude))), underflow);
        double absolute = mul_up(multiplier, magnitude);
        double raw_absolute = mul_up(qmax, magnitude);
        if (!admitted || !isfinite(raw_absolute) || raw_absolute > DBL_MAX/4 ||
            !isfinite(score) || !isfinite(radius) ||
            !isfinite(absolute) || absolute > DBL_MAX/4) { lo[i] = -INFINITY; hi[i] = INFINITY; }
        else { lo[i] = step_down(score-radius); hi[i] = step_up(score+radius); }
    }
    finish_storage(*lower); finish_storage(*upper);
    return CETTA_DENSE_TENSOR_V1_OK;
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_top_k_candidates(const CettaDenseTensorV1 *lower,
    const CettaDenseTensorV1 *upper, size_t k, CettaDenseTensorV1 **out) {
    if (!out || !lower || !upper) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    if (lower->dtype != CETTA_DENSE_TENSOR_V1_F64 || upper->dtype != CETTA_DENSE_TENSOR_V1_F64) return CETTA_DENSE_TENSOR_V1_DTYPE;
    if (lower->rank != 1u || !same_shape(lower, upper)) return CETTA_DENSE_TENSOR_V1_SHAPE;
    if (lower->count > INT64_MAX) return CETTA_DENSE_TENSOR_V1_OVERFLOW;
    const double *lo = (const double *)lower->storage->data, *hi = (const double *)upper->storage->data;
    size_t capacity = k < lower->count ? k : lower->count, count = 0;
    if (capacity > SIZE_MAX / sizeof(size_t)) return CETTA_DENSE_TENSOR_V1_OVERFLOW;
    size_t *heap = capacity ? malloc(capacity*sizeof(size_t)) : NULL;
    if (capacity && !heap) return CETTA_DENSE_TENSOR_V1_NOMEM;
    for (size_t i = 0; i < lower->count; ++i) {
        if (isnan(lo[i]) || isnan(hi[i]) || lo[i] > hi[i]) { free(heap); return CETTA_DENSE_TENSOR_V1_VALUE; }
        if (!capacity) continue;
        if (count < capacity) {
            size_t child = count++; heap[child] = i;
            while (child) {
                size_t parent = (child-1u)/2u;
                if (!score_better(lower, heap[parent], heap[child])) break;
                size_t old = heap[parent]; heap[parent] = heap[child]; heap[child] = old; child = parent;
            }
        } else if (score_better(lower, i, heap[0])) { heap[0] = i; heap_down(lower, heap, count, 0); }
    }
    double threshold = count ? lo[heap[0]] : INFINITY;
    free(heap);
    count = 0;
    if (capacity) for (size_t i = 0; i < lower->count; ++i) if (hi[i] >= threshold) ++count;
    CettaDenseTensorV1Status status = allocate(CETTA_DENSE_TENSOR_V1_I64, &count, 1u, out);
    if (status == CETTA_DENSE_TENSOR_V1_OK) {
        int64_t *values = (int64_t *)(*out)->storage->data;
        size_t j = 0;
        if (capacity) for (size_t i = 0; i < lower->count; ++i) if (hi[i] >= threshold) values[j++] = (int64_t)i;
        finish_storage(*out);
    }
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

CettaDenseTensorV1Status cetta_dense_tensor_v1_encoded_size(const CettaDenseTensorV1 *tensor, size_t *size) {
    if (!tensor || !size) return CETTA_DENSE_TENSOR_V1_VALUE;
    *size = 0u;
    if (tensor->rank > UINT32_MAX || tensor->rank > (SIZE_MAX - 9u) / 8u)
        return CETTA_DENSE_TENSOR_V1_OVERFLOW;
    size_t prefix = 9u + 8u * tensor->rank;
    if (tensor->storage->bytes > SIZE_MAX - prefix) return CETTA_DENSE_TENSOR_V1_OVERFLOW;
    *size = prefix + tensor->storage->bytes;
    return CETTA_DENSE_TENSOR_V1_OK;
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_encode(const CettaDenseTensorV1 *tensor,
    unsigned char **bytes, size_t *size) {
    if (!bytes || !size) return CETTA_DENSE_TENSOR_V1_VALUE;
    *bytes = NULL; *size = 0u;
    size_t required;
    CettaDenseTensorV1Status status = cetta_dense_tensor_v1_encoded_size(tensor, &required);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    size_t prefix = 9u + 8u * tensor->rank;
    unsigned char *data = malloc(required);
    if (!data) return CETTA_DENSE_TENSOR_V1_NOMEM;
    memcpy(data, "CTN1", 4u); data[4] = (unsigned char)tensor->dtype;
    put_le(data + 5u, tensor->rank, 4u);
    for (size_t i = 0; i < tensor->rank; ++i) put_le(data + 9u + 8u * i, tensor->shape[i], 8u);
    if (tensor->dtype == CETTA_DENSE_TENSOR_V1_U8) {
        if (tensor->storage->bytes) memcpy(data + prefix, tensor->storage->data, tensor->storage->bytes);
    } else {
        for (size_t i = 0; i < tensor->count; ++i) {
            size_t width = cetta_dense_tensor_v1_itemsize(tensor->dtype);
            uint64_t bits;
            if (width == 4u) { uint32_t word; memcpy(&word, tensor->storage->data + i*width, width); bits = word; }
            else memcpy(&bits, tensor->storage->data + i*width, width);
            put_le(data + prefix + i*width, bits, width);
        }
    }
    *bytes = data; *size = required;
    return CETTA_DENSE_TENSOR_V1_OK;
}

CettaDenseTensorV1Status cetta_dense_tensor_v1_decode(const unsigned char *bytes, size_t size,
    CettaDenseTensorV1 **out) {
    if (!out) return CETTA_DENSE_TENSOR_V1_VALUE;
    *out = NULL;
    if (!bytes || size < 9u || memcmp(bytes, "CTN1", 4u)) return CETTA_DENSE_TENSOR_V1_ENCODING;
    CettaDenseTensorV1DType dtype = (CettaDenseTensorV1DType)bytes[4];
    size_t rank = (size_t)get_le(bytes + 5u, 4u);
    if (rank > (size - 9u) / 8u || rank > SIZE_MAX / sizeof(size_t)) return CETTA_DENSE_TENSOR_V1_ENCODING;
    size_t *shape = rank ? malloc(rank * sizeof(size_t)) : NULL;
    if (rank && !shape) return CETTA_DENSE_TENSOR_V1_NOMEM;
    for (size_t i = 0; i < rank; ++i) {
        uint64_t dimension = get_le(bytes + 9u + i * 8u, 8u);
        if (dimension > SIZE_MAX) { free(shape); return CETTA_DENSE_TENSOR_V1_OVERFLOW; }
        shape[i] = (size_t)dimension;
    }
    size_t count, required, prefix = 9u + rank * 8u;
    CettaDenseTensorV1Status status = cetta_dense_tensor_v1_byte_count(dtype, shape, rank, &count, &required);
    if (status == CETTA_DENSE_TENSOR_V1_OK && required != size - prefix) status = CETTA_DENSE_TENSOR_V1_ENCODING;
    if (status == CETTA_DENSE_TENSOR_V1_OK) status = allocate(dtype, shape, rank, out);
    free(shape);
    if (status != CETTA_DENSE_TENSOR_V1_OK) return status;
    if (dtype == CETTA_DENSE_TENSOR_V1_U8) {
        if (required) memcpy((*out)->storage->data, bytes + prefix, required);
    } else {
        for (size_t i = 0; i < count; ++i) {
            size_t width = cetta_dense_tensor_v1_itemsize(dtype);
            uint64_t bits = get_le(bytes + prefix + i*width, width);
            if (width == 4u) { uint32_t word = (uint32_t)bits; memcpy((*out)->storage->data + i*width, &word, width); }
            else memcpy((*out)->storage->data + i*width, &bits, width);
        }
    }
    finish_storage(*out);
    return CETTA_DENSE_TENSOR_V1_OK;
}

void cetta_dense_tensor_v1_print(const CettaDenseTensorV1 *tensor, FILE *out) {
    if (!tensor) { fputs("<invalid-tensor>", out); return; }
    fprintf(out, "<tensor %s (", cetta_dense_tensor_v1_dtype_name(tensor->dtype));
    for (size_t i = 0; i < tensor->rank; ++i) fprintf(out, "%s%zu", i ? " " : "", tensor->shape[i]);
    fprintf(out, ") %zu elements>", tensor->count);
}

static Atom *failure(Arena *arena, CettaDenseTensorV1Status status) {
    return atom_expr2(arena, atom_symbol(arena, "TensorFailure"),
        atom_symbol(arena, cetta_dense_tensor_v1_status_name(status)));
}

static Atom *publish(Arena *arena, CettaDenseTensorV1Status status, CettaDenseTensorV1 *tensor) {
    if (status != CETTA_DENSE_TENSOR_V1_OK) return failure(arena, status);
    Atom *atom = atom_tensor(arena, tensor);
    cetta_dense_tensor_v1_release(tensor);
    return atom;
}

static CettaDenseTensorV1 *from_atom(const Atom *atom) {
    return atom && atom->kind == ATOM_GROUNDED && atom->ground.gkind == GV_TENSOR
        ? atom->ground.ptr : NULL;
}

static bool natural(const Atom *atom, size_t *value) {
    if (!atom || atom->kind != ATOM_GROUNDED || atom->ground.gkind != GV_INT ||
        atom->ground.ival < 0 || (uint64_t)atom->ground.ival > SIZE_MAX) return false;
    *value = (size_t)atom->ground.ival;
    return true;
}

static CettaDenseTensorV1DType dtype_from_atom(const Atom *atom) {
    if (atom_is_symbol((Atom *)atom, "i64")) return CETTA_DENSE_TENSOR_V1_I64;
    if (atom_is_symbol((Atom *)atom, "f64")) return CETTA_DENSE_TENSOR_V1_F64;
    if (atom_is_symbol((Atom *)atom, "u8")) return CETTA_DENSE_TENSOR_V1_U8;
    if (atom_is_symbol((Atom *)atom, "f32")) return CETTA_DENSE_TENSOR_V1_F32;
    return (CettaDenseTensorV1DType)0;
}

static CettaDenseTensorV1Status shape_from_atom(const Atom *atom, size_t **shape, size_t *rank) {
    *shape = NULL; *rank = 0;
    if (!atom || atom->kind != ATOM_EXPR) return CETTA_DENSE_TENSOR_V1_SHAPE;
    size_t skip = atom_is_list(atom) ? 1u : 0u;
    if (atom->expr.len - skip > SIZE_MAX / sizeof(size_t)) return CETTA_DENSE_TENSOR_V1_OVERFLOW;
    *rank = (size_t)atom->expr.len - skip;
    if (*rank) {
        *shape = malloc(*rank * sizeof(size_t));
        if (!*shape) return CETTA_DENSE_TENSOR_V1_NOMEM;
    }
    for (size_t i = 0; i < *rank; ++i) {
        if (!natural(atom->expr.elems[i + skip], &(*shape)[i])) {
            free(*shape); *shape = NULL; return CETTA_DENSE_TENSOR_V1_SHAPE;
        }
    }
    return CETTA_DENSE_TENSOR_V1_OK;
}

static CettaDenseTensorV1Status element_from_atom(CettaDenseTensorV1DType dtype, const Atom *atom,
    unsigned char *element) {
    if (!atom || atom->kind != ATOM_GROUNDED) return CETTA_DENSE_TENSOR_V1_VALUE;
    if (dtype == CETTA_DENSE_TENSOR_V1_F64 || dtype == CETTA_DENSE_TENSOR_V1_F32) {
        double value;
        if (atom->ground.gkind == GV_FLOAT) value = atom->ground.fval;
        else if (atom->ground.gkind == GV_INT) value = (double)atom->ground.ival;
        else return CETTA_DENSE_TENSOR_V1_VALUE;
        if (dtype == CETTA_DENSE_TENSOR_V1_F32) {
            if (isfinite(value) && fabs(value) > FLT_MAX) return CETTA_DENSE_TENSOR_V1_VALUE;
            float compact = (float)value; memcpy(element, &compact, 4u);
        }
        else memcpy(element, &value, 8u);
    } else if (dtype == CETTA_DENSE_TENSOR_V1_I64) {
        if (atom->ground.gkind != GV_INT) return CETTA_DENSE_TENSOR_V1_VALUE;
        memcpy(element, &atom->ground.ival, 8u);
    } else if (dtype == CETTA_DENSE_TENSOR_V1_U8) {
        if (atom->ground.gkind != GV_INT || atom->ground.ival < 0 || atom->ground.ival > 255)
            return CETTA_DENSE_TENSOR_V1_VALUE;
        element[0] = (unsigned char)atom->ground.ival;
    } else return CETTA_DENSE_TENSOR_V1_DTYPE;
    return CETTA_DENSE_TENSOR_V1_OK;
}

static Atom *element_atom(Arena *arena, const CettaDenseTensorV1 *tensor, size_t index) {
    if (tensor->dtype == CETTA_DENSE_TENSOR_V1_F32) {
        float value; memcpy(&value, tensor->storage->data + index*4u, 4u);
        return atom_float(arena, value);
    }
    if (tensor->dtype == CETTA_DENSE_TENSOR_V1_F64) {
        double value;
        memcpy(&value, tensor->storage->data + index * 8u, 8u);
        return atom_float(arena, value);
    }
    if (tensor->dtype == CETTA_DENSE_TENSOR_V1_I64) {
        int64_t value;
        memcpy(&value, tensor->storage->data + index * 8u, 8u);
        return atom_int(arena, value);
    }
    return atom_int(arena, tensor->storage->data[index]);
}

Atom *cetta_dense_tensor_v1_dispatch(Arena *arena, Atom *head, Atom **args, uint32_t nargs) {
    if (!head || head->kind != ATOM_SYMBOL) return NULL;
    CettaDenseTensorV1 *bulk = NULL;
    CettaDenseTensorV1Status bulk_status;
    if (atom_is_symbol(head, "__cetta_lib_tensor_cast")) {
        if (nargs != 2u) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        bulk_status = cetta_dense_tensor_v1_cast(from_atom(args[1]), dtype_from_atom(args[0]), &bulk);
        return publish(arena, bulk_status, bulk);
    }
    if (atom_is_symbol(head, "__cetta_lib_tensor_row_error_bounds")) {
        if (nargs != 2u) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        bulk_status = cetta_dense_tensor_v1_row_error_bounds(from_atom(args[0]), from_atom(args[1]), &bulk);
        return publish(arena, bulk_status, bulk);
    }
    if (atom_is_symbol(head, "__cetta_lib_tensor_matvec_bounds_selected")) {
        if (nargs != 5u || !from_atom(args[3])) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        unsigned char element[8];
        bulk_status = element_from_atom(CETTA_DENSE_TENSOR_V1_F64, args[4], element);
        if (bulk_status != CETTA_DENSE_TENSOR_V1_OK) return failure(arena, bulk_status);
        double scale; memcpy(&scale, element, sizeof(scale));
        CettaDenseTensorV1 *upper = NULL;
        bulk_status = cetta_dense_tensor_v1_matvec_bounds(from_atom(args[0]), from_atom(args[1]),
            from_atom(args[2]), from_atom(args[3]), scale, &bulk, &upper);
        if (bulk_status != CETTA_DENSE_TENSOR_V1_OK) return failure(arena, bulk_status);
        Atom *pair[] = {publish(arena, CETTA_DENSE_TENSOR_V1_OK, bulk), publish(arena, CETTA_DENSE_TENSOR_V1_OK, upper)};
        return atom_expr(arena, pair, 2u);
    }
    if (atom_is_symbol(head, "__cetta_lib_tensor_top_k_candidates")) {
        size_t k;
        if (nargs != 3u || !natural(args[2], &k)) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        bulk_status = cetta_dense_tensor_v1_top_k_candidates(from_atom(args[0]), from_atom(args[1]), k, &bulk);
        return publish(arena, bulk_status, bulk);
    }
    if (atom_is_symbol(head, "__cetta_lib_tensor_matvec_provider")) {
        if (nargs != 1u || !atom_is_symbol(args[0], "auto"))
            return failure(arena, CETTA_DENSE_TENSOR_V1_PROVIDER);
        return atom_symbol(arena,
            cetta_dense_tensor_v1_matvec_provider_available(CETTA_MATVEC_AVX4) ? "avx4" : "scalar4");
    }
    bool provider_selected = atom_is_symbol(head, "__cetta_lib_tensor_matvec_selected_with");
    if (provider_selected || atom_is_symbol(head, "__cetta_lib_tensor_matvec_with")) {
        if (nargs != (provider_selected ? 4u : 3u)) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        CettaDenseTensorV1MatvecProvider provider;
        if (atom_is_symbol(args[0], "auto")) provider = CETTA_MATVEC_AUTO;
        else if (atom_is_symbol(args[0], "scalar4")) provider = CETTA_MATVEC_SCALAR4;
        else if (atom_is_symbol(args[0], "avx4")) provider = CETTA_MATVEC_AVX4;
        else return failure(arena, CETTA_DENSE_TENSOR_V1_PROVIDER);
        CettaDenseTensorV1 *indices = provider_selected ? from_atom(args[3]) : NULL;
        if (provider_selected && !indices) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        bulk_status = cetta_dense_tensor_v1_matvec_with_provider(from_atom(args[1]), from_atom(args[2]), indices, provider, &bulk);
        return publish(arena, bulk_status, bulk);
    }
    if (atom_is_symbol(head, "__cetta_lib_tensor_gather")) {
        if (nargs != 2u) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        bulk_status = cetta_dense_tensor_v1_gather(from_atom(args[0]), from_atom(args[1]), &bulk);
        return publish(arena, bulk_status, bulk);
    }
    bool selected = atom_is_symbol(head, "__cetta_lib_tensor_matvec_selected");
    if (selected || atom_is_symbol(head, "__cetta_lib_tensor_matvec")) {
        if (nargs != (selected ? 3u : 2u)) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        CettaDenseTensorV1 *indices = selected ? from_atom(args[2]) : NULL;
        if (selected && !indices) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        bulk_status = cetta_dense_tensor_v1_matvec(from_atom(args[0]), from_atom(args[1]), indices, &bulk);
        return publish(arena, bulk_status, bulk);
    }
    if (atom_is_symbol(head, "__cetta_lib_tensor_div")) {
        if (nargs != 2u) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        bulk_status = cetta_dense_tensor_v1_div(from_atom(args[0]), from_atom(args[1]), &bulk);
        return publish(arena, bulk_status, bulk);
    }
    if (atom_is_symbol(head, "__cetta_lib_tensor_div_rows")) {
        if (nargs != 2u) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        bulk_status = divide_rows(from_atom(args[0]), from_atom(args[1]), &bulk);
        return publish(arena, bulk_status, bulk);
    }
    if (atom_is_symbol(head, "__cetta_lib_tensor_scale")) {
        if (nargs != 2u) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        unsigned char element[8];
        bulk_status = element_from_atom(CETTA_DENSE_TENSOR_V1_F64, args[1], element);
        if (bulk_status != CETTA_DENSE_TENSOR_V1_OK) return failure(arena, bulk_status);
        double factor; memcpy(&factor, element, sizeof(factor));
        bulk_status = cetta_dense_tensor_v1_scale(from_atom(args[0]), factor, &bulk);
        return publish(arena, bulk_status, bulk);
    }
    if (atom_is_symbol(head, "__cetta_lib_tensor_mask_indices")) {
        size_t length;
        if (nargs != 2u || !natural(args[0], &length)) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        bulk_status = cetta_dense_tensor_v1_mask_indices(length, from_atom(args[1]), &bulk);
        return publish(arena, bulk_status, bulk);
    }
    bool mask_or = atom_is_symbol(head, "__cetta_lib_tensor_mask_or");
    if (mask_or || atom_is_symbol(head, "__cetta_lib_tensor_mask_and")) {
        if (nargs != 2u) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        bulk_status = cetta_dense_tensor_v1_mask_binary(from_atom(args[0]), from_atom(args[1]), mask_or, &bulk);
        return publish(arena, bulk_status, bulk);
    }
    if (atom_is_symbol(head, "__cetta_lib_tensor_mask_not")) {
        if (nargs != 1u) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        bulk_status = cetta_dense_tensor_v1_mask_not(from_atom(args[0]), &bulk);
        return publish(arena, bulk_status, bulk);
    }
    if (atom_is_symbol(head, "__cetta_lib_tensor_nonzero")) {
        if (nargs != 1u) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        bulk_status = cetta_dense_tensor_v1_nonzero(from_atom(args[0]), &bulk);
        return publish(arena, bulk_status, bulk);
    }
    bool masked = atom_is_symbol(head, "__cetta_lib_tensor_top_k_masked");
    if (masked || atom_is_symbol(head, "__cetta_lib_tensor_top_k")) {
        size_t k;
        if (nargs != (masked ? 3u : 2u) || !natural(args[nargs-1], &k))
            return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        CettaDenseTensorV1 *mask = masked ? from_atom(args[1]) : NULL;
        if (masked && !mask) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        bulk_status = cetta_dense_tensor_v1_top_k(from_atom(args[0]), mask, k, &bulk);
        return publish(arena, bulk_status, bulk);
    }
    /* Whole-buffer matrix primitives. Collection/profile/filter policy stays
     * in MeTTa. These names use the ordinary dynamic library-op boundary. */
    if (atom_is_symbol(head, "__cetta_lib_tensor_stack")) {
        if (nargs != 3 || args[2]->kind != ATOM_EXPR) return failure(arena,CETTA_DENSE_TENSOR_V1_VALUE);
        CettaDenseTensorV1DType dtype=dtype_from_atom(args[0]); size_t width;
        if (!natural(args[1],&width)) return failure(arena,CETTA_DENSE_TENSOR_V1_SHAPE);
        size_t count=args[2]->expr.len, shape[2]={count,width};
        CettaDenseTensorV1 *matrix=NULL;
        CettaDenseTensorV1Status status=allocate(dtype,shape,2,&matrix);
        if (status!=CETTA_DENSE_TENSOR_V1_OK) return failure(arena,status);
        size_t row_bytes=count ? matrix->storage->bytes/count : 0;
        for (size_t i=0;i<count;++i) {
            CettaDenseTensorV1 *row=from_atom(args[2]->expr.elems[i]);
            if (!row || row->dtype!=dtype || row->rank!=1 || row->count!=width) {
                cetta_dense_tensor_v1_release(matrix); return failure(arena,CETTA_DENSE_TENSOR_V1_SHAPE);
            }
            if (row_bytes) memcpy(matrix->storage->data+i*row_bytes,row->storage->data,row_bytes);
        }
        finish_storage(matrix); return publish(arena,CETTA_DENSE_TENSOR_V1_OK,matrix);
    }
    bool row_norms=atom_is_symbol(head,"__cetta_lib_tensor_row_norms");
    bool cosine_rows=atom_is_symbol(head,"__cetta_lib_tensor_cosine_rows");
    if (row_norms || cosine_rows) {
        if (nargs!=(row_norms ? 1u : 3u)) return failure(arena,CETTA_DENSE_TENSOR_V1_VALUE);
        CettaDenseTensorV1 *matrix=from_atom(args[0]);
        if (!matrix || matrix->dtype!=CETTA_DENSE_TENSOR_V1_F64 || matrix->rank!=2)
            return failure(arena,CETTA_DENSE_TENSOR_V1_SHAPE);
        size_t rows=matrix->shape[0], columns=matrix->shape[1];
        CettaDenseTensorV1 *norms=cosine_rows ? from_atom(args[1]) : NULL;
        CettaDenseTensorV1 *query=cosine_rows ? from_atom(args[2]) : NULL;
        double query_norm=1;
        if (cosine_rows) {
            if (!norms || !query || norms->dtype!=CETTA_DENSE_TENSOR_V1_F64 || query->dtype!=CETTA_DENSE_TENSOR_V1_F64 ||
                norms->rank!=1 || norms->count!=rows || query->rank!=1 || query->count!=columns)
                return failure(arena,CETTA_DENSE_TENSOR_V1_SHAPE);
            double total=0;
            for (size_t j=0;j<columns;++j) {
                double v;memcpy(&v,query->storage->data+j*8,8);
                volatile double product=v*v;total+=product;
            }
            query_norm=sqrt(total);
            if (!isfinite(query_norm) || query_norm<=0) return failure(arena,CETTA_DENSE_TENSOR_V1_VALUE);
        }
        CettaDenseTensorV1 *output=NULL;
        CettaDenseTensorV1Status status=allocate(CETTA_DENSE_TENSOR_V1_F64,&rows,1,&output);
        if (status!=CETTA_DENSE_TENSOR_V1_OK) return failure(arena,status);
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
                if (!isfinite(norm) || norm<=0) { cetta_dense_tensor_v1_release(output);return failure(arena,CETTA_DENSE_TENSOR_V1_VALUE); }
                total/=norm*query_norm;
            } else total=sqrt(total);
            if (!isfinite(total) || (row_norms && total<=0)) {
                cetta_dense_tensor_v1_release(output);return failure(arena,CETTA_DENSE_TENSOR_V1_VALUE);
            }
            memcpy(output->storage->data+i*8,&total,8);
        }
        finish_storage(output);return publish(arena,CETTA_DENSE_TENSOR_V1_OK,output);
    }
    SymbolId id = atom_symbol_current_id(head);
    if (id < g_builtin_syms.dense_v1_tensor_from || id > g_builtin_syms.dense_v1_tensor_decode) return NULL;
    CettaDenseTensorV1 *tensor = NULL, *left = nargs ? from_atom(args[0]) : NULL;
    CettaDenseTensorV1Status status = CETTA_DENSE_TENSOR_V1_VALUE;
    if (id == g_builtin_syms.dense_v1_tensor_from || id == g_builtin_syms.dense_v1_tensor_full) {
        if (nargs != 3u) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        CettaDenseTensorV1DType dtype = dtype_from_atom(args[0]);
        if (!cetta_dense_tensor_v1_itemsize(dtype)) return failure(arena, CETTA_DENSE_TENSOR_V1_DTYPE);
        size_t *shape, rank;
        status = shape_from_atom(args[1], &shape, &rank);
        if (status != CETTA_DENSE_TENSOR_V1_OK) return failure(arena, status);
        size_t count = 0, bytes = 0;
        status = cetta_dense_tensor_v1_byte_count(dtype, shape, rank, &count, &bytes);
        size_t skip = atom_is_list(args[2]) ? 1u : 0u;
        if (status == CETTA_DENSE_TENSOR_V1_OK && id == g_builtin_syms.dense_v1_tensor_from &&
            (args[2]->kind != ATOM_EXPR || args[2]->expr.len - skip != count))
            status = CETTA_DENSE_TENSOR_V1_COUNT;
        unsigned char element[8];
        if (status == CETTA_DENSE_TENSOR_V1_OK && id == g_builtin_syms.dense_v1_tensor_full)
            status = element_from_atom(dtype, args[2], element);
        if (status == CETTA_DENSE_TENSOR_V1_OK) status = allocate(dtype, shape, rank, &tensor);
        free(shape);
        if (status != CETTA_DENSE_TENSOR_V1_OK) return failure(arena, status);
        size_t width = cetta_dense_tensor_v1_itemsize(dtype);
        for (size_t i = 0; i < count; ++i) {
            if (id == g_builtin_syms.dense_v1_tensor_from)
                status = element_from_atom(dtype, args[2]->expr.elems[i + skip], element);
            if (status != CETTA_DENSE_TENSOR_V1_OK) { cetta_dense_tensor_v1_release(tensor); return failure(arena, status); }
            memcpy(tensor->storage->data + i * width, element, width);
        }
        finish_storage(tensor);
        return publish(arena, CETTA_DENSE_TENSOR_V1_OK, tensor);
    }
    if (id == g_builtin_syms.dense_v1_tensor_decode) {
        if (nargs != 1u || args[0]->kind != ATOM_GROUNDED || args[0]->ground.gkind != GV_STRING)
            return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        status = cetta_dense_tensor_v1_decode((const unsigned char *)args[0]->ground.sval,
            args[0]->ground.slen, &tensor);
        return publish(arena, status, tensor);
    }
    if (!left) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
    if (id == g_builtin_syms.dense_v1_tensor_shape && nargs == 1u) {
        Atom **dims = arena_alloc(arena, left->rank * sizeof(Atom *));
        for (size_t i = 0; i < left->rank; ++i) {
            if (left->shape[i] > INT64_MAX) return failure(arena, CETTA_DENSE_TENSOR_V1_OVERFLOW);
            dims[i] = atom_int(arena, (int64_t)left->shape[i]);
        }
        return atom_expr(arena, dims, left->rank);
    }
    if (id == g_builtin_syms.dense_v1_tensor_dtype && nargs == 1u)
        return atom_symbol(arena, cetta_dense_tensor_v1_dtype_name(left->dtype));
    if (id == g_builtin_syms.dense_v1_tensor_to_expr && nargs == 1u) {
        if (left->count > SIZE_MAX / sizeof(Atom *)) return failure(arena, CETTA_DENSE_TENSOR_V1_OVERFLOW);
        Atom **items = arena_alloc(arena, left->count * sizeof(Atom *));
        for (size_t i = 0; i < left->count; ++i) items[i] = element_atom(arena, left, i);
        return atom_expr(arena, items, left->count);
    }
    if (id == g_builtin_syms.dense_v1_tensor_reshape && nargs == 2u) {
        size_t *shape, rank;
        status = shape_from_atom(args[1], &shape, &rank);
        if (status == CETTA_DENSE_TENSOR_V1_OK) status = cetta_dense_tensor_v1_reshape(left, shape, rank, &tensor);
        free(shape);
    } else if (id == g_builtin_syms.dense_v1_tensor_copy && nargs == 1u) {
        status = cetta_dense_tensor_v1_copy(left, &tensor);
    } else if (id == g_builtin_syms.dense_v1_tensor_with && nargs == 3u) {
        size_t index;
        if (!natural(args[1], &index)) return failure(arena, CETTA_DENSE_TENSOR_V1_INDEX);
        unsigned char element[8];
        status = element_from_atom(left->dtype, args[2], element);
        if (status == CETTA_DENSE_TENSOR_V1_OK) status = cetta_dense_tensor_v1_with(left, index, element, &tensor);
    } else if ((id == g_builtin_syms.dense_v1_tensor_add || id == g_builtin_syms.dense_v1_tensor_mul) && nargs == 2u) {
        status = cetta_dense_tensor_v1_binary(left, from_atom(args[1]), id == g_builtin_syms.dense_v1_tensor_mul, &tensor);
    } else if ((id == g_builtin_syms.dense_v1_tensor_sum && nargs == 1u) ||
               (id == g_builtin_syms.dense_v1_tensor_dot && nargs == 2u)) {
        CettaDenseTensorV1Scalar scalar;
        CettaDenseTensorV1 *right = nargs == 2u ? from_atom(args[1]) : NULL;
        if (nargs == 2u && !right) return failure(arena, CETTA_DENSE_TENSOR_V1_VALUE);
        status = cetta_dense_tensor_v1_reduce(left, right, &scalar);
        if (status != CETTA_DENSE_TENSOR_V1_OK) return failure(arena, status);
        return scalar.dtype == CETTA_DENSE_TENSOR_V1_F64 ? atom_float(arena, scalar.value.floating) :
            atom_int(arena, scalar.value.integer);
    } else if (id == g_builtin_syms.dense_v1_tensor_encode && nargs == 1u) {
        unsigned char *bytes; size_t size;
        status = cetta_dense_tensor_v1_encoded_size(left, &size);
        if (status != CETTA_DENSE_TENSOR_V1_OK) return failure(arena, status);
        if (size > UINT32_MAX) return failure(arena, CETTA_DENSE_TENSOR_V1_OVERFLOW);
        status = cetta_dense_tensor_v1_encode(left, &bytes, &size);
        if (status != CETTA_DENSE_TENSOR_V1_OK) return failure(arena, status);
        Atom *encoded = atom_string_n(arena, (const char *)bytes, size);
        free(bytes);
        return encoded;
    }
    return publish(arena, status, tensor);
}
